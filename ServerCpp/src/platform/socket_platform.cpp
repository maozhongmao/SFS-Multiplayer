// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "platform/socket_platform.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace sfs::platform {

bool InitializeSockets() {
#ifdef _WIN32
    WSADATA data{};
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
    return true;
#endif
}

void ShutdownSockets() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void CloseSocket(sfs_socket_t socket) {
    if (socket == kInvalidSocket) return;
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

bool SetNonBlocking(sfs_socket_t socket) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(socket, FIONBIO, &mode) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool SetReuseAddress(sfs_socket_t socket) {
    int enabled = 1;
    return setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
#ifdef _WIN32
        reinterpret_cast<const char*>(&enabled), sizeof(enabled)
#else
        &enabled, sizeof(enabled)
#endif
    ) == 0;
}

// 扩大内核收发缓冲。为什么必须做：默认值（Linux net.core.rmem_default，常见 256 KB）在
// 满速同步下会被内核直接丢掉 —— 现场实测板子 eth1 的 rx_dropped 持续增长（615 -> 618 / 15s），
// 而同一时刻物理链路是零丢包、零错误、ping 最坏 2ms。
// 内核丢掉的每一个包都会变成 KCP 的一次超时重传，把 srtt 从 2ms 抬到近 1 秒，
// 表现为"玩到一半突然卡顿"，最后服务端按 heartbeat timeout 把会话判死。
// 注意 Linux 会把请求值翻倍后再用 net.core.rmem_max 截断，所以请求 1 MB 实际最多也拿到 rmem_max。
bool SetSocketBuffers(sfs_socket_t socket, int receive_bytes, int send_bytes) {
    bool ok = true;
    if (receive_bytes > 0) {
        int value = receive_bytes;
        ok = setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
#ifdef _WIN32
            reinterpret_cast<const char*>(&value), sizeof(value)
#else
            &value, sizeof(value)
#endif
        ) == 0 && ok;
    }
    if (send_bytes > 0) {
        int value = send_bytes;
        ok = setsockopt(socket, SOL_SOCKET, SO_SNDBUF,
#ifdef _WIN32
            reinterpret_cast<const char*>(&value), sizeof(value)
#else
            &value, sizeof(value)
#endif
        ) == 0 && ok;
    }
    return ok;
}

int LastSocketError() {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool IsWouldBlock(int error) {
#ifdef _WIN32
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return error == EAGAIN || error == EWOULDBLOCK;
#endif
}

}  // namespace sfs::platform
