// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one from https://mozilla.org/MPL/2.0/.

// 对接（Dock）与拆解（Undock）的服务端处理。
// 纯计算部分放在 server/dock_math.{hpp,cpp}（可脱离网络层单测），这里只管
// 权限、双人确认、世界写回与广播。
//
// 对齐 C# TcpMultiplayerServer.HandleDockTransaction / HandleUndockTransaction。
//
// 迁移前的实现是"客户端算好 -> 服务端校验后转发"，缺三样东西：
//   1. 服务端权威重算合并（MergeDockedRockets）
//   2. 双人确认（_pendingDocks + 5 秒超时）
//   3. Undock 拆解（连通块拆分 -> 生成第二枚火箭）
// 而客户端代码里明确写着 "Multiplayer docking is committed by the server"
// 且从不发送 Committed=true —— 所以旧实现既不安全，也等于功能没通。

#include "server/server.hpp"
#include "server/dock_math.hpp"
#include "server/util.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>

namespace sfs::server {

namespace {

using sfs::log::Cat;
using sfs::protocol::DockTransactionOperation;
using sfs::protocol::JointState;
using sfs::protocol::PartState;
using sfs::protocol::RocketState;

void log_dock_reject(std::int32_t player_id, std::uint8_t raw_type, const char* step) {
    SFS_LOGW(Cat::World, "[cpp-server] 拒绝 DockTransaction: player=%d type=%u 环节=%s",
             player_id, static_cast<unsigned>(raw_type), step);
}

}  // namespace

// ---------------------------------------------------------------- 权限与控制者
bool Server::has_authority(std::int32_t player_id, std::int32_t rocket_id) const {
    const auto ait = update_authority_.find(player_id);
    return ait != update_authority_.end() && ait->second.count(rocket_id) > 0;
}

std::int32_t Server::controller_of(std::int32_t rocket_id) const {
    for (const auto& [pid, rid] : controlled_rocket_) {
        if (pid >= 0 && rid == rocket_id && players_.find(pid) != players_.end()) return pid;
    }
    return -1;
}

// ---------------------------------------------------------------- 对接
void Server::handle_dock_transaction(int fd, std::int32_t player_id, std::uint8_t raw_type,
                                     sfs::protocol::DockTransactionPacket& pkt) {
    using PT = sfs::protocol::PacketType;
    (void)fd;

    // 对齐 C#：服务端不处理"客户端自称已提交"的事务。
    // 合并结果只能由服务端算出来 —— 否则任何人都能下发一个 committed 包，
    // 用任意 id 覆写/删除别人的火箭。客户端实际上从不发送 Committed=true。
    if (pkt.committed || pkt.keep_rocket_id == pkt.remove_rocket_id) return;

    if (!sfs::protocol::IsValidDockOperation(static_cast<std::uint8_t>(pkt.operation))) {
        log_dock_reject(player_id, raw_type, "非法 operation");
        return;
    }

    if (pkt.operation == DockTransactionOperation::kUndock) {
        handle_undock_transaction(fd, player_id, raw_type, pkt);
        return;
    }

    // 归一化：始终让 keep <= remove，使同一对接在两端得到同一个键
    // （否则双人确认的两次请求落到不同 key 上，永远凑不齐）。
    if (pkt.keep_rocket_id > pkt.remove_rocket_id) {
        std::swap(pkt.keep_rocket_id, pkt.remove_rocket_id);
        std::swap(pkt.keep_part_id, pkt.remove_part_id);
    }

    auto keep_it = rockets_.find(pkt.keep_rocket_id);
    auto remove_it = rockets_.find(pkt.remove_rocket_id);
    if (keep_it == rockets_.end() || remove_it == rockets_.end()) {
        log_dock_reject(player_id, raw_type, "火箭不存在");
        return;
    }
    // 冷火箭：part_find 读的是 state.parts，两枚都得先展开；查完立刻回包，
    // 这样后面那些 early return（等确认、无权限……）不会把展开态长期留在这两个条目里。
    keep_it->second.parts_begin();
    remove_it->second.parts_begin();
    const bool port_missing =
        sfs::dock::part_find(keep_it->second.state, pkt.keep_part_id) == nullptr ||
        sfs::dock::part_find(remove_it->second.state, pkt.remove_part_id) == nullptr;
    keep_it->second.parts_end();
    remove_it->second.parts_end();
    if (port_missing) {
        log_dock_reject(player_id, raw_type, "对接口部件不存在");
        return;
    }

    const std::int32_t keep_controller = controller_of(pkt.keep_rocket_id);
    const std::int32_t remove_controller = controller_of(pkt.remove_rocket_id);
    // 对齐 C#：请求者必须是其中一方的控制者，或者对该火箭有权威。
    if (player_id != keep_controller && player_id != remove_controller &&
        !has_authority(player_id, pkt.keep_rocket_id)) {
        log_dock_reject(player_id, raw_type, "无权限");
        return;
    }

    // ---- 双人确认：两侧各有控制者且不是同一人时，两个人都要发过请求才结算。
    //     只收一侧的请求会变成"我的火箭被别人合并掉了"。
    const std::array<std::int32_t, 4> key{pkt.keep_rocket_id, pkt.remove_rocket_id,
                                          pkt.keep_part_id, pkt.remove_part_id};
    const auto now = std::chrono::steady_clock::now();
    PendingDock fresh;
    fresh.keep_rocket_id = pkt.keep_rocket_id;
    fresh.remove_rocket_id = pkt.remove_rocket_id;
    fresh.keep_part_id = pkt.keep_part_id;
    fresh.remove_part_id = pkt.remove_part_id;
    fresh.created_at = now;
    fresh.confirmations.push_back(player_id);

    PendingDock& state = pending_docks_.emplace(key, fresh).first->second;
    const bool expired = state.created_at.time_since_epoch().count() == 0 ||
                         now - state.created_at > kPendingDockTimeoutSeconds;
    if (expired) {
        state = fresh;
    } else if (std::find(state.confirmations.begin(), state.confirmations.end(), player_id) ==
               state.confirmations.end()) {
        state.confirmations.push_back(player_id);
    }

    const bool two_controllers =
        keep_controller >= 0 && remove_controller >= 0 && keep_controller != remove_controller;
    if (two_controllers) {
        const bool keep_ok = std::find(state.confirmations.begin(), state.confirmations.end(),
                                       keep_controller) != state.confirmations.end();
        const bool remove_ok = std::find(state.confirmations.begin(), state.confirmations.end(),
                                         remove_controller) != state.confirmations.end();
        if (!keep_ok || !remove_ok) {
            SFS_LOGI(Cat::World, "[cpp-server] DockTransaction 等待另一方确认: 已确认 %zu 人（需要 %d 和 %d）",
                     state.confirmations.size(), keep_controller, remove_controller);
            return;
        }
    }

    // ---- 服务端权威重算合并
    // 冷火箭：merge 同时读 keep / remove 两枚的部件表，两枚都要先展开。
    keep_it->second.parts_begin();
    remove_it->second.parts_begin();
    const RocketState merged = sfs::dock::merge_docked_rockets(
        keep_it->second.state, remove_it->second.state, pkt.keep_part_id, pkt.remove_part_id);
    const char* reject_reason = nullptr;
    if (!validate_rocket(merged, &reject_reason)) {
        // early return 也必须配对回包，否则两枚火箭会一直停在展开态。
        keep_it->second.parts_end();
        remove_it->second.parts_end();
        SFS_LOGE(Cat::World, "[cpp-server] 拒绝 DockTransaction: 合并结果校验失败(%s)",
                 reject_reason != nullptr ? reject_reason : "unknown");
        return;
    }

    keep_it->second.state = merged;          // keep 整表覆盖写
    keep_it->second.last_update = now;
    keep_it->second.parts_end();             // 写完回包，作废 blob 里的旧部件表
    // remove 整枚被删：条目连同它的 blob 一起销毁，不必（也不该）多花一次压缩回包。
    rockets_.erase(pkt.remove_rocket_id);
    pending_docks_.erase(key);

    // 悬垂清理：原先控制这两枚火箭的人必须解绑。
    // 不清的话原控制者会停在"同目标幂等回执"分支上，以为自己还控制着已消失的火箭。
    for (auto& [pid, rid] : controlled_rocket_) {
        if (rid != pkt.keep_rocket_id && rid != pkt.remove_rocket_id) continue;
        rid = -1;
        auto pit = players_.find(pid);
        if (pit != players_.end()) pit->second.controlled_rocket = -1;
        sfs::protocol::UpdatePlayerControlPacket ctl;
        ctl.player_id = pid;
        ctl.rocket_id = -1;
        broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePlayerControl), ctl, -1);
    }
    for (auto it = created_local_ids_.begin(); it != created_local_ids_.end();) {
        if (it->second == pkt.remove_rocket_id) it = created_local_ids_.erase(it);
        else ++it;
    }

    pkt.committed = true;
    pkt.world_time = world_time();
    pkt.merged_rocket = merged;
    pkt.second_rocket_id = -1;
    pkt.second_rocket.reset();
    broadcast_packet(raw_type, pkt, -1);

    if (two_controllers) {
        sfs::protocol::PlayerEventToastPacket toast;
        toast.message = players_[keep_controller].name + " docked with " +
                        players_[remove_controller].name;
        broadcast_packet(static_cast<std::uint8_t>(PT::kShowToastMessage), toast, -1);
    }

    SFS_LOGI(Cat::World, "[cpp-server] DockTransaction 已提交: player=%d keep=%d remove=%d 部件=%zu",
             player_id, pkt.keep_rocket_id, pkt.remove_rocket_id, merged.parts.size());

    refresh_authorities();
}

