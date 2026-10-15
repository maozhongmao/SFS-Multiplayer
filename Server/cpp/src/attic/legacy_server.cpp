// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server.hpp"

#include "protocol/handshake.hpp"
#include "protocol/server_info.hpp"
#include "protocol/packets.hpp"
#include "net/lidgren_buffer.hpp"
#include "net/network_service.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

namespace sfs::server {

MultiplayerServer::MultiplayerServer(std::uint16_t tcp_port, std::uint16_t udp_port)
    : tcp_port_(tcp_port), udp_port_(udp_port) {
    // 直接使用全局工厂获取具体实现
    auto& svc = net::get_network_service();

    tcp_listener_ = svc.create_tcp_listener();
    udp_endpoint_ = svc.create_udp_endpoint();

    // 设置回调
    svc.set_tcp_data_callback([this](std::uint64_t conn_id, std::vector<std::uint8_t>&& data) {
        on_tcp_data(conn_id, std::move(data));
    });
    svc.set_tcp_close_callback([this](std::uint64_t conn_id) {
        on_tcp_close(conn_id);
    });
}

MultiplayerServer::~MultiplayerServer() {
    stop();
}

void MultiplayerServer::run() {
    std::cout << "[Server] Starting on TCP:" << tcp_port_ << " UDP:" << udp_port_ << std::endl;

    // 启动 TCP 监听
    tcp_listener_->start(tcp_port_, [this](std::unique_ptr<net::TcpConnection> conn) {
        on_tcp_accept(std::move(conn));
    });

    // 启动 UDP
    auto udp_impl = dynamic_cast<UdpEndpointImpl*>(udp_endpoint_.get());
    udp_impl->bind(udp_port_);
    udp_impl->set_recv_callback([this](const std::vector<std::uint8_t>& data,
                                       const std::string& from_addr, std::uint16_t from_port) {
        on_udp_recv(data, from_addr, from_port);
    });
    static_cast<NetworkServiceImpl&>(*net_).register_udp(
        std::unique_ptr<UdpEndpointImpl>(udp_impl)); // 转移所有权

    running_ = true;
    net_->run(); // 阻塞直到 stop()
}

void MultiplayerServer::stop() {
    running_ = false;
    net_->stop();
    tcp_listener_->stop();
    std::cout << "[Server] Stopped" << std::endl;
}

void MultiplayerServer::on_tcp_accept(std::unique_ptr<net::TcpConnection> conn) {
    auto* impl = dynamic_cast<net::TcpConnectionImpl*>(conn.get());
    if (!impl) return;

    std::uint64_t conn_id = impl->connection_id();
    std::cout << "[Server] New connection: " << conn_id << " from " << impl->remote_endpoint() << std::endl;

    // 注册到网络服务（epoll） - 需要访问具体实现
    auto& svc = net::get_network_service();
    static_cast<net::NetworkServiceImpl&>(svc).register_connection(
        std::unique_ptr<net::TcpConnectionImpl>(impl));

    // 创建帧处理器
    frame_handlers_.emplace(conn_id, TcpFrameHandler(
        [this, conn_id](const std::vector<std::uint8_t>& frame) {
            auto it = connections_.find(conn_id);
            if (it != connections_.end() && !it->second->is_closed()) {
                it->second->send_frame(frame);
            }
        },
        [this](std::uint64_t cid) {
            auto& svc2 = net::get_network_service();
            static_cast<net::NetworkServiceImpl&>(svc2).ensure_write_events(cid);
        }
    ));
    frame_handlers_[conn_id].set_connection_id(conn_id);
    frame_handlers_[conn_id].set_frame_callback(
        [this, conn_id](std::uint8_t kind, std::uint32_t seq,
                        std::vector<std::uint8_t>&& payload_bits,
                        std::vector<std::uint8_t>&& payload_bytes) {
            handle_frame(conn_id, kind, seq, std::move(payload_bits), std::move(payload_bytes));
        }
    );

    connections_[conn_id] = std::unique_ptr<net::TcpConnectionImpl>(impl);
}

void MultiplayerServer::on_tcp_data(std::uint64_t conn_id, std::vector<std::uint8_t>&& data) {
    auto it = frame_handlers_.find(conn_id);
    if (it != frame_handlers_.end()) {
        it->second.on_data(data);
    }
}

void MultiplayerServer::on_tcp_close(std::uint64_t conn_id) {
    std::cout << "[Server] Connection closed: " << conn_id << std::endl;

    // 玩家离线处理
    auto pit = conn_to_player_.find(conn_id);
    if (pit != conn_to_player_.end()) {
        std::int32_t pid = pit->second;
        conn_to_player_.erase(pit);
        auto psit = players_.find(pid);
        if (psit != players_.end()) {
            std::cout << "[Server] Player " << pid << " (" << psit->second.name << ") disconnected" << std::endl;
            // TODO: 广播玩家离开、保存火箭状态等
            players_.erase(psit);
        }
    }

    frame_handlers_.erase(conn_id);
    connections_.erase(conn_id);
}

void MultiplayerServer::on_udp_recv(const std::vector<std::uint8_t>& data, const std::string& from_addr, std::uint16_t from_port) {
    if (data.empty()) return;
    std::uint8_t kind = data[0];
    if (kind == 0x01) { // UdpKind.Bind
        // Bind 由 TCP 连接关联的玩家发起，这里不直接处理
        return;
    }
    if (kind == 0x02) { // UdpKind.Data
        // [kind:1][tokenLen:1][token][payload]
        if (data.size() < 2) return;
        std::uint8_t token_len = data[1];
        if (data.size() < 2 + token_len) return;
        std::string token(reinterpret_cast<const char*>(data.data() + 2), token_len);
        std::vector<std::uint8_t> payload(data.begin() + 2 + token_len, data.end());

        // 查找 token 对应的玩家
        for (auto& [pid, player] : players_) {
            if (player.resume_token == token) {
                // 转发给对应连接的帧处理器（作为 UDP 数据包）
                auto fit = frame_handlers_.find(player.conn_id);
                if (fit != frame_handlers_.end()) {
                    // 这里简化：直接作为 Packet 帧转发
                    // 实际应该解析 UDP payload 里的包类型
                }
                break;
            }
        }
    }
}

void MultiplayerServer::handle_frame(std::uint64_t conn_id, std::uint8_t kind, std::uint32_t seq,
                                     std::vector<std::uint8_t>&& payload_bits,
                                     std::vector<std::uint8_t>&& payload_bytes) {
    using protocol::TcpFrameKind;
    switch (static_cast<TcpFrameKind>(kind)) {
        case TcpFrameKind::Hello:
            handle_hello(conn_id, seq, std::move(payload_bits), std::move(payload_bytes));
            break;
        case TcpFrameKind::Packet:
            handle_packet(conn_id, seq, std::move(payload_bits), std::move(payload_bytes));
            break;
        case TcpFrameKind::Ping:
            handle_ping(conn_id, seq);
            break;
        case TcpFrameKind::Disconnect:
            handle_disconnect(conn_id, seq);
            break;
        case TcpFrameKind::RequestWorldSnapshot:
            handle_snapshot_request(conn_id, seq, std::move(payload_bits), std::move(payload_bytes));
            break;
        case TcpFrameKind::RequestRocketSnapshot:
            handle_rocket_snapshot_request(conn_id, seq, std::move(payload_bits), std::move(payload_bytes));
            break;
        case TcpFrameKind::ServerInfoRequest:
            handle_server_info_request(conn_id, seq, std::move(payload_bits), std::move(payload_bytes));
            break;
        default:
            std::cerr << "[Server] Unknown frame kind: " << static_cast<int>(kind) << std::endl;
            break;
    }
}

void MultiplayerServer::handle_hello(std::uint64_t conn_id, std::uint32_t seq,
                                     std::vector<std::uint8_t>&& payload_bits,
                                     std::vector<std::uint8_t>&& payload_bytes) {
    try {
        net::NetReader reader(payload_bits.data(), payload_bits.size(),
                              payload_bytes.data(), payload_bytes.size());

        // Hello: protocol_version(u8), player_name(str), resume_token(str?)
        std::uint8_t protocol_version = reader.read_u8();
        std::string player_name = reader.read_string();
        std::string resume_token;
        if (!reader.at_end()) resume_token = reader.read_string();

        std::cout << "[Server] Hello from " << player_name << " (proto=" << int(protocol_version) << ")" << std::endl;

        if (protocol_version != protocol::kProtocolVersion) {
            // 版本不匹配，发 Disconnect
            auto fit = frame_handlers_.find(conn_id);
            if (fit != frame_handlers_.end()) {
                fit->second.send_control(static_cast<std::uint8_t>(protocol::TcpFrameKind::Disconnect));
            }
            return;
        }

        // 分配玩家 ID
        std::int32_t player_id = next_player_id_++;
        PlayerSession session;
        session.conn_id = conn_id;
        session.name = player_name;
        session.player_id = player_id;
        session.authenticated = true;
        session.resume_token = resume_token.empty() ? std::to_string(conn_id) + "-" + std::to_string(player_id) : resume_token;
        session.last_activity = std::chrono::steady_clock::now();

        players_[player_id] = std::move(session);
        conn_to_player_[conn_id] = player_id;

        // 回复 HelloAck: player_id(i32), resume_token(str), server_time(i64), world_checksum(u32)
        net::NetWriter w;
        w.write_i32(player_id);
        w.write_string(players_[player_id].resume_token);
        w.write_i64(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
        w.write_u32(0); // world_checksum placeholder

        auto fit = frame_handlers_.find(conn_id);
        if (fit != frame_handlers_.end()) {
            fit->second.send_frame(
                static_cast<std::uint8_t>(protocol::TcpFrameKind::HelloAck),
                fit->second.next_sequence(),
                w.payload_bits(), w.payload_bytes()
            );
        }

        // 广播玩家加入
        broadcast_player_joined(player_id);

        // 发送世界快照（简化：空世界）
        send_world_snapshot(conn_id, player_id);

    } catch (const std::exception& e) {
        std::cerr << "[Server] Hello parse error: " << e.what() << std::endl;
    }
}

void MultiplayerServer::handle_packet(std::uint64_t conn_id, std::uint32_t seq,
                                      std::vector<std::uint8_t>&& payload_bits,
                                      std::vector<std::uint8_t>&& payload_bytes) {
    auto pit = conn_to_player_.find(conn_id);
    if (pit == conn_to_player_.end()) return;
    std::int32_t player_id = pit->second;
    auto psit = players_.find(player_id);
    if (psit == players_.end()) return;
    PlayerSession& player = psit->second;
    player.last_activity = std::chrono::steady_clock::now();

    try {
        net::NetReader reader(payload_bits.data(), payload_bits.size(),
                              payload_bytes.data(), payload_bytes.size());

        if (reader.at_end()) return;
        std::uint8_t packet_type = reader.read_u8();

        dispatch_packet(conn_id, player, packet_type, reader);

    } catch (const std::exception& e) {
        std::cerr << "[Server] Packet parse error: " << e.what() << std::endl;
    }
}

void MultiplayerServer::handle_ping(std::uint64_t conn_id, std::uint32_t seq) {
    auto fit = frame_handlers_.find(conn_id);
    if (fit != frame_handlers_.end()) {
        fit->second.send_frame(
            static_cast<std::uint8_t>(protocol::TcpFrameKind::Pong),
            seq, {}, {}
        );
    }
}

void MultiplayerServer::handle_disconnect(std::uint64_t conn_id, std::uint32_t seq) {
    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        it->second->close();
    }
}

void MultiplayerServer::handle_snapshot_request(std::uint64_t conn_id, std::uint32_t seq,
                                                std::vector<std::uint8_t>&& payload_bits,
                                                std::vector<std::uint8_t>&& payload_bytes) {
    auto pit = conn_to_player_.find(conn_id);
    if (pit == conn_to_player_.end()) return;
    send_world_snapshot(conn_id, pit->second);
}

void MultiplayerServer::handle_rocket_snapshot_request(std::uint64_t conn_id, std::uint32_t seq,
                                                       std::vector<std::uint8_t>&& payload_bits,
                                                       std::vector<std::uint8_t>&& payload_bytes) {
    auto pit = conn_to_player_.find(conn_id);
    if (pit == conn_to_player_.end()) return;

    try {
        net::NetReader reader(payload_bits.data(), payload_bits.size(),
                              payload_bytes.data(), payload_bytes.size());
        std::int32_t rocket_id = reader.read_i32();
        // TODO: 发送特定火箭快照
        std::cout << "[Server] Rocket snapshot request for rocket " << rocket_id << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Server] Rocket snapshot parse error: " << e.what() << std::endl;
    }
}

