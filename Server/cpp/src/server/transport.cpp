// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "server/util.hpp"
#include "util/log.hpp"

#include <cstdio>
#include <exception>

namespace sfs::server {

using sfs::log::Cat;
using sfs::protocol::TcpFrame;
using sfs::protocol::TcpFrameKind;

std::int32_t Server::next_seq(int fd) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return 1;
    return it->second.next_seq++;
}

void Server::send_control(int fd, TcpFrameKind kind) {
    send_frame(fd, TcpFrame{kind, next_seq(fd), {}, 0});
}

void Server::send_frame(int fd, const TcpFrame& frame) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    ++it->second.packets_out;
    try {
        auto bytes = sfs::protocol::EncodeTcpFrame(frame);
        kcp_.send(it->second.conv, bytes.data(), static_cast<int>(bytes.size()));
    } catch (const std::exception& e) {
        SFS_LOGE(Cat::Net, "[cpp-server] encode error: %s", e.what());
    }
}

// 拒绝加入：对齐 C# TcpMultiplayerServer.DisconnectFrame(reason)
//   new TcpFrame(TcpFrameKind.Disconnect, Sequence=0, UTF8(reason), reason.Length * 8)
// 客户端把这段文本直接当终止原因展示（TcpTransportCore / 握手期直接抛该文本）。
// 帧发出去之后不立刻断：KCP 还得有机会重传，否则丢一个 UDP 包玩家就只看到"连接断开"。
void Server::deny_join(int fd, const std::string& reason) {
    auto it = conns_.find(fd);
    if (it == conns_.end()) return;
    Connection& c = it->second;
    if (c.denied) return;   // 只拒一次：重复的 Hello 不该刷屏
    c.denied = true;
    c.deny_reason = reason;
    c.denied_at = std::chrono::steady_clock::now();

    SFS_LOGW(Cat::Net, "[cpp-server] 拒绝加入: user='%s' 原因=%s",
             sfs::server::sanitize_for_log(c.username).c_str(), reason.c_str());

    const std::vector<std::uint8_t> payload(reason.begin(), reason.end());
    send_frame(fd, TcpFrame{TcpFrameKind::Disconnect, 0, payload,
                            static_cast<std::int32_t>(payload.size() * 8)});
    std::fflush(stdout);
}

// ---------------------------------------------------------- 底层发送
void Server::send_payload(int fd, std::uint8_t packet_type, const sfs::net::NetWriter& body) {
    sfs::net::NetWriter w;
    w.write_u8(packet_type);
    if (body.get_bit_length() > 0) {
        w.write_bytes(body.data(), body.get_bytes().size());
    }
    auto data = w.get_bytes();
    send_frame(fd, TcpFrame{TcpFrameKind::Packet, next_seq(fd), data,
                            static_cast<std::int32_t>(w.get_bit_length())});
}

// ---------------------------------------------------------- UDP 下发

// UpdateRocketPrimary 是唯一走 UDP 状态通道的包（对齐 C# BroadcastLatest）：
// 接收方已建 UDP 通道则直发 UDP，否则退回 TCP。
void Server::broadcast_rocket_primary(const sfs::protocol::UpdateRocketPrimaryPacket& pkt, int exclude_fd) {
    using PT = sfs::protocol::PacketType;
    sfs::net::NetWriter w;
    w.write_u8(static_cast<std::uint8_t>(PT::kUpdateRocketPrimary));
    pkt.serialize(w);
    const auto data = w.get_bytes();
    const auto bits = static_cast<std::int32_t>(w.get_bit_length());

    for (auto& [cfd, c] : conns_) {
        if (cfd == exclude_fd || !c.handshaked) continue;
        send_frame(cfd, TcpFrame{TcpFrameKind::Packet, c.next_seq++, data, bits});
    }
}

}  // namespace sfs::server
