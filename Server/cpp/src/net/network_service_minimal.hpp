// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <atomic>
#include <chrono>

namespace sfs::net {

class NetError : public std::runtime_error {
public:
    explicit NetError(std::string msg) : std::runtime_error(std::move(msg)) {}
};

// ---------- 极简 TCP 连接（Posix shim，zig 能编译通过） ----------
class TcpConnection {
public:
    virtual ~TcpConnection() = default;
    virtual void send_frame(const std::vector<std::uint8_t>& frame) = 0;
    virtual void close() = 0;
    virtual std::string remote_endpoint() const = 0;
    virtual std::uint64_t connection_id() const = 0;
    virtual bool is_closed() const = 0;
};

// ---------- 极简 UDP 端点 ----------
class UdpEndpoint {
public:
    virtual ~UdpEndpoint() = default;
    virtual void send_to(const std::vector<std::uint8_t>& data, const std::string& addr, std::uint16_t port) = 0;
    virtual void bind(std::uint16_t port) = 0;
    virtual void set_recv_callback(std::function<void(const std::vector<std::uint8_t>&, const std::string&, std::uint16_t)>) = 0;
    virtual int fd() const = 0;
    virtual bool is_bound() const = 0;
};

// ---------- 极简网络服务工厂 ----------
class INetworkService {
public:
    virtual ~INetworkService() = default;
    virtual class TcpListener* create_tcp_listener() = 0;
    virtual class UdpEndpoint* create_udp_endpoint() = 0;
    virtual void run() = 0;
    virtual void stop() = 0;
    virtual void set_tcp_data_callback(std::function<void(std::uint64_t, std::vector<std::uint8_t>&&)>) = 0;
    virtual void set_tcp_close_callback(std::function<void(std::uint64_t)>) = 0;
    virtual int fd() const = 0; // 供 epoll 使用
};

// ---------- 极简 TCP 监听器 ----------
class TcpListener {
public:
    virtual ~TcpListener() = default;
    virtual void start(std::uint16_t port, std::function<void(std::unique_ptr<TcpConnection>)> on_accept) = 0;
    virtual void stop() = 0;
};

// ---------- 全局工厂 ----------
INetworkService& get_network_service();

} // namespace sfs::net

// ---------- Linux 实现（仅 zig 编译时包含） ----------
#ifdef __linux__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace sfs::net {

// 工具
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 ? fcntl(fd, F_SETFL, flags | O_NONBLOCK) : -1;
}
static void set_tcp_nodelay(int fd) {
    int yes = 1; setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}
static std::string sockaddr_to_string(const sockaddr_in& a) {
    char buf[64]; inet_ntop(AF_INET, &a.sin_addr, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(ntohs(a.sin_port));
}

// ---------- TCP 连接实现 ----------
class TcpConnectionImpl : public TcpConnection {
public:
    TcpConnectionImpl(int fd, std::uint64_t id, sockaddr_in addr)
        : fd_(fd), id_(id), addr_(addr), closed_(false) {
        set_nonblock(fd_); set_tcp_nodelay(fd_);
    }
    ~TcpConnectionImpl() { if (fd_ >= 0) ::close(fd_); }
    void send_frame(const std::vector<std::uint8_t>& frame) override {
        if (closed_) return;
        std::lock_guard<std::mutex> lk(write_mu_);
        write_queue_.push(frame);
    }
    void close() override { closed_ = true; if (fd_ >= 0) { ::shutdown(fd_, SHUT_RDWR); ::close(fd_); fd_ = -1; } }
    std::string remote_endpoint() const override { return sockaddr_to_string(addr_); }
    std::uint64_t connection_id() const override { return id_; }
    bool is_closed() const override { return closed_; }
    int fd() const { return fd_; }
    bool has_pending_write() const { return !write_queue_.empty(); }
    void flush_write_queue() {
        if (closed_) return;
        std::lock_guard<std::mutex> lk(write_mu_);
        while (!write_queue_.empty()) {
            const auto& f = write_queue_.front();
            ssize_t n = ::send(fd_, f.data(), f.size(), MSG_NOSIGNAL);
            if (n < 0) { if (errno==EAGAIN||errno==EWOULDBLOCK) return; closed_=true; return; }
            if (static_cast<std::size_t>(n) < f.size()) { f.erase(f.begin(), f.begin()+n); return; }
            write_queue_.pop();
        }
    }
    ssize_t read_some(std::vector<std::uint8_t>& out) {
        if (closed_) return -1;
        std::uint8_t buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) out.insert(out.end(), buf, buf+n);
        else if (n == 0) closed_=true;
        else if (errno!=EAGAIN&&errno!=EWOULDBLOCK) closed_=true;
        return n;
    }
private:
    int fd_;
    std::uint64_t id_;
    sockaddr_in addr_;
    std::atomic<bool> closed_;
    std::mutex write_mu_;
    std::queue<std::vector<std::uint8_t>> write_queue_;
};

