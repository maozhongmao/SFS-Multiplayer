// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "util/log.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;
using sfs::protocol::kMaxTcpPayloadBytes;

// EncodeTcpFrame 的帧体头长度：kind(1) + sequence(4) + payload_bits(4)
constexpr std::size_t kTcpBodyHeaderBytes = 9;
constexpr std::size_t kMaxRecvBuffer = 1u << 20;  // 单连接接收缓冲上限 1MB

// ---------------------------------------------------------- 连接管理
void Server::close_conn(int fd, const char* why) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    if (connected_fds_.erase(fd) > 0 && player_count_ > 0) --player_count_;
    // 清理该玩家的控制声明与权威，并广播离开事件（对齐 C# 断线处理）
    if (it->second.handshaked && it->second.player_id >= 0) {
        const std::int32_t pid = it->second.player_id;
        controlled_rocket_.erase(pid);
        update_authority_.erase(pid);
        sent_authority_.erase(pid);   // 重连后要重新下发一次权威列表
        // 只有该玩家不再被任何连接持有时才回收身份，而且保留 PlayerInfo 一个宽限窗口：
        // 直接 erase 会在 10 秒心跳超时时把 resume_token 一起删掉，重连必然变成"新玩家"。
        bool still_held = false;
        for (const auto& [ofd, oc] : conns_) {
            if (ofd != fd && oc.handshaked && oc.player_id == pid) { still_held = true; break; }
        }
        if (!still_held) {
            if (auto pit = players_.find(pid); pit != players_.end()) pit->second.offline_since = uptime_now();
            offline_since_[pid] = uptime_now();
        }
        // 清掉该玩家的"本地编号 -> 全局 ID"去重表，避免重连后复用旧编号（local_id 会从头分配）
        for (auto cit = created_local_ids_.begin(); cit != created_local_ids_.end();) {
            if (static_cast<std::int32_t>(cit->first >> 32) == pid) cit = created_local_ids_.erase(cit);
            else ++cit;
        }
        sfs::protocol::PlayerDisconnectedPacket disc;
        disc.player_id = pid;
        broadcast_packet(static_cast<std::uint8_t>(sfs::protocol::PacketType::kPlayerDisconnected),
                         disc, fd);
        refresh_authorities();
    }
    SFS_LOGI(Cat::Net, "[cpp-server] -conn #%llu (%s)",
             static_cast<unsigned long long>(it->second.id), why);
    kcp_.close_session(static_cast<std::uint32_t>(fd));
    conns_.erase(it);
}

// KCP 没有 accept：枚举当前所有会话，新出现的建立 Connection，已有的收帧。
void Server::poll_kcp_sessions() {
    for (const auto conv : kcp_.session_convs()) {
        // 同一 conv 的 KCP 会话被新源端口重建（客户端开了新 socket，例如快速重试/重进世界）：
        // 旧 Connection 必须一并丢掉，否则新客户端会被当成"重复 Hello"——只拿到 ack、
        // 跳过 send_initial_state，表现为"连上了但世界是空的/第一次加入像失败"。
        if (kcp_.consume_rebuilt(conv) && conns_.count(conv) > 0) {
            close_conn(static_cast<int>(conv), "kcp session rebuilt (new peer)");
        }
        auto it = conns_.find(conv);
        if (it == conns_.end()) {
            Connection c;
            c.conv = conv;
            c.id = ++next_conn_id_;
            conns_.emplace(conv, std::move(c));
            SFS_LOGI(Cat::Net, "[cpp-server] +conn #%llu conv=%u",
                     static_cast<unsigned long long>(conns_[conv].id), conv);
            continue;
        }
        if (!drain_frames(conv)) continue;
    }
    // KCP 侧超时回收的会话，这里同步清理
    for (auto it = conns_.begin(); it != conns_.end();) {
        if (kcp_.has_session(it->first)) { ++it; continue; }
        const std::uint32_t conv = it->first;
        // 复用 close_conn 的清理路径（它会 erase，所以先把迭代器推进）
        ++it;
        close_conn(conv, "kcp session closed");
    }
}

// ---------------------------------------------------------- 帧解析
bool Server::drain_frames(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return false;
    Connection& c = it->second;

    // 从 KCP 取出所有完整消息（每次 receive 得到一条 EncodeTcpFrame 的输出）
    std::string msg;
    for (;;) {
        const int got = kcp_.receive(c.conv, &msg);
        if (got <= 0) break;
        c.recv.insert(c.recv.end(), msg.begin(), msg.end());
        if (c.recv.size() > kMaxRecvBuffer) { close_conn(fd, "recv overflow"); return false; }
    }

    std::size_t consumed = 0;
    while (true) {
        const std::size_t avail = c.recv.size() - consumed;
        if (avail < sizeof(std::int32_t)) break;

        std::int32_t body_len = 0;
        std::memcpy(&body_len, c.recv.data() + consumed, sizeof(body_len));
        if (body_len < static_cast<std::int32_t>(kTcpBodyHeaderBytes)
            || static_cast<std::size_t>(body_len) > kMaxTcpPayloadBytes + kTcpBodyHeaderBytes) {
            close_conn(fd, "bad frame length");
            return false;
        }
        const std::size_t total = sizeof(std::int32_t) + static_cast<std::size_t>(body_len);
        if (avail < total) break;

        std::vector<std::uint8_t> raw(c.recv.begin() + consumed,
                                      c.recv.begin() + consumed + total);
        consumed += total;

        TcpFrame frame;
        try {
            frame = sfs::protocol::DecodeTcpFrame(raw);
        } catch (const std::exception& e) {
            SFS_LOGE(Cat::Net, "[cpp-server] decode error: %s", e.what());
            close_conn(fd, "decode error");
            return false;
        }
        // 单个帧处理出错不能拖垮整个服务端（曾因 ServerInfoRequest 触发异常导致进程退出）
        try {
            handle_frame(fd, frame);
        } catch (const std::exception& e) {
            SFS_LOGW(Cat::Net, "[cpp-server] 帧处理异常（已忽略）: %s", e.what());
        }
        if (conns_.find(fd) == conns_.end()) return false;
    }

    if (consumed > 0) {
        auto jt = conns_.find(fd);
        if (jt == conns_.end()) return false;
        jt->second.recv.erase(jt->second.recv.begin(), jt->second.recv.begin() + consumed);
    }
    return true;
}

}  // namespace sfs::server
