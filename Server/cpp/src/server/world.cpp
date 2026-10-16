// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "server/util.hpp"
#include "util/log.hpp"
#include "state_store.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <limits>
#include <random>
#include <utility>

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

// ---------------------------------------------------------- 存档
void Server::load_world_from_disk() {
    if (state_path_.empty()) return;
    // 主档坏了自动回退 .bak，并把坏档改名隔离（详见 state_store 的 load_state_with_fallback）。
    // 这一步绝不留 NDEBUG 式的静默：用了哪份、为什么回退，必须进日志。
    const sfs::store::LoadResult loaded = sfs::store::load_state_with_fallback(state_path_);
    if (!loaded.ok) {
        SFS_LOGW(Cat::World, "[cpp-server] 存档未加载: 主档=[%s] 备份=[%s]",
                 loaded.primary_error.c_str(), loaded.backup_error.c_str());
        return;
    }
    if (loaded.source == sfs::store::LoadSource::kBackup) {
        SFS_LOGW(Cat::World, "[cpp-server] 存档主档损坏，已回退备份: %s（坏档隔离为 %s）",
                 loaded.path_used.c_str(), loaded.isolated_path.c_str());
    }
    const auto& saved = loaded.world;
    // 关键：世界时间必须接上存档的值。
    // 从 0 重新计时会让客户端时间倒退约 100 万秒，物理状态与时间失配导致卡死。
    world_time_base_ = saved.world_time;
    difficulty_ = saved.difficulty;
    world_clock_origin_ = std::chrono::steady_clock::now();
    rockets_.clear();
    std::size_t part_count = 0;
    for (const auto& [rid, r] : saved.rockets) {
        RocketEntry e;
        e.state = r;
        e.owner_player = -1;
        part_count += r.parts.size();
        rockets_.emplace(rid, std::move(e));
    }
    SFS_LOGI(Cat::World, "[cpp-server] 存档已加载: 世界时间=%.3f 难度=%d 火箭=%zu 部件=%zu",
             world_time_base_, difficulty_, rockets_.size(), part_count);
}

void Server::save_world_to_disk() {
    if (state_path_.empty()) return;
    sfs::store::SavedWorld out;
    out.world_time = world_time();
    out.difficulty = difficulty_;
    for (auto& [rid, e] : rockets_) {
        // 冷火箭的部件表在 blob 里：存档前必须先展开，否则写出去的是空部件表（重开档世界全空）。
        e.parts_begin();
        out.rockets.emplace(rid, e.state);
        e.parts_end();
    }
    std::string err;
    if (!sfs::store::save_state(state_path_, out, &err)) {
        SFS_LOGE(Cat::World, "[cpp-server] 存档失败: %s", err.c_str());
    }
}

double Server::world_time() const {
    return world_time_base_ + steady_seconds_since(world_clock_origin_) * time_scale_;
}

// 对齐 C# RandomColor()：HSV(h, 1, 1) → RGB
sfs::protocol::Color3 Server::random_color() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    const float h = dist(rng_);
    const int i = static_cast<int>(h * 6.0f);
    const float f = h * 6.0f - static_cast<float>(i);
    const float p = 0.0f;                    // v*(1-s)，s=1
    const float q = 1.0f - f;                // v*(1-f*s)
    const float t = f;                       // v*(1-(1-f)*s)

    sfs::protocol::Color3 c;
    switch (i % 6) {
        case 0: c.r = 1.0f; c.g = t;    c.b = p;    break;
        case 1: c.r = q;    c.g = 1.0f; c.b = p;    break;
        case 2: c.r = p;    c.g = 1.0f; c.b = t;    break;
        case 3: c.r = p;    c.g = q;    c.b = 1.0f; break;
        case 4: c.r = t;    c.g = p;    c.b = 1.0f; break;
        default: c.r = 1.0f; c.g = p;   c.b = q;    break;
    }
    return c;
}

