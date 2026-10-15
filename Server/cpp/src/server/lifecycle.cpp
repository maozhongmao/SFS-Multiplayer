// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "server/util.hpp"
#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <utility>

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

// ---------------------------------------------------------- 生命周期

Server::Server(std::uint16_t port, std::int32_t max_players, std::string state_path,
               P2PSettings p2p, JoinPolicy join)
    : port_(port), max_players_(max_players), state_path_(std::move(state_path)),
      p2p_(p2p), join_(std::move(join)) {
    // 防御：配置校验在 main.cpp 里已经做过，这里再兜一次，避免 0/负数让"用户名长度上限"
    // 变成"所有人都被拒"或"完全不校验"。
    if (join_.max_username_length < 1) join_.max_username_length = 1;
    if (join_.max_username_length > 128) join_.max_username_length = 128;
    if (max_players_ < 1) max_players_ = 1;
}

bool Server::start() {
    if (!kcp_.init(port_)) { std::perror("kcp init"); return false; }

    SFS_LOGI(Cat::Net, "[cpp-server] listening on KCP (UDP) port %u, max_players=%d",
             static_cast<unsigned>(port_), max_players_);
    // 加入准入的生效值（对齐 C# 启动时会打印的 server.yml 相关字段）。
    // 密码默认空 = 不校验任何人（与改动前的行为一致）。
    SFS_LOGI(Cat::Config, "[cpp-server] 加入准入: 连接密码=%s 重名拦截=%s 用户名长度上限=%d 人数上限=%d",
             join_.password.empty() ? "未设置(任何人可进)" : "已设置",
             join_.block_duplicate_player_names ? "开" : "关",
             join_.max_username_length, max_players_);
    // P2P 信令撮合的实际生效值（对齐 C# 启动时会打印的 server.yml p2p: 段）
    SFS_LOGI(Cat::P2P, "[cpp-server] P2P 信令: %s 距离阈值=%.0fm 校验周期=%ds 切换缓冲=%ds",
             p2p_.enabled ? "启用" : "禁用", p2p_.proximity_meters,
             p2p_.validation_interval_seconds, p2p_.transition_buffer_seconds);
    load_world_from_disk();
    return true;
}