void MultiplayerServer::handle_server_info_request(std::uint64_t conn_id, std::uint32_t seq,
                                                   std::vector<std::uint8_t>&& payload_bits,
                                                   std::vector<std::uint8_t>&& payload_bytes) {
    // 回复 ServerInfoResponse
    protocol::ServerInfoResponse resp;
    resp.max_players = 16;
    resp.current_players = static_cast<std::int32_t>(players_.size());
    resp.server_name = "SFS Multiplayer (C++)";
    resp.game_version = "1.2.0";
    resp.protocol_version = protocol::kProtocolVersion;
    resp.map_name = "SolarSystem";
    resp.description = "C++ rewrite for embedded";

    net::NetWriter w;
    protocol::EncodeServerInfoResponse(resp, w);

    auto fit = frame_handlers_.find(conn_id);
    if (fit != frame_handlers_.end()) {
        fit->second.send_frame(
            static_cast<std::uint8_t>(protocol::TcpFrameKind::ServerInfoResponse),
            fit->second.next_sequence(),
            w.payload_bits(), w.payload_bytes()
        );
    }
}

void MultiplayerServer::dispatch_packet(std::uint64_t conn_id, const PlayerSession& player,
                                        std::uint8_t packet_type, net::NetReader& reader) {
    using protocol::PacketType;

    switch (static_cast<PacketType>(packet_type)) {
        case PacketType::PlayerInput: {
            // TODO: 解析输入并应用到火箭
            break;
        }
        case PacketType::CreateRocket: {
            // TODO: 创建火箭
            break;
        }
        case PacketType::DestroyRocket: {
            // TODO: 销毁火箭
            break;
        }
        case PacketType::RocketState: {
            // TODO: 更新火箭状态
            break;
        }
        case PacketType::PartState: {
            // TODO: 更新部件状态
            break;
        }
        case PacketType::ChatMessage: {
            // 广播聊天
            protocol::ChatMessagePacket pkt;
            protocol::DecodeChatMessagePacket(reader, pkt);
            broadcast_packet(packet_type, {}, {}); // 简化
            break;
        }
        case PacketType::UdpBind: {
            handle_udp_bind(conn_id, reader);
            break;
        }
        case PacketType::P2PPeerOffer: {
            // TODO: P2P 信令
            break;
        }
        default:
            std::cerr << "[Server] Unhandled packet type: " << static_cast<int>(packet_type) << std::endl;
            break;
    }
}

