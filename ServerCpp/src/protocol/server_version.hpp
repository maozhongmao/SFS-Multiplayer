// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 登录前的版本探测载荷，对应客户端的 MultiplayerSFS.Common.ServerVersionCodec。
//
// 载荷：[握手版本 int32][协议版本 int32][版本字符串字节数 int32][版本字符串 UTF-8][flags int32]
// 全小端。握手版本就是 Hello 帧里服务端硬校验的那个数，所以它才是真正的兼容门槛；
// 版本字符串给人看，客户端拿它比前两位（1.2）。
//
// 尾部 flags 是可选扩展（bit0 = 需要连接密码）：旧客户端只判「够不够长」，会忽略这 4 字节；
// 旧服务端整段不发，新客户端读不到就按 0 处理。所以这条扩展是双向兼容的。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace sfs::protocol {

// 服务端对外报的版本号。改服务端功能时跟着客户端发布号走（前两位必须一致）。
inline constexpr std::string_view kServerVersionString = "1.3.3";

constexpr std::size_t kServerVersionStringMaxBytes = 64;

// 尾部 flags 的字节数与位定义（与客户端 ServerVersionCodec 保持一致）。
constexpr std::size_t kServerVersionFlagsBytes = 4;
constexpr std::int32_t kServerVersionFlagPasswordRequired = 1;

std::vector<std::uint8_t> EncodeServerVersionResponse(
    std::int32_t handshake_version,
    std::int32_t protocol_version,
    std::string_view server_version,
    std::int32_t flags = 0);

// 解析失败（长度不对、字符串越界）返回 false，不抛异常。
// flags 允许传 nullptr；载荷里没有尾部 flags（旧版本发来的）时写出 0。
bool DecodeServerVersionResponse(
    const std::vector<std::uint8_t>& payload,
    std::int32_t* handshake_version,
    std::int32_t* protocol_version,
    std::string* server_version,
    std::int32_t* flags = nullptr);

}  // namespace sfs::protocol
