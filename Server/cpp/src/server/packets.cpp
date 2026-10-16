// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "server/util.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <string>

#if defined(_WIN32)
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

namespace {

// 日志上传落盘：文件名只保留安全字符，避免客户端用相对路径写到别处
std::string sanitize_upload_name(const std::string& raw) {
    std::string out;
    for (const char c : raw) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
        if (ok) out.push_back(c);
    }
    if (out.empty()) out = "upload.log";
    if (out.size() > 64) out.resize(64);
    return out;
}

// 上传文件名里的可读时间戳（本地时间，目标设备上是 UTC+0）
std::string upload_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm_buf{};
#if defined(_WIN32)
    ::localtime_s(&tm_buf, &now);
#else
    ::localtime_r(&now, &tm_buf);
#endif
    char stamp[32];
    if (std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tm_buf) == 0) {
        return "unknown-time";
    }
    return std::string(stamp);
}

// 存到进程工作目录下的 logs/（目标设备上是 data/logs
std::string save_uploaded_log(std::int32_t player_id,
                              const std::string& username,
                              const sfs::protocol::UploadLogPacket& pkt) {
#if defined(_WIN32)
    ::_mkdir("logs");
#else
    ::mkdir("logs", 0755);
#endif
    const std::string path = "logs/" + upload_timestamp() +
                             "_p" + std::to_string(player_id) + "_" +
                             sanitize_upload_name(username) + "_" +
                             sanitize_upload_name(pkt.file_name) + ".gz";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return std::string("(写入失败) ") + path;
    if (!pkt.data.empty()) {
        out.write(reinterpret_cast<const char*>(pkt.data.data()),
                  static_cast<std::streamsize>(pkt.data.size()));
    }
    out.close();
    return path;
}

// ---------------------------------------------------------- 校验（对齐 C# MultiplayerServer.Validation.cs）
constexpr std::size_t kMaxNameLength = 256;

bool all_finite_2(double a, double b) { return std::isfinite(a) && std::isfinite(b); }

bool all_finite_4(double a, double b, double c, double d) {
    return std::isfinite(a) && std::isfinite(b) && std::isfinite(c) && std::isfinite(d);
}

// C# ValidateFinite(location, rotation, angularVelocity)
bool validate_finite(const sfs::protocol::NetLocation& loc, float rotation, float angular_velocity) {
    if (!all_finite_4(loc.x, loc.y, loc.vx, loc.vy)) return false;
    if (!std::isfinite(rotation) || !std::isfinite(angular_velocity)) return false;
    if (loc.address.empty() || loc.address.size() > kMaxNameLength) return false;
    // 控制字符检查（对齐 C# location.Address.Any(char.IsControl)）
    for (unsigned char ch : loc.address) {
        if (ch < 0x20 || ch == 0x7F) return false;
    }
    return true;
}

bool validate_name(const std::string& value) {
    if (value.size() > kMaxNameLength) return false;
    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7F) return false;
    }
    return true;
}

// C# ValidateStages(stages, rocket)：阶段引用的零件必须存在
bool validate_stages(const std::vector<sfs::protocol::StageState>& stages,
                     const sfs::protocol::RocketState& rocket) {
    for (const auto& stage : stages) {
        if (stage.part_ids.size() > static_cast<std::size_t>(sfs::protocol::kMaxCollectionCount)) return false;
        for (auto id : stage.part_ids) {
            bool found = false;
            for (const auto& kv : rocket.parts) {
                if (kv.first == id) { found = true; break; }
            }
            if (!found) return false;
        }
    }
    return true;
}

// C# Math.Clamp(value, 0, 1)
double clamp01(double value) { return value < 0.0 ? 0.0 : (value > 1.0 ? 1.0 : value); }

// ---------------------------------------------------------- 模块包写回辅助
// 对齐 C# TcpMultiplayerServer.HandleEngine / HandleWheel / HandleBooster / HandleParachute /
// HandleMove / HandleResource 与 MultiplayerServer.Validation.cs 的 TryAuthorizedPart。
//
// C# 的 PartState.NumberVariables / ToggleVariables 是 Dictionary<string, ...>，
// `part.ToggleVariables[key] = value` 的语义是"键存在就覆盖、不存在就新增"。C++ 侧
// PartState 用保序 vector<pair> 承载（线上顺序即容器顺序，见 protocol/packets.hpp），
// 所以这里手工实现同一套字典语义。
void set_number_variable(sfs::protocol::PartState& part, const std::string& key, double value) {
    for (auto& [existing_key, existing_value] : part.number_variables) {
        if (existing_key == key) {
            existing_value = value;
            return;
        }
    }
    part.number_variables.emplace_back(key, value);
}

void set_toggle_variable(sfs::protocol::PartState& part, const std::string& key, bool value) {
    for (auto& [existing_key, existing_value] : part.toggle_variables) {
        if (existing_key == key) {
            existing_value = value;
            return;
        }
    }
    part.toggle_variables.emplace_back(key, value);
}

// 模块包拒绝必须留痕：静默丢弃会让"某模块状态没同步"永远无法从日志里查证。
void log_module_reject(std::int32_t player_id, std::uint8_t raw_type, std::int32_t rocket_id,
                       std::int32_t part_id, const char* reason) {
    // 限流：同一 player+rocket 每秒最多打一条（实测客户端拿着过期零件表时会刷上千条）
    static std::map<std::pair<std::int32_t, std::int32_t>, double> last_reject_log;
    {
        static std::chrono::steady_clock::time_point last_cleanup{};
        const auto now_tp = std::chrono::steady_clock::now();
        if (now_tp - last_cleanup > std::chrono::seconds(10)) {
            last_cleanup = now_tp;
            last_reject_log.clear();
        }
        const double now_s = std::chrono::duration<double>(now_tp.time_since_epoch()).count();
        auto& stamp = last_reject_log[{player_id, rocket_id}];
        if (now_s - stamp < 1.0) return;
        stamp = now_s;
    }
    SFS_LOGW(Cat::World, "[cpp-server] 拒绝模块包: player=%d type=%u rocket=%d part=%d 原因=%s",
             player_id, raw_type, rocket_id, part_id, reason);
}

}  // namespace

