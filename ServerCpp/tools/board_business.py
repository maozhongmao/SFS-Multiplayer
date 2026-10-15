#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 板端双客户端业务测试：验证火箭创建/同步/销毁/聊天的广播链路。
#
# 关键：Lidgren 的 bool 只占 1 位，且位流是 LSB-first。
# 因此所有包必须用位精确的 BitWriter 构造，不能用 struct.pack（struct 的 '?' 是整字节）。
#
# 流程：
#   A、B 各自连接 + 握手
#   A 创建火箭 → A 收到回执(LocalId 保留) + B 收到广播(LocalId=-1)
#   A 获得权威 → A 收到 UpdatePlayerAuthority
#   A 更新位置 → B 收到 UpdateRocketPrimary
#   A 发聊天   → B 收到 SendChatMessage
#   A 请求快照 → A 收到世界快照
#   A 销毁火箭 → B 收到 DestroyRocket

import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9806

MAGIC = 0x31465353
HANDSHAKE_VERSION = 2

KIND_HELLO = 1
KIND_HELLO_ACK = 2
KIND_PACKET = 3
KIND_PING = 4
KIND_PONG = 5
KIND_DISCONNECT = 6
KIND_REQUEST_WORLD_SNAPSHOT = 7

PT_JOIN_REQUEST = 0
PT_JOIN_RESPONSE = 1
PT_PLAYER_CONNECTED = 2
PT_PLAYER_DISCONNECTED = 3
PT_UPDATE_PLAYER_CONTROL = 4
PT_UPDATE_PLAYER_AUTHORITY = 5
PT_SEND_CHAT = 8
PT_CREATE_ROCKET = 9
PT_DESTROY_ROCKET = 10
PT_UPDATE_ROCKET_PRIMARY = 11
PT_UPDATE_ROCKET_SECONDARY = 12

failures = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print("  [%s] %s %s" % (status, name, detail))
    if not cond:
        failures.append(name)
    return cond


# ---------------------------------------------------------------- 位写入器
class BitWriter:
    """Lidgren 兼容的位写入器：LSB-first，bool 占 1 位。"""

    def __init__(self):
        self.buf = bytearray()
        self.bit_len = 0

    def write_bits(self, value, nbits):
        for i in range(nbits):
            bit = (value >> i) & 1
            byte_idx = self.bit_len >> 3
            if byte_idx >= len(self.buf):
                self.buf.append(0)
            if bit:
                self.buf[byte_idx] |= (1 << (self.bit_len & 7))
            self.bit_len += 1

    def write_bool(self, v):
        self.write_bits(1 if v else 0, 1)

    def write_u8(self, v):
        self.write_bits(v & 0xFF, 8)

    def write_i32(self, v):
        self.write_bits(v & 0xFFFFFFFF, 32)

    def write_u32(self, v):
        self.write_bits(v & 0xFFFFFFFF, 32)

    def write_f32(self, v):
        self.write_bits(int.from_bytes(struct.pack("<f", v), "little"), 32)

    def write_f64(self, v):
        self.write_bits(int.from_bytes(struct.pack("<d", v), "little"), 64)

    def write_string(self, s):
        b = s.encode("utf-8")
        n = len(b)
        # 7 位一组变长整数
        while n >= 0x80:
            self.write_u8((n & 0x7F) | 0x80)
            n >>= 7
        self.write_u8(n)
        for byte in b:
            self.write_u8(byte)

    def bytes(self):
        return bytes(self.buf)

    def bits(self):
        return self.bit_len


def encode_frame(kind, seq, payload, payload_bits):
    body_len = 9 + len(payload)
    return (struct.pack("<i", body_len) + bytes([kind])
            + struct.pack("<i", seq) + struct.pack("<i", payload_bits) + bytes(payload))


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        c = sock.recv(n - len(buf))
        if not c:
            raise EOFError("closed")
        buf += c
    return buf


def read_frame(sock):
    body_len = struct.unpack("<i", read_exact(sock, 4))[0]
    body = read_exact(sock, body_len)
    return body[0], struct.unpack("<i", body[1:5])[0], struct.unpack("<i", body[5:9])[0], body[9:]


def wstr_plain(s):
    """握手用的 BinaryWriter 字符串（4 字节长度前缀，非位流）"""
    b = s.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def build_hello(username):
    return (struct.pack("<I", MAGIC) + struct.pack("<i", HANDSHAKE_VERSION)
            + wstr_plain(username) + wstr_plain("") + wstr_plain("Earth")
            + struct.pack("<i", -1) + wstr_plain(""))


