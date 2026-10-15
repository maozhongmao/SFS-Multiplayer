#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 板端负载测试：模拟 max_players 个并发玩家，每个玩家握手后持续 Ping。
# 目的：观察服务端在“满员 + 持续流量”下的 RSS / CPU 变化。

import socket
import struct
import sys
import threading
import time

HOST = "127.0.0.1"
PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 9806
N_CONN = int(sys.argv[2]) if len(sys.argv) > 2 else 16
PINGS = int(sys.argv[3]) if len(sys.argv) > 3 else 50

MAGIC = 0x31465353
HANDSHAKE_VERSION = 2
KIND_HELLO, KIND_HELLO_ACK, KIND_PACKET, KIND_PING, KIND_PONG = 1, 2, 3, 4, 5
KIND_SERVERINFO_REQUEST, KIND_SERVERINFO_RESPONSE = 9, 10

lock = threading.Lock()
stats = {"handshake_ok": 0, "handshake_fail": 0, "pong": 0, "timeout": 0, "error": 0}
latencies = []


def encode_frame(kind, seq, payload, payload_bits=None):
    if payload_bits is None:
        payload_bits = len(payload) * 8
    return (struct.pack("<i", 9 + len(payload)) + bytes([kind])
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
    return body[0], struct.unpack("<i", body[1:5])[0], body[9:]


def wstr(s):
    b = s.encode("utf-8")
    return struct.pack("<i", len(b)) + b


def hello(username):
    return (struct.pack("<I", MAGIC) + struct.pack("<i", HANDSHAKE_VERSION)
            + wstr(username) + wstr("") + wstr("Earth")
            + struct.pack("<i", -1) + wstr(""))


def worker(idx, results):
    try:
        s = socket.create_connection((HOST, PORT), timeout=10)
        s.sendall(encode_frame(KIND_SERVERINFO_REQUEST, 1, b"", 0))
        k, _, _ = read_frame(s)
        if k != KIND_SERVERINFO_RESPONSE:
            with lock:
                stats["error"] += 1
            return

        s.sendall(encode_frame(KIND_HELLO, 2, hello("load-%02d" % idx), 0))
        k, _, _ = read_frame(s)
        with lock:
            if k == KIND_HELLO_ACK:
                stats["handshake_ok"] += 1
            else:
                stats["handshake_fail"] += 1
                return

        short = 0
        for i in range(PINGS):
            t0 = time.time()
            s.sendall(encode_frame(KIND_PING, 1000 + i, b"", 0))
            k, _, _ = read_frame(s)
            dt = (time.time() - t0) * 1000.0
            if k == KIND_PONG:
                with lock:
                    stats["pong"] += 1
                    latencies.append(dt)
            else:
                short += 1
        results[idx] = short
        s.close()
    except Exception:
        with lock:
            stats["error"] += 1


def main():
    print("=== 负载测试: %d 并发连接 × %d 次 Ping (%s:%d) ===" % (N_CONN, PINGS, HOST, PORT))
    results = [0] * N_CONN
    t0 = time.time()
    threads = [threading.Thread(target=worker, args=(i, results)) for i in range(N_CONN)]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    elapsed = time.time() - t0

    print("  握手成功 : %d" % stats["handshake_ok"])
    print("  握手失败 : %d" % stats["handshake_fail"])
    print("  Pong 收到: %d  (期望 %d)" % (stats["pong"], N_CONN * PINGS))
    print("  异常     : %d" % stats["error"])
    if latencies:
        srt = sorted(latencies)
        p50 = srt[len(srt) // 2]
        p99 = srt[min(len(srt) - 1, int(len(srt) * 0.99))]
        print("  延迟(ms) : min=%.3f p50=%.3f p99=%.3f max=%.3f avg=%.3f"
              % (srt[0], p50, p99, srt[-1], sum(srt) / len(srt)))
    print("  总耗时   : %.3f s  (%.0f 请求/秒)" % (elapsed, (stats["pong"] / elapsed) if elapsed > 0 else 0))
    ok = (stats["handshake_ok"] == N_CONN and stats["pong"] == N_CONN * PINGS and stats["error"] == 0)
    print("  结果     : %s" % ("全部通过 ✓" if ok else "有失败 ✗"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