// -----------------------------------------------------------------------------
// 以下两个是跨翻译单元共用的校验/查询（对接与拆解也要用，故放在 server/dock.cpp 之外）。
// 声明见 server/server.hpp。它们必须保持外部链接 —— 早先放在这里的匿名命名空间里时，
// 只局限在本文件可用，于是对接/拆解不得不各自复制一份，逐渐就跑偏了。
// -----------------------------------------------------------------------------

// C# ValidateRocket(rocket)
bool validate_rocket(const sfs::protocol::RocketState& rocket, const char** reason) {
    if (!validate_name(rocket.rocket_name)) { if (reason) *reason = "rocket_name invalid"; return false; }
    const auto max_count = static_cast<std::size_t>(sfs::protocol::kMaxCollectionCount);
    if (rocket.parts.size() > max_count || rocket.joints.size() > max_count ||
        rocket.stages.size() > max_count) {
        if (reason) *reason = "collection count over limit";
        return false;
    }
    if (!validate_finite(rocket.location, rocket.rotation, rocket.angular_velocity)) {
        if (reason) *reason = "location/rotation not finite";
        return false;
    }
    if (!std::isfinite(rocket.throttle_percent)) { if (reason) *reason = "throttle not finite"; return false; }
    for (const auto& [id, part] : rocket.parts) {
        (void)id;
        if (!validate_name(part.name)) { if (reason) *reason = "part.name invalid"; return false; }
        if (!std::isfinite(part.x) || !std::isfinite(part.y) ||
            !std::isfinite(part.orientation_x) || !std::isfinite(part.orientation_y) ||
            !std::isfinite(part.orientation_z)) {
            if (reason) *reason = "part transform not finite";
            return false;
        }
        // 温度允许 -Infinity（C# 用 WriteCompressedFloat 传 -Infinity 表示未加热）
        if (std::isnan(part.temperature)) { if (reason) *reason = "part temperature NaN"; return false; }
    }
    if (!validate_stages(rocket.stages, rocket)) { if (reason) *reason = "stages invalid"; return false; }
    if (reason) *reason = "ok";
    return true;
}

// 对齐 C# rocket.Parts.TryGetValue(partId, out part)：partId 不在该火箭里返回 nullptr，
// 调用方必须整包拒绝（不写权威世界、也不转发）。
//
// 【冷火箭 blob】本函数只遍历 state.parts，拿不到 blob，因此不负责解包：
// 打开 SFS_COLD_ROCKET_BLOB 时，调用方必须先 RocketEntry::parts_begin() 展开部件表，
// 查/改完再 parts_end() 回包 —— 见 packets.cpp 里各模块包处理点。
sfs::protocol::PartState* find_part(sfs::protocol::RocketState& rocket, std::int32_t part_id) {
    for (auto& [id, part] : rocket.parts) {
        if (id == part_id) return &part;
    }
    return nullptr;
}

