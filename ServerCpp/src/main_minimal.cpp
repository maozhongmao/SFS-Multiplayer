// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace sfs::server {

class MultiplayerServer {
public:
    MultiplayerServer(std::uint16_t tcp_port, std::uint16_t udp_port)
        : tcp_port_(tcp_port), udp_port_(udp_port) {
        printf("[C++] Server constructor: TCP=%d UDP=%d\n", tcp_port, udp_port);
    }
    ~MultiplayerServer() { printf("[C++] Server dtor\n"); }

    void run() {
        printf("[C++] Server run() — minimal mode: hello + serverinfo only, no packet logic.\n");
        // 不阻塞太久，仅作展示
        sleep(2);
        printf("[C++] Server run() done.\n");
    }

    void stop() {}

private:
    std::uint16_t tcp_port_;
    std::uint16_t udp_port_;
};

} // namespace sfs::server

int main(int argc, char* argv[]) {
    std::uint16_t tcp_port = 9806;
    std::uint16_t udp_port = 9806;
    if (argc >= 2) tcp_port = static_cast<std::uint16_t>(std::atoi(argv[1]));
    if (argc >= 3) udp_port = static_cast<std::uint16_t>(std::atoi(argv[2]));

    sfs::server::MultiplayerServer server(tcp_port, udp_port);
    server.run();
    return 0;
}
