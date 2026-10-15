// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace sfs::platform {

#ifdef _WIN32
using sfs_socket_t = SOCKET;
constexpr sfs_socket_t kInvalidSocket = INVALID_SOCKET;
// Windows 的 recvfrom 收 int*，POSIX 收 socklen_t*：统一成一个别名，免得满屏 #ifdef。
using sfs_socklen_t = int;
#else
using sfs_socket_t = int;
constexpr sfs_socket_t kInvalidSocket = -1;
using sfs_socklen_t = socklen_t;
#endif

bool InitializeSockets();
void ShutdownSockets();
void CloseSocket(sfs_socket_t socket);
bool SetNonBlocking(sfs_socket_t socket);
bool SetReuseAddress(sfs_socket_t socket);
// 扩大内核收发缓冲。不设的话用的是系统默认（Linux 常见 net.core.rmem_default = 256 KB），
// 满速同步下内核会直接丢包。
bool SetSocketBuffers(sfs_socket_t socket, int receive_bytes, int send_bytes);
int LastSocketError();
bool IsWouldBlock(int error);

}  // namespace sfs::platform