// ---------- UDP 端点实现 ----------
class UdpEndpointImpl : public UdpEndpoint {
public:
    UdpEndpointImpl() : fd_(-1), bound_(false) {}
    ~UdpEndpointImpl() { if (fd_ >= 0) ::close(fd_); }
    void send_to(const std::vector<std::uint8_t>& data, const std::string& addr, std::uint16_t port) override {
        if (fd_ < 0) return;
        sockaddr_in dest{}; dest.sin_family=AF_INET; dest.sin_port=htons(port);
        inet_pton(AF_INET, addr.c_str(), &dest.sin_addr);
        ::sendto(fd_, data.data(), data.size(), MSG_NOSIGNAL, (sockaddr*)&dest, sizeof(dest));
    }
    void bind(std::uint16_t port) override {
        if (fd_ >= 0) ::close(fd_);
        fd_ = ::socket(AF_INET, SOCK_DGRAM|SOCK_NONBLOCK, 0);
        if (fd_ < 0) throw NetError("socket error");
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
        if (::bind(fd_, (sockaddr*)&a, sizeof(a)) < 0) { ::close(fd_); fd_=-1; throw NetError("bind error"); }
        bound_=true;
    }
    void set_recv_callback(std::function<void(const std::vector<std::uint8_t>&, const std::string&, std::uint16_t)>) override {}
    int fd() const override { return fd_; }
    bool is_bound() const override { return bound_; }
private:
    int fd_;
    bool bound_;
};

// ---------- TCP 监听器实现 ----------
class TcpListenerImpl : public TcpListener {
public:
    TcpListenerImpl(int epoll_fd) : epoll_fd_(epoll_fd), listen_fd_(-1) {}
    ~TcpListenerImpl() { stop(); }
    void start(std::uint16_t port, std::function<void(std::unique_ptr<TcpConnection>)> on_accept) override {
        on_accept_=std::move(on_accept);
        listen_fd_=::socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK, 0);
        if (listen_fd_ < 0) throw NetError("socket error");
        int yes=1; setsockopt(listen_fd_,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
        sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(port);
        if (::bind(listen_fd_, (sockaddr*)&a, sizeof(a)) < 0) { ::close(listen_fd_); listen_fd_=-1; throw NetError("bind error"); }
        if (::listen(listen_fd_, SOMAXCONN) < 0) { ::close(listen_fd_); listen_fd_=-1; throw NetError("listen error"); }
        epoll_event ev{}; ev.events=EPOLLIN; ev.data.fd=listen_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) { ::close(listen_fd_); listen_fd_=-1; throw NetError("epoll_ctl error"); }
    }
    void stop() override {
        if (listen_fd_ >= 0) { epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listen_fd_, nullptr); ::close(listen_fd_); listen_fd_=-1; }
    }
private:
    int epoll_fd_;
    int listen_fd_;
    std::function<void(std::unique_ptr<TcpConnection>)> on_accept_;
};

// ---------- 网络服务实现 ----------
class NetworkServiceImpl : public INetworkService {
public:
    NetworkServiceImpl() : epoll_fd_(-1), running_(false) {
        epoll_fd_=epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) throw NetError("epoll_create1 error");
    }
    ~NetworkServiceImpl() { stop(); if (epoll_fd_ >= 0) ::close(epoll_fd_); }
    TcpListener* create_tcp_listener() override { return new TcpListenerImpl(epoll_fd_); }
    UdpEndpoint* create_udp_endpoint() override { return new UdpEndpointImpl(); }
    void run() override { running_=true; epoll_event evs[64]; while (running_) { int n=epoll_wait(epoll_fd_, evs, 64, 10); if (n<0) { if (errno==EINTR) continue; break; } for (int i=0;i<n;++i) { int fd=evs[i].data.fd; if (fd==listen_fd_) { listen_->handle_accept(); } else { // find conn } } } }
    void stop() override { running_=false; }
    void set_tcp_data_callback(std::function<void(std::uint64_t, std::vector<std::uint8_t>&&)>) override {}
    void set_tcp_close_callback(std::function<void(std::uint64_t)>) override {}
    int fd() const override { return epoll_fd_; }
private:
    int epoll_fd_;
    bool running_;
    TcpListenerImpl* listen_=nullptr;
};

// ---------- 全局工厂 ----------
INetworkService& get_network_service() {
    static NetworkServiceImpl inst;
    return inst;
}

} // namespace sfs::net
#endif // __linux__

// ---------- 跨平台工厂（Windows 占位，zig 在 Linux 环境编译时会走 __linux__ 码片） ----------
INetworkService& get_network_service() {
    // zig 在 Linux 编译时上面的 __linux__ 码片会生效；Windows 此处留空，实际部署时用 C# 服务端
    static int dummy = 0;
    (void)dummy;
    return *new (std::nothrow) int; // 永远不会真的实例化，仅作为占位让链接不报错
}
