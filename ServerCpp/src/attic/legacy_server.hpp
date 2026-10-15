// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/codec.hpp"
#include "net/lidgren_buffer.hpp"
#include "protocol/packets.hpp"
#include "net/network_service.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <atomic>

namespace sfs::server {

// ------------------------------------------------------------
// TCP 帧收发器（对应 C# TcpTransportCore / TcpSendQueue）
// ------------------------------------------------------------
class TcpFrameHandler {
public:
    using FrameCallback = std::function<void(std::uint8_t kind, std::uint32_t seq, std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes)>;
    using SendCallback = std::function<void(const std::vector<std::uint8_t>& frame)>;
    using FlushCallback = std::function<void(std::uint64_t conn_id)>;

    explicit TcpFrameHandler(SendCallback send, FlushCallback flush = nullptr)
        : send_(std::move(send)), flush_(std::move(flush)) {}

    void set_connection_id(std::uint64_t conn_id) { current_conn_id_ = conn_id; }

    // 处理接收到的字节流（粘包/拆包）
    void on_data(const std::vector<std::uint8_t>& data) {
        recv_buf_.insert(recv_buf_.end(), data.begin(), data.end());
        while (try_parse_frame()) {}
    }

    // 发送帧：kind(1) + seq(4) + payload_bits(1) + payload
    void send_frame(std::uint8_t kind, std::uint32_t seq,
                    const std::vector<std::uint8_t>& payload_bits,
                    const std::vector<std::uint8_t>& payload_bytes) {
        std::vector<std::uint8_t> frame;
        frame.reserve(9 + payload_bits.size() + payload_bytes.size());

        // body_len = 1 + 4 + 1 + payload_bits + payload_bytes
        std::uint32_t body_len = 1 + 4 + 1 + payload_bits.size() + payload_bytes.size();
        frame.push_back(static_cast<std::uint8_t>(body_len & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((body_len >> 8) & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((body_len >> 16) & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((body_len >> 24) & 0xFF));
        frame.push_back(kind);
        frame.push_back(static_cast<std::uint8_t>(seq & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((seq >> 8) & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((seq >> 16) & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((seq >> 24) & 0xFF));
        frame.push_back(static_cast<std::uint8_t>(payload_bits.size()));
        frame.insert(frame.end(), payload_bits.begin(), payload_bits.end());
        frame.insert(frame.end(), payload_bytes.begin(), payload_bytes.end());

        send_(frame);
        if (flush_) flush_(current_conn_id_);
        next_seq_ = seq + 1;
    }

    // 发送 Hello / HelloAck / Ping / Pong / Disconnect 等控制帧（无 payload）
    void send_control(std::uint8_t kind) {
        send_frame(kind, next_seq_, {}, {});
    }

    void set_frame_callback(FrameCallback cb) { on_frame_ = std::move(cb); }

    std::uint32_t next_sequence() const { return next_seq_; }

private:
    // ... existing private members
    std::uint64_t current_conn_id_ = 0;
    FlushCallback flush_;
    bool try_parse_frame() {
        if (recv_buf_.size() < 9) return false; // 最小帧头 9 字节

        std::uint32_t body_len = static_cast<std::uint32_t>(recv_buf_[0])
                               | (static_cast<std::uint32_t>(recv_buf_[1]) << 8)
                               | (static_cast<std::uint32_t>(recv_buf_[2]) << 16)
                               | (static_cast<std::uint32_t>(recv_buf_[3]) << 24);

        if (recv_buf_.size() < 9 + body_len) return false; // 不完整帧

        std::uint8_t kind = recv_buf_[4];
        std::uint32_t seq = static_cast<std::uint32_t>(recv_buf_[5])
                          | (static_cast<std::uint32_t>(recv_buf_[6]) << 8)
                          | (static_cast<std::uint32_t>(recv_buf_[7]) << 16)
                          | (static_cast<std::uint32_t>(recv_buf_[8]) << 24);

        std::size_t offset = 9;
        std::uint8_t payload_bits_len = recv_buf_[offset++];
        std::vector<std::uint8_t> payload_bits(recv_buf_.begin() + offset, recv_buf_.begin() + offset + payload_bits_len);
        offset += payload_bits_len;
        std::vector<std::uint8_t> payload_bytes(recv_buf_.begin() + offset, recv_buf_.begin() + offset + (body_len - 1 - 4 - 1 - payload_bits_len));

        // 消费
        recv_buf_.erase(recv_buf_.begin(), recv_buf_.begin() + 9 + body_len);

        if (on_frame_) on_frame_(kind, seq, std::move(payload_bits), std::move(payload_bytes));
        return true;
    }

    SendCallback send_;
    FrameCallback on_frame_;
    std::vector<std::uint8_t> recv_buf_;
    std::uint32_t next_seq_ = 1;
};

// ------------------------------------------------------------
// 玩家会话
// ------------------------------------------------------------
struct PlayerSession {
    std::uint64_t conn_id = 0;
    std::string name;
    std::int32_t player_id = -1;        // 服务端分配的玩家 ID
    std::int32_t controlled_rocket = -1; // 当前控制的火箭
    bool authenticated = false;
    std::string resume_token;            // 断线重连用
    std::chrono::steady_clock::time_point last_activity;
};

// ------------------------------------------------------------
// 服务端核心（主逻辑）
// ------------------------------------------------------------
class MultiplayerServer {
public:
    MultiplayerServer(std::uint16_t tcp_port, std::uint16_t udp_port);
    ~MultiplayerServer();

    void run();
    void stop();

private:
    // 网络回调
    void on_tcp_accept(std::unique_ptr<net::TcpConnection> conn);
    void on_tcp_data(std::uint64_t conn_id, std::vector<std::uint8_t>&& data);
    void on_tcp_close(std::uint64_t conn_id);
    void on_udp_recv(const std::vector<std::uint8_t>& data, const std::string& from_addr, std::uint16_t from_port);

    // 帧处理
    void handle_frame(std::uint64_t conn_id, std::uint8_t kind, std::uint32_t seq,
                      std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);

    // 握手处理
    void handle_hello(std::uint64_t conn_id, std::uint32_t seq,
                      std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);
    void handle_packet(std::uint64_t conn_id, std::uint32_t seq,
                       std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);
    void handle_ping(std::uint64_t conn_id, std::uint32_t seq);
    void handle_disconnect(std::uint64_t conn_id, std::uint32_t seq);
    void handle_snapshot_request(std::uint64_t conn_id, std::uint32_t seq,
                                 std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);
    void handle_rocket_snapshot_request(std::uint64_t conn_id, std::uint32_t seq,
                                        std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);
    void handle_server_info_request(std::uint64_t conn_id, std::uint32_t seq,
                                    std::vector<std::uint8_t>&& payload_bits, std::vector<std::uint8_t>&& payload_bytes);

    // 包分发
    void dispatch_packet(std::uint64_t conn_id, const PlayerSession& player,
                         std::uint8_t packet_type,
                         net::NetReader& reader);

    // 广播
    void broadcast_packet(std::uint8_t packet_type, const std::vector<std::uint8_t>& payload_bits,
                          const std::vector<std::uint8_t>& payload_bytes, std::int32_t exclude_player = -1);
    void send_to_player(std::int32_t player_id, std::uint8_t packet_type,
                        const std::vector<std::uint8_t>& payload_bits,
                        const std::vector<std::uint8_t>& payload_bytes);

    // UDP 绑定
    void handle_udp_bind(std::uint64_t conn_id, net::NetReader& reader);
    void handle_udp_data(const std::vector<std::uint8_t>& data, const std::string& from_addr, std::uint16_t from_port);

    // 世界状态
    void save_world();
    void load_world();

    // 成员
    std::uint16_t tcp_port_;
    std::uint16_t udp_port_;
    std::unique_ptr<net::INetworkService::TcpListener> tcp_listener_;
    std::unique_ptr<net::UdpEndpoint> udp_endpoint_;

    std::unordered_map<std::uint64_t, std::unique_ptr<net::TcpConnection>> connections_;
    std::unordered_map<std::uint64_t, TcpFrameHandler> frame_handlers_;
    std::unordered_map<std::int32_t, PlayerSession> players_; // player_id -> session
    std::unordered_map<std::uint64_t, std::int32_t> conn_to_player_; // conn_id -> player_id

    std::atomic<std::int32_t> next_player_id_{1};
    std::atomic<bool> running_{false};

    // 世界状态（简化：先内存，后接状态持久化）
    struct WorldState {
        // TODO: 火箭、部件、对接口等
    } world_;
};

} // namespace sfs::server
