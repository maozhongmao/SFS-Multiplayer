// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 加入准入校验的"规则层"（对齐 C# 服务端实现）：
//   C# 参考：ServerSettings.cs:20,22,25（Password 默认空 / BlockDuplicatePlayerNames 默认 true /
//            MaxUsernameLength 默认 32）
//            TcpMultiplayerServer.cs:504-513（ValidateJoin）
//            TcpMultiplayerServer.cs:1198-1204（PasswordMatches：SHA256 + 定时比较）
//   规则与协议/连接状态解耦：Server 负责在 Hello 分支按顺序调用它们。
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace sfs::server {

// 拒绝原因文本：与 C# ValidateJoin 的字符串逐字一致。
// 客户端把 Disconnect 帧的 payload 直接当终止原因展示（Client TcpTransportCore：
// LastDisconnectReason / 握手失败时抛出该文本），所以不能随手改写。
constexpr const char* kDenyServerFull = "Server is full.";
constexpr const char* kDenyInvalidUsername = "Invalid username.";
constexpr const char* kDenyUsernameInUse = "Username is already in use.";
constexpr const char* kDenyInvalidPassword = "Invalid password.";

// 加入准入配置，默认值全部对齐 C# ServerSettings：
//   password 默认空串 = 不校验（与改动前的 C++ 服务端行为一致，老部署不会被挡在门外）；
//   block_duplicate_player_names 默认 true（C# server.yml 的 block_duplicate_player_names: true）；
//   max_username_length 默认 32（C# MaxUsernameLength）。
struct JoinPolicy {
    std::string password;
    bool block_duplicate_player_names = true;
    int max_username_length = 32;
};

// 对齐 C# request.Username.Trim()：去掉首尾 Unicode 空白（char.IsWhiteSpace 等价集合）。
std::string trim_username(const std::string& value);

// 对齐 C# username.Length：UTF-16 码元个数（增补平面字符按 2 计，例如 emoji）。
int username_utf16_length(const std::string& value);

// 对齐 C# username.Any(char.IsControl)：存在 U+0000..U+001F 或 U+007F..U+009F。
bool has_control_char(const std::string& value);

// 对齐 C# string.Equals(a, b, StringComparison.OrdinalIgnoreCase)：
// 长度一致且逐码元相同（ASCII 大小写不敏感；非 ASCII 按字节精确比较）。
bool username_equals_ci(const std::string& a, const std::string& b);

// 对齐 C# PasswordMatches：expected 为空 = 不校验且恒 true；
// 否则两边各自 SHA-256 后做定时比较（不因首个不同字节提前返回）。
bool password_matches(const std::string& supplied, const std::string& expected);

// SHA-256 摘要（32 字节）。
std::array<std::uint8_t, 32> sha256_bytes(const std::string& data);

}  // namespace sfs::server
