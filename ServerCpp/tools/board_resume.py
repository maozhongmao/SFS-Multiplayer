#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 板端原生测试：验证断线重连（resume）语义与快照世界时间戳。
#
# 这两个点直接对应客户端崩溃与卡死的两个根因：
#   1) resume 时服务端必须复用原 player_id / udp_token / resume_token。
#      若每次都给新身份，客户端会判定重连失败并无限重连，而每次重连又收一遍
#      完整世界快照（反复销毁重建全部火箭），最终在 Unity 侧崩溃。
#   2) CreateRocket（含世界快照）里的 world_time 必须是真实世界时间。
#      客户端把它当插值器时间戳用（Interpolator.AddPacketToQueue），
#      恒为 0 会让插值器时间基准错乱。
#
# 帧格式：[body_len:i32][kind:u8][seq:i32][payload_bits:i32][payload]

import socket
import struct
import sys

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9806

MAGIC = 0x31465353
HANDSHAKE_VERSION = 2

KIND_HELLO = 1
KIND_HELLO_ACK = 2
KIND_PACKET = 3
KIND_PING = 4
KIND_PONG = 5

PT_CREATE_ROCKET = 9

passed = []
failed = []


def check(ok, label, detail=""):
    if ok:
        passed.append(label)
        print("  [PASS] %s %s" % (label, detail))
    else:
        failed.append(label)
        print("  [FAIL] %s %s" % (label, detail))


def encode_frame(kind, seq, payload, payload_bits=None):
    if payload_bits is None:
        payload_bits = len(payload) * 8
    body_len = 9 + len(payload)
    return (struct.pack("<i", body_len)
            + bytes([kind])
            + struct.pack("<i", seq)
            + struct.pack("<i", payload_bits)
            + bytes(payload))


def read_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("closed after %d/%d bytes" % (len(buf), n))
        buf += chunk
    return buf


def read_frame(sock):
    body_len = struct.unpack("<i", read_exact(sock, 4))[0]
    if body_len < 9:
        raise ValueError("bad body length %d" % body_len)
    body = read_exact(sock, body_len)
    return body[0], struct.unpack("<i", body[1:5])[0], struct.unpack("<i", body[5:9])[0], body[9:]


def wstr(s):
    b = s.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def rstr(buf, off):
    n = struct.unpack("<i", buf[off:off + 4])[0]
    off += 4
    if n < 0 or off + n > len(buf):
        raise ValueError("bad string length %d" % n)
    return buf[off:off + n].decode("utf-8", "replace"), off + n


def do_handshake(username, resume_player_id=-1, resume_token=""):
    """建立连接并握手，返回 (sock, ack_dict)。"""
    sock = socket.create_connection((HOST, PORT), timeout=8.0)
    payload = (struct.pack("<I", MAGIC)
               + struct.pack("<i", HANDSHAKE_VERSION)
               + wstr(username) + wstr("") + wstr("")
               + struct.pack("<i", resume_player_id)
               + wstr(resume_token))
    # 关键：Hello 帧的 Sequence 承载握手版本号(2)，不是序号
    # （对齐客户端 TcpTransportCore.cs:381）——填错会得到 handshake version mismatch
    sock.sendall(encode_frame(KIND_HELLO, HANDSHAKE_VERSION, payload))

    while True:
        kind, seq, bits, body = read_frame(sock)
        if kind == KIND_HELLO_ACK:
            return sock, parse_ack(body)
        if kind in (KIND_PING, KIND_PONG, KIND_PACKET):
            continue
        raise ValueError("unexpected frame kind %d during handshake" % kind)


def parse_ack(body):
    off = 8  # magic + version
    ack = {}
    ack["player_id"] = struct.unpack("<i", body[off:off + 4])[0]; off += 4
    ack["update_rockets_period"] = struct.unpack("<d", body[off:off + 8])[0]; off += 8
    ack["chat_message_cooldown"] = struct.unpack("<d", body[off:off + 8])[0]; off += 8
    ack["world_time"] = struct.unpack("<d", body[off:off + 8])[0]; off += 8
    ack["send_time"] = struct.unpack("<d", body[off:off + 8])[0]; off += 8
    ack["difficulty"] = body[off]; off += 1
    ack["solar_system_name"], off = rstr(body, off)
    ack["udp_token"], off = rstr(body, off)
    ack["resume_token"], off = rstr(body, off)
    return ack


