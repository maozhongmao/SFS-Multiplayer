// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "server/util.hpp"
#include "util/config.hpp"
#include "util/log.hpp"

#include "protocol/handshake.hpp"
#include "protocol/server_info.hpp"
#include "protocol/server_version.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

// 对齐 C# ValidateJoin 的重名判定（只比对"在线"连接，理由见 server.hpp 的声明处注释）
bool Server::name_taken(const std::string& name) const {
    for (const auto& [cfd, c] : conns_) {
        (void)cfd;
        if (!c.handshaked || c.player_id < 0) continue;
        // 服务端每 2 秒 Ping、客户端必回 Pong：10 秒静默视为连接已死，名字不算被占用
        if (steady_seconds_since(c.last_receive) > kDupNameLivenessSeconds) continue;
        if (sfs::server::username_equals_ci(c.username, name)) return true;
    }
    return false;
}

// ---------------------------------------------------------- 帧处理
void Server::handle_frame(int fd, const TcpFrame& frame) {
    // 已拒绝的连接：不再处理任何帧（客户端可能在收到 Disconnect 前又发一个 Hello），
    // 由 lifecycle 的延时清理负责关闭。
    {
        auto dit = conns_.find(fd);
        if (dit != conns_.end() && dit->second.denied) return;
    }
    // 任何帧都算"活着"，用于 10 秒无接收掉线判定
    {
        auto rit = conns_.find(fd);
        if (rit != conns_.end()) {
            rit->second.last_receive = std::chrono::steady_clock::now();
            ++rit->second.packets_in;
        }
    }
    switch (frame.kind) {
        case TcpFrameKind::ServerInfoRequest: {
            // 登录前的探测：只回人数，不建会话。
            // 客户端 JoinMenu 用 Sequence=0 发探测，且只校验 Kind，不校验 Sequence。
            // 满员上限必须是真实配置值（对齐 C# ServerInfoWire.EncodeResponse(_players.Count,
            // _settings.MaxConnections)）：以前恒回常量 16，配置成 4 人的服务器在登录页也显示 16。
            // 人数必须钳到合法区间：编码器对越界值直接抛异常（曾把整个服务端带退出）
            std::int32_t cap = max_players_;
            if (cap < 1) cap = 1;
            if (cap > 256) cap = 256;   // 对齐 C# ServerSettings.Validate 的 MaxConnections 上限
            std::int32_t count = player_count_;
            if (count < 0) count = 0;
            if (count > cap) count = cap;
            const auto payload = sfs::protocol::EncodeServerInfoResponse(count, cap);
            send_frame(fd, TcpFrame{TcpFrameKind::ServerInfoResponse, 0,
                                    payload, static_cast<std::int32_t>(payload.size() * 8)});
            break;
        }
        case TcpFrameKind::ServerVersionRequest: {
            // 登录页头部那行版本号：握手版本用服务端 Hello 硬校验的同一个数，
            // 所以"这里显示匹配"和"Hello 能不能过"永远是同一个口径。
            // flags bit0 = 需要密码：登录页据此决定要不要显示密码行。
            const std::int32_t flags = join_.password.empty()
                ? 0
                : sfs::protocol::kServerVersionFlagPasswordRequired;
            const auto payload = sfs::protocol::EncodeServerVersionResponse(
                sfs::protocol::kHandshakeVersion,
                sfs::protocol::kProtocolVersion,
                sfs::protocol::kServerVersionString,
                flags);
            send_frame(fd, TcpFrame{TcpFrameKind::ServerVersionResponse, 0,
                                    payload, static_cast<std::int32_t>(payload.size() * 8)});
            break;
        }
        case TcpFrameKind::Hello: {
            // 握手帧的 Sequence 不是序号，而是握手版本号（客户端与 C# 服务端都这么约定）。
            // 客户端 TcpTransportCore.cs:389 会校验 ack.Sequence == SessionHandshakeCodec.Version，
            // 回错值客户端直接报 "Server returned an invalid session handshake."。
            if (frame.sequence != sfs::protocol::kHandshakeVersion) {
                // 文本对齐 C#：$"Handshake mismatch: server={SessionHandshakeCodec.Version}, client={hello.Sequence}."
                char reason[96];
                std::snprintf(reason, sizeof(reason),
                              "Handshake mismatch: server=%d, client=%d.",
                              sfs::protocol::kHandshakeVersion, frame.sequence);
                deny_join(fd, reason);
                break;
            }
            sfs::protocol::JoinRequest req;
            try {
                req = sfs::protocol::DecodeHello(frame.payload);
            } catch (const std::exception& e) {
                SFS_LOGW(Cat::Net, "[cpp-server] Hello decode failed: %s", e.what());
                send_control(fd, TcpFrameKind::Disconnect);
                close_conn(fd, "bad hello");
                break;
            }
            auto it = conns_.find(fd);
            if (it == conns_.end()) break;
            Connection& c = it->second;
            const bool first_handshake = !c.handshaked;
            if (first_handshake) {
                // 断线重连判定（对齐 C#：ResumePlayerId >= 0 且 token 匹配则复用原会话，
                // player_id 与两个 token 全部保持不变）
                const bool is_resume = req.resume_player_id >= 0
                    && !req.resume_token.empty()
                    && players_.count(req.resume_player_id) > 0
                    && players_[req.resume_player_id].can_resume(req.resume_token);

                // ---- 加入准入校验：顺序与拒绝文本严格对齐 C#
                //      TcpMultiplayerServer.ValidateJoin（满员 → 用户名 → 重名 → 密码）。
                //      拒绝文本会经 Disconnect 帧发给客户端并直接展示，别改文案。
                // C# 用 request.Username.Trim() 后的名字做校验与建档，这里同样先 trim。
                const std::string username = sfs::server::trim_username(req.username);
                c.username = username;   // 拒绝日志里要能看到被拒的名字

                const char* denial = nullptr;
                if (!is_resume && player_count_ >= max_players_) {
                    // 口径：在线连接数 player_count_ —— 也就是登录页 ServerInfo 显示的人数，
                    // 登录页显示满员就进不去，不会出现"显示 3/4 却被拒"。
                    // （C# 判 _players.Count，其中含 20 秒恢复窗口；C++ 的掉线身份宽限期是 60 秒，
                    //   若把离线身份也算进来，全员退出后还会有 60 秒对所有人回 Server is full.。
                    //   客户端已退出但会话尚未回收(≤30 秒心跳回收)的连接仍占名额，属既有行为。）
                    denial = sfs::server::kDenyServerFull;
                } else if (username.empty()
                           || sfs::server::username_utf16_length(username) > join_.max_username_length
                           || sfs::server::has_control_char(username)) {
                    denial = sfs::server::kDenyInvalidUsername;
                } else if (!is_resume && join_.block_duplicate_player_names
                           && name_taken(username)) {
                    denial = sfs::server::kDenyUsernameInUse;
                } else if (!sfs::server::password_matches(req.password, join_.password)) {
                    denial = sfs::server::kDenyInvalidPassword;
                }
                if (denial != nullptr) {
                    deny_join(fd, denial);
                    break;   // 不再回 JoinResponse，也不建会话
                }

                c.handshaked = true;

                // 【作弊策略 2026-10-15】握手成功即下发一次：客户端据此显示/隐藏作弊入口、
                // 逐项允许或拦截。没有配置文件时 = 总开关开 + 全项允许 = 与"没有策略"等价，
                // 客户端 CheatPolicy 在 HasPolicy=false 时本来也是全放行，两边行为一致。
                {
                    const sfs::config::CheatConfig& cheats = sfs::config::active_cheat_config();
                    sfs::protocol::UpdateCheatStatusPacket cheat;
                    cheat.enabled = cheats.enabled;
                    cheat.individual_flags = cheats.individual_flags;
                    send_packet(fd, static_cast<std::uint8_t>(sfs::protocol::PacketType::kUpdateCheatStatus), cheat);
                }

                if (is_resume) {
                    const std::int32_t pid = req.resume_player_id;
                    PlayerInfo& info = players_[pid];
                    c.player_id = pid;
                    c.username = info.name;
                    c.udp_token = info.udp_token;
                    c.resume_token = info.resume_token;
                    // 旧连接必须直接关掉（而不是只摘身份）：只摘身份会让它继续占着 connected_fds_
                    //（人数虚高），又因为 handshaked=false 而不再参与心跳超时，永不回收；
                    // 它甚至能再发一次带 token 的 Hello 反过来把新连接的身份抢走。
                    std::vector<int> superseded;
                    for (auto& [ofd, oc] : conns_) {
                        if (ofd != fd && oc.handshaked && oc.player_id == pid)
                            superseded.push_back(static_cast<int>(ofd));
                    }
                    for (int ofd : superseded) close_conn(ofd, "superseded by resume");
                    offline_since_.erase(pid);
                    if (auto pit = players_.find(pid); pit != players_.end()) pit->second.offline_since = 0.0;
                    SFS_LOGI(Cat::Net, "[cpp-server] 会话恢复: player=%d user='%s'",
                             pid, c.username.c_str());
                } else {
                    c.player_id = ++next_player_id_;
                    // 用 trim 后的名字建档（对齐 C#：new TcpSession(..., username, ...)）
                    c.username = username;
                    // 对齐 C#：两个 token 都是 48 位随机 hex（不是用户名拼接）
                    c.udp_token = random_token();
                    c.resume_token = random_token();
                    PlayerInfo info;
                    info.name = username;
                    info.color = random_color();
                    info.udp_token = c.udp_token;
                    info.resume_token = c.resume_token;
                    players_[c.player_id] = info;
                }

                // 全 KCP：UDP 令牌表已废弃，绑定由 KCP 会话本身承担
                if (connected_fds_.insert(fd).second) ++player_count_;
            }

            sfs::protocol::JoinResponse resp;
            resp.player_id = c.player_id;
            resp.update_rockets_period = 50.0;   // 对齐 C# 服务端实际下发的值
            resp.chat_message_cooldown = 3.0;
            resp.world_time = world_time();   // 必须是真实世界时间：客户端把它当世界时间基准，
                                              // 发 0 会让加入后到首次心跳校正之间所有出站时间戳
                                              // 与包内时间戳不在同一坐标系（"第一次操作"错位）
            resp.send_time = now_millis();
            // 难度必须来自服务端保存的那一档（存档加载时写入 difficulty_），
            // 写死 Normal 会让 Hard 档的玩家每次重连都被告知是当前 Utils.Difficulty 的另一档。
            resp.difficulty = (difficulty_ >= 0 && difficulty_ <= 2)
                                  ? static_cast<std::uint8_t>(difficulty_)
                                  : static_cast<std::uint8_t>(sfs::protocol::DifficultyType::kNormal);
            resp.solar_system_name = std::string();   // 对齐 C#：服务端下发空串
            resp.udp_session_token = c.udp_token;
            resp.resume_token = c.resume_token;

            const auto payload = sfs::protocol::EncodeAck(resp);
            send_frame(fd, TcpFrame{TcpFrameKind::HelloAck, sfs::protocol::kHandshakeVersion,
                                    payload,
                                    static_cast<std::int32_t>(payload.size() * 8)});

            // 重复 Hello 必须只回 ack，绝不能重放初始状态：
            // 客户端握手是 200ms~2s 指数退避重发，ack 到达前发出的那几个 Hello 都会走到这里。
            // 上游实现无条件往下跑 send_initial_state，于是"同一次加入"被处理两三次：
            // 日志里会出现两条 handshake ok，客户端会收到重复的 PlayerConnected 广播、
            // TimeWarp 重置和【整份世界快照】—— 刚发射的火箭正好落在这个窗口里，
            // 被后到的旧快照洗掉，表现就是"第一次创建火箭神秘消失，第二次百分百成功"。
            if (!first_handshake) {
                SFS_LOGI(Cat::Net, "[cpp-server] 重复 Hello（同一会话）: player=%d user='%s' 只回 ack",
                         c.player_id, c.username.c_str());
                break;
            }

            SFS_LOGI(Cat::Net, "[cpp-server] handshake ok: player=%d user='%s'",
                     c.player_id, c.username.c_str());

            // 对齐 C# SendInitialState：加入时按固定顺序补齐客户端所需状态。
            // 顺序不能乱 —— 客户端靠 UpdatePlayerControl 知道谁在开哪枚火箭，
            // 靠 TimeWarp(Applied) 初始化时间倍率。
            send_initial_state(fd, c.player_id, c.username);
            refresh_authorities();
            break;
        }
        case TcpFrameKind::Ping:
            // 客户端也会主动发 Ping（它自己的 HeartbeatLoop），原样回显即可：
            // 客户端把发送时刻的 ticks 放在 payload 里，收到 Pong 后用 UpdateRoundTrip 反算 RTT。
            // 回空 payload 会让客户端的延迟显示永远为空。
            send_frame(fd, TcpFrame{TcpFrameKind::Pong, frame.sequence,
                                    frame.payload, frame.payload_bits});
            break;
        case TcpFrameKind::Pong: {
            // 服务端心跳 Ping 的回显 —— 世界时间就在这里下发（对齐 C# HandlePong）。
            // 客户端的世界时间完全由服务端驱动，不下发就会导致游戏时间停滞。
            auto pit = conns_.find(fd);
            if (pit == conns_.end()) break;
            Connection& c = pit->second;
            if (frame.payload.size() < 8) break;

            std::int64_t sent_ticks = 0;
            std::memcpy(&sent_ticks, frame.payload.data(), 8);
            // 必须是最新一次 Ping 的原样回显，否则丢弃（对齐 C# RecordHeartbeatPong）
            if (c.last_ping_ticks == 0 || sent_ticks != c.last_ping_ticks
                || c.last_pong_ticks == sent_ticks) {
                break;
            }
            c.last_pong_ticks = sent_ticks;
            const std::int64_t delta_ticks = now_dotnet_ticks() - sent_ticks;
            c.rtt_ms = delta_ticks > 0 ? static_cast<double>(delta_ticks) / 10000.0 : 0.0;
            if (c.rtt_ms > 0.0) {
                c.jitter_ms = c.previous_rtt_ms <= 0.0
                    ? 0.0
                    : c.jitter_ms * 0.8 + std::abs(c.rtt_ms - c.previous_rtt_ms) * 0.2;
                c.previous_rtt_ms = c.rtt_ms;
            }

            // 世界时间 = 基准 + 经过秒数×倍率，再补偿单程延迟（与 C# 完全一致）
            sfs::protocol::UpdateWorldTimePacket wt;
            wt.world_time = world_time() + c.rtt_ms / 2000.0 * time_scale_;
            send_packet(fd, static_cast<std::uint8_t>(sfs::protocol::PacketType::kUpdateWorldTime), wt);
            break;
        }
        case TcpFrameKind::Disconnect:
            send_control(fd, TcpFrameKind::Disconnect);
            close_conn(fd, "client disconnect");
            break;
        case TcpFrameKind::Packet: {
            // 业务包必须来自已完成握手的连接：未握手连接（或被 resume 摘掉身份的旧连接）
            // player_id = -1，会以 -1 为键写进状态（去重表/owner/控制声明），而这些键永不回收 ——
            // 留下 owner=-1 的幽灵火箭（进所有人的快照、却无人有权威也删不掉）。
            auto cit = conns_.find(fd);
            if (cit == conns_.end()) return;
            if (!cit->second.handshaked || cit->second.player_id < 0) return;
            // 业务包：分发给 handle_packet 处理（创建/销毁/同步/聊天等）
            ++packet_count_;
            if (!frame.payload.empty()) last_packet_type_ = frame.payload[0];
            // 单个坏包只丢弃该包并记录，绝不终止进程（真实客户端异常包不能拖垮服务端）
            try {
                handle_packet(fd, frame.payload, frame.payload_bits);
            } catch (const std::exception& e) {
                ++bad_packets_;
                SFS_LOGW(Cat::Net, "[cpp-server] packet %u from player dropped: %s",
                         frame.payload.empty() ? 0u : frame.payload[0], e.what());
            }
            break;
        }
        case TcpFrameKind::RequestWorldSnapshot: {
            // 客户端请求完整世界快照。
            // 节流 1 秒/连接：客户端"插值判定陈旧→请求快照→服务端整枚重发→再请求"会形成
            // 自激风暴（每枚火箭整份 CreateRocket，12 枚可上几十 KB），是 6 秒级卡顿与
            // 每秒 GC 的服务端侧放大器。
            static std::unordered_map<int, std::chrono::steady_clock::time_point> last_snapshot_at;
            const auto now_snap = std::chrono::steady_clock::now();
            std::chrono::steady_clock::time_point& last = last_snapshot_at[fd];
            if (last.time_since_epoch().count() != 0 && now_snap - last < std::chrono::seconds(1)) break;
            last = now_snap;
            ++snapshot_requests_;
            send_world_snapshot(fd);
            break;
        }
        case TcpFrameKind::RequestRocketSnapshot: {
            // 单火箭快照：只补客户端点名的这一枚（payload = i32 rocketId）
            std::int32_t rocket_id = -1;
            if (frame.payload.size() >= 4) {
                std::memcpy(&rocket_id, frame.payload.data(), 4);
            }
            if (rocket_id < 0) break;
            {
                // 【限流键修正 2026-10-09】原来只按 fd 限流：同一个客户端不管点了几枚火箭，
                // 每秒只放行 1 份单火箭快照，其余全部静默丢弃。而客户端是按枚请求的
                // （附近快照循环 / 插值陈旧补包 / Excellent 档 750ms 校验），17 枚火箭时
                // 恢复通道只吐出不到 1/17 的量 —— "漏一次就永久错"的兜底等于瘫痪。
                // 实测（目标设备 9806，探针）：一次突发 6 枚只回 1 份；同样 6 枚间隔 1.2 秒发则 6/6。
                // 改为 (fd, rocket_id) 维度：每枚火箭各自 1 次/秒，防自激风暴的作用不变。
                static std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> last_rocket_snapshot_at;
                const auto now_snap2 = std::chrono::steady_clock::now();
                const std::uint64_t snapshot_key =
                    (static_cast<std::uint64_t>(static_cast<std::uint32_t>(fd)) << 32)
                    | static_cast<std::uint32_t>(rocket_id);
                std::chrono::steady_clock::time_point& last2 = last_rocket_snapshot_at[snapshot_key];
                if (last2.time_since_epoch().count() != 0 && now_snap2 - last2 < std::chrono::seconds(1)) break;
                last2 = now_snap2;
            }
            ++snapshot_requests_;
            send_rocket_snapshot(fd, rocket_id);
            break;
        }
        default:
            ++unknown_frames_;
            break;
    }
}

}  // namespace sfs::server
