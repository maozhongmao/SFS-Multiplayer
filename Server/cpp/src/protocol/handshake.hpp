// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 会话握手编解码，对应 C# 的 SfsMultiplayer.Protocol.SessionHandshakeCodec。
//
// 这一层不走 Lidgren 位格式，而是 BinaryWriter/BinaryReader 风格：
//   - 所有整数小端定长（uint32 / int32 / int64）
//   - 字符串 = int32 UTF-8 字节长度 + UTF-8 原始字节
//   - 字节 0 起始为魔数，线序字节为 "SSF1"（常量值 0x31465353 按小端写出）
//
// 权威依据：Server/cpp/golden/wire.txt 中 handshake.* 向量，由真实 C# 实现导出。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sfs::protocol {

// 与 C# SessionHandshakeCodec 的常量一一对应，改动前必须先确认客户端。
constexpr std::uint32_t kHandshakeMagic = 0x31465353u;
constexpr std::int32_t kHandshakeVersion = 2;
constexpr std::size_t kMaxHandshakeStringBytes = 4096;

// 对应 JoinRequestPacket。
struct JoinRequest {
    std::string username;
    std::string password;
    std::string solar_system_name;
    std::int32_t resume_player_id = -1;
    std::string resume_token;
};

// 对应 JoinResponsePacket。difficulty 用原始字节值，不引入游戏枚举依赖。
struct JoinResponse {
    std::int32_t player_id = 0;
    double update_rockets_period = 0.0;
    double chat_message_cooldown = 0.0;
    double world_time = 0.0;
    double send_time = 0.0;
    std::uint8_t difficulty = 0;
    std::string solar_system_name;
    std::string udp_session_token;
    std::string resume_token;
};

std::vector<std::uint8_t> EncodeHello(const JoinRequest& request);
std::vector<std::uint8_t> EncodeAck(const JoinResponse& response);

// 解码失败一律抛 std::invalid_argument，不返回半成品。
JoinRequest DecodeHello(const std::vector<std::uint8_t>& payload);
JoinResponse DecodeAck(const std::vector<std::uint8_t>& payload);

}  // namespace sfs::protocol