void MultiplayerServer::handle_udp_bind(std::uint64_t conn_id, net::NetReader& reader) {
    auto pit = conn_to_player_.find(conn_id);
    if (pit == conn_to_player_.end()) return;
    auto psit = players_.find(pit->second);
    if (psit == players_.end()) return;

    try {
        std::uint16_t port = reader.read_u16();
        psit->second.resume_token = std::to_string(conn_id) + "-" + std::to_string(pit->second);

        // 回复 UdpBindAck
        net::NetWriter w;
        w.write_u8(static_cast<std::uint8_t>(protocol::PacketType::UdpBindAck));
        w.write_u16(port);
        w.write_string(psit->second.resume_token);

        auto fit = frame_handlers_.find(conn_id);
        if (fit != frame_handlers_.end()) {
            fit->second.send_frame(
                static_cast<std::uint8_t>(protocol::TcpFrameKind::Packet),
                fit->second.next_sequence(),
                w.payload_bits(), w.payload_bytes()
            );
        }
        std::cout << "[Server] UDP bind for player " << pit->second << " port " << port << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Server] UDP bind parse error: " << e.what() << std::endl;
    }
}

void MultiplayerServer::broadcast_player_joined(std::int32_t player_id) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    net::NetWriter w;
    w.write_u8(static_cast<std::uint8_t>(protocol::PacketType::PlayerJoined));
    w.write_i32(player_id);
    w.write_string(it->second.name);

    for (auto& [pid, player] : players_) {
        if (pid == player_id) continue;
        auto fit = frame_handlers_.find(player.conn_id);
        if (fit != frame_handlers_.end()) {
            fit->second.send_frame(
                static_cast<std::uint8_t>(protocol::TcpFrameKind::Packet),
                fit->second.next_sequence(),
                w.payload_bits(), w.payload_bytes()
            );
        }
    }
}