void Server::run() {
    auto next_report = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (g_running) {
        // 轮询不能长于 KCP 的工作间隔，否则没有 UDP 可读时重传和 ACK 会被延迟。
        if (kcp_.poll(5) < 0) {
            // 不能静默退出：以前直接 break 会让服务端"人间蒸发"——玩家侧只看到数据断流、
            // 反复重连（每次重连又重收整份世界快照，把客户端拖到崩溃），日志里却什么都没有。
            // 这里记录、保存世界，再干净退出。
            SFS_LOGE(Cat::Net, "[cpp-server] kcp poll 失败，保存世界后退出");
            save_world_to_disk();
            return;
        }
        poll_kcp_sessions();

        // 加入被拒的连接：Disconnect 帧（含拒绝原因）已经发出去，等 kDenyCloseDelaySeconds
        // 让 KCP 有机会重传，再真正回收会话。立刻关会让丢包场景下的玩家只看到"连接断开"。
        {
            std::vector<int> denied_fds;
            for (auto& [cfd, c] : conns_) {
                if (c.denied && steady_seconds_since(c.denied_at) >= kDenyCloseDelaySeconds) {
                    denied_fds.push_back(cfd);
                }
            }
            for (int dfd : denied_fds) close_conn(dfd, "join denied");
        }

        // 心跳：每 2 秒给每个已握手玩家发一次 Ping（对齐 C# MaintenanceLoop 的 _heartbeatClock）
        // 客户端原样回 Pong，服务端在 Pong 处理里下发 UpdateWorldTime —— 客户端的世界时间靠这个推进，
        // 不发 Ping 会导致客户端游戏时间停滞（表现为整体卡死）。
        if (std::chrono::steady_clock::now() - last_heartbeat_ >= std::chrono::seconds(2)) {
            last_heartbeat_ = std::chrono::steady_clock::now();
            const std::int64_t ticks = now_dotnet_ticks();
            for (auto& [cfd, c] : conns_) {
                if (!c.handshaked) {
                    // 未握手连接必须有期限：KCP 会话由任意 ≥24 字节 UDP 数据报创建，
                    // 没有期限时对端每隔 <30 秒发一个包就能维持一个永不回收的僵尸会话
                    //（日志里那些 hs=0 in=0 的条目）。
                    if (steady_seconds_since(c.last_receive) > 15.0) {
                        pending_disconnects_.push_back(cfd);
                    }
                    continue;
                }
                // 无接收判掉线的阈值：C# 原版是 10 秒，但对走公网隧道/UDP 的场景太激进——
                // 一次误判的代价是客户端重连并重收整份世界快照（12 枚火箭整枚重建 = 数秒卡顿 + 崩溃）。
                // 放宽到 30 秒，仍能回收真死的会话。
                if (steady_seconds_since(c.last_receive) > 30.0) {
                    pending_disconnects_.push_back(cfd);
                    continue;
                }
                c.last_ping_ticks = ticks;
                c.last_pong_ticks = 0;
                sfs::net::NetWriter body;
                body.write_i64(ticks);
                auto data = body.get_bytes();
                send_frame(cfd, TcpFrame{TcpFrameKind::Ping, c.next_seq++, data,
                                        static_cast<std::int32_t>(body.get_bit_length())});
            }
            // 掉线清理放在遍历外，避免边遍历边改容器
            for (auto dfd : pending_disconnects_) close_conn(dfd, "heartbeat timeout");
            pending_disconnects_.clear();
        }

        // 掉线宽限到期：回收玩家记录（resume_token 随之失效）
        for (auto oit = offline_since_.begin(); oit != offline_since_.end();) {
            if (uptime_now() - oit->second >= kResumeGraceSeconds) {
                SFS_LOGI(Cat::Net, "[cpp-server] 掉线宽限到期，回收玩家身份: player=%d", oit->first);
                players_.erase(oit->first);
                oit = offline_since_.erase(oit);
            } else {
                ++oit;
            }
        }

        // 自动清理太空垃圾（对齐 C# _autoClearDebris：开启后每 5 秒扫一次）
        if (auto_clear_debris_ &&
            std::chrono::steady_clock::now() - last_debris_sweep_ >= std::chrono::seconds(5)) {
            last_debris_sweep_ = std::chrono::steady_clock::now();
            const int removed = clear_debris(kAutoClearDebrisMaxParts);
            if (removed > 0) {
                SFS_LOGI(Cat::World, "[cpp-server] debris auto: removed %d", removed);
            }
        }

        // 清理无主火箭：只保留有人操控的 + 刚发射的（折中方案：发射台仍不参与碰撞，
        // 但世界里不再堆积无人操控的火箭 —— 那正是"算了又不算"循环的来源）。
        if (std::chrono::steady_clock::now() - last_uncontrolled_sweep_ >= std::chrono::seconds(5)) {
            last_uncontrolled_sweep_ = std::chrono::steady_clock::now();
            clear_uncontrolled_rockets(kUncontrolledGraceSeconds);
        }

        // 权威按距离重分配（"离你最近的那枚无人火箭归你算"）。
        //
        // 为什么必须周期重算而不能在创建时一次算对：火箭会飞。创建时离你最近的那枚，
        // 一分钟后可能已经飞到你脸前 —— 那时它在你屏幕上必须是活的（正常碰撞/燃烧/坠落），
        // 否则就是"没人操控的火箭怎么不是金刚不坏之身了"。
        // 权威分配原本只在"创建/控制权变更/对接"这些事件里重算，位置变化不会触发，
        // 所以这里补一个低频节拍。
        //
        // 为什么是 2 秒：距离判据的容差本来就是百米量级，1 秒内两次重算毫无意义；
        // 而每次重算都要遍历"火箭数 × 玩家数"，太密会白烧 CPU。
        // refresh_authorities() 内部按集合是否变化决定发不发包，未变时连包都不发。
        if (std::chrono::steady_clock::now() - last_authority_distance_pass_ >= std::chrono::seconds(2)) {
            last_authority_distance_pass_ = std::chrono::steady_clock::now();
            if (authority_distance_recheck_needed_) {
                authority_distance_recheck_needed_ = false;
                refresh_authorities();
            }
        }

        // 自动存档（对齐 server.yml 的 auto_save_seconds: 30）
        if (std::chrono::steady_clock::now() - last_autosave_ >= std::chrono::seconds(30)) {
            last_autosave_ = std::chrono::steady_clock::now();
            save_world_to_disk();
        }

        // P2P 信令撮合：按 validation_interval_seconds 节流
        //（对齐 C# MaintenanceLoop 里的 _p2pClock.Elapsed >= P2P.ValidationIntervalSeconds）
        if (p2p_.enabled &&
            std::chrono::steady_clock::now() - last_p2p_refresh_ >=
                std::chrono::seconds(p2p_.validation_interval_seconds)) {
            last_p2p_refresh_ = std::chrono::steady_clock::now();
            refresh_p2p_peers();
        }

        if (std::chrono::steady_clock::now() >= next_report) {
            report("periodic");
            next_report = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
    }
    report("final");
}

void Server::stop() { g_running = false; }

// ---------------------------------------------------------- 统计
void Server::report(const char* tag) {
    const auto s = read_self_stats();
    SFS_LOGI(Cat::Net, "[cpp-server][%s] rss=%ldkB vm=%ldkB threads=%ld conns=%zu players=%d "
             "pkts=%llu bad=%llu snap=%llu rockets=%zu udp=%llu(data=%llu bind=%llu bad=%llu) cpu=%ld.%02lds",
             tag, s.vm_rss_kb, s.vm_size_kb, s.threads, conns_.size(), player_count_,
             static_cast<unsigned long long>(packet_count_),
             static_cast<unsigned long long>(bad_packets_),
             static_cast<unsigned long long>(snapshot_requests_),
             rockets_.size(),
             static_cast<unsigned long long>(udp_datagrams_),
             static_cast<unsigned long long>(udp_data_),
             static_cast<unsigned long long>(udp_binds_),
             static_cast<unsigned long long>(udp_bad_),
             (s.utime + s.stime) / 100, (s.utime + s.stime) % 100);

    // 每个连接一行明细：客户端"突然不发包"这类问题在这里一眼可见
    for (const auto& [cfd, c] : conns_) {
        const double rx_age = steady_seconds_since(c.last_receive);
        int auth = 0;
        auto ait = update_authority_.find(c.player_id);
        if (ait != update_authority_.end()) auth = static_cast<int>(ait->second.size());
        auto cit = controlled_rocket_.find(c.player_id);
        const std::int32_t ctrl = (cit == controlled_rocket_.end()) ? -1 : cit->second;
        SFS_LOGI(Cat::Net, "[cpp-server][%s][conn] id=%llu player=%d user='%s' hs=%d rtt=%.0fms "
                 "lastRecv=%.1fs in=%llu out=%llu auth=%d ctrl=%d",
                 tag, static_cast<unsigned long long>(c.id), c.player_id, c.username.c_str(),
                 c.handshaked ? 1 : 0, c.rtt_ms, rx_age,
                 static_cast<unsigned long long>(c.packets_in),
                 static_cast<unsigned long long>(c.packets_out),
                 auth, ctrl);

        // KCP 传输层内部状态：判断"重传是否真的减少了"要看 xmit（累计重传次数），
        // fastresend 直接印证预设里 kFastResend=0 是否已在本连接生效。
        sfs::net::KcpTransport::Diagnostics kd;
        if (kcp_.diagnostics(c.conv, &kd)) {
            SFS_LOGI(Cat::Net, "[cpp-server][%s][kcp] conv=%u fastresend=%d xmit=%u nrt=%u cwnd=%u "
                     "sndWnd=%u sndBuf=%u rcvBuf=%u rto=%dms srtt=%dms ssthresh=%u",
                     tag, c.conv, kd.fastresend, kd.xmit, kd.nrt, kd.cwnd, kd.snd_wnd,
                     kd.nsnd_buf, kd.nrcv_buf, kd.rx_rto, kd.rx_srtt, kd.ssthresh);
        }

        // 包类型直方图（只打非零项）：确认客户端到底发了哪些动作
        std::string histogram;
        for (std::size_t t = 0; t < c.type_counts.size(); ++t) {
            if (c.type_counts[t] == 0) continue;
            if (!histogram.empty()) histogram += ' ';
            histogram += std::to_string(t) + ":" + std::to_string(c.type_counts[t]);
        }
        SFS_LOGI(Cat::Net, "[cpp-server][%s][types] player=%d %s", tag, c.player_id,
                 histogram.empty() ? "(none)" : histogram.c_str());
    }
}

}  // namespace sfs::server
