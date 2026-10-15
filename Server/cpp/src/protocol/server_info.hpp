// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 登录前服务器探测载荷，对应 C# 的 SfsMultiplayer.Protocol.ServerInfoWire。
//
// 载荷固定 8 字节，两个小端 int32：[当前人数][人数上限]。
// 登录页的 Ping / 当前人数 / 人数上限就靠它，缺了它登录页信息是坏的。
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace sfs::protocol {

constexpr std::size_t kServerInfoResponseBytes = 8;

// current_players 必须 >= 0，max_players 必须 >= 1 且 >= current_players，
// 越界抛 std::invalid_argument（与 C# 的 ArgumentOutOfRangeException 等价）。
std::vector<std::uint8_t> EncodeServerInfoResponse(
    std::int32_t current_players,
    std::int32_t max_players);

std::pair<std::int32_t, std::int32_t> DecodeServerInfoResponse(
    const std::vector<std::uint8_t>& payload);

}  // namespace sfs::protocol
