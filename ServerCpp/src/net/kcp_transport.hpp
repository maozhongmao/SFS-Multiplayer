// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "platform/socket_platform.hpp"
#include "third_party/kcp/ikcp.h"

namespace sfs::net {

// 单端口 KCP 传输层。
//
// 一个 UDP socket 承载任意多个 KCP 会话，按 KCP 包头中的 conv 区分。
// 应用层消息在 KCP 字节流之上以 4 字节小端长度前缀切分，所以上层可以继续
// 按「一帧一条消息」的模型工作，不必关心 KCP 的 MTU 分片。
//
// 设计依据见 ServerCpp/docs/ADR-001-kcp-transport.md。
class KcpTransport {
public:
    // KCP 参数。默认配置是慢的（interval 100ms），这里按 ADR-001 的结论显式设快。
    static constexpr int kNodelay      = 1;   // 不延迟发送
    static constexpr int kIntervalMs   = 10;  // 内部工作间隔（ms）
    // 快速重传阈值。V1.2.4.1 起为 0：只走超时重传。
    // 为什么改：resend=2 表示"丢 2 个包就抢着重传"。在抖动链路（内网穿透 / 移动网络）上
    // 丢包多半是延迟抖动而不是真丢，抢先重传会把本来就拥塞的链路进一步挤占，
    // 延迟越重越抢着重传，形成正反馈。本协议不依赖重传保证送达 ——
    // 丢掉的最新状态包由下一个状态包补上，位置偏差过大的火箭走 200 米硬同步，
    // 插值陈旧的火箭走单火箭快照补发。所以这里宁可丢，不要抢着重传。
    static constexpr int kFastResend   = 0;   // 0 = 不抢着重传，只等超时重传
    static constexpr int kNoCongestion = 1;   // 关闭拥塞控制
    static constexpr int kMtu          = 1400;

    // 单条应用层消息上限，与现有帧上限保持一致。
    static constexpr int kMaxMessageBytes = 8 * 1024 * 1024;

    // 会话空闲多久后回收（毫秒）。
    static constexpr std::uint32_t kSessionTimeoutMs = 30000;

    struct Session {
        ikcpcb* kcp = nullptr;
        // 用 sockaddr_storage + 实际长度：同一个 socket 要同时容纳 IPv6 与 IPv4
        //（双栈下 IPv4 对端表现为 ::ffff:a.b.c.d）。
        sockaddr_storage peer{};
        sfs::platform::sfs_socklen_t peer_len = 0;
        std::uint32_t last_active_ms = 0;
        std::string in_buf;            // 应用层重组缓冲（含 4 字节长度前缀）
        // 同一 conv 被新源端口重建过（对端开了新 socket）。上层必须据此丢掉旧 Connection，
        // 否则新客户端会被当成"重复 Hello"，只拿到 ack 而跳过整个初始状态。
        bool rebuilt = false;
    };

    KcpTransport() = default;
    ~KcpTransport();

    KcpTransport(const KcpTransport&) = delete;
    KcpTransport& operator=(const KcpTransport&) = delete;

    // 绑定 UDP 端口。
    // bind_addr 传 nullptr / "" / "::" = **双栈**：绑 :: 并关闭 IPV6_V6ONLY，
    // 同一个 socket 同时收 IPv6 与 IPv4-mapped 的 IPv4；传具体字面量则只绑那一个地址。
    // 默认值以前是 "0.0.0.0"（纯 IPv4）—— 那正是"模组连不上 IPv6"的根因。
    bool init(std::uint16_t port, const char* bind_addr = nullptr);
    void shutdown();

    // 显式创建一个到指定对端的会话（用于主动发起，例如测试或反向通知）。
    bool connect(std::uint32_t conv, const char* ip, std::uint16_t port);

    // 事件循环一次迭代：收取 UDP 数据喂给对应会话，并按 kIntervalMs 驱动
    // ikcp_update。返回本轮处理的 UDP 包数；-1 表示致命错误。
    int poll(int timeout_ms);

    // 发送一条完整应用层消息（内部自动加长度前缀并按需分片）。
    bool send(std::uint32_t conv, const void* data, int len);

    // 取出一条完整应用层消息写入 out。
    // 返回消息长度；0 表示当前没有完整消息；-1 表示会话不存在。
    int receive(std::uint32_t conv, std::string* out);

    // 主动关闭一个会话并释放其 KCP 状态。
    void close_session(std::uint32_t conv);

