// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace sfs::protocol {

enum class TcpFrameKind : std::uint8_t {
    Hello = 1,
    HelloAck = 2,
    Packet = 3,
    Ping = 4,
    Pong = 5,
    Disconnect = 6,
    RequestWorldSnapshot = 7,
    RequestRocketSnapshot = 8,
    // 登录前的可选探测帧，不建立玩家会话（登录页的 Ping / 人数靠它）。
    ServerInfoRequest = 9,
    ServerInfoResponse = 10,
    // 登录前的版本探测：登录页头部那行"服务端版本 / 客户端版本"靠它。
    // 旧客户端不发、旧服务端不回，两边都只是探测不到，不影响进服。
    ServerVersionRequest = 11,
    ServerVersionResponse = 12,
};

struct TcpFrame {
    TcpFrameKind kind;
    std::int32_t sequence;
    std::vector<std::uint8_t> payload;
    std::int32_t payload_bits;

    static TcpFrame Packet(
        std::int32_t sequence,
        std::vector<std::uint8_t> payload,
        std::int32_t payload_bits) {
        return {TcpFrameKind::Packet, sequence, std::move(payload), payload_bits};
    }
};

// 必须与 C# TcpFrameCodec.ProtocolVersion 一致。
// 登录握手会拿它做版本核对，改了会导致新旧客户端连不上。
constexpr std::int32_t kProtocolVersion = 32;

constexpr std::size_t kMaxTcpPayloadBytes = 8u * 1024u * 1024u;

std::vector<std::uint8_t> EncodeTcpFrame(const TcpFrame& frame);
TcpFrame DecodeTcpFrame(const std::vector<std::uint8_t>& encoded);

struct UdpEnvelope {
    std::uint8_t kind;
    std::string token;
    std::vector<std::uint8_t> payload;
};

enum class UdpKind : std::uint8_t {
    Bind = 1,
    BindAck = 2,
    Data = 3,
};

std::vector<std::uint8_t> EncodeUdpEnvelope(
    UdpKind kind,
    const std::string& token,
    const std::vector<std::uint8_t>& payload);
UdpEnvelope DecodeUdpEnvelope(const std::vector<std::uint8_t>& encoded);

}  // namespace sfs::protocol