// ---------------------------------------------------------- 业务包处理
void Server::handle_packet(int fd, const std::vector<std::uint8_t>& payload, std::int32_t payload_bits) {
    if (payload.empty()) return;
    sfs::net::NetReader r(payload.data(), static_cast<std::size_t>(payload_bits));
    if (r.at_end()) return;

    const std::uint8_t raw_type = r.read_u8();
    const auto packet_type = static_cast<sfs::protocol::PacketType>(raw_type);
    using PT = sfs::protocol::PacketType;

    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    const std::int32_t player_id = it->second.player_id;
    // 包类型直方图：用于判断"客户端到底有没有把某个动作发出来"
    ++it->second.type_counts[raw_type < 64 ? raw_type : 63];

    // 权限判定：只有拥有权威的玩家能改这枚火箭
    auto has_authority = [this, player_id](std::int32_t rid) {
        auto ait = update_authority_.find(player_id);
        return ait != update_authority_.end() && ait->second.count(rid) > 0;
    };

    switch (packet_type) {
        case PT::kCreateRocket: {
            // 客户端造了一枚火箭：验证 → 分配全局 ID → 广播
            sfs::protocol::CreateRocketPacket pkt;
            pkt.deserialize(r);

            const char* reject_reason = nullptr;
            if (!validate_rocket(pkt.rocket, &reject_reason)) {
                // 静默拒绝会让客户端"火箭凭空消失"，必须留下原因
                SFS_LOGW(Cat::World, "[cpp-server] 拒绝 CreateRocket: player=%d local_id=%d for_launch=%d "
                         "parts=%zu joints=%zu stages=%zu name='%s' 原因=%s",
                         player_id, pkt.local_id, pkt.for_launch ? 1 : 0,
                         pkt.rocket.parts.size(), pkt.rocket.joints.size(), pkt.rocket.stages.size(),
                         pkt.rocket.rocket_name.c_str(),
                         reject_reason != nullptr ? reject_reason : "unknown");
                return;
            }

            // 已存在的火箭视为整枚状态覆盖
            if (pkt.global_id >= 0 && rockets_.count(pkt.global_id)) {
                if (!has_authority(pkt.global_id)) {
                    // 【重做 2026-10-10】以前这里静默 return：发送方（本端已按本地猜测改过零件表）
                    // 什么也收不到，于是它的结构与服务端永久分叉 —— 实测"拆完/撞完永远不同步"。
                    // 现在把服务端手里的权威副本回发给发送者，让客户端按权威结构自愈。
                    sfs::protocol::CreateRocketPacket authoritative = pkt;
                    authoritative.world_time = world_time();
                    authoritative.local_id = -1;
                    RocketEntry& entry = rockets_[pkt.global_id];
                    entry.parts_begin();          // 冷火箭：先展开再拷贝权威部件表
                    authoritative.rocket = entry.state;
                    entry.parts_end();
                    send_packet(fd, static_cast<std::uint8_t>(PT::kCreateRocket), authoritative);
                    SFS_LOGW(Cat::World, "[cpp-server] CreateRocket 被否决(无权威) -> 回发权威副本: global_id=%d", pkt.global_id);
                    return;
                }
                RocketEntry& entry = rockets_[pkt.global_id];
                entry.state = pkt.rocket;
                entry.parts_end();   // 整表覆盖写点：回包新部件表，作废 blob 里的旧内容
                pkt.world_time = world_time();
                pkt.local_id = -1;
                broadcast_packet(static_cast<std::uint8_t>(PT::kCreateRocket), pkt, fd);
                return;
            }

            // 重发去重：客户端没收到回执时会重发同一个 local_id（会话刚建立时首包容易丢，
            // 实测"第一次发射失败、第二次百分百成功"）。命中过就复用原全局 ID 重发回执，
            // 绝不能再建第二枚 —— 否则服务端会同时存在两枚一模一样的火箭。
            if (pkt.local_id >= 0) {
                const std::uint64_t dup_key = created_local_key(player_id, pkt.local_id);
                const auto dup = created_local_ids_.find(dup_key);
                if (dup != created_local_ids_.end()) {
                    if (rockets_.count(dup->second) > 0) {
                        sfs::protocol::CreateRocketPacket receipt = pkt;
                        receipt.global_id = dup->second;
                        // 回执必须带服务端的权威状态：重发包里带的是客户端"创建那一刻"的旧状态，
                        // 客户端收到会把它当状态覆盖 —— 本地火箭被回退到几秒前（瞬移/贴回发射台）。
                        RocketEntry& entry = rockets_[dup->second];
                        entry.parts_begin();          // 冷火箭：先展开再拷贝权威部件表
                        receipt.rocket = entry.state;
                        entry.parts_end();
                        receipt.world_time = world_time();
                        send_packet(fd, static_cast<std::uint8_t>(PT::kCreateRocket), receipt);
                        SFS_LOGI(Cat::World, "[cpp-server] CreateRocket 重复请求: player=%d local_id=%d -> 复用 global_id=%d",
                                 player_id, pkt.local_id, dup->second);
                        return;
                    }
                    created_local_ids_.erase(dup);
                }
            }

            const std::int32_t new_id = next_rocket_id();
            pkt.global_id = new_id;
            pkt.world_time = world_time();
            RocketEntry entry;
            entry.state = pkt.rocket;
            entry.owner_player = player_id;
            // 刚建出来的火箭先给创建者保留几秒权威（分离就是这个路径进来的），见 RocketEntry。
            entry.authority_hold_player = player_id;
            entry.authority_hold_until = std::chrono::steady_clock::now() +
                std::chrono::milliseconds(static_cast<int>(kAuthorityHoldSeconds * 1000));
            rockets_.emplace(new_id, std::move(entry));
            if (pkt.local_id >= 0) created_local_ids_[created_local_key(player_id, pkt.local_id)] = new_id;

            // ① 回执给创建者：LocalId 必须保留原值，客户端靠它把本地临时火箭绑到 GlobalId
            //    （写成 -1 会让 unsyncedRockets 永远清不掉，表现为"卡在创建火箭"）
            send_packet(fd, static_cast<std::uint8_t>(PT::kCreateRocket), pkt);
            SFS_LOGI(Cat::World, "[cpp-server] rocket created: global_id=%d owner=%d parts=%zu",
                     new_id, player_id, rockets_[new_id].state.parts.size());

            // ② 广播给其他人：LocalId 清成 -1，避免各方临时编号碰撞造成错绑
            pkt.local_id = -1;
            broadcast_packet(static_cast<std::uint8_t>(PT::kCreateRocket), pkt, fd);

            // 创建后必须刷新权威（原来只在非发射创建时刷）：否则新火箭不属于任何人，
            // 创建者写不了自己刚造的火箭（发射瞬间的分级/点火/模块包全被静默丢弃）。
            refresh_authorities();
            break;
        }
        case PT::kDestroyRocket: {
            sfs::protocol::DestroyRocketPacket pkt;
            pkt.deserialize(r);
            // 无人操控的火箭任何人都可以清掉：发射台上没人开的那些不属于任何玩家，
            // 若这里只认"权威"，客户端模组的清场包会被拒绝，幽灵火箭下一份快照就复活，
            // 世界里的无主火箭越堆越多、每枚都在算碰撞箱 —— 用户实测的卡顿就是这么来的。
            bool destroy_denied = !has_authority(pkt.rocket_id);
            if (destroy_denied) {
                auto rit = rockets_.find(pkt.rocket_id);
                if (rit != rockets_.end()) {
                    destroy_denied = false;
                    for (const auto& [pid, rid] : controlled_rocket_) {
                        if (rid != pkt.rocket_id) continue;
                        if (players_.find(pid) == players_.end()) continue;   // 已离线玩家的旧记录不算数
                        destroy_denied = true;
                        break;
                    }
                }
            }
            if (destroy_denied) {
                // 静默拒绝的后果：客户端"清除发射台/拆解"毫无反馈，火箭下次快照又复活。
                // 留日志 + 重发权威集合，让客户端知道谁才是有权限的人。
                SFS_LOGW(Cat::World, "[cpp-server] 拒绝 DestroyRocket: player=%d rocket=%d 原因=无权威",
                         player_id, pkt.rocket_id);
                refresh_authorities();
                return;
            }
            if (!rockets_.erase(pkt.rocket_id)) {
                SFS_LOGW(Cat::World, "[cpp-server] 拒绝 DestroyRocket: player=%d rocket=%d 原因=火箭不存在",
                         player_id, pkt.rocket_id);
                return;
            }
            pkt.world_time = world_time();
            // 同步清理重发去重表：原来只有断线/Dock/去重命中才清，于是"创建过又被销毁"的火箭
            // 永久留下一条指向死 global_id 的记录，长时间游戏该 map 只增不减（无界增长）。
            for (auto it = created_local_ids_.begin(); it != created_local_ids_.end();) {
                if (it->second == pkt.rocket_id) it = created_local_ids_.erase(it);
                else ++it;
            }

            // 对齐 C#：清除所有玩家对该火箭的控制与权威
            for (auto& [pid, rid] : controlled_rocket_) {
                if (rid == pkt.rocket_id) {
                    rid = -1;
                    sfs::protocol::UpdatePlayerControlPacket ctl;
                    ctl.player_id = pid;
                    ctl.rocket_id = -1;
                    broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePlayerControl), ctl, -1);
                }
            }
            broadcast_packet(static_cast<std::uint8_t>(PT::kDestroyRocket), pkt, -1);
            SFS_LOGI(Cat::World, "[cpp-server] rocket destroyed: id=%d by player %d",
                     pkt.rocket_id, player_id);
            refresh_authorities();
            break;
        }
        case PT::kUpdateRocketPrimary: {
            sfs::protocol::UpdateRocketPrimaryPacket pkt;
            pkt.deserialize(r);
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            if (!has_authority(pkt.rocket_id)) return;
            if (!validate_finite(pkt.location, pkt.rotation, pkt.angular_velocity)) return;  // 对齐 C# ValidateFinite
            // 关键：世界时间由服务端统一盖章（对齐 C# packet.WorldTime = WorldTime）。
            // 直接转发客户端自己的时间会让接收端插值器拿到不一致的时间基准，
            // 表现为远端火箭乱飞/不落地（"引力不存在"）。
            pkt.world_time = world_time();
            rit->second.state.location = pkt.location;
            rit->second.state.rotation = pkt.rotation;
            rit->second.state.angular_velocity = pkt.angular_velocity;
            rit->second.state.apply_primary(pkt.location, pkt.rotation, pkt.angular_velocity);
            rit->second.last_update = std::chrono::steady_clock::now();   // 保活：这枚有人在模拟
            // 位置变了 → "按距离重分配权威"的结果可能过期，置位让周期节拍重算一次。
            authority_distance_recheck_needed_ = true;
            broadcast_rocket_primary(pkt, fd);
            break;
        }
        case PT::kUpdateRocketSecondary: {
            sfs::protocol::UpdateRocketSecondaryPacket pkt;
            pkt.deserialize(r);
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            if (!has_authority(pkt.rocket_id)) return;
            // 对齐 C#：所有输入分量必须有限，油门钳制到 [0,1]
            if (!std::isfinite(pkt.input_turn) || !std::isfinite(pkt.raw_x) || !std::isfinite(pkt.raw_y) ||
                !std::isfinite(pkt.horizontal_x) || !std::isfinite(pkt.horizontal_y) ||
                !std::isfinite(pkt.vertical_x) || !std::isfinite(pkt.vertical_y) ||
                !std::isfinite(pkt.throttle_percent))
                return;
            pkt.throttle_percent = static_cast<float>(clamp01(pkt.throttle_percent));
            pkt.world_time = world_time();
            rit->second.state.throttle_on = pkt.throttle_on;
            rit->second.state.throttle_percent = pkt.throttle_percent;
            rit->second.state.rcs = pkt.rcs;
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdateRocketSecondary), pkt, fd);
            break;
        }
        case PT::kDestroyPart: {
            sfs::protocol::DestroyPartPacket pkt;
            pkt.deserialize(r);
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            if (!has_authority(pkt.rocket_id)) return;
            rit->second.parts_begin();   // 冷火箭：先展开部件表
            {
                auto& parts = rit->second.state.parts;
                parts.erase(std::remove_if(parts.begin(), parts.end(),
                    [&pkt](const std::pair<std::int32_t, sfs::protocol::PartState>& p) {
                        return p.first == pkt.part_id; }),
                    parts.end());
            }
            rit->second.state.joints.erase(
                std::remove_if(rit->second.state.joints.begin(), rit->second.state.joints.end(),
                    [&pkt](const sfs::protocol::JointState& j) {
                        return j.part_a == pkt.part_id || j.part_b == pkt.part_id;
                    }),
                rit->second.state.joints.end());
            for (auto& st : rit->second.state.stages) {
                st.part_ids.erase(
                    std::remove(st.part_ids.begin(), st.part_ids.end(), pkt.part_id),
                    st.part_ids.end());
            }
            pkt.world_time = world_time();
            broadcast_packet(static_cast<std::uint8_t>(PT::kDestroyPart), pkt, -1);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdateStaging: {
            sfs::protocol::UpdateStagingPacket pkt;
            pkt.deserialize(r);
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            if (!has_authority(pkt.rocket_id)) return;
            rit->second.parts_begin();   // validate_stages 要按部件表校验阶段引用
            if (!validate_stages(pkt.stages, rit->second.state)) {   // 对齐 C# ValidateStages
                rit->second.parts_end();
                return;
            }
            pkt.world_time = world_time();
            rit->second.state.stages = pkt.stages;
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdateStaging), pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePartEngineModule:
        case PT::kUpdatePartWheelModule: {
            // 对齐 C# HandleEngine / HandleWheel：两种包字段布局完全相同
            // （WorldTime(f64) + RocketId(i32) + PartId(i32) + bool），只是写回的开关键不同。
            // Wheel 包用 UpdatePartEnginePacket 解析是安全的：两者 serialize/deserialize 逐字段
            // 同布局（见 protocol/packets.cpp），解析后按原始类型字节转发，线上格式不变。
            const bool engine_module = (packet_type == PT::kUpdatePartEngineModule);
            sfs::protocol::UpdatePartEnginePacket pkt;
            pkt.deserialize(r);
            // 对齐 C# TryAuthorizedPart(player, RocketId, PartId, out part)：
            // ① 有权威 ② 火箭存在 ③ partId 真的在这枚火箭里 —— 三者缺一即整包拒绝。
            if (!has_authority(pkt.rocket_id)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "无权威");
                return;
            }
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "火箭不存在");
                return;
            }
            rit->second.parts_begin();   // 冷火箭：find_part 之前必须先展开部件表
            sfs::protocol::PartState* part = find_part(rit->second.state, pkt.part_id);
            if (part == nullptr) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "partId 不存在");
                return;
            }
            // 世界时间由服务端盖章（对齐 C# packet.WorldTime = WorldTime）。
            // 同时必须写回权威世界：中途加入者的快照是 CreateRocket(rockets_[id].state)，
            // 只转发不写回会让新加入者拿到模块的旧状态（引擎开关/油门仍是旧值）。
            pkt.world_time = world_time();
            set_toggle_variable(*part, engine_module ? "engine_on" : "wheel_on", pkt.engine_on);
            broadcast_packet(raw_type, pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePartBoosterModule: {
            // 对齐 C# HandleBooster
            sfs::protocol::UpdatePartBoosterPacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "无权威");
                return;
            }
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "火箭不存在");
                return;
            }
            rit->second.parts_begin();   // 冷火箭：find_part 之前必须先展开部件表
            sfs::protocol::PartState* part = find_part(rit->second.state, pkt.part_id);
            if (part == nullptr) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "partId 不存在");
                return;
            }
            // 对齐 C#：非有限值一律拒收（C# 抛 InvalidDataException 并断开该玩家；这里沿用服务端
            // 既有的"丢弃坏包并计数"策略，见 frames.cpp 对 handle_packet 的 try/catch）
            if (!all_finite_2(pkt.throttle, pkt.fuel_percent)) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "助推器状态非有限值");
                return;
            }
            pkt.throttle = static_cast<float>(clamp01(pkt.throttle));
            pkt.fuel_percent = static_cast<float>(clamp01(pkt.fuel_percent));
            pkt.world_time = world_time();
            // C# 只把燃料百分比写进权威世界（throttle 钳制后仅用于转发）
            set_number_variable(*part, "fuel_percent", pkt.fuel_percent);
            broadcast_packet(raw_type, pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePartParachuteModule: {
            // 对齐 C# HandleParachute
            sfs::protocol::UpdatePartParachutePacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "无权威");
                return;
            }
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "火箭不存在");
                return;
            }
            rit->second.parts_begin();   // 冷火箭：find_part 之前必须先展开部件表
            sfs::protocol::PartState* part = find_part(rit->second.state, pkt.part_id);
            if (part == nullptr) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "partId 不存在");
                return;
            }
            if (!all_finite_2(pkt.state, pkt.target_state)) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "降落伞状态非有限值");
                return;
            }
            pkt.world_time = world_time();
            // 对齐 C#：animation_state / deploy_state 两个数值变量都写回
            set_number_variable(*part, "animation_state", pkt.state);
            set_number_variable(*part, "deploy_state", pkt.target_state);
            broadcast_packet(raw_type, pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePartMoveModule: {
            // 对齐 C# HandleMove
            sfs::protocol::UpdatePartMovePacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "无权威");
                return;
            }
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "火箭不存在");
                return;
            }
            rit->second.parts_begin();   // 冷火箭：find_part 之前必须先展开部件表
            sfs::protocol::PartState* part = find_part(rit->second.state, pkt.part_id);
            if (part == nullptr) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "partId 不存在");
                return;
            }
            if (!all_finite_2(pkt.time, pkt.target_time)) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, pkt.part_id, "移动模块状态非有限值");
                return;
            }
            pkt.world_time = world_time();
            // 对齐 C#：state / state_target 两个数值变量都写回
            set_number_variable(*part, "state", pkt.time);
            set_number_variable(*part, "state_target", pkt.target_time);
            broadcast_packet(raw_type, pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePartResourceModule: {
            // 对齐 C# HandleResource：权限按火箭判定（不是单个 partId），一个存在的部件都没有时整包丢弃
            sfs::protocol::UpdatePartResourcePacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, -1, "无权威");
                return;
            }
            if (!std::isfinite(pkt.resource_percent)) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, -1, "资源百分比非有限值");
                return;
            }
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) {
                log_module_reject(player_id, raw_type, pkt.rocket_id, -1, "火箭不存在");
                return;
            }
            pkt.resource_percent = clamp01(pkt.resource_percent);
            bool found = false;
            rit->second.parts_begin();   // 冷火箭：find_part 之前必须先展开部件表
            for (const std::int32_t id : pkt.part_ids) {
                if (sfs::protocol::PartState* part = find_part(rit->second.state, id); part != nullptr) {
                    set_number_variable(*part, "fuel_percent", pkt.resource_percent);
                    found = true;
                }
            }
            if (!found) {
                rit->second.parts_end();
                log_module_reject(player_id, raw_type, pkt.rocket_id, -1, "包内 partIds 在该火箭里都不存在");
                return;
            }
            pkt.world_time = world_time();
            broadcast_packet(raw_type, pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdatePlayerControl: {
            sfs::protocol::UpdatePlayerControlPacket pkt;
            pkt.deserialize(r);
            // 对齐 C# HandlePlayerControl：PlayerId 一律用服务端认证身份覆盖
            pkt.player_id = player_id;

            const auto self_it = controlled_rocket_.find(player_id);
            const std::int32_t current_rocket =
                (self_it == controlled_rocket_.end()) ? -1 : self_it->second;

            if (pkt.rocket_id != -1) {
                // 火箭不存在，或已被别的玩家控制 -> 单播回"你当前实际控制的目标"
                bool taken_by_other = false;
                for (const auto& [other_pid, other_rid] : controlled_rocket_) {
                    if (other_pid != player_id && other_rid == pkt.rocket_id) {
                        taken_by_other = true;
                        break;
                    }
                }
                if (rockets_.find(pkt.rocket_id) == rockets_.end() || taken_by_other) {
                    // 拒绝原因必须留痕：客户端只会收到"你当前实际控制的目标"，
                    // 不加日志就无法区分"火箭不存在"和"被别人开着"（排查控制权问题全靠它）
                    SFS_LOGW(Cat::World, "[cpp-server] 拒绝控制: player=%d rocket=%d 原因=%s 当前控制=%d",
                             player_id, pkt.rocket_id,
                             (rockets_.find(pkt.rocket_id) == rockets_.end()) ? "火箭不存在"
                                                                             : "已被其他玩家控制",
                             current_rocket);
                    sfs::protocol::UpdatePlayerControlPacket reply;
                    reply.player_id = player_id;
                    reply.rocket_id = current_rocket;
                    send_packet(fd, static_cast<std::uint8_t>(PT::kUpdatePlayerControl), reply);
                    break;
                }
            }

            if (pkt.rocket_id == current_rocket) {
                // 幂等回执（勿删）：客户端在 P2P 重建 / 原生选择 / 销毁等路径会重复请求同一目标。
                // 相同目标只单播确认，不刷新权威、不广播，否则会把控制交接竞态放大成更新风暴。
                sfs::protocol::UpdatePlayerControlPacket reply;
                reply.player_id = player_id;
                reply.rocket_id = current_rocket;
                send_packet(fd, static_cast<std::uint8_t>(PT::kUpdatePlayerControl), reply);
                break;
            }

            controlled_rocket_[player_id] = pkt.rocket_id;
            refresh_authorities();
            // 必须广播给所有人（含请求者自己）：客户端靠这条回执确认拿到控制权，
            // 排除请求者会导致「世界在更新但无法进入控制权」。
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePlayerControl), pkt, -1);
            SFS_LOGI(Cat::World, "[cpp-server] player %d controls rocket %d",
                     player_id, pkt.rocket_id);
            break;
        }
        case PT::kUpdatePlayerColor: {
            sfs::protocol::UpdatePlayerColorPacket pkt;
            pkt.deserialize(r);
            if (pkt.player_id != player_id) return;
            // 对齐 C# ClampColor：必须有限 + 钳制到 [0,1]
            if (!std::isfinite(pkt.color.r) || !std::isfinite(pkt.color.g) || !std::isfinite(pkt.color.b)) return;
            pkt.color.r = static_cast<float>(clamp01(pkt.color.r));
            pkt.color.g = static_cast<float>(clamp01(pkt.color.g));
            pkt.color.b = static_cast<float>(clamp01(pkt.color.b));
            auto pit = players_.find(player_id);
            if (pit != players_.end()) pit->second.color = pkt.color;
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePlayerColor), pkt, fd);
            break;
        }
        case PT::kSendChatMessage: {
            sfs::protocol::SendChatMessagePacket pkt;
            pkt.deserialize(r);
            // 对齐 C#：长度与冷却限制
            std::string text = pkt.message;
            // 去掉首尾空白
            const auto not_space = [](unsigned char c) { return !std::isspace(c); };
            text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
            text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
            if (text.empty() || text.size() > 512) return;
            for (unsigned char ch : text) {
                if (ch < 0x20 || ch == 0x7F) return;
            }
            constexpr double kChatCooldownSeconds = 3.0;
            const auto now_tp = std::chrono::steady_clock::now();
            auto cit = conns_.find(fd);
            if (cit != conns_.end()) {
                const double elapsed =
                    std::chrono::duration<double>(now_tp - cit->second.last_chat).count();
                if (elapsed < kChatCooldownSeconds) return;
                cit->second.last_chat = now_tp;
            }
            pkt.sender_id = player_id;   // 以服务端记录的 ID 为准，不信客户端自称
            pkt.message = text;
            broadcast_packet(static_cast<std::uint8_t>(PT::kSendChatMessage), pkt, -1);
            SFS_LOGI(Cat::World, "[cpp-server] chat from %d: %s", player_id, pkt.message.c_str());
            break;
        }
        case PT::kUpdateWorldTime: {
            // 世界时间只由服务端驱动（收到 Pong 后下发）。客户端不会发这个包，忽略即可。
            break;
        }
        case PT::kTimeWarp: {
            // 多人时个人时间加速的上限（老 C# TimeWarpControlRules.MultiplayerMaximumMultiplier）
            constexpr double kPersonalTimeScaleMax = 5.0;
            sfs::protocol::TimeWarpPacket pkt;
            pkt.deserialize(r);
            // 服务端必须裁决：以前是纯透传，客户端能伪造请求者身份并按任意倍率广播"已应用"，
            // 于是时间倍率两端不同步、时间被反复拉回/跳变。客户端也不该把非法 operation 当 Applied。
            if (!sfs::protocol::IsValidTimeWarpOperation(static_cast<std::uint8_t>(pkt.operation))) {
                SFS_LOGW(Cat::World, "[cpp-server] 拒绝 TimeWarp: player=%d 非法 operation=%u",
                         player_id, static_cast<unsigned>(pkt.operation));
                return;
            }
            if (!std::isfinite(pkt.multiplier)) return;
            pkt.requester_id = player_id;                                // 身份一律用服务端认证值覆盖

            // ================= 多人：个人时间加速（每人 1~5 倍）=================
            // 语义照搬老 C# 服务端（Server/src/.../TimeWarpControlRules.CanSetPersonal +
            // TcpMultiplayerServer.HandleTimeWarp），那套当初有单元测试：
            //   * 只把 Applied 回给**请求者本人**；
            //   * 服务端自己的时钟**一动不动**。
            // 这样每个客户端各跑各的本地时钟，而服务端 world_time() 盖的章始终是 1x，
            // 两端的"同步时钟"永远同源，谁也不会把谁拉回去。
            if (player_count_ > 1) {
                if (!(pkt.multiplier >= 1.0 && pkt.multiplier <= kPersonalTimeScaleMax)) {
                    sfs::protocol::TimeWarpPacket notice;
                    notice.operation = sfs::protocol::TimeWarpOperation::kNotice;
                    notice.requester_id = player_id;
                    notice.multiplier = time_scale_;
                    notice.world_time = world_time();
                    notice.message = "Personal time scale in multiplayer must be between 1x and 5x.";
                    send_packet(fd, static_cast<std::uint8_t>(PT::kTimeWarp), notice);
                    return;
                }
                char scale_buf[64];
                std::snprintf(scale_buf, sizeof(scale_buf), "Personal time scale set to %.2gx.",
                              pkt.multiplier);
                pkt.operation = sfs::protocol::TimeWarpOperation::kApplied;
                pkt.approved = true;
                pkt.world_time = world_time();
                pkt.message = scale_buf;
                // 【必须单播给请求者本人，不能 broadcast_packet(..., fd)】
                // 老代码把请求者排除在广播之外，而客户端 RequestTimeScale 只发包、不本地应用 ——
                // 结果按加速的那个人自己收不到回执：他不变快，别人全变快。
                // 老 C# 版在同一类坑上留过注释，这里又踩了一次。
                send_packet(fd, static_cast<std::uint8_t>(PT::kTimeWarp), pkt);
                SFS_LOGI(Cat::World, "[cpp-server] personal time scale: player=%d %.3gx", player_id, pkt.multiplier);
                return;
            }

            // ================= 单人：全局加速 =================
            // 老 C# 的 TimeWarpControlRules.CanSet：倍率 1，或**恰好一个玩家在控制火箭**。
            // 防止"没人开船"或"两个人在各开一艘"时把全局时间拖走。
            {
                int controlling = 0;
                for (const auto& [pid, rid] : controlled_rocket_) {
                    (void)pid;
                    if (rid != -1) controlling++;
                }
                if (!(pkt.multiplier == 1.0 || controlling == 1)) {
                    sfs::protocol::TimeWarpPacket notice;
                    notice.operation = sfs::protocol::TimeWarpOperation::kNotice;
                    notice.requester_id = player_id;
                    notice.multiplier = time_scale_;
                    notice.world_time = world_time();
                    notice.message = "The time scale cannot be changed right now.";
                    send_packet(fd, static_cast<std::uint8_t>(PT::kTimeWarp), notice);
                    return;
                }
            }
            if (pkt.multiplier < 1.0) pkt.multiplier = 1.0;
            if (pkt.multiplier > 20000.0) pkt.multiplier = 20000.0;     // 上限 20000x（按玩家要求）
            // 倍率必须落到服务端自己的时钟上：只广播不落时钟时，服务端继续按旧倍率给 world_time()
            // 盖章，与它刚广播的"Applied 倍率=x"自相矛盾，客户端时间同步被持续负向拉
            //（现场 gameTimeScale 反复 -1.000、插值判定陈旧后反复请求快照）。
            // 结算方式与 Console 的 set_time_scale() 一致，但不重复发包（本处已有自己的广播）。
            {
                const auto now = std::chrono::steady_clock::now();
                const double elapsed = std::chrono::duration<double>(now - world_clock_origin_).count();
                world_time_base_ += elapsed * time_scale_;
                world_clock_origin_ = now;
                time_scale_ = pkt.multiplier;
            }
            // 【operation 必须改成 Applied】以前是把客户端的 Request 原样广播回去，
            // 而客户端 TimeWarpVoteUI.Receive 只在 operation == Applied 时才应用倍率 ——
            // 于是全球加速在客户端从来没生效过（按了没反应）。
            // 【exclude_fd 必须是 -1】请求者也要收到这条回执：客户端 RequestTimeScale 只发包、
            // 不本地应用，把它排除掉就等于"按加速的人自己不变快"。
            pkt.operation = sfs::protocol::TimeWarpOperation::kApplied;
            pkt.approved = true;
            pkt.world_time = world_time();
            broadcast_packet(static_cast<std::uint8_t>(PT::kTimeWarp), pkt, -1);
            break;
        }
        case PT::kDockTransaction: {
            sfs::protocol::DockTransactionPacket pkt;
            pkt.deserialize(r);
            // 合并/拆解结果一律由服务端权威产出，实现见 server/dock.cpp。
            // 以前这里是"信任客户端算好的 merged_rocket 再转发"，留下两个洞：
            //   ① 任意已握手玩家都能用任意 id 覆写/删除别人的火箭；
            //   ② 客户端其实从不发送 Committed=true，所以对接永远收不到回执 —— 功能等于没通。
            handle_dock_transaction(fd, player_id, raw_type, pkt);
            break;
        }
        case PT::kP2PPeerOffer: {
            sfs::protocol::P2PPeerOfferPacket pkt;
            pkt.deserialize(r);
            // P2P 信令：转发给目标玩家
            for (auto& [cfd, c] : conns_) {
                if (!c.handshaked || c.player_id != pkt.peer_player_id) continue;
                send_packet(cfd, static_cast<std::uint8_t>(PT::kP2PPeerOffer), pkt);
                break;
            }
            break;
        }
        case PT::kRenameRocket: {
            sfs::protocol::RenameRocketPacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) return;
            if (!validate_name(pkt.rocket_name)) return;
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            rit->second.state.rocket_name = pkt.rocket_name;
            broadcast_packet(static_cast<std::uint8_t>(PT::kRenameRocket), pkt, fd);
            break;
        }
        case PT::kUpdatePartTemperature: {
            sfs::protocol::UpdatePartTemperaturePacket pkt;
            pkt.deserialize(r);
            if (!has_authority(pkt.rocket_id)) return;
            if (std::isnan(pkt.temperature)) return;
            auto rit = rockets_.find(pkt.rocket_id);
            if (rit == rockets_.end()) return;
            rit->second.parts_begin();   // 冷火箭：先展开部件表
            for (auto& [id, part] : rit->second.state.parts) {
                if (id == pkt.part_id) { part.temperature = pkt.temperature; break; }
            }
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdatePartTemperature), pkt, fd);
            rit->second.parts_end();
            break;
        }
        case PT::kUpdateDestructionReason: {
            sfs::protocol::UpdateDestructionReasonPacket pkt;
            pkt.deserialize(r);
            broadcast_packet(static_cast<std::uint8_t>(PT::kUpdateDestructionReason), pkt, fd);
            break;
        }
        case PT::kExperimentalAccess: {
            // 【已停用 2026-10-06】"实验性访问"整套机制已砍掉（用户决定：权限分组以后并进密码系统）。
            // 消息号 25 保留占位（消息号永不改，重编号会让老客户端错位解析）；
            // 老客户端发来的请求直接忽略 —— 不再授权、也不回包。
            break;
        }
        case PT::kServerCommand: {
            // 客户端 debug 弹窗的实验性命令入口：必须先通过实验性授权
            sfs::protocol::ServerCommandPacket pkt;
            pkt.deserialize(r);
            auto cit = conns_.find(fd);
            if (cit == conns_.end()) break;

            sfs::protocol::ServerCommandPacket reply;
            reply.command = pkt.command;
            {
                // 【2026-10-06】原"实验性访问"授权门已砍掉：用户决定本版本不校验身份，
                // 权限分组后续并进密码系统。
                // 保留的唯一护栏是 stop —— 它不是权限判定，是防手滑：远程把服务端关掉之后，
                // 目标设备上没有交互式终端，只能 SSH 进去重启。
                // 取首 token 转小写比对，避免 "STOP" / 前导空格绕过。
                std::string first = pkt.command.substr(0, pkt.command.find_first_of(" \t\r\n"));
                for (char& ch : first) {
                    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                if (first == "stop") {
                    reply.output = "That command can only be run from the server console.";
                } else {
                    const CommandResult result = execute_command(pkt.command);
                    reply.output = result.message;
                    if (result.request_shutdown) {
                        SFS_LOGW(Cat::Config, "[cpp-server] 远程命令触发关服（player %d: %s）",
                                 player_id, pkt.command.c_str());
                        stop();
                    }
                }
            }
            send_packet(fd, static_cast<std::uint8_t>(PT::kServerCommand), reply);
            break;
        }
        case PT::kP2PEndpoint: {
            // P2P 端口广告：记到连接上，并让撮合下一轮（1 秒）用新端口重发报价。
            // 客户端每次重建 P2P socket（含失败后的动态重绑）都会上报一次，端口变化自动同步。
            sfs::protocol::P2PEndpointPacket pkt;
            pkt.deserialize(r);
            auto eit = conns_.find(fd);
            if (eit != conns_.end() && pkt.port > 0 && pkt.port <= 65535
                && eit->second.p2p_port != pkt.port) {
                eit->second.p2p_port = pkt.port;
                // 端口变了 -> 上一轮报价里的端口已作废：清掉补发计时，让撮合立刻带正确端口重发。
                p2p_pair_last_offer_.clear();
                SFS_LOGI(Cat::P2P, "[cpp-server] P2P 端口广告: player=%d port=%d (重发报价)", player_id, pkt.port);
            }
            break;
        }
        case PT::kUploadLog: {
            // debug 弹窗「上传日志」：客户端把 Player.log 尾部 GZip 后发过来，服务端存到 logs/
            sfs::protocol::UploadLogPacket pkt;
            pkt.deserialize(r);
            auto cit = conns_.find(fd);
            const std::string user = (cit == conns_.end()) ? std::string("unknown") : cit->second.username;
            const std::string path = save_uploaded_log(player_id, user, pkt);
            SFS_LOGI(Cat::Config, "[cpp-server] 收到日志上传: player=%d user='%s' 压缩=%zu 字节 comment='%s' -> %s",
                     player_id, user.c_str(), pkt.data.size(), pkt.comment.c_str(), path.c_str());
            break;
        }
        default:
            ++unknown_frames_;
            break;
    }
}