// ---------------------------------------------------------------- 拆解
void Server::handle_undock_transaction(int fd, std::int32_t player_id, std::uint8_t raw_type,
                                       sfs::protocol::DockTransactionPacket& pkt) {
    using PT = sfs::protocol::PacketType;
    (void)fd;

    auto source_it = rockets_.find(pkt.keep_rocket_id);
    if (source_it == rockets_.end()) {
        log_dock_reject(player_id, raw_type, "火箭不存在");
        return;
    }
    if (!has_authority(player_id, pkt.keep_rocket_id)) {
        log_dock_reject(player_id, raw_type, "无权威");
        return;
    }

    // 找到连接这两个部件的那个 joint —— 它就是拆解要断开的那座"桥"。
    auto& joints = source_it->second.state.joints;
    JointState bridge{};
    if (auto found = std::find_if(joints.begin(), joints.end(),
                                  [&pkt](const JointState& j) {
                                      return (j.part_a == pkt.keep_part_id &&
                                              j.part_b == pkt.remove_part_id) ||
                                             (j.part_a == pkt.remove_part_id &&
                                              j.part_b == pkt.keep_part_id);
                                  });
        found == joints.end()) {
        log_dock_reject(player_id, raw_type, "对接口之间没有连接");
        return;
    } else {
        bridge = *found;          // 留一份快照：任何一条提前返回都必须把它放回去
        joints.erase(found);
    }

    // 冷火箭：连通块划分 / 拆分 / 校验读的都是 state.parts，从这里展开，
    // 写回或任何提前返回时用 parts_end() 回包。
    source_it->second.parts_begin();
    const auto groups = sfs::dock::connected_part_groups(source_it->second.state);
    if (groups.size() != 2) {
        // 断开后不是恰好两堆：可能拆出了更多碎片，或者这两部件之间还有别的连接。
        // 严格对齐 C#：不提交事务。此时 joints 已被改动，必须还原 —— 否则世界被悄悄改坏。
        joints.push_back(bridge);
        source_it->second.parts_end();   // early return 也要配对
        SFS_LOGW(Cat::World, "[cpp-server] 拒绝 Undock: 断开后分成 %zu 堆（要求恰好 2 堆）", groups.size());
        return;
    }

    // keep 那堆 = 含请求者所在部件的一堆；另一堆就是被拆出去的那枚。
    std::vector<std::int32_t> keep_group = sfs::dock::group_containing(groups, pkt.keep_part_id);
    if (keep_group.empty()) keep_group = groups[0];
    std::vector<std::int32_t> second_group;
    for (const auto& group : groups) {
        if (group != keep_group) { second_group = group; break; }
    }
    if (second_group.empty()) {
        joints.push_back(bridge);
        source_it->second.parts_end();   // early return 也要配对
        log_dock_reject(player_id, raw_type, "取不到第二堆");
        return;
    }

    const RocketState source_snapshot = source_it->second.state;   // joints 已不含那座桥
    const RocketState first_rocket = sfs::dock::extract_rocket(source_snapshot, keep_group);
    const RocketState second_rocket = sfs::dock::extract_rocket(source_snapshot, second_group);

    const char* reject_reason = nullptr;
    if (!validate_rocket(first_rocket, &reject_reason) ||
        !validate_rocket(second_rocket, &reject_reason)) {
        joints.push_back(bridge);
        source_it->second.parts_end();   // early return 也要配对
        SFS_LOGE(Cat::World, "[cpp-server] 拒绝 Undock: 拆分结果校验失败(%s)",
                 reject_reason != nullptr ? reject_reason : "unknown");
        return;
    }

    const std::int32_t second_id = next_rocket_id();
    source_it->second.state = first_rocket;   // 整表覆盖写
    source_it->second.last_update = std::chrono::steady_clock::now();
    source_it->second.parts_end();            // 写完回包，作废 blob 里的旧部件表
    RocketEntry entry;
    entry.state = second_rocket;
    // 这是新建条目：parts_in_memory 仍是 false 但 state.parts 已有内容。这里回包把它
    // 直接存成冷火箭 blob；即便不回包，cool_idle_rockets() 也会兜底打包。
    entry.parts_end();
    entry.owner_player = source_it->second.owner_player;
    // 分离/合并产生的这枚新火箭同样保留权威：这两条是瞬移最常见的来源。
    entry.authority_hold_player = source_it->second.owner_player;
    entry.authority_hold_until = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(static_cast<int>(kAuthorityHoldSeconds * 1000));
    rockets_.emplace(second_id, std::move(entry));

    // 原来控制这枚火箭的人不能再控制它：拆分后两边的所有权要重新裁决。
    for (auto& [pid, rid] : controlled_rocket_) {
        if (rid != pkt.keep_rocket_id) continue;
        rid = -1;
        auto pit = players_.find(pid);
        if (pit != players_.end()) pit->second.controlled_rocket = -1;
        sfs::protocol::UpdatePlayerControlPacket ctl;
        ctl.player_id = pid;
        ctl.rocket_id = -1;
        broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePlayerControl), ctl, -1);
    }

    pkt.committed = true;
    pkt.world_time = world_time();
    pkt.merged_rocket = first_rocket;
    pkt.second_rocket_id = second_id;
    pkt.second_rocket = second_rocket;
    broadcast_packet(raw_type, pkt, -1);

    SFS_LOGI(Cat::World, "[cpp-server] Undock 已提交: player=%d 原=%d 新=%d（%zu / %zu 部件）",
             player_id, pkt.keep_rocket_id, second_id, first_rocket.parts.size(),
             second_rocket.parts.size());

    refresh_authorities();
}

}  // namespace sfs::server
