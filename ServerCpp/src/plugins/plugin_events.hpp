// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - 事件载荷（引擎无关）。
//
// 事件是"粗粒度、批量"的：服务端按秒产出最新世界快照，宿主 (PluginHost)
// 每秒只投递一次；插件不会收到每帧回调，因此插件执行频率与世界规模解耦。

#pragma once

#include "plugins/plugin_types.hpp"

#include <cstdint>
#include <vector>

namespace sfs::plugins {

// 世界快照里的一枚火箭。POD、定长，便于直接映射成 JS 对象，
// 也避免在骨架里做每枚火箭的字符串分配。
struct RocketView {
    std::int32_t rocket_id   = -1;
    std::int32_t owner_player = -1;  // -1 = 无主
    std::int32_t controlled_by = -1; // 当前操控者 player_id，-1 = 无人
    std::int32_t part_count  = 0;
    double pos_x = 0.0, pos_y = 0.0, pos_z = 0.0;
    double vel_x = 0.0, vel_y = 0.0, vel_z = 0.0;
    double throttle = 0.0;           // 0..1
    std::uint8_t has_update_authority = 0;  // 该火箭当前是否有人有权上报
};

// 快照里的一个玩家。
struct PlayerView {
    std::int32_t player_id = -1;
    std::int32_t controlled_rocket = -1;
    char name[32] = {};  // UTF-8，最长 31 字节，宿主保证 NUL 结尾
};

// 每秒一次、批量投递的世界快照（粗粒度事件总线的唯一大载荷）。
struct WorldSnapshot {
    std::uint64_t tick = 0;
    double world_time = 0.0;
    std::uint32_t dropped_rockets = 0;  // 超出服务端快照上限而没进事件的枚数
    std::vector<RocketView> rockets;
    std::vector<PlayerView> players;
};

// 一次事件投递的载荷。指针只在 dispatch 期间有效，插件不得保存。
struct PluginEvent {
    EventKind kind = kEventNone;
    const WorldSnapshot* world = nullptr;  // kEventWorldSnapshot
    const PlayerView* player = nullptr;    // kEventPlayerJoined / kEventPlayerLeft
    const char* text = nullptr;            // kEventChatMessage（UTF-8，已限长）
};

}  // namespace sfs::plugins