// ============================================================
// 显式实例化 Server::send_packet / broadcast_packet 所需的模板
// （模板定义在 server.hpp，serialize/deserialize 实现在其它编译单元，
//   不实例化会链接失败。这里已在 sfs::server 内，类型用前导 :: 绝对限定）
// ============================================================
template void Server::send_packet<::sfs::protocol::PlayerConnectedPacket>(int, std::uint8_t, const ::sfs::protocol::PlayerConnectedPacket&);
template void Server::send_packet<::sfs::protocol::UpdatePlayerControlPacket>(int, std::uint8_t, const ::sfs::protocol::UpdatePlayerControlPacket&);
template void Server::send_packet<::sfs::protocol::UpdateWorldTimePacket>(int, std::uint8_t, const ::sfs::protocol::UpdateWorldTimePacket&);
template void Server::send_packet<::sfs::protocol::TimeWarpPacket>(int, std::uint8_t, const ::sfs::protocol::TimeWarpPacket&);
template void Server::send_packet<::sfs::protocol::CreateRocketPacket>(int, std::uint8_t, const ::sfs::protocol::CreateRocketPacket&);
template void Server::send_packet<::sfs::protocol::UpdatePlayerAuthorityPacket>(int, std::uint8_t, const ::sfs::protocol::UpdatePlayerAuthorityPacket&);
template void Server::send_packet<::sfs::protocol::P2PPeerOfferPacket>(int, std::uint8_t, const ::sfs::protocol::P2PPeerOfferPacket&);

