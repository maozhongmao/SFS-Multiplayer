// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "net/mini_server.hpp"

#include "protocol/codec.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

namespace sfs::net {
namespace {

constexpr std::size_t kMaxClients = 16;
constexpr std::size_t kReceiveBufferBytes = 4096;

sockaddr_in MakeAddress(const std::string& address, std::uint16_t port) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_port = htons(port);
    if (address.empty() || address == "0.0.0.0") {
        result.sin_addr.s_addr = htonl(INADDR_ANY);
        return result;
    }
    if (inet_pton(AF_INET, address.c_str(), &result.sin_addr) != 1) {
        throw std::invalid_argument("bind address must be an IPv4 address");
    }
    return result;
}

}  // namespace

MiniServer::MiniServer(std::string bind_address, std::uint16_t port)
    : bind_address_(std::move(bind_address)), requested_port_(port) {}

MiniServer::~MiniServer() {
    CloseAll();
}

bool MiniServer::OpenTcp() {
    tcp_listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (tcp_listener_ == sfs::platform::kInvalidSocket) return false;
    sfs::platform::SetReuseAddress(tcp_listener_);
    const auto address = MakeAddress(bind_address_, requested_port_);
    if (bind(tcp_listener_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return false;
    }
    if (listen(tcp_listener_, static_cast<int>(kMaxClients)) != 0) return false;
    if (!sfs::platform::SetNonBlocking(tcp_listener_)) return false;
    sockaddr_in actual{};
#ifdef _WIN32
    int length = sizeof(actual);
#else
    socklen_t length = sizeof(actual);
#endif
    if (getsockname(tcp_listener_, reinterpret_cast<sockaddr*>(&actual), &length) != 0) return false;
    port_ = ntohs(actual.sin_port);
    return true;
}

bool MiniServer::OpenUdp() {
    udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == sfs::platform::kInvalidSocket) return false;
    sfs::platform::SetReuseAddress(udp_socket_);
    const auto address = MakeAddress(bind_address_, port_);
    if (bind(udp_socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        return false;
    }
    return sfs::platform::SetNonBlocking(udp_socket_);
}

bool MiniServer::Start() {
    if (!sfs::platform::InitializeSockets()) return false;
    sockets_initialized_ = true;
    try {
        if (!OpenTcp() || !OpenUdp()) {
            CloseAll();
            return false;
        }
    } catch (...) {
        CloseAll();
        return false;
    }
    running_ = true;
    return true;
}

void MiniServer::RequestStop() {
    running_ = false;
}

void MiniServer::CloseAll() {
    running_ = false;
    for (std::size_t i = 0; i < client_count_; ++i) {
        sfs::platform::CloseSocket(clients_[i].socket);
        clients_[i] = Client{};
    }
    client_count_ = 0;
    sfs::platform::CloseSocket(tcp_listener_);
    sfs::platform::CloseSocket(udp_socket_);
    tcp_listener_ = sfs::platform::kInvalidSocket;
    udp_socket_ = sfs::platform::kInvalidSocket;
    port_ = 0;
    if (sockets_initialized_) {
        sfs::platform::ShutdownSockets();
        sockets_initialized_ = false;
    }
}

void MiniServer::AcceptClients() {
    while (client_count_ < kMaxClients) {
        sockaddr_storage address{};
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        const auto client = accept(tcp_listener_, reinterpret_cast<sockaddr*>(&address), &length);
        if (client == sfs::platform::kInvalidSocket) {
            if (!sfs::platform::IsWouldBlock(sfs::platform::LastSocketError())) running_ = false;
            return;
        }
        if (!sfs::platform::SetNonBlocking(client)) {
            sfs::platform::CloseSocket(client);
            continue;
        }
        clients_[client_count_].socket = client;
        clients_[client_count_].input.reserve(kReceiveBufferBytes);
        ++client_count_;
    }
}

void MiniServer::RemoveClient(std::size_t index) {
    if (index >= client_count_) return;
    sfs::platform::CloseSocket(clients_[index].socket);
    clients_[index] = std::move(clients_[client_count_ - 1]);
    clients_[client_count_ - 1] = Client{};
    --client_count_;
}

void MiniServer::ReadClient(std::size_t index) {
    std::array<std::uint8_t, kReceiveBufferBytes> buffer{};
    const auto received = recv(clients_[index].socket,
        reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0);
    if (received == 0) {
        RemoveClient(index);
        return;
    }
    if (received < 0) {
        if (!sfs::platform::IsWouldBlock(sfs::platform::LastSocketError())) RemoveClient(index);
        return;
    }
    auto& input = clients_[index].input;
    input.insert(input.end(), buffer.begin(), buffer.begin() + received);
    if (input.size() > protocol::kMaxTcpPayloadBytes + 13u) {
        RemoveClient(index);
        return;
    }
    // 第一阶段只验证连接和有界收包；业务帧处理在协议主线接入时实现。
    input.clear();
}

void MiniServer::ReadUdp() {
    std::array<std::uint8_t, 65536> buffer{};
    while (true) {
        sockaddr_storage address{};
#ifdef _WIN32
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        const auto received = recvfrom(udp_socket_, reinterpret_cast<char*>(buffer.data()),
            static_cast<int>(buffer.size()), 0, reinterpret_cast<sockaddr*>(&address), &length);
        if (received < 0) {
            if (!sfs::platform::IsWouldBlock(sfs::platform::LastSocketError())) running_ = false;
            return;
        }
        if (received < 2) continue;
        // 第一阶段只做 envelope 边界校验，不建立状态队列。
        try {
            protocol::DecodeUdpEnvelope(std::vector<std::uint8_t>(buffer.begin(), buffer.begin() + received));
        } catch (const std::exception&) {
            continue;
        }
    }
}

void MiniServer::Run() {
    while (running_) {
        const auto ready_client_count = client_count_;
        fd_set descriptors;
        FD_ZERO(&descriptors);
        FD_SET(tcp_listener_, &descriptors);
        FD_SET(udp_socket_, &descriptors);
#ifndef _WIN32
        int max_descriptor = std::max(tcp_listener_, udp_socket_);
#endif
        for (std::size_t i = 0; i < ready_client_count; ++i) {
            FD_SET(clients_[i].socket, &descriptors);
#ifndef _WIN32
            max_descriptor = std::max(max_descriptor, clients_[i].socket);
#endif
        }

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
#ifdef _WIN32
        const auto result = select(0, &descriptors, nullptr, nullptr, &timeout);
#else
        const auto result = select(max_descriptor + 1, &descriptors, nullptr, nullptr, &timeout);
#endif
        if (result < 0) {
#ifdef _WIN32
            if (sfs::platform::LastSocketError() != WSAEINTR) running_ = false;
#else
            if (errno != EINTR) running_ = false;
#endif
            continue;
        }
        if (FD_ISSET(tcp_listener_, &descriptors)) AcceptClients();
        if (FD_ISSET(udp_socket_, &descriptors)) ReadUdp();
        for (std::size_t i = ready_client_count; i > 0; --i) {
            const auto index = i - 1;
            if (FD_ISSET(clients_[index].socket, &descriptors)) ReadClient(index);
        }
    }
    CloseAll();
}

}  // namespace sfs::net