def collect_snapshot_world_times(sock, want=3, budget=30):
    """读若干帧，收集 Packet 帧里 CreateRocket 的 world_time。"""
    times = []
    sock.settimeout(2.0)
    reads = 0
    while len(times) < want and reads < budget:
        try:
            kind, seq, bits, body = read_frame(sock)
        except (socket.timeout, EOFError):
            break
        reads += 1
        if kind != KIND_PACKET or len(body) < 17:
            continue
        if body[0] != PT_CREATE_ROCKET:
            continue
        wt = struct.unpack("<d", body[1:9])[0]
        local_id = struct.unpack("<i", body[9:13])[0]
        global_id = struct.unpack("<i", body[13:17])[0]
        times.append((wt, local_id, global_id))
    return times


print("=== resume / 快照时间戳测试 %s:%d ===" % (HOST, PORT))

# ── 首连：拿原始身份
sock1, ack1 = do_handshake("resume-test-A")
print("  首连: player=%d period=%g world_time=%.3f udp_token=%d 位"
      % (ack1["player_id"], ack1["update_rockets_period"],
         ack1["world_time"], len(ack1["udp_token"])))

check(ack1["update_rockets_period"] == 50.0, "UpdateRocketsPeriod = 50（对齐 C#）",
      "= %g" % ack1["update_rockets_period"])
check(len(ack1["udp_token"]) == 48, "UdpToken 是 48 位随机 hex（对齐 C#）",
      "= %d 位" % len(ack1["udp_token"]))
check(len(ack1["resume_token"]) == 48, "ResumeToken 是 48 位随机 hex",
      "= %d 位" % len(ack1["resume_token"]))
check(ack1["world_time"] > 100000.0, "HelloAck 世界时间来自存档（非从 0 开始）",
      "= %.3f" % ack1["world_time"])

# ── 快照里的 CreateRocket 必须带真实世界时间戳
times = collect_snapshot_world_times(sock1)
print("  快照 CreateRocket 样本: %d 条" % len(times))
if times:
    nonzero = [t for t in times if t[0] > 100000.0]
    check(len(nonzero) == len(times),
          "快照 CreateRocket 的 world_time 非 0（插值器时间戳正确）",
          "例: %.3f (local=%d global=%d)" % (times[0][0], times[0][1], times[0][2]))
    zero_count = len(times) - len(nonzero)
    if zero_count:
        print("        其中 %d 条仍为 0" % zero_count)
else:
    check(False, "收到快照 CreateRocket", "一条都没收到")

sock1.close()

# ── 重连：带 resume_player_id + resume_token，必须拿到同一身份
sock2, ack2 = do_handshake("resume-test-A", ack1["player_id"], ack1["resume_token"])
print("  重连: player=%d" % ack2["player_id"])
check(ack2["player_id"] == ack1["player_id"],
      "resume 复用原 player_id（客户端靠它认回会话）",
      "%d -> %d" % (ack1["player_id"], ack2["player_id"]))
check(ack2["udp_token"] == ack1["udp_token"],
      "resume 复用原 UdpToken（UDP 通道不乱）")
check(ack2["resume_token"] == ack1["resume_token"],
      "resume 复用原 ResumeToken（可继续重连）")
sock2.close()

# ── 反例：token 不对时必须分配新身份（防止冒用别人会话）
sock3, ack3 = do_handshake("resume-test-B", ack1["player_id"], "DEADBEEF" * 6)
print("  错误 token: player=%d" % ack3["player_id"])
check(ack3["player_id"] != ack1["player_id"],
      "错误 resume token 被拒绝（分配新 player_id）",
      "%d -> %d" % (ack1["player_id"], ack3["player_id"]))
sock3.close()

print()
print("结果: 通过 %d 项，失败 %d 项" % (len(passed), len(failed)))
if failed:
    print("失败项: " + ", ".join(failed))
    sys.exit(1)
print("全部通过")