def connect_and_handshake(username):
    s = socket.create_connection((HOST, PORT), timeout=8)
    hello = build_hello(username)
    s.sendall(encode_frame(KIND_HELLO, HANDSHAKE_VERSION, hello, len(hello) * 8))
    kind, seq, bits, payload = read_frame(s)
    if kind != KIND_HELLO_ACK:
        raise RuntimeError("handshake failed for %s: kind=%d" % (username, kind))
    player_id, = struct.unpack("<i", payload[8:12])
    return s, player_id


def wait_for_packet(sock, want_type, timeout=4.0, collect=None):
    """循环读帧，跳过非目标包，直到读到 want_type 或超时。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        sock.settimeout(max(0.1, deadline - time.time()))
        try:
            kind, seq, bits, payload = read_frame(sock)
        except socket.timeout:
            return None, b"", 0
        if kind != KIND_PACKET or not payload:
            continue
        ptype = payload[0]
        if collect is not None:
            collect.append(ptype)
        if ptype == want_type:
            # 返回去掉 packet_type 字节后的位流（此时位偏移 8，即字节对齐）
            return ptype, payload[1:], bits - 8
    return None, b"", 0


def make_rocket_state(w, name="Alpha"):
    """用位写入器构造 RocketState（字段顺序对齐 C# RocketState.Serialize）"""
    w.write_string(name)
    # NetLocation: 4 doubles + address string
    w.write_f64(100.5)
    w.write_f64(-200.25)
    w.write_f64(1.5)
    w.write_f64(-2.5)
    w.write_string("Earth")
    # rotation / angular_velocity
    w.write_f32(0.75)
    w.write_f32(0.125)
    # throttle_on(bool 1位) / throttle_percent / rcs(bool 1位)
    w.write_bool(True)
    w.write_f32(0.5)
    w.write_bool(False)
    # parts: count + [id + PartState]
    w.write_i32(2)
    for i in range(2):
        w.write_i32(i)                          # part id
        w.write_string("Part%d" % i)            # name
        w.write_f32(float(i))                   # x
        w.write_f32(float(i * 2))               # y
        w.write_f32(1.0)                        # orientation_x
        w.write_f32(1.0)                        # orientation_y
        w.write_f32(0.0)                        # orientation_z
        w.write_f32(300.0)                      # temperature
        w.write_i32(0)                          # number_variables count
        w.write_i32(0)                          # toggle_variables count
        w.write_i32(0)                          # text_variables count
        w.write_bool(True)                      # burns is null
    w.write_i32(0)                              # joints count
    w.write_i32(0)                              # stages count


