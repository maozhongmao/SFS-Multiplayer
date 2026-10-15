// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "protocol/packets.hpp"

namespace sfs::server {

// 火箭的【部件表】压缩存证（RAM 压缩方案 B 的核心）。
//
// 为什么只压 parts 而不动整枚火箭：
//   实测单部件碎片约 3.4 KB/枚，其中 95% 以上是部件里的字符串与容器
//   （name + number_variables/text_variables 的 key/value，见 ServerCpp/RAM-COMPRESSION.md）；
//   而 location / rotation / throttle / rcs 这些标量是热数据 —— 每帧的权威刷新都在读它们，
//   必须留在展开形态。只压 parts 就能省掉绝大部分内存，且不必引入 LRU 或改动协议。
//
// 编码方式完全复用现成序列化器：把一个只装了 parts 的 RocketState 序列化，
// 再 deflate；解包走反路径。因此协议与存档格式都不变，客户端一行都不用改。
struct PartsBlob {
    std::uint32_t part_count = 0;      // 单独存计数：clear_debris 只数部件，不必解压
    bool compressed = true;            // 压缩后反而更大时按原样存（小部件表常见）
    std::vector<std::uint8_t> bytes;
};

// 打包部件表（输入通常直接来自 RocketState::parts）。
PartsBlob pack_parts(const std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& parts);

// 解包；成功时写入 out 并返回 true。
bool unpack_parts(const PartsBlob& blob, std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& out);

}  // namespace sfs::server
