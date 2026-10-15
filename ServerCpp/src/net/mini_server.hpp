// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "platform/socket_platform.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sfs::net {

class MiniServer {
public:
    MiniServer(std::string bind_address, std::uint16_t port);
    ~MiniServer();

    MiniServer(const MiniServer&) = delete;
    MiniServer& operator=(const MiniServer&) = delete;

    bool Start();
    void RequestStop();
    void Run();
    std::uint16_t Port() const { return port_; }

private:
    struct Client {
        sfs_socket_t socket = sfs::platform::kInvalidSocket;
        std::vector<std::uint8_t> input;
    };

    bool OpenTcp();
    bool OpenUdp();
    void CloseAll();
    void AcceptClients();
    void ReadClient(std::size_t index);
    void ReadUdp();
    void RemoveClient(std::size_t index);

    std::string bind_address_;
    std::uint16_t requested_port_;
    std::uint16_t port_ = 0;
    sfs_socket_t tcp_listener_ = sfs::platform::kInvalidSocket;
    sfs_socket_t udp_socket_ = sfs::platform::kInvalidSocket;
    std::array<Client, 16> clients_{};
    std::size_t client_count_ = 0;
    bool running_ = false;
    bool sockets_initialized_ = false;
};

}  // namespace sfs::net