void MultiplayerServer::send_world_snapshot(std::uint64_t conn_id, std::int32_t player_id) {
    // 简化：发送空世界 + 现有玩家列表
    net::NetWriter w;
    w.write_u8(static_cast<std::uint8_t>(protocol::PacketType::WorldSnapshot));
    w.write_i32(0); // world_time (placeholder)
    w.write_i32(static_cast<std::int32_t>(players_.size())); // player count

    for (auto& [pid, p] : players_) {
        w.write_i32(pid);
        w.write_string(p.name);
        w.write_i32(p.controlled_rocket);
    }
    w.write_i32(0); // rocket count
    w.write_i32(0); // part count

    auto fit = frame_handlers_.find(conn_id);
    if (fit != frame_handlers_.end()) {
        fit->second.send_frame(
            static_cast<std::uint8_t>(protocol::TcpFrameKind::Packet),
            fit->second.next_sequence(),
            w.payload_bits(), w.payload_bytes()
        );
    }
}

void MultiplayerServer::broadcast_packet(std::uint8_t packet_type,
                                         const std::vector<std::uint8_t>& payload_bits,
                                         const std::vector<std::uint8_t>& payload_bytes,
                                         std::int32_t exclude_player) {
    net::NetWriter w;
    w.write_u8(packet_type);
    w.write_bits(payload_bits);
    w.write_bytes(payload_bytes);

    for (auto& [pid, player] : players_) {
        if (pid == exclude_player) continue;
        auto fit = frame_handlers_.find(player.conn_id);
        if (fit != frame_handlers_.end()) {
            fit->second.send_frame(
                static_cast<std::uint8_t>(protocol::TcpFrameKind::Packet),
                fit->second.next_sequence(),
                w.payload_bits(), w.payload_bytes()
            );
        }
    }
}

void MultiplayerServer::send_to_player(std::int32_t player_id, std::uint8_t packet_type,
                                       const std::vector<std::uint8_t>& payload_bits,
                                       const std::vector<std::uint8_t>& payload_bytes) {
    auto it = players_.find(player_id);
    if (it == players_.end()) return;

    net::NetWriter w;
    w.write_u8(packet_type);
    w.write_bits(payload_bits);
    w.write_bytes(payload_bytes);

    auto fit = frame_handlers_.find(it->second.conn_id);
    if (fit != frame_handlers_.end()) {
        fit->second.send_frame(
            static_cast<std::uint8_t>(protocol::TcpFrameKind::Packet),
            fit->second.next_sequence(),
            w.payload_bits(), w.payload_bytes()
        );
    }
}

void MultiplayerServer::save_world() {
    // TODO: 持久化世界状态
}

void MultiplayerServer::load_world() {
    // TODO: 加载世界状态
}

} // namespace sfs::server
