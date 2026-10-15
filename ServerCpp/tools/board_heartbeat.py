#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 验证服务端心跳链路 + 世界时间下发。
#
# 机制（对齐 C# MaintenanceLoop / HandlePong）：
#   服务端每 2 秒发 Ping（payload = 8 字节 .NET ticks）
#   客户端原样回 Pong
#   服务端收到 Pong 后回 UpdateWorldTime（客户端的世界时间完全靠它推进）
#
# 关键：之前服务端只被动回 Pong、从不主动发 Ping，导致收不到 Pong，
#      也就永远不下发 WorldTime —— 客户端游戏时间停滞，表现为整体卡死。

import socket
import struct
import sys
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9806
WAIT_SECONDS = float(sys.argv[2]) if len(sys.argv) > 2 else 8.0

MAGIC = 0x31465353
HANDSHAKE_VERSION = 2
KIND_HELLO, KIND_HELLO_ACK, KIND_PACKET, KIND_PING, KIND_PONG, KIND_DISCONNECT = 1, 2, 3, 4, 5, 6
PT_UPDATE_WORLD_TIME = 6

failures = []


def check(name, cond, detail=""):
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, detail))
    if not cond:
        failures.append(name)
    return cond


def encode_frame(kind, seq, payload, bits):
    return (struct.pack("<i", 9 + len(payload)) + bytes([kind])
            + struct.pack("<i", seq) + struct.pack("<i", bits) + bytes(payload))


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


def wstr(s):
    b = s.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def build_hello(username):
    return (struct.pack("<I", MAGIC) + struct.pack("<i", HANDSHAKE_VERSION)
            + wstr(username) + wstr("") + wstr("Earth")
            + struct.pack("<i", -1) + wstr(""))


def main():
    print("=== 心跳 / 世界时间链路测试 %s:%d（观察 %.0f 秒）===" % (HOST, PORT, WAIT_SECONDS))
    s = socket.create_connection((HOST, PORT), timeout=10)
    hello = build_hello("heartbeat-test")
    s.sendall(encode_frame(KIND_HELLO, HANDSHAKE_VERSION, hello, len(hello) * 8))
    kind, seq, bits, payload = read_frame(s)
    check("握手成功", kind == KIND_HELLO_ACK, "kind=%d" % kind)

    pings = 0
    world_times = []
    deadline = time.time() + WAIT_SECONDS
    while time.time() < deadline:
        s.settimeout(max(0.2, deadline - time.time()))
        try:
            kind, seq, bits, payload = read_frame(s)
        except socket.timeout:
            break
        if kind == KIND_PING:
            pings += 1
            # 原样回显（客户端就是这么做的）
            s.sendall(encode_frame(KIND_PONG, seq, payload, bits))
        elif kind == KIND_PACKET and payload and payload[0] == PT_UPDATE_WORLD_TIME:
            wt, = struct.unpack("<d", payload[1:9])
            world_times.append(wt)

    check("服务端主动发 Ping（心跳）", pings >= 2, "收到 %d 次 Ping" % pings)
    check("Pong 后收到 UpdateWorldTime", len(world_times) >= 2,
          "收到 %d 次 WorldTime" % len(world_times))

    if len(world_times) >= 2:
        delta = world_times[-1] - world_times[0]
        check("世界时间在推进", delta > 0.0,
              "%.3f → %.3f（+%.3fs）" % (world_times[0], world_times[-1], delta))

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
