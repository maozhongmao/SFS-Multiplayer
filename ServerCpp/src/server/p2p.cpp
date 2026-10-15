// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// P2P 信令撮合（对齐 C# SfsMultiplayer.Server/TcpMultiplayerServer.cs 的
// RefreshP2PPeers / P2PRocketIds / FindP2PMatches / SendP2POffer / SendP2PRevoke）。
//
// C# 侧的完整规则：
//   1. 只在 P2P.Enabled 且距上次校验 >= P2P.ValidationIntervalSeconds 时跑（调用点在 lifecycle.cpp）；
//   2. 候选玩家 = 有 UDP 端点（服务端观察到的客户端 UDP 源端点）的在线玩家，按 player id 升序；
//   3. 两两配对，各自的\"权威火箭集合\"两两比位置：同一 Address 且二维距离 <= ProximityMeters 才算一对；
//   4. 有配对的两人各收到一份 P2PPeerOffer(Active=true)：对方 player id、对方地址与端口、pairToken、
//      本次配对命中的双方火箭 id 列表（去重升序），token 在配对存续期间复用；
//   5. 上一轮有、这一轮不再匹配的配对，给双方补发 P2PPeerOffer(Active=false) 并丢弃 token。

#include "server/server.hpp"
#include "util/log.hpp"

#include "protocol/policies/p2p_proximity.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <utility>
#include <vector>

