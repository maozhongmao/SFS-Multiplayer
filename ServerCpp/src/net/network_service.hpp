// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <atomic>
#include <condition_variable>

namespace sfs::net {

// ------------------------------------------------------------
// 网络层错误
// ------------------------------------------------------------
class NetError : public std::runtime_error {
public:
    explicit NetError(std::string msg) : std::runtime_error(std::move(msg)) {}
};

// ------------------------------------------------------------
// TCP 连接抽象（单连接）
// ------------------------------------------------------------
class TcpConnection {
public:
    virtual ~TcpConnection() = default;
    virtual void send_frame(const std::vector<std::uint8_t>& frame) = 0;
    virtual void close() = 0;
    virtual std::string remote_endpoint() const = 0;
    virtual std::uint64_t connection_id() const = 0;
    virtual bool is_closed() const = 0;
};

// ------------------------------------------------------------
// UDP 端点抽象
// ------------------------------------------------------------
class UdpEndpoint {
public:
    virtual ~UdpEndpoint() = default;
    virtual void send_to(const std::vector<std::uint8_t>& data, const std::string& addr, std::uint16_t port) = 0;
    virtual void bind(std::uint16_t port) = 0;
    using RecvCallback = std::function<void(const std::vector<std::uint8_t>& data, const std::string& from_addr, std::uint16_t from_port)>;
    virtual void set_recv_callback(RecvCallback cb) = 0;
    virtual int fd() const = 0;  // 供 epoll 使用
    virtual bool is_bound() const = 0;
};

// ------------------------------------------------------------
// 网络服务工厂
// ------------------------------------------------------------
class INetworkService {
public:
    virtual ~INetworkService() = default;

    class TcpListener {
    public:
        virtual ~TcpListener() = default;
        using AcceptCallback = std::function<void(std::unique_ptr<TcpConnection>)>;
        virtual void start(std::uint16_t port, AcceptCallback on_accept) = 0;
        virtual void stop() = 0;
    };
    virtual std::unique_ptr<TcpListener> create_tcp_listener() = 0;
    virtual std::unique_ptr<UdpEndpoint> create_udp_endpoint() = 0;
    virtual void run() = 0;
    virtual void stop() = 0;

    // 回调：TCP 数据到达 / 连接关闭
    using TcpDataCallback = std::function<void(std::uint64_t conn_id, std::vector<std::uint8_t>&& data)>;
    using TcpCloseCallback = std::function<void(std::uint64_t conn_id)>;
    virtual void set_tcp_data_callback(TcpDataCallback cb) = 0;
    virtual void set_tcp_close_callback(TcpCloseCallback cb) = 0;
};

// ------------------------------------------------------------
// Linux 实现（内联在头文件，方便 server.cpp 直接使用）
// ------------------------------------------------------------

// 前置声明
class TcpConnectionImpl;
class UdpEndpointImpl;
class TcpListenerImpl;
class NetworkServiceImpl;

// 全局工厂
INetworkService& get_network_service();

} // namespace sfs::net

// ------------------------------------------------------------
// Linux 实现细节（仅在 Linux 编译时包含）
// ------------------------------------------------------------
#if defined(__linux__) || defined(__unix__)
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
#include <unordered_map>

namespace sfs::net {

// 工具函数
static int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
static void set_tcp_nodelay(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}
static std::string sockaddr_to_string(const sockaddr_in& addr) {
    char buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &addr.sin_addr, buf, sizeof(buf));
    return std::string(buf) + ":" + std::to_string(ntohs(addr.sin_port));
}

// ------------------------------------------------------------
// TCP 连接实现
// ------------------------------------------------------------
class TcpConnectionImpl : public TcpConnection {
public:
    TcpConnectionImpl(int fd, std::uint64_t id, sockaddr_in addr)
        : fd_(fd), id_(id), addr_(addr), closed_(false),
          epollout_enabled_(false), has_pending_write_(false) {
        set_nonblock(fd_);
        set_tcp_nodelay(fd_);
    }
    ~TcpConnectionImpl() override {
        if (fd_ >= 0) ::close(fd_);
    }

    void send_frame(const std::vector<std::uint8_t>& frame) override {
        if (closed_) return;
        std::lock_guard<std::mutex> lk(write_mu_);
        write_queue_.push(frame);
        has_pending_write_ = true;
    }
    void close() override {
        closed_ = true;
        if (fd_ >= 0) {
            ::shutdown(fd_, SHUT_RDWR);
            ::close(fd_);
            fd_ = -1;
        }
    }
    std::string remote_endpoint() const override { return sockaddr_to_string(addr_); }
    std::uint64_t connection_id() const override { return id_; }