def main():
    print("=== 双客户端业务测试 %s:%d ===" % (HOST, PORT))

    sa, pid_a = connect_and_handshake("client-A")
    sb, pid_b = connect_and_handshake("client-B")
    print("  A player_id=%d, B player_id=%d" % (pid_a, pid_b))

    # ---------- A 创建火箭 ----------
    local_id_sent = 7
    w = BitWriter()
    w.write_u8(PT_CREATE_ROCKET)
    w.write_f64(0.0)                 # world_time
    w.write_i32(local_id_sent)       # local_id（客户端本地临时 ID）
    w.write_i32(-1)                  # global_id（未分配）
    w.write_bool(False)              # for_launch
    make_rocket_state(w, "Alpha")
    sa.sendall(encode_frame(KIND_PACKET, 100, w.bytes(), w.bits()))

    # ① 创建者回执：LocalId 必须保留原值
    ptype, body, bits = wait_for_packet(sa, PT_CREATE_ROCKET)
    check("A 收到 CreateRocket 回执", ptype == PT_CREATE_ROCKET, "type=%s" % ptype)

    global_id = -1
    if ptype == PT_CREATE_ROCKET:
        world_time, = struct.unpack("<d", body[0:8])
        recv_local, = struct.unpack("<i", body[8:12])
        global_id, = struct.unpack("<i", body[12:16])
        check("回执 LocalId 保留原值（客户端靠它绑定 GlobalId）",
              recv_local == local_id_sent, "local_id=%d (期望 %d)" % (recv_local, local_id_sent))
        check("回执 GlobalId > 0", global_id > 0, "global_id=%d" % global_id)

    if global_id <= 0:
        print("  无法继续（未拿到 global_id）")
        return 1

    # ② 旁观者广播：LocalId 必须为 -1
    ptype, body, bits = wait_for_packet(sb, PT_CREATE_ROCKET)
    check("B 收到 CreateRocket 广播", ptype == PT_CREATE_ROCKET, "type=%s" % ptype)
    if ptype == PT_CREATE_ROCKET:
        b_local, = struct.unpack("<i", body[8:12])
        b_global, = struct.unpack("<i", body[12:16])
        check("广播 LocalId = -1（旁观者不认领）", b_local == -1, "local_id=%d" % b_local)
        check("广播 GlobalId 与回执一致", b_global == global_id,
              "b=%d a=%d" % (b_global, global_id))

    # ③ 权威分配
    ptype, body, bits = wait_for_packet(sa, PT_UPDATE_PLAYER_AUTHORITY)
    check("A 收到 UpdatePlayerAuthority", ptype == PT_UPDATE_PLAYER_AUTHORITY, "type=%s" % ptype)
    if ptype == PT_UPDATE_PLAYER_AUTHORITY:
        count, = struct.unpack("<i", body[0:4])
        ids = [struct.unpack("<i", body[4 + i * 4:8 + i * 4])[0] for i in range(count)]
        check("  A 的权威集合含新建火箭", global_id in ids, "authority=%s" % ids)

    # ---------- A 更新火箭位置 → B 收到 ----------
    w = BitWriter()
    w.write_u8(PT_UPDATE_ROCKET_PRIMARY)
    w.write_f64(1.0)                  # world_time
    w.write_i32(global_id)
    w.write_f64(555.5); w.write_f64(-666.25)
    w.write_f64(3.0);   w.write_f64(-4.0)
    w.write_string("Earth")
    w.write_f32(1.25); w.write_f32(0.5)
    sa.sendall(encode_frame(KIND_PACKET, 101, w.bytes(), w.bits()))

    ptype, body, bits = wait_for_packet(sb, PT_UPDATE_ROCKET_PRIMARY)
    check("B 收到 UpdateRocketPrimary 广播", ptype == PT_UPDATE_ROCKET_PRIMARY, "type=%s" % ptype)
    if ptype == PT_UPDATE_ROCKET_PRIMARY:
        rid, = struct.unpack("<i", body[8:12])
        x, y = struct.unpack("<dd", body[12:28])
        check("  火箭 ID 匹配", rid == global_id, "rid=%d" % rid)
        check("  位置同步正确", abs(x - 555.5) < 1e-9 and abs(y + 666.25) < 1e-9,
              "x=%.2f y=%.2f" % (x, y))

    # ---------- A 发聊天 → B 收到 ----------
    w = BitWriter()
    w.write_u8(PT_SEND_CHAT)
    w.write_i32(pid_a)
    w.write_string("hello from A")
    sa.sendall(encode_frame(KIND_PACKET, 102, w.bytes(), w.bits()))

    ptype, body, bits = wait_for_packet(sb, PT_SEND_CHAT)
    check("B 收到 SendChatMessage 广播", ptype == PT_SEND_CHAT, "type=%s" % ptype)
    if ptype == PT_SEND_CHAT:
        sender, = struct.unpack("<i", body[0:4])
        check("  发送者是 A", sender == pid_a, "sender=%d" % sender)

    # ---------- A 请求世界快照 ----------
    sa.sendall(encode_frame(KIND_REQUEST_WORLD_SNAPSHOT, 103, b"", 0))
    ptype, body, bits = wait_for_packet(sa, PT_CREATE_ROCKET)
    check("A 收到世界快照（CreateRocket 形式）", ptype == PT_CREATE_ROCKET, "type=%s" % ptype)
    if ptype == PT_CREATE_ROCKET:
        snap_global, = struct.unpack("<i", body[12:16])
        check("  快照里的火箭 ID 与创建的一致", snap_global == global_id,
              "snap=%d expect=%d" % (snap_global, global_id))

    # ---------- A 销毁火箭 → B 收到 ----------
    w = BitWriter()
    w.write_u8(PT_DESTROY_ROCKET)
    w.write_f64(2.0)
    w.write_i32(global_id)
    w.write_u8(1)                     # reason
    sa.sendall(encode_frame(KIND_PACKET, 104, w.bytes(), w.bits()))

    ptype, body, bits = wait_for_packet(sb, PT_DESTROY_ROCKET)
    check("B 收到 DestroyRocket 广播", ptype == PT_DESTROY_ROCKET, "type=%s" % ptype)

    sa.close()
    sb.close()

    print()
    if failures:
        print("结果: 失败 %d 项 -> %s" % (len(failures), ", ".join(failures)))
        return 1
    print("结果: 全部通过 ✓")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as e:
        print("异常: %s: %s" % (type(e).__name__, e))
        sys.exit(2)
