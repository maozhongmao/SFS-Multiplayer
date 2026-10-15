// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"

#include "util/config.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace sfs::server {

using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

// ---------------------------------------------------------- 加入时的初启状态
// 严格对齐 C# TcpServer.SendInitialState 的顺序与内容。
void Server::send_initial_state(int fd, std::int32_t joining_id, const std::string& joining_name) {
    using PT = sfs::protocol::PacketType;

    // ⓪ 【作弊策略 2026-10-15】先把策略下发一次：客户端据此显示/隐藏作弊入口、逐项允许或拦截。
    //    放在初启状态最前面，保证它比世界快照更早到达（客户端收到即生效，幂等）。
    //    没有配置文件 / 没有 cheats 段 = 总开关开 + 全项允许 = 与"没有策略"完全等价，行为不变。
    {
        const sfs::config::CheatConfig& cheats = sfs::config::active_cheat_config();
        sfs::protocol::UpdateCheatStatusPacket cheat;
        cheat.enabled = cheats.enabled;
        cheat.individual_flags = cheats.individual_flags;
        send_packet(fd, static_cast<std::uint8_t>(PT::kUpdateCheatStatus), cheat);
    }

    // ① 按 player_id 升序，把每个在线玩家（含自己）告知新玩家
    std::vector<std::pair<std::int32_t, int>> peers;  // (player_id, fd)
    for (const auto& [cfd, c] : conns_) {
        if (c.handshaked && c.player_id >= 0) peers.emplace_back(c.player_id, cfd);
    }
    std::sort(peers.begin(), peers.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (const auto& [pid, _] : peers) {
        auto pit = players_.find(pid);
        if (pit == players_.end()) continue;

        sfs::protocol::PlayerConnectedPacket pc;
        pc.player_id = pid;
        pc.username = pit->second.name;
        pc.icon_color = pit->second.color;
        pc.print_message = false;
        send_packet(fd, static_cast<std::uint8_t>(PT::kPlayerConnected), pc);

        sfs::protocol::UpdatePlayerControlPacket ctl;
        ctl.player_id = pid;
        auto cit = controlled_rocket_.find(pid);
        ctl.rocket_id = (cit != controlled_rocket_.end()) ? cit->second : -1;
        send_packet(fd, static_cast<std::uint8_t>(PT::kUpdatePlayerControl), ctl);
    }

    // ② 世界快照（逐枚 CreateRocket）
    send_world_snapshot(fd);

    // ③ 时间倍率（客户端靠它初始化 time scale，缺了会导致时间行为异常）
    sfs::protocol::TimeWarpPacket tw;
    tw.operation = sfs::protocol::TimeWarpOperation::kApplied;
    tw.multiplier = time_scale_;
    tw.world_time = world_time();
    send_packet(fd, static_cast<std::uint8_t>(PT::kTimeWarp), tw);

    // ④ 广播自己加入给其他人（PrintMessage=true 让其他人看到"已进入世界"）
    {
        sfs::protocol::PlayerConnectedPacket joined;
        joined.player_id = joining_id;
        joined.username = joining_name;
        auto pit = players_.find(joining_id);
        joined.icon_color = (pit != players_.end()) ? pit->second.color : sfs::protocol::Color3::default_color();
        joined.print_message = true;
        broadcast_packet(static_cast<std::uint8_t>(PT::kPlayerConnected), joined, fd);
    }

    // ⑤ 广播进入世界提示给其他人（屏幕文案必须英文，中文会显示方框）
    {
        sfs::protocol::PlayerEventToastPacket toast;
        toast.message = joining_name + " entered the world";
        broadcast_packet(static_cast<std::uint8_t>(PT::kShowToastMessage), toast, fd);
    }
}

}  // namespace sfs::server