    // 该会话是否还活着（未被超时回收）。
    bool has_session(std::uint32_t conv) const;

    // 该 conv 对端的 UDP 端点（IP 文本 + 主机序端口）。IPv4-mapped 的 IPv6 按纯 IPv4 输出。
    // 这是服务端\"观察到\"的客户端源地址，也就是客户端那个 UDP 套接字在服务端眼里的位置。
    // P2P 信令要把它告诉另一个客户端（对应 C# TCP 版的 TcpSession.UdpEndpoint ——
    // C# 也是从客户端发来的 UDP 数据报里记下来的观察端点）。
    // 会话不存在 / 地址不可解析时返回 false。
    bool peer_endpoint(std::uint32_t conv, std::string* ip_out, std::uint16_t* port_out) const;

    // 该 conv 的会话是否刚被新源端口重建过；返回 true 并清除标记（一次性事件）。
    // 上层用它决定"丢掉旧连接、让新连接重新握手"。
    bool consume_rebuilt(std::uint32_t conv);

    // 诊断快照：把 KCP 内部的关键计数/窗口取出来，供 periodic 日志打印。
    // 目的：验证"快速重传阈值改 0"到底有没有降低无谓重传。
    // 字段含义（都取自 ikcp.c，名字必须对得上，否则打出来的是别的含义）：
    //   fastresend  = 我方设的"快速重传阈值"。0 = 不抢着重传，只等超时重传。
    //   xmit        = KCP 累计重传次数（ikcp.c 每次重传自增）—— 验证改动的核心数字。
    //   nrt         = 发送队列里被判定需要重传的段数（涨 = 有丢包或超时）
    //   cwnd        = 拥塞窗口（当前允许多少在途字节）
    //   snd_wnd     = 对端通告的接收窗口
    //   nsnd_buf    = 还在发送缓冲里、尚未被确认的段数（排队越久延迟越高）
    //   nrcv_buf    = 接收重组缓冲里待交付的段数
    //   rx_rto      = 当前重传超时（毫秒，KCP 自适应算出来的）
    //   rx_srtt     = 平滑后的往返时延（毫秒）
    //   ssthresh    = 慢启动阈值
    // 注意：不要打 kcp->fastlimit。那个是 KCP 自己的常量 IKCP_FASTACK_LIMIT(5)，
    // 与我方预设的快速重传阈值无关，打出来会误导判断（V1.2.5 曾误打过一次）。
    struct Diagnostics {
        std::uint32_t nrt = 0;
        std::uint32_t cwnd = 0;
        std::uint32_t snd_wnd = 0;
        std::uint32_t nsnd_buf = 0;
        std::uint32_t nrcv_buf = 0;
        std::int32_t  rx_rto = 0;
        std::int32_t  rx_srtt = 0;
        std::uint32_t ssthresh = 0;
        std::uint32_t xmit = 0;
        int           fastresend = -1;
        bool          valid = false;
    };
    bool diagnostics(std::uint32_t conv, Diagnostics* out) const;

    std::size_t session_count() const { return sessions_.size(); }
    std::uint16_t port() const { return port_; }

    // 当前所有会话的 conv。KCP 没有 accept，上层靠对比这个列表发现新连接。
    std::vector<std::uint32_t> session_convs() const;

private:
    static std::uint32_t now_ms();
    static int udp_output(const char* buf, int len, ikcpcb* kcp, void* user);

    Session* ensure_session(std::uint32_t conv, const sockaddr_storage& peer,
                            sfs::platform::sfs_socklen_t peer_len);
    void pump_recv(Session& s);            // ikcp_recv → in_buf
    void drain_udp();                      // 收空 UDP 接收缓冲
    void trim_dead_sessions(std::uint32_t now);
    void drive_updates(std::uint32_t now); // 定时 ikcp_update

    sfs::platform::sfs_socket_t udp_fd_ = sfs::platform::kInvalidSocket;
    // 绑定时的地址族。绑成 AF_INET6（双栈）时，往纯 IPv4 目标发送必须先转成 v4-mapped，
    // 否则 sendto 会被内核以 EAFNOSUPPORT 拒掉 —— 表现为"IPv6 通了、IPv4 一个包都发不出去"。
    int socket_family_ = AF_INET;
    std::uint16_t port_ = 0;
    std::uint32_t last_update_ms_ = 0;
    std::unordered_map<std::uint32_t, Session> sessions_;
};

}  // namespace sfs::net
