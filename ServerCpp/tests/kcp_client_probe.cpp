// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// KCP 客户端探针：登录页那套探测 + 会话心跳的端到端契约验证，全部在同一个 KCP 会话里完成
// （和客户端 JoinMenu.QueryServerStatusAsync 的用法一致，不建玩家会话）：
//   1) ServerInfoRequest(9)     -> ServerInfoResponse(10)     人数 / 上限
//   2) ServerVersionRequest(11) -> ServerVersionResponse(12)  握手版本 / 协议版本 / 版本串
//   3) Ping(4)                  -> Pong(5)                    payload 必须原样回显（客户端靠它算 RTT）
//
// 用法: ./kcp-client-probe [端口] [主机]      默认 9807 127.0.0.1

#include "net/kcp_transport.hpp"
#include "protocol/codec.hpp"
#include "protocol/server_info.hpp"
#include "protocol/server_version.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace {

using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

constexpr std::uint16_t kDefaultPort = 9807;

bool SendFrame(sfs::net::KcpTransport& transport, std::uint32_t conv,
               TcpFrameKind kind, std::vector<std::uint8_t> payload) {
    TcpFrame frame{};
    frame.kind = kind;
    frame.sequence = 0;
    frame.payload = std::move(payload);
    frame.payload_bits = static_cast<std::int32_t>(frame.payload.size() * 8);
    const auto bytes = sfs::protocol::EncodeTcpFrame(frame);
    return transport.send(conv, bytes.data(), static_cast<int>(bytes.size()));
}

// 服务端可能穿插别的帧（心跳、广播），只挑目标类型，其余丢弃。
bool WaitForFrame(sfs::net::KcpTransport& transport, std::uint32_t conv,
                  TcpFrameKind want, TcpFrame* out, int attempts = 500) {
    std::string msg;
    for (int i = 0; i < attempts; ++i) {
        transport.poll(10);
        const int got = transport.receive(conv, &msg);
        if (got <= 0) continue;
        TcpFrame decoded{};
        try {
            decoded = sfs::protocol::DecodeTcpFrame(
                std::vector<std::uint8_t>(msg.begin(), msg.end()));
        } catch (const std::exception& e) {
            std::printf("  解码失败: %s\n", e.what());
            continue;
        }
        if (decoded.kind != want) continue;
        *out = std::move(decoded);
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint16_t port =
        (argc > 1) ? static_cast<std::uint16_t>(std::atoi(argv[1])) : kDefaultPort;
    const char* host = (argc > 2) ? argv[2] : "127.0.0.1";
    const std::uint32_t conv = 0x5A5A0001u;

    sfs::net::KcpTransport transport;
    if (!transport.init(0)) {
        std::printf("本地绑定失败\n");
        return 1;
    }
    if (!transport.connect(conv, host, port)) {
        std::printf("connect 失败\n");
        return 1;
    }
    std::printf("KCP 会话已建立 conv=0x%X -> %s:%u\n", conv, host, port);

    int failures = 0;

    // ---- 1) 人数探测 ----
    if (!SendFrame(transport, conv, TcpFrameKind::ServerInfoRequest, {})) {
        std::printf("ServerInfoRequest 发送失败\n");
        return 1;
    }
    TcpFrame info{};
    if (!WaitForFrame(transport, conv, TcpFrameKind::ServerInfoResponse, &info)) {
        std::printf("FAIL 未收到 ServerInfoResponse\n");
        ++failures;
    } else {
        try {
            const auto counts = sfs::protocol::DecodeServerInfoResponse(info.payload);
            std::printf("PASS ServerInfoResponse: players=%d max=%d\n", counts.first, counts.second);
        } catch (const std::exception& e) {
            std::printf("FAIL ServerInfoResponse 载荷非法: %s\n", e.what());
            ++failures;
        }
    }

    // ---- 2) 版本探测 ----
    if (!SendFrame(transport, conv, TcpFrameKind::ServerVersionRequest, {})) {
        std::printf("ServerVersionRequest 发送失败\n");
        return 1;
    }
    TcpFrame version{};
    if (!WaitForFrame(transport, conv, TcpFrameKind::ServerVersionResponse, &version)) {
        std::printf("FAIL 未收到 ServerVersionResponse\n");
        ++failures;
    } else {
        std::int32_t handshake = 0;
        std::int32_t protocol = 0;
        std::string server_version;
        if (sfs::protocol::DecodeServerVersionResponse(
                version.payload, &handshake, &protocol, &server_version)) {
            std::printf("PASS ServerVersionResponse: handshake=%d protocol=%d version='%s'\n",
                        handshake, protocol, server_version.c_str());
        } else {
            std::printf("FAIL ServerVersionResponse 载荷非法（%zu 字节）\n", version.payload.size());
            ++failures;
        }
    }

    // ---- 3) Ping -> Pong（客户端心跳正是这样算 RTT 的） ----
    const std::int64_t ticks = 1234567890123LL;
    std::vector<std::uint8_t> ping_payload(sizeof(ticks));
    std::memcpy(ping_payload.data(), &ticks, sizeof(ticks));
    if (!SendFrame(transport, conv, TcpFrameKind::Ping, ping_payload)) {
        std::printf("Ping 发送失败\n");
        return 1;
    }
    TcpFrame pong{};
    if (!WaitForFrame(transport, conv, TcpFrameKind::Pong, &pong)) {
        std::printf("FAIL 未收到 Pong\n");
        ++failures;
    } else if (pong.payload != ping_payload) {
        // 回空 payload 或改写 payload 都会让客户端的延迟显示永远是空
        std::printf("FAIL Pong payload 未原样回显（收到 %zu 字节）\n", pong.payload.size());
        ++failures;
    } else {
        std::printf("PASS Pong: payload 原样回显 %zu 字节\n", pong.payload.size());
    }

    std::printf(failures == 0 ? "结果: PASS\n" : "结果: FAIL（%d 项）\n", failures);
    return failures == 0 ? 0 : 1;
}
