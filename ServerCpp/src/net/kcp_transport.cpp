// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "net/kcp_transport.hpp"
#include "util/log.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>      // getaddrinfo/freeaddrinfo：双栈绑定与 IPv6 地址解析
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace sfs::net {

using sfs::log::Cat;

namespace {

// KCP 头长度为 24 字节（conv/cmd/frg/wnd/ts/sn/una/len），短于这个长度
// 的 UDP 包不可能是 KCP 数据。
constexpr int kKcpHeaderBytes = 24;

inline std::uint32_t read_u32_le(const char* p) {
    const auto* b = reinterpret_cast<const std::uint8_t*>(p);
    return static_cast<std::uint32_t>(b[0]) |
           (static_cast<std::uint32_t>(b[1]) << 8) |
           (static_cast<std::uint32_t>(b[2]) << 16) |
           (static_cast<std::uint32_t>(b[3]) << 24);
}

inline void write_u32_le(char* p, std::uint32_t v) {
    auto* b = reinterpret_cast<std::uint8_t*>(p);
    b[0] = static_cast<std::uint8_t>(v & 0xFF);
    b[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    b[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    b[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

// ---- 端点工具：同时处理 IPv4 与 IPv6 ----
// 双栈 socket 上 IPv4 对端表现为 ::ffff:a.b.c.d，所以到处比较/打印前先归一。
struct AddrKey {
    std::uint8_t family = 0;   // 4 或 6
    std::uint8_t bytes[16] = {0};
    std::size_t len = 0;
};

inline AddrKey address_key(const sockaddr_storage& ss) {
    AddrKey k;
    if (ss.ss_family == AF_INET) {
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(&ss);
        k.family = 4;
        k.len = 4;
        std::memcpy(k.bytes, &v4->sin_addr, 4);
    } else if (ss.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&ss);
        static const std::uint8_t v4mapped[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
        if (std::memcmp(v6->sin6_addr.s6_addr, v4mapped, 12) == 0) {
            k.family = 4;
            k.len = 4;
            std::memcpy(k.bytes, v6->sin6_addr.s6_addr + 12, 4);
        } else {
            k.family = 6;
            k.len = 16;
            std::memcpy(k.bytes, v6->sin6_addr.s6_addr, 16);
        }
    }
    return k;
}

inline std::uint16_t endpoint_port(const sockaddr_storage& ss) {
    if (ss.ss_family == AF_INET) {
        return ntohs(reinterpret_cast<const sockaddr_in*>(&ss)->sin_port);
    }
    if (ss.ss_family == AF_INET6) {
        return ntohs(reinterpret_cast<const sockaddr_in6*>(&ss)->sin6_port);
    }
    return 0;
}

inline bool endpoint_same_ip(const sockaddr_storage& a, const sockaddr_storage& b) {
    const AddrKey ka = address_key(a);
    const AddrKey kb = address_key(b);
    return ka.family == kb.family && ka.len == kb.len &&
           std::memcmp(ka.bytes, kb.bytes, ka.len) == 0;
}

inline bool endpoint_equal(const sockaddr_storage& a, const sockaddr_storage& b) {
    return endpoint_same_ip(a, b) && endpoint_port(a) == endpoint_port(b);
}

inline const char* ntop(int family, const void* addr, char* out, std::size_t out_len) {
#ifdef _WIN32
    return ::inet_ntop(family, addr, out, out_len);
#else
    return ::inet_ntop(family, addr, out, static_cast<socklen_t>(out_len));
#endif
}

// 端点转文本：IPv4-mapped 的 IPv6 按纯 IPv4 打，否则日志里会满是 ::ffff:192.168.1.6。
inline bool endpoint_to_string(const sockaddr_storage& ss, char* out, std::size_t out_len) {
    const AddrKey k = address_key(ss);
    if (k.family == 4) return ntop(AF_INET, k.bytes, out, out_len) != nullptr;
    if (ss.ss_family == AF_INET6) {
        const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&ss);
        return ntop(AF_INET6, &v6->sin6_addr, out, out_len) != nullptr;
    }
    return false;
}

// 把纯 IPv4 端点写成 IPv4-mapped IPv6 形式（::ffff:a.b.c.d）。
// 双栈 socket 上向纯 IPv4 目标 sendto 会失败，必须走这个形式。
inline void to_v4_mapped(const sockaddr_in& v4, sockaddr_in6* out) {
    std::memset(out, 0, sizeof(*out));
    out->sin6_family = AF_INET6;
    out->sin6_port = v4.sin_port;
    std::uint8_t* b = out->sin6_addr.s6_addr;
    std::memset(b, 0, 16);
    b[10] = 0xFF;
    b[11] = 0xFF;
    std::memcpy(b + 12, &v4.sin_addr, 4);
}

// 解析数字形式的主机地址（IPv4 或 IPv6 字面量）成 sockaddr_storage。
inline bool parse_numeric_host(const char* host, std::uint16_t port,
                               sockaddr_storage* out,
                               sfs::platform::sfs_socklen_t* out_len) {
    if (host == nullptr || out == nullptr || out_len == nullptr) return false;
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICHOST;   // 只要字面量，不做 DNS（域名解析在客户端那侧）
    char port_text[16];
    std::snprintf(port_text, sizeof(port_text), "%u", static_cast<unsigned>(port));
    addrinfo* res = nullptr;
    if (::getaddrinfo(host, port_text, &hints, &res) != 0 || res == nullptr) return false;
    bool ok = res->ai_addrlen > 0 &&
              static_cast<std::size_t>(res->ai_addrlen) <= sizeof(sockaddr_storage);
    if (ok) {
        std::memset(out, 0, sizeof(sockaddr_storage));
        std::memcpy(out, res->ai_addr, static_cast<std::size_t>(res->ai_addrlen));
        *out_len = static_cast<sfs::platform::sfs_socklen_t>(res->ai_addrlen);
    }
    ::freeaddrinfo(res);
    return ok;
}

}  // namespace

// ---------------------------------------------------------------- 生命周期

KcpTransport::~KcpTransport() { shutdown(); }

std::uint32_t KcpTransport::now_ms() {
    using namespace std::chrono;
    return static_cast<std::uint32_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

bool KcpTransport::init(std::uint16_t port, const char* bind_addr) {
    shutdown();

    if (!sfs::platform::InitializeSockets()) return false;

    // 绑定地址：nullptr / 空串 / "::" 都表示【双栈】（见头文件）。
    // 以前默认 "0.0.0.0" 是纯 IPv4 —— IPv6 的包根本到不了这个 socket，
    // 客户端表现就是"登录页永远 unreachable"，这正是模组连不上 IPv6 的根因。
    const char* requested = (bind_addr == nullptr || *bind_addr == '\0') ? "::" : bind_addr;

    char port_text[16];
    std::snprintf(port_text, sizeof(port_text), "%u", static_cast<unsigned>(port));

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST;   // 只接受字面量，不做 DNS
    addrinfo* resolved = nullptr;
    if (::getaddrinfo(requested, port_text, &hints, &resolved) != 0 || resolved == nullptr) {
        SFS_LOGE(Cat::Net, "[cpp-server] 无法解析绑定地址 %s", requested);
        shutdown();
        return false;
    }

    socket_family_ = resolved->ai_family;
    udp_fd_ = ::socket(resolved->ai_family, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_fd_ == sfs::platform::kInvalidSocket) {
        SFS_LOGE(Cat::Net, "[cpp-server] 创建 UDP 套接字失败（资源不足或无权限），端口=%u",
                 static_cast<unsigned>(port));
        ::freeaddrinfo(resolved);
        shutdown();
        return false;
    }

    // 双栈：绑的是 IPv6 时关掉 IPV6_V6ONLY，同一个 socket 才收得到 IPv4（表现为 ::ffff:a.b.c.d）。
    // Windows 的默认值是 V6ONLY=1，不显式关掉就会出现"IPv6 通了、原来的 IPv4 玩家反而全掉线"。
    if (resolved->ai_family == AF_INET6) {
        int v6only = 0;
        ::setsockopt(udp_fd_, IPPROTO_IPV6, IPV6_V6ONLY,
#ifdef _WIN32
                     reinterpret_cast<const char*>(&v6only), sizeof(v6only)
#else
                     &v6only, sizeof(v6only)
#endif
        );
    }
    // 故意不设 SO_REUSEADDR：UDP 下它会让两个实例同时绑同一端口，收包被劈开，
    // 表现为"服务在跑却收不到玩家的包"。单实例由 /etc/init.d/S99sfs 的 pidof 守卫保证。
    // 扩大内核收发缓冲（默认 256 KB 在满速同步下会被内核丢包，丢的包全变成 KCP 重传）。
    // 必须在 bind 之前设；值会被 net.core.rmem_max 截断，所以板子上也要把 rmem_max 抬起来。
    sfs::platform::SetSocketBuffers(udp_fd_, 1048576, 1048576);

    if (!sfs::platform::SetNonBlocking(udp_fd_)) {
        SFS_LOGE(Cat::Net, "[cpp-server] 设置 UDP 非阻塞失败，端口=%u", static_cast<unsigned>(port));
        shutdown();
        return false;
    }

    try {
        if (::bind(udp_fd_, resolved->ai_addr,
                   static_cast<sfs::platform::sfs_socklen_t>(resolved->ai_addrlen)) < 0) {
            SFS_LOGE(Cat::Net, "[cpp-server] 绑定 UDP %s:%u 失败：端口已被占用或无权限（是否已有实例在跑？）",
                     requested, static_cast<unsigned>(port));
            ::freeaddrinfo(resolved);
            shutdown();
            return false;
        }
    } catch (...) {
        SFS_LOGE(Cat::Net, "[cpp-server] 绑定 UDP %u 时抛出异常（已捕获，不退出）",
                 static_cast<unsigned>(port));
        ::freeaddrinfo(resolved);
        shutdown();
        return false;
    }
    ::freeaddrinfo(resolved);

    port_ = port;
    last_update_ms_ = now_ms();
    return true;
}

void KcpTransport::shutdown() {
    for (auto& kv : sessions_) {
        if (kv.second.kcp) {
            ikcp_release(kv.second.kcp);
            kv.second.kcp = nullptr;
        }
    }
    sessions_.clear();

    if (udp_fd_ != sfs::platform::kInvalidSocket) {
        sfs::platform::CloseSocket(udp_fd_);
        udp_fd_ = sfs::platform::kInvalidSocket;
    }
    sfs::platform::ShutdownSockets();
    port_ = 0;
}

bool KcpTransport::connect(std::uint32_t conv, const char* ip, std::uint16_t port) {
    if (udp_fd_ == sfs::platform::kInvalidSocket || ip == nullptr) return false;

    sockaddr_storage peer{};
    sfs::platform::sfs_socklen_t peer_len = 0;
    if (!parse_numeric_host(ip, port, &peer, &peer_len)) return false;

    // 绑成双栈（AF_INET6）却要发给纯 IPv4 字面量时，必须转成 v4-mapped，
    // 否则 sendto 直接被拒 —— 现象是"对端什么都收不到"。
    if (socket_family_ == AF_INET6 && peer.ss_family == AF_INET) {
        sockaddr_in6 mapped{};
        to_v4_mapped(*reinterpret_cast<const sockaddr_in*>(&peer), &mapped);
        std::memcpy(&peer, &mapped, sizeof(mapped));
        peer_len = static_cast<sfs::platform::sfs_socklen_t>(sizeof(mapped));
    }

    return ensure_session(conv, peer, peer_len) != nullptr;
}

// ------------------------------------------------------------ KCP 发送回调

int KcpTransport::udp_output(const char* buf, int len, ikcpcb* kcp, void* user) {
    auto* self = static_cast<KcpTransport*>(user);
    if (self == nullptr || kcp == nullptr || self->udp_fd_ == sfs::platform::kInvalidSocket || len <= 0) return -1;

    auto it = self->sessions_.find(kcp->conv);
    if (it == self->sessions_.end()) return -1;

    const int n = static_cast<int>(::sendto(
        self->udp_fd_, buf, len, 0,
        reinterpret_cast<const sockaddr*>(&it->second.peer),
        it->second.peer_len));
    return n < 0 ? -1 : 0;
}

// ------------------------------------------------------------------ 会话管理

bool KcpTransport::consume_rebuilt(std::uint32_t conv) {
    auto it = sessions_.find(conv);
    if (it == sessions_.end() || !it->second.rebuilt) return false;
    it->second.rebuilt = false;
    return true;
}

KcpTransport::Session* KcpTransport::ensure_session(std::uint32_t conv,
                                                   const sockaddr_storage& peer,
                                                   sfs::platform::sfs_socklen_t peer_len) {
    auto it = sessions_.find(conv);
    if (it != sessions_.end()) {
        // 同一 conv 换地址，要分两种情况：
        //  - IP 变了：真的是另一台机器/新 socket → 重建 KCP 状态，并标记 rebuilt 让上层丢旧 Connection；
        //  - 只有源端口变了：**内网穿透（MEFrp）转发时源端口不稳定**，一个客户端会话会被看到多个端口。
        //    此时绝不能重建：重建会把在途的旧段喂进清零后的新状态 → 服务端解出垃圾
        //    （线上表现：`decode error: TCP frame kind is invalid` 刷屏 + 探针永远无回应 = 登录页 unreachable）。
        //    正确做法是只更新对端端口，KCP 状态原样保留。
        if (!endpoint_equal(it->second.peer, peer)) {
            if (!endpoint_same_ip(it->second.peer, peer)) {
                if (it->second.kcp) ikcp_release(it->second.kcp);
                it->second = Session{};
                it->second.peer = peer;
                it->second.peer_len = peer_len;
                // 标记"这一 conv 换了新对端"：上层要据此丢掉旧 Connection，
                // 否则新客户端会被当成"重复 Hello"而跳过整个初始状态（世界是空的）。
                it->second.rebuilt = true;
                it->second.kcp = ikcp_create(conv, this);
                if (it->second.kcp == nullptr) { sessions_.erase(it); return nullptr; }
                ikcp_setoutput(it->second.kcp, &KcpTransport::udp_output);
                ikcp_nodelay(it->second.kcp, kNodelay, kIntervalMs, kFastResend, kNoCongestion);
                ikcp_setmtu(it->second.kcp, kMtu);
                ikcp_wndsize(it->second.kcp, 256, 512);
            } else {
                it->second.peer = peer;
                it->second.peer_len = peer_len;
            }
        }
        return &it->second;
    }

    Session s;
    s.peer = peer;
    s.peer_len = peer_len;
    s.last_active_ms = now_ms();
    s.kcp = ikcp_create(conv, this);
    if (s.kcp == nullptr) return nullptr;
    ikcp_setoutput(s.kcp, &KcpTransport::udp_output);
    ikcp_nodelay(s.kcp, kNodelay, kIntervalMs, kFastResend, kNoCongestion);
    ikcp_setmtu(s.kcp, kMtu);
    ikcp_wndsize(s.kcp, 256, 512);

    auto res = sessions_.emplace(conv, std::move(s));
    return &res.first->second;
}

void KcpTransport::close_session(std::uint32_t conv) {
    auto it = sessions_.find(conv);
    if (it == sessions_.end()) return;
    if (it->second.kcp) ikcp_release(it->second.kcp);
    sessions_.erase(it);
}

bool KcpTransport::has_session(std::uint32_t conv) const {
    return sessions_.find(conv) != sessions_.end();
}

bool KcpTransport::diagnostics(std::uint32_t conv, Diagnostics* out) const {
    if (out == nullptr) return false;
    *out = Diagnostics{};
    const auto it = sessions_.find(conv);
    if (it == sessions_.end() || it->second.kcp == nullptr) return false;

    // ikcpcb 的内部计数是只读快照：这里不解释其语义，只如实取出来供日志观察。
    ikcpcb* k = it->second.kcp;
    out->nrt        = k->nsnd_que;
    out->cwnd       = k->cwnd;
    out->snd_wnd    = k->snd_wnd;
    out->nsnd_buf   = k->nsnd_buf;
    out->nrcv_buf   = k->nrcv_buf;
    out->rx_rto     = k->rx_rto;
    out->rx_srtt    = k->rx_srtt;
    out->ssthresh   = k->ssthresh;
    out->xmit       = k->xmit;       // 累计重传次数（ikcp.c:290 初始化，1129 行自增）
    out->fastresend = k->fastresend; // 我方设的阈值，不是 fastlimit
    out->valid      = true;
    return true;
}

bool KcpTransport::peer_endpoint(std::uint32_t conv, std::string* ip_out,
                                std::uint16_t* port_out) const {
    const auto it = sessions_.find(conv);
    if (it == sessions_.end()) return false;

    // INET6_ADDRSTRLEN 容得下 v6；v4-mapped 会在 endpoint_to_string 里被归一成纯 v4。
    char text[INET6_ADDRSTRLEN] = {0};
    if (!endpoint_to_string(it->second.peer, text, sizeof(text))) return false;
    if (ip_out != nullptr) *ip_out = text;
    if (port_out != nullptr) *port_out = endpoint_port(it->second.peer);
    return true;
}

std::vector<std::uint32_t> KcpTransport::session_convs() const {
    std::vector<std::uint32_t> out;
    out.reserve(sessions_.size());
    for (const auto& kv : sessions_) out.push_back(kv.first);
    return out;
}

void KcpTransport::trim_dead_sessions(std::uint32_t now) {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const bool dead = (now - it->second.last_active_ms) > kSessionTimeoutMs;
        if (dead) {
            if (it->second.kcp) ikcp_release(it->second.kcp);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- 事件循环

void KcpTransport::drain_udp() {
    char buf[4096];
    for (;;) {
        // 用 sockaddr_storage 收：同一个 socket 既可能是 IPv6 也可能是 IPv4-mapped。
        sockaddr_storage peer{};
        sfs::platform::sfs_socklen_t plen =
            static_cast<sfs::platform::sfs_socklen_t>(sizeof(peer));
        const int r = static_cast<int>(::recvfrom(
            udp_fd_, buf, static_cast<int>(sizeof(buf)), 0,
            reinterpret_cast<sockaddr*>(&peer), &plen));
        if (r <= 0) {
            if (sfs::platform::IsWouldBlock(sfs::platform::LastSocketError())) return;
            return;                               // 收完或出错
        }
        if (r < kKcpHeaderBytes) continue;        // 短包不可能是 KCP

        const std::uint32_t conv = read_u32_le(buf);
        Session* s = ensure_session(conv, peer, plen);
        if (s == nullptr) continue;

        s->last_active_ms = now_ms();
        ikcp_input(s->kcp, buf, static_cast<long>(r));
    }
}

void KcpTransport::drive_updates(std::uint32_t now) {
    if (static_cast<std::uint32_t>(now - last_update_ms_) <
        static_cast<std::uint32_t>(kIntervalMs)) {
        return;
    }
    last_update_ms_ = now;
    for (auto& kv : sessions_) {
        if (kv.second.kcp) ikcp_update(kv.second.kcp, now);
    }
}

void KcpTransport::pump_recv(Session& s) {
    for (;;) {
        const int size = ikcp_peeksize(s.kcp);
        if (size <= 0) return;

        std::string chunk;
        chunk.resize(static_cast<std::size_t>(size));
        const int r = ikcp_recv(s.kcp, chunk.empty() ? nullptr : &chunk[0], size);
        if (r <= 0) return;

        // 防御性上限：重组缓冲不应无界增长。
        if (s.in_buf.size() + static_cast<std::size_t>(r) >
            static_cast<std::size_t>(kMaxMessageBytes) + 4) {
            s.in_buf.clear();
        }
        s.in_buf.append(chunk.data(), static_cast<std::size_t>(r));
    }
}

int KcpTransport::poll(int timeout_ms) {
    if (udp_fd_ == sfs::platform::kInvalidSocket) return -1;

    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(udp_fd_, &readable);

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

#ifdef _WIN32
    // Windows 忽略 select 的第一个参数
    const int n = ::select(0, &readable, nullptr, nullptr, &tv);
#else
    const int n = ::select(static_cast<int>(udp_fd_) + 1, &readable, nullptr, nullptr, &tv);
#endif
    if (n < 0) {
        const int err = sfs::platform::LastSocketError();
        if (sfs::platform::IsWouldBlock(err)) return 0;
        // 被信号打断不算错误
#ifdef _WIN32
        if (err == WSAEINTR) return 0;
#else
        if (err == EINTR) return 0;
#endif
        return -1;
    }

    int handled = 0;
    if (n > 0 && FD_ISSET(udp_fd_, &readable)) {
        drain_udp();
        handled = 1;
    }

    const std::uint32_t now = now_ms();
    drive_updates(now);
    for (auto& kv : sessions_) {
        if (kv.second.kcp) pump_recv(kv.second);
    }
    trim_dead_sessions(now);
    return handled;
}

// ------------------------------------------------------------------ 收发接口

bool KcpTransport::send(std::uint32_t conv, const void* data, int len) {
    auto it = sessions_.find(conv);
    if (it == sessions_.end() || it->second.kcp == nullptr) return false;
    if (len < 0 || len > kMaxMessageBytes) return false;

    // 线格式（单层长度前缀，与客户端 TcpTransportCore.ReceiveKcpMessage 对齐）：
    //   [4-byte 消息总长 = len][data]
    // data 本身是 EncodeTcpFrame 的输出，已经含自己的 4 字节 body 长度：
    //   [bodyLength][kind][seq][bits][payload]
    // 所以完整线上字节是 [len][bodyLength][kind][seq][bits][payload]，其中 len = bodyLength + 4。
    // 客户端正是读偏移 0 作为外层、偏移 4 作为内层并校验 inner + 4 == outer。
    // 千万不要在这里再加一层长度，否则整体错位 4 字节（曾导致握手全部失败）。
    std::string msg;
    msg.resize(static_cast<std::size_t>(4 + len));
    write_u32_le(&msg[0], static_cast<std::uint32_t>(len));
    if (len > 0 && data != nullptr) {
        std::memcpy(&msg[4], data, static_cast<std::size_t>(len));
    }

    const int r = ikcp_send(it->second.kcp, msg.data(), static_cast<int>(msg.size()));
    if (r < 0) return false;

    it->second.last_active_ms = now_ms();
    ikcp_flush(it->second.kcp);
    return true;
}

int KcpTransport::receive(std::uint32_t conv, std::string* out) {
    auto it = sessions_.find(conv);
    if (it == sessions_.end()) return -1;

    Session& s = it->second;
    if (s.in_buf.size() < 4) return 0;

    const std::uint32_t len = read_u32_le(s.in_buf.data());
    if (len > static_cast<std::uint32_t>(kMaxMessageBytes)) {
        s.in_buf.clear();     // 长度不合理，丢弃重组缓冲
        return 0;
    }
    if (s.in_buf.size() < static_cast<std::size_t>(4) + len) return 0;  // 尚未收全

    if (out != nullptr) out->assign(s.in_buf, 4, len);
    s.in_buf.erase(0, static_cast<std::size_t>(4) + len);
    return static_cast<int>(len);
}

}  // namespace sfs::net