namespace sfs::server {

using sfs::log::Cat;

namespace {

using PT = sfs::protocol::PacketType;

// 去重 + 升序：对齐 C# matches.Select(m => m.First).Distinct().OrderBy(id => id)
void sort_unique(std::vector<std::int32_t>& ids) {
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
}

}  // namespace

std::vector<std::int32_t> Server::p2p_rocket_ids(std::int32_t player_id) const {
    std::vector<std::int32_t> ids;
    const auto ait = update_authority_.find(player_id);
    if (ait == update_authority_.end()) return ids;
    ids.reserve(ait->second.size());
    for (const std::int32_t rid : ait->second) {
        // 只算世界里真实存在的火箭（对齐 C# _world.Rockets.ContainsKey 过滤）
        if (rockets_.find(rid) != rockets_.end()) ids.push_back(rid);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

int Server::find_fd_by_player(std::int32_t player_id) const {
    for (const auto& [fd, c] : conns_) {
        if (c.handshaked && c.player_id == player_id) return fd;
    }
    return -1;
}

void Server::send_p2p_offer(int fd, std::int32_t peer_player_id, const std::string& peer_address,
                            std::uint16_t peer_port, const std::string& pair_token,
                            const std::vector<std::int32_t>& local_rocket_ids,
                            const std::vector<std::int32_t>& peer_rocket_ids) {
    sfs::protocol::P2PPeerOfferPacket pkt;
    pkt.active = true;
    pkt.peer_player_id = peer_player_id;
    pkt.peer_address = peer_address;
    pkt.peer_port = static_cast<std::int32_t>(peer_port);
    pkt.pair_token = pair_token;
    pkt.transition_buffer_seconds = p2p_.transition_buffer_seconds;
    pkt.local_rocket_ids = local_rocket_ids;
    pkt.peer_rocket_ids = peer_rocket_ids;
    // 走可靠通道（KCP）下发：客户端在 ClientManager 里收到后交给 P2PConnectionManager.ApplyOffer
    send_packet(fd, static_cast<std::uint8_t>(PT::kP2PPeerOffer), pkt);
}

void Server::send_p2p_revoke(int fd, std::int32_t peer_player_id) {
    sfs::protocol::P2PPeerOfferPacket pkt;
    pkt.active = false;
    pkt.peer_player_id = peer_player_id;
    pkt.transition_buffer_seconds = p2p_.transition_buffer_seconds;
    // 其余字段保持默认（peer_address 空、peer_port 0、两个火箭列表空）：
    // C# 的撤销包也只写 active/peerPlayerId/transitionBufferSeconds，客户端只按 player id 认键。
    send_packet(fd, static_cast<std::uint8_t>(PT::kP2PPeerOffer), pkt);
}

void Server::refresh_p2p_peers() {
    struct Candidate {
        int fd = -1;
        std::int32_t player_id = -1;
        std::string address;
        std::uint16_t port = 0;
        std::vector<std::int32_t> rocket_ids;
    };

    // 1) 候选玩家：已握手 + 端点可读（对齐 C# eligible = _players.Where(s => s.UdpEndpoint != null)），
    //    按 player_id 升序（对齐 C# OrderBy(session => session.Id)）。
    //    连接地址来源：KCP 会话记录的对端 UDP 源端点 —— 与 C# 的 UdpEndpoint 同义
    //    （都是\"服务端实际观察到的客户端 UDP 端点\"，不是客户端自报的）。
    std::vector<Candidate> candidates;
    candidates.reserve(conns_.size());
    for (const auto& [fd, c] : conns_) {
        if (!c.handshaked || c.player_id < 0) continue;
        Candidate cand;
        cand.fd = fd;
        cand.player_id = c.player_id;
        if (!kcp_.peer_endpoint(static_cast<std::uint32_t>(fd), &cand.address, &cand.port)) continue;
        // 【端口来源修正 2026-10-10】优先用客户端广告的 P2P socket 端口。
        // peer_endpoint 给的是"主传输会话观察到的端点"，而 P2P socket 绑在另一个随机空闲端口上 ——
        // 按它撮合等于让对方敲错门（探测全打进主传输 socket，KCP conv 不匹配直接丢）。
        // 只有客户端还没广告时才退回观察端口（旧客户端兼容）。
        if (c.p2p_port > 0 && c.p2p_port <= 65535) {
            cand.port = static_cast<std::uint16_t>(c.p2p_port);
        }
        if (cand.address.empty() || cand.port == 0) continue;
        cand.rocket_ids = p2p_rocket_ids(c.player_id);
        candidates.push_back(std::move(cand));
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.player_id < b.player_id; });

    std::unordered_set<std::uint64_t> active_pairs;

    // 2) 两两撮合（对齐 C# 双层循环 + FindP2PMatches）
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        for (std::size_t j = i + 1; j < candidates.size(); ++j) {
            const Candidate& first = candidates[i];
            const Candidate& second = candidates[j];

            std::vector<std::int32_t> first_matched;
            std::vector<std::int32_t> second_matched;
            for (const std::int32_t fid : first.rocket_ids) {
                const auto fit = rockets_.find(fid);
                if (fit == rockets_.end()) continue;
                for (const std::int32_t sid : second.rocket_ids) {
                    const auto sit = rockets_.find(sid);
                    if (sit == rockets_.end()) continue;
                    // 同一 Address 且 2D 距离 <= 阈值（阈值与判据都在 P2PProximityPolicy 里）
                    if (!sfs::protocol::P2PProximityPolicy::is_eligible(
                            fit->second.state.location, sit->second.state.location,
                            p2p_.proximity_meters)) {
                        continue;
                    }
                    first_matched.push_back(fid);
                    second_matched.push_back(sid);
                }
            }
            if (first_matched.empty()) continue;  // 没有近距火箭 → 不撮合（旧配对会在第 3 步被撤销）

            const std::uint64_t key = p2p_pair_key(first.player_id, second.player_id);
            active_pairs.insert(key);
            bool newly_paired = false;
            auto tit = p2p_pair_tokens_.find(key);
            if (tit == p2p_pair_tokens_.end()) {
                // 首次撮合这一对：生成 pairToken（24 字节随机 → 48 位大写 hex，与 C# 生成方式一致）
                tit = p2p_pair_tokens_.emplace(key, random_token()).first;
                newly_paired = true;
                SFS_LOGI(Cat::P2P, "[cpp-server] P2P 配对: %d <-> %d 建立（阈值 %.0fm）",
                         first.player_id, second.player_id, p2p_.proximity_meters);
            }
            const std::string token = tit->second;
            sort_unique(first_matched);
            sort_unique(second_matched);

            // 【报价时机 2026-10-10】原先只在"配对新建"时发一次：
            //   - 每轮（1 秒）无条件重发会把客户端日志刷屏（历史问题，注释还在下面）；
            //   - 但只发一次也不行：客户端握手 10 秒超时后永久停在 Relay Fallback，
            //     而服务端 token 还在、配对仍有效，于是**再也不会补发** —— 实测现象就是
            //     "直连玩家永远是 0"，除非两人拉开距离让配对撤销再重新靠近。
            // 折中：新建时发，之后每 15 秒补发一次。客户端对"相同 token + 相同端点"的重复报价
            // 是幂等的（ApplyOffer 只在 token/端点变化时才重建 peer），所以补发不会打断已建立的链路。
            constexpr int kReOfferSeconds = 15;
            const auto now_offer = std::chrono::steady_clock::now();
            const auto offer_it = p2p_pair_last_offer_.find(key);
            const bool re_offer_due = offer_it == p2p_pair_last_offer_.end()
                || now_offer - offer_it->second >= std::chrono::seconds(kReOfferSeconds);
            if (newly_paired || re_offer_due) {
                p2p_pair_last_offer_[key] = now_offer;
                send_p2p_offer(first.fd, second.player_id, second.address, second.port, token,
                               first_matched, second_matched);
                send_p2p_offer(second.fd, first.player_id, first.address, first.port, token,
                               second_matched, first_matched);
            }
        }
    }

    // 3) 上一轮存在、这一轮不再匹配（含对方已掉线）的配对：撤销并回收 token
    for (auto it = p2p_pair_tokens_.begin(); it != p2p_pair_tokens_.end();) {
        if (active_pairs.count(it->first) > 0) {
            ++it;
            continue;
        }
        const std::int32_t a = static_cast<std::int32_t>(it->first >> 32);
        const std::int32_t b = static_cast<std::int32_t>(it->first & 0xFFFFFFFFu);
        p2p_pair_last_offer_.erase(it->first);
        it = p2p_pair_tokens_.erase(it);

        // 谁还在线就给谁发作废包。注：C# 这里要求\"双方都还在线\"才发，于是掉线一方消失后，
        // 幸存者只能靠客户端自己的握手超时（约 10s）兜底；这里改成在线的一方立刻收到，
        // 客户端 ApplyOffer(Active=false) 会马上 Dispose 掉那个 peer。
        const int fd_a = find_fd_by_player(a);
        if (fd_a >= 0) send_p2p_revoke(fd_a, b);
        const int fd_b = find_fd_by_player(b);
        if (fd_b >= 0) send_p2p_revoke(fd_b, a);
        SFS_LOGI(Cat::P2P, "[cpp-server] P2P 配对: %d <-> %d 撤销（在线: %d/%d）", a, b,
                 fd_a >= 0 ? 1 : 0, fd_b >= 0 ? 1 : 0);
    }
}

}  // namespace sfs::server
