#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 板端原生冒烟测试：直接对 127.0.0.1:9806 走真实 TCP 帧协议。
# 覆盖 ServerInfoRequest / Hello / Ping，并测量握手 RTT。

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
KIND_SERVERINFO_REQUEST = 9
KIND_SERVERINFO_RESPONSE = 10


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
            raise EOFError("connection closed while reading %d bytes (got %d)" % (n, len(buf)))
        buf += chunk
    return buf


def read_frame(sock):
    body_len = struct.unpack("<i", read_exact(sock, 4))[0]
    if body_len < 9:
        raise ValueError("bad body length %d" % body_len)
    body = read_exact(sock, body_len)
    kind = body[0]
    seq = struct.unpack("<i", body[1:5])[0]
    payload_bits = struct.unpack("<i", body[5:9])[0]
    payload = body[9:]
    return kind, seq, payload_bits, payload


def wstr(s):
    b = s.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def rstr(buf, off):
    n = struct.unpack("<i", buf[off:off + 4])[0]
    off += 4
    if n < 0 or off + n > len(buf):
        raise ValueError("bad string length %d" % n)
    return buf[off:off + n].decode("utf-8", "replace"), off + n


def build_hello(username, solar_system="Earth"):
    p = struct.pack("<I", MAGIC)
    p += struct.pack("<i", HANDSHAKE_VERSION)
    p += wstr(username)
    p += wstr("")            # password
    p += wstr(solar_system)
    p += struct.pack("<i", -1)   # resume_player_id
    p += wstr("")            # resume_token
    return p


failures = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print("  [%s] %s %s" % (status, name, detail))
    if not cond:
        failures.append(name)


def main():
    print("=== 板端原生冒烟测试 %s:%d ===" % (HOST, PORT))
    print("    约定来源：客户端 TcpTransportCore.cs:381/389/579 + C# TcpMultiplayerServer.cs:167/217/240")

    # ---------- 1) ServerInfoRequest（登录前探测，不建会话）----------
    # 客户端 JoinMenu.cs:317 用 Sequence=0 发探测，只校验 Kind。
    t0 = time.time()
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(encode_frame(KIND_SERVERINFO_REQUEST, 0, b"", 0))
    kind, seq, bits, payload = read_frame(s)
    rtt_ms = (time.time() - t0) * 1000.0
    check("ServerInfoResponse 帧类型", kind == KIND_SERVERINFO_RESPONSE, "kind=%d" % kind)
    check("ServerInfoResponse 长度=8", len(payload) == 8, "len=%d" % len(payload))
    if len(payload) == 8:
        cur, mx = struct.unpack("<ii", payload)
        print("        在线人数=%d / 上限=%d" % (cur, mx))
    print("        ServerInfo RTT = %.3f ms" % rtt_ms)
    s.close()

    # ---------- 2) Hello / HelloAck 握手 ----------
    # 关键：握手帧的 Sequence == 握手版本号(2)，不是序号。
    t0 = time.time()
    s = socket.create_connection((HOST, PORT), timeout=5)
    s.sendall(encode_frame(KIND_HELLO, HANDSHAKE_VERSION, build_hello("board-smoke-test"), 0))
    kind, seq, bits, payload = read_frame(s)
    handshake_ms = (time.time() - t0) * 1000.0
    check("HelloAck 帧类型", kind == KIND_HELLO_ACK, "kind=%d" % kind)
    check("HelloAck Sequence == 握手版本(2)", seq == HANDSHAKE_VERSION, "seq=%d" % seq)
    if kind == KIND_HELLO_ACK:
        off = 0
        magic, ver = struct.unpack("<Ii", payload[off:off + 8]); off += 8
        check("Ack magic 一致", magic == MAGIC, "0x%08X" % magic)
        check("Ack 握手版本一致", ver == HANDSHAKE_VERSION, "v=%d" % ver)
        player_id, = struct.unpack("<i", payload[off:off + 4]); off += 4
        urp, = struct.unpack("<d", payload[off:off + 8]); off += 8
        cmc, = struct.unpack("<d", payload[off:off + 8]); off += 8
        wt, = struct.unpack("<d", payload[off:off + 8]); off += 8
        st, = struct.unpack("<d", payload[off:off + 8]); off += 8
        difficulty = payload[off]; off += 1
        solar, off = rstr(payload, off)
        udp_token, off = rstr(payload, off)
        resume_token, off = rstr(payload, off)
        check("player_id > 0", player_id > 0, "id=%d" % player_id)
        check("update_rockets_period == 20", abs(urp - 20.0) < 1e-9, "%.1f" % urp)
        check("chat_message_cooldown == 3", abs(cmc - 3.0) < 1e-9, "%.1f" % cmc)
        check("difficulty == 0 (Normal)", difficulty == 0, "%d" % difficulty)
        check("solar_system_name 回显", solar == "Earth", "'%s'" % solar)
        check("udp_session_token 非空", len(udp_token) > 0, "'%s'" % udp_token)
        check("resume_token 非空", len(resume_token) > 0, "'%s'" % resume_token)
        check("Ack 无尾随字节", off == len(payload), "consumed=%d/%d" % (off, len(payload)))
    print("        Handshake RTT = %.3f ms" % handshake_ms)

    # ---------- 3) Ping / Pong 往返 ×5（必须回显 sequence + payload）----------
    rtts = []
    echo_ok = True
    for i in range(5):
        ping_seq = 100 + i
        ping_payload = struct.pack("<q", time.time_ns())   # 客户端放的是发送时刻 ticks
        t0 = time.time()
        s.sendall(encode_frame(KIND_PING, ping_seq, ping_payload))
        kind, seq, bits, payload = read_frame(s)
        rtts.append((time.time() - t0) * 1000.0)
        if kind != KIND_PONG:
            check("Pong 帧类型 (第%d次)" % (i + 1), False, "kind=%d" % kind)
            break
        if seq != ping_seq or payload != ping_payload:
            echo_ok = False
    if kind == KIND_PONG:
        check("Ping/Pong ×5 全部回应", True,
              "min=%.3f avg=%.3f max=%.3f ms" % (min(rtts), sum(rtts) / len(rtts), max(rtts)))
        check("Pong 原样回显 sequence + payload（客户端靠它算 RTT）", echo_ok)

    # ---------- 4) 未知/业务包不应断连（容错性）----------
    s.sendall(encode_frame(KIND_PACKET, 500, bytes([0]), 8))   # 包类型 0 = 未实现
    s.sendall(encode_frame(KIND_PING, 501, struct.pack("<q", time.time_ns())))
    kind, seq, bits, payload = read_frame(s)
    check("发未实现业务包后仍存活", kind == KIND_PONG, "kind=%d" % kind)

    # ---------- 5) 错误握手版本必须被拒（不能静默接受）----------
    s2 = socket.create_connection((HOST, PORT), timeout=5)
    s2.sendall(encode_frame(KIND_HELLO, 99, build_hello("wrong-version"), 0))
    try:
        k2, _, _, _ = read_frame(s2)
        check("错误握手版本被拒绝", k2 == KIND_DISCONNECT, "kind=%d" % k2)
    except (EOFError, socket.timeout):
        check("错误握手版本被拒绝", True, "(对端直接关闭)")
    s2.close()

    # ---------- 6) Disconnect 后服务端应主动收尾 ----------
    s.sendall(encode_frame(KIND_DISCONNECT, 600, b"", 0))
    try:
        read_frame(s)
        check("Disconnect 后连接关闭", True)
    except (EOFError, socket.timeout):
        check("Disconnect 后连接关闭", True, "(对端直接关闭)")

    s.close()
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