template void Server::broadcast_packet<::sfs::protocol::PlayerConnectedPacket>(std::uint8_t, const ::sfs::protocol::PlayerConnectedPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::PlayerDisconnectedPacket>(std::uint8_t, const ::sfs::protocol::PlayerDisconnectedPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePlayerControlPacket>(std::uint8_t, const ::sfs::protocol::UpdatePlayerControlPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::TimeWarpPacket>(std::uint8_t, const ::sfs::protocol::TimeWarpPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::PlayerEventToastPacket>(std::uint8_t, const ::sfs::protocol::PlayerEventToastPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::CreateRocketPacket>(std::uint8_t, const ::sfs::protocol::CreateRocketPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::DestroyRocketPacket>(std::uint8_t, const ::sfs::protocol::DestroyRocketPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdateRocketSecondaryPacket>(std::uint8_t, const ::sfs::protocol::UpdateRocketSecondaryPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::DestroyPartPacket>(std::uint8_t, const ::sfs::protocol::DestroyPartPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdateStagingPacket>(std::uint8_t, const ::sfs::protocol::UpdateStagingPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePlayerColorPacket>(std::uint8_t, const ::sfs::protocol::UpdatePlayerColorPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::SendChatMessagePacket>(std::uint8_t, const ::sfs::protocol::SendChatMessagePacket&, int);
template void Server::broadcast_packet<::sfs::protocol::DockTransactionPacket>(std::uint8_t, const ::sfs::protocol::DockTransactionPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::P2PPeerOfferPacket>(std::uint8_t, const ::sfs::protocol::P2PPeerOfferPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdateRocketPrimaryPacket>(std::uint8_t, const ::sfs::protocol::UpdateRocketPrimaryPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::RenameRocketPacket>(std::uint8_t, const ::sfs::protocol::RenameRocketPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartTemperaturePacket>(std::uint8_t, const ::sfs::protocol::UpdatePartTemperaturePacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdateDestructionReasonPacket>(std::uint8_t, const ::sfs::protocol::UpdateDestructionReasonPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartEnginePacket>(std::uint8_t, const ::sfs::protocol::UpdatePartEnginePacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartBoosterPacket>(std::uint8_t, const ::sfs::protocol::UpdatePartBoosterPacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartParachutePacket>(std::uint8_t, const ::sfs::protocol::UpdatePartParachutePacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartMovePacket>(std::uint8_t, const ::sfs::protocol::UpdatePartMovePacket&, int);
template void Server::broadcast_packet<::sfs::protocol::UpdatePartResourcePacket>(std::uint8_t, const ::sfs::protocol::UpdatePartResourcePacket&, int);

}  // namespace sfs::server
