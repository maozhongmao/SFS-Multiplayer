// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one from https://mozilla.org/MPL/2.0/.

// 对接/拆解的纯计算部分，刻意不依赖 Server —— 这样旋转对齐、零件重编号、
// 连通块拆分这些最容易出错的数学可以脱离网络层单独跑单测。
//
// 对齐对象（C# TcpMultiplayerServer.cs）：
//   MergeDockedRockets / ConnectedPartGroups / ExtractRocket / PortDirectionDegrees / Rotate

#pragma once

#include "protocol/packets.hpp"

#include <cstdint>
#include <vector>

namespace sfs::dock {

// C# PortDirectionDegrees(part)：对接口朝向角（度）。
// OrientationY 的符号表示这一侧朝哪，方向角决定两枚火箭拼接时要转多少度才能对齐。
float port_direction_degrees(const sfs::protocol::PartState& port);

// C# Rotate(x, y, degrees, out rx, out ry)：绕原点旋转一个平面偏移量。
void rotate_offset(float x, float y, float degrees, float& out_x, float& out_y);

// 只读查找：该 partId 在这枚火箭里返回指针，否则 nullptr。
const sfs::protocol::PartState* part_find(const sfs::protocol::RocketState& rocket,
                                          std::int32_t part_id);

// 当前最大的零件编号（给冲突重编号当起点）。
std::int32_t part_max_id(const sfs::protocol::RocketState& rocket);

// C# MergeDockedRockets：以 keep 为基准合并 remove，服务端权威产出合并后的火箭。
// remove 的部件会先相对自己的对接口做旋转平移，再并进 keep；编号冲突时重编号。
sfs::protocol::RocketState merge_docked_rockets(sfs::protocol::RocketState keep,
                                                const sfs::protocol::RocketState& remove,
                                                std::int32_t keep_part_id,
                                                std::int32_t remove_part_id);

// C# ConnectedPartGroups：按 joints 把部件切成若干连通块。
std::vector<std::vector<std::int32_t>> connected_part_groups(
    const sfs::protocol::RocketState& rocket);

// 含有某个零件的那一堆；找不到返回空（调用方负责兜底）。
std::vector<std::int32_t> group_containing(const std::vector<std::vector<std::int32_t>>& groups,
                                           std::int32_t part_id);

// C# ExtractRocket：从原火箭摘出一部分零件组成新火箭，连带筛选相关的 joints / stages。
sfs::protocol::RocketState extract_rocket(const sfs::protocol::RocketState& source,
                                          const std::vector<std::int32_t>& ids);

}  // namespace sfs::dock