    int fd() const { return fd_; }
    bool is_closed() const { return closed_; }
    bool has_pending_write() const { return has_pending_write_; }
    void set_epollout_enabled(bool v) { epollout_enabled_ = v; }
    bool epollout_enabled() const { return epollout_enabled_; }

    bool flush_write_queue() {
        if (closed_) return false;
        std::lock_guard<std::mutex> lk(write_mu_);
        while (!write_queue_.empty()) {
            const auto& frame = write_queue_.front();
            ssize_t sent = ::send(fd_, frame.data(), frame.size(), MSG_NOSIGNAL);
            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
                closed_ = true;
                has_pending_write_ = false;
                return false;
            }
            if (static_cast<std::size_t>(sent) < frame.size()) {
                write_queue_.front().erase(write_queue_.front().begin(),
                                           write_queue_.front().begin() + sent);
                return true;
            }
            write_queue_.pop();
        }
        has_pending_write_ = false;
        return false;
    }

    ssize_t read_some(std::vector<std::uint8_t>& out) {
        if (closed_) return -1;
        std::uint8_t buf[4096];
        ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
        if (n > 0) out.insert(out.end(), buf, buf + n);
        else if (n == 0) closed_ = true;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) closed_ = true;
        return n;
    }

private:
    int fd_;
    std::uint64_t id_;
    sockaddr_in addr_;
    std::atomic<bool> closed_;
    std::mutex write_mu_;
    std::queue<std::vector<std::uint8_t>> write_queue_;
    bool has_pending_write_ = false;
    bool epollout_enabled_ = false;
};

// ------------------------------------------------------------
// UDP 端点实现
// ------------------------------------------------------------
class UdpEndpointImpl : public UdpEndpoint {
public:
    UdpEndpointImpl() : fd_(-1), bound_(false) {}
    ~UdpEndpointImpl() override { if (fd_ >= 0) ::close(fd_); }

    void send_to(const std::vector<std::uint8_t>& data, const std::string& addr, std::uint16_t port) override {
        if (fd_ < 0) return;
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        inet_pton(AF_INET, addr.c_str(), &dest.sin_addr);
        ::sendto(fd_, data.data(), data.size(), MSG_NOSIGNAL,
                 reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    }
    void bind(std::uint16_t port) override {
        if (fd_ >= 0) ::close(fd_);
        fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (fd_ < 0) throw NetError("socket() failed: " + std::string(strerror(errno)));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(fd_);
            fd_ = -1;
            throw NetError("bind() failed: " + std::string(strerror(errno)));
        }
        bound_ = true;
    }
    void set_recv_callback(RecvCallback cb) override { recv_cb_ = std::move(cb); }

    int fd() const { return fd_; }
    bool is_bound() const { return bound_; }

    void handle_recv() {
        if (fd_ < 0 || !recv_cb_) return;
        std::uint8_t buf[65536];
        sockaddr_in src{};
        socklen_t srclen = sizeof(src);
        ssize_t n = ::recvfrom(fd_, buf, sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&src), &srclen);
        if (n > 0) {
            std::vector<std::uint8_t> data(buf, buf + n);
            recv_cb_(std::move(data), inet_ntoa(src.sin_addr), ntohs(src.sin_port));
        }
    }

private:
    int fd_;
    bool bound_;
    RecvCallback recv_cb_;
};

// ------------------------------------------------------------
// TCP 监听器实现
// ------------------------------------------------------------
class TcpListenerImpl : public INetworkService::TcpListener {
public:
    explicit TcpListenerImpl(int epoll_fd) : epoll_fd_(epoll_fd), listen_fd_(-1) {}
    ~TcpListenerImpl() override { stop(); }

    void start(std::uint16_t port, AcceptCallback on_accept) override {
        on_accept_ = std::move(on_accept);
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (listen_fd_ < 0) throw NetError("socket() failed: " + std::string(strerror(errno)));
        int yes = 1;
        setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw NetError("bind() failed: " + std::string(strerror(errno)));
        }
        if (::listen(listen_fd_, SOMAXCONN) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw NetError("listen() failed: " + std::string(strerror(errno)));
        }
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = listen_fd_;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev) < 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
            throw NetError("epoll_ctl ADD failed: " + std::string(strerror(errno)));
        }
    }
    void stop() override {
        if (listen_fd_ >= 0) {
            epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listen_fd_, nullptr);
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }
    int fd() const { return listen_fd_; }
    void handle_accept() {
        if (!on_accept_) return;
        while (true) {
            sockaddr_in addr{};
            socklen_t len = sizeof(addr);
            int cfd = ::accept4(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len, SOCK_NONBLOCK);
            if (cfd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                return;
            }
            static std::atomic<std::uint64_t> next_id{1};
            auto conn = std::make_unique<TcpConnectionImpl>(cfd, next_id++, addr);
            on_accept_(std::move(conn));
        }
    }