// 对齐 C# Convert.ToHexString(RandomNumberGenerator.GetBytes(24))：48 个十六进制字符
std::string Server::random_token() {
    static const char* kHex = "0123456789ABCDEF";
    std::uniform_int_distribution<int> dist(0, 255);
    std::string out;
    out.reserve(48);
    for (int i = 0; i < 24; ++i) {
        const int b = dist(rng_);
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

// 对齐 C# NextRocketId：1..INT_MAX 随机，避开已有 ID。
// （存档里的 ID 是这种大随机数，用自增 1,2,3 会和存档冲突）
std::int32_t Server::next_rocket_id() {
    std::uniform_int_distribution<std::int32_t> dist(1, std::numeric_limits<std::int32_t>::max());
    for (int attempt = 0; attempt < 200; ++attempt) {
        const std::int32_t id = dist(rng_);
        if (!rockets_.count(id)) return id;
    }
    for (std::int32_t id = 1;; ++id) {
        if (!rockets_.count(id)) return id;
    }
}

// ---------------------------------------------------------- 权威分配
// 对应 C# AuthorityAllocationPolicy.Allocate：
//   玩家声明控制某火箭则归其所有，否则按 player_id 排序轮询分配。
void Server::refresh_authorities() {
    update_authority_.clear();

    // 参与者 = 当前已握手的连接，按 player_id 升序（对齐 C# 的 OrderBy(PlayerId)）
    std::vector<std::int32_t> participants;
    for (const auto& [fd, c] : conns_) {
        if (c.handshaked && c.player_id >= 0) participants.push_back(c.player_id);
    }
    std::sort(participants.begin(), participants.end());

    if (participants.empty()) {
        // 没有在线玩家：世界里的火箭全都无人操控，正是最该冷却的时刻。
        cool_idle_rockets();
        return;
    }
    for (auto pid : participants) update_authority_[pid] = {};

    // 火箭按 id 升序
    std::vector<std::int32_t> rocket_ids;
    rocket_ids.reserve(rockets_.size());
    for (const auto& [rid, _] : rockets_) rocket_ids.push_back(rid);
    std::sort(rocket_ids.begin(), rocket_ids.end());

    std::size_t round_robin = 0;
    for (auto rid : rocket_ids) {
        std::int32_t owner = -1;
        for (auto pid : participants) {
            auto cit = controlled_rocket_.find(pid);
            if (cit != controlled_rocket_.end() && cit->second == rid) { owner = pid; break; }
        }
        if (owner < 0) {
            // 【权威保留】刚分离/刚对接出来的火箭先留给创建者几秒，别让服务端立刻按
            // "最近玩家"改判（改判会让它按服务端那份重建出来的状态瞬移，撞掉原来那枚）。
            auto hit = rockets_.find(rid);
            if (hit != rockets_.end() && hit->second.authority_hold_player >= 0 &&
                std::chrono::steady_clock::now() < hit->second.authority_hold_until &&
                std::find(participants.begin(), participants.end(),
                          hit->second.authority_hold_player) != participants.end()) {
                owner = hit->second.authority_hold_player;
            }
        }
        if (owner < 0) {
            // 未被任何人控制的火箭：归"离它最近的那个玩家"算，而不是归创建者。
            //
            // 为什么改（原策略是"创建者优先，否则轮询"）：物理只该在需要它的那台机器上发生。
            // 归创建者意味着"你在 1000 公里外造的一枚火箭，在你屏幕上照样是活的"，
            // 而它在你这边必须冻结成 Kinematic（你没有它的部件表，跑了物理也传不回来）——
            // 于是表现为"别人眼里的无人火箭是金刚不坏之身，撞不动也不烧不着"。
            // 改成按距离分配后：离你近的那枚归你算（正常碰撞、正常燃烧、正常坠落），
            // 离所有人都远的自动落到远端玩家头上，你这边就冻它，冻结范围被压到最小。
            //
            // 距离比较必须限定在同一地址（行星）内：跨天体的两个位置没有可比性，
            // 拿不同 address 的 x/y 直接相减会得到毫无意义的数值，可能把月球上的火箭
            // 判成"离地球上的玩家最近"。所以跨天体一律视为"不在附近"，交给下面的轮询兜底。
            auto eit = rockets_.find(rid);
            bool assigned = false;
            if (eit != rockets_.end()) {
                const sfs::protocol::NetLocation& rloc = eit->second.state.location;
                double best = -1.0;
                for (auto pid : participants) {
                    // 玩家自己的位置：优先用他正在操控的那枚火箭（一定有最新位置），
                    // 没有就退回他托管的任意一枚（这个循环里 update_authority_ 还没填这枚，
                    // 所以只能用"他自己造的"当锚点，位置足够新）。
                    std::int32_t probe = -1;
                    auto cit = controlled_rocket_.find(pid);
                    if (cit != controlled_rocket_.end()) probe = cit->second;
                    if (probe < 0) probe = (eit->second.owner_player == pid) ? rid : -1;
                    if (probe < 0) continue;
                    auto pit = rockets_.find(probe);
                    if (pit == rockets_.end()) continue;
                    const sfs::protocol::NetLocation& ploc = pit->second.state.location;
                    if (ploc.address != rloc.address) continue;   // 跨天体不可比
                    double dx = ploc.x - rloc.x;
                    double dy = ploc.y - rloc.y;
                    double d2 = dx * dx + dy * dy;
                    if (best < 0.0 || d2 < best) { best = d2; owner = pid; }
                }
                assigned = best >= 0.0;
            }
            // 所有人都跨天体（或位置都没传上来）→ 退回旧策略兜底。
            if (!assigned) {
                if (eit != rockets_.end() && eit->second.owner_player >= 0 &&
                    std::find(participants.begin(), participants.end(), eit->second.owner_player) != participants.end()) {
                    owner = eit->second.owner_player;
                } else {
                    owner = participants[round_robin++ % participants.size()];
                }
            }
        }
        update_authority_[owner].insert(rid);
    }

    // 发给每个玩家：集合变化时立刻发，之外每 2 秒补发一次兜底
    //（客户端在世界加载/重置时会清空这个集合，纯"变化才发"会让它永久空着、一枚火箭状态都不发）。
    // 补发是低频的，不会有 C# 那次每轮全量重发的风暴（真机曾达 37820 次发送）。
    for (auto& [fd, c] : conns_) {
        if (!c.handshaked || c.player_id < 0) continue;
        std::vector<std::int32_t> ids;
        auto ait = update_authority_.find(c.player_id);
        if (ait != update_authority_.end()) ids.assign(ait->second.begin(), ait->second.end());
        std::sort(ids.begin(), ids.end());
        auto prev = sent_authority_.find(c.player_id);
        bool same = prev != sent_authority_.end() && prev->second == ids;
        if (same && ++authority_resend_tick_ < 2) continue;   // 校验周期 1s -> 约每 2 秒补发一次
        authority_resend_tick_ = 0;
        sent_authority_[c.player_id] = ids;
        sfs::protocol::UpdatePlayerAuthorityPacket pkt;
        pkt.rocket_ids = ids;
        send_packet(fd, static_cast<std::uint8_t>(sfs::protocol::PacketType::kUpdatePlayerAuthority), pkt);
    }

    // 权威重算完，顺手冷却"当前没人在操控"的火箭（宏关闭时为空操作）。
    cool_idle_rockets();
}

// 【冷火箭 blob】冷却"当前没人在操控"的火箭：把 state.parts 压进 blob 并清空展开态。
// 判据用 controlled_rocket_（只有连接中的玩家会出现在这里）：被操控的火箭每帧都在被读，
// 保持展开；无人操控的（碎片、发射台、对方离线留下的）下次被快照/模块包访问时自动 ensure。
// 宏关闭时整个函数体为空，不产生任何代码（默认行为不变）。
void Server::cool_idle_rockets() {
#if SFS_COLD_ROCKET_BLOB_ENABLED
    std::unordered_set<std::int32_t> controlled;
    for (const auto& [pid, rid] : controlled_rocket_) {
        (void)pid;
        if (rid >= 0) controlled.insert(rid);
    }
    std::size_t cooled = 0;
    for (auto& [rid, entry] : rockets_) {
        if (controlled.count(rid) != 0) continue;
        if (!entry.parts_in_memory && entry.state.parts.empty()) continue;   // 已经是 blob 形态
        entry.commit_parts();
        ++cooled;
    }
    if (cooled != 0) {
        // 诊断行必须 ASCII：服务端 stdout 在目标设备终端上只保证 ASCII（TMP 无中文字形）。
        SFS_LOGI(Cat::World, "[cpp-server] cold-rocket blob: packed %llu idle rocket(s)",
                 static_cast<unsigned long long>(cooled));
    }
#endif
}

// ---------------------------------------------------------- 世界快照
// 对应 C# SendWorldSnapshot：逐枚火箭发 CreateRocket（GlobalId=火箭ID，LocalId 默认 0）
void Server::send_world_snapshot(int fd) {
    std::vector<std::int32_t> ids;
    ids.reserve(rockets_.size());
    for (const auto& [rid, _] : rockets_) ids.push_back(rid);
    std::sort(ids.begin(), ids.end());

    for (auto rid : ids) {
        auto it = rockets_.find(rid);
        if (it == rockets_.end()) continue;
        sfs::protocol::CreateRocketPacket pkt;
        pkt.world_time = world_time();
        pkt.local_id = -1;
        pkt.global_id = rid;
        pkt.for_launch = false;
        it->second.parts_begin();          // 冷火箭：先把部件表解包出来再拷贝
        pkt.rocket = it->second.state;
        it->second.parts_end();            // 拷完立刻回包（pkt.rocket 是独立副本）
        send_packet(fd, static_cast<std::uint8_t>(sfs::protocol::PacketType::kCreateRocket), pkt);
    }
}

// 单枚火箭快照：客户端插值陈旧时只补这一枚。
// 千万别复用 send_world_snapshot —— 那会把全世界所有火箭都用 CreateRocket 重发一遍，
// 客户端对已同步火箭会整枚重建，配合"插值陈旧→再请求"形成自激循环（实测 fps=1）。
void Server::send_rocket_snapshot(int fd, std::int32_t rocket_id) {
    auto it = rockets_.find(rocket_id);
    if (it == rockets_.end()) return;
    sfs::protocol::CreateRocketPacket pkt;
    pkt.world_time = world_time();
    pkt.local_id = -1;
    pkt.global_id = rocket_id;
    pkt.for_launch = false;
    it->second.parts_begin();              // 冷火箭：先把部件表解包出来再拷贝
    pkt.rocket = it->second.state;
    it->second.parts_end();                // 拷完立刻回包（pkt.rocket 是独立副本）
    send_packet(fd, static_cast<std::uint8_t>(sfs::protocol::PacketType::kCreateRocket), pkt);
}

}  // namespace sfs::server