private:
    int epoll_fd_;
    int listen_fd_;
    AcceptCallback on_accept_;
};

// ------------------------------------------------------------
// 网络服务实现
// ------------------------------------------------------------
class NetworkServiceImpl : public INetworkService {
public:
    NetworkServiceImpl() : epoll_fd_(-1), running_(false) {
        epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
        if (epoll_fd_ < 0) throw NetError("epoll_create1 failed: " + std::string(strerror(errno)));
    }
    ~NetworkServiceImpl() override {
        stop();
        if (epoll_fd_ >= 0) ::close(epoll_fd_);
    }

    std::unique_ptr<TcpListener> create_tcp_listener() override {
        return std::make_unique<TcpListenerImpl>(epoll_fd_);
    }
    std::unique_ptr<UdpEndpoint> create_udp_endpoint() override {
        return std::make_unique<UdpEndpointImpl>();
    }

    void run() override {
        running_ = true;
        epoll_event events[64];
        while (running_) {
            int n = epoll_wait(epoll_fd_, events, 64, 10);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                std::uint32_t ev = events[i].events;

                // 监听套接字
                for (auto& l : listeners_) {
                    if (l->fd() == fd) {
                        l->handle_accept();
                        break;
                    }
                }

                // TCP 连接
                for (auto it = connections_.begin(); it != connections_.end();) {
                    auto& conn = *it;
                    if (conn->fd() == fd) {
                        if (ev & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                            conn->close();
                            if (on_tcp_close_) on_tcp_close_(conn->connection_id());
                            it = connections_.erase(it);
                            continue;
                        }
                        if (ev & EPOLLIN) {
                            std::vector<std::uint8_t> buf;
                            conn->read_some(buf);
                            if (!buf.empty() && on_tcp_data_) {
                                on_tcp_data_(conn->connection_id(), std::move(buf));
                            }
                            if (conn->is_closed()) {
                                if (on_tcp_close_) on_tcp_close_(conn->connection_id());
                                it = connections_.erase(it);
                                continue;
                            }
                        }
                        if (ev & EPOLLOUT) {
                            conn->flush_write_queue();
                            if (!conn->has_pending_write()) {
                                epoll_event ev_mod{};
                                ev_mod.events = EPOLLIN | EPOLLET;
                                ev_mod.data.fd = conn->fd();
                                epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd(), &ev_mod);
                                conn->set_epollout_enabled(false);
                            }
                        }
                        ++it;
                        break;
                    } else {
                        ++it;
                    }
                }

                // UDP 端点
                for (auto& u : udp_endpoints_) {
                    if (u->fd() == fd && (ev & EPOLLIN)) {
                        u->handle_recv();
                        break;
                    }
                }
            }
        }
    }
    void stop() override { running_ = false; }

    void register_connection(std::unique_ptr<TcpConnectionImpl> conn) {
        connections_.push_back(std::move(conn));
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = connections_.back()->fd();
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ev.data.fd, &ev);
    }
    void ensure_write_events(std::uint64_t conn_id) {
        for (auto& conn : connections_) {
            if (conn->connection_id() == conn_id) {
                if (conn->has_pending_write() && !conn->epollout_enabled()) {
                    epoll_event ev{};
                    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
                    ev.data.fd = conn->fd();
                    epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, conn->fd(), &ev);
                    conn->set_epollout_enabled(true);
                }
                break;
            }
        }
    }
    void register_udp(std::unique_ptr<UdpEndpointImpl> ep) {
        udp_endpoints_.push_back(std::move(ep));
        epoll_event ev{};
        ev.events = EPOLLIN;
        ev.data.fd = udp_endpoints_.back()->fd();
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, ev.data.fd, &ev);
    }

    using TcpDataCallback = std::function<void(std::uint64_t conn_id, std::vector<std::uint8_t>&& data)>;
    using TcpCloseCallback = std::function<void(std::uint64_t conn_id)>;
    void set_tcp_data_callback(TcpDataCallback cb) { on_tcp_data_ = std::move(cb); }
    void set_tcp_close_callback(TcpCloseCallback cb) { on_tcp_close_ = std::move(cb); }

private:
    int epoll_fd_;
    std::atomic<bool> running_;
    std::vector<std::unique_ptr<TcpListenerImpl>> listeners_;
    std::vector<std::unique_ptr<TcpConnectionImpl>> connections_;
    std::vector<std::unique_ptr<UdpEndpointImpl>> udp_endpoints_;
    TcpDataCallback on_tcp_data_;
    TcpCloseCallback on_tcp_close_;
};

// 全局单例
static NetworkServiceImpl* g_service = nullptr;

INetworkService& get_network_service() {
    if (!g_service) g_service = new NetworkServiceImpl();
    return *g_service;
}

} // namespace sfs::net
#endif // __linux__ || __unix__
