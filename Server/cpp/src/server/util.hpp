// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace sfs::server {

// 进程运行标志：信号处理把它置 false，主循环与 Server 都据此判断是否退出。
extern std::atomic<bool> g_running;

void on_signal(int);

std::int32_t now_millis();

// .NET DateTime.UtcNow.Ticks 等价：自 0001-01-01 起的 100 纳秒数。
// 服务端心跳 Ping 用它做时间戳，客户端原样回显，服务端据此算 RTT。
// （客户端 TcpTransportCore 的 UpdateRoundTrip 也按同样的 ticks 语义解读）
constexpr std::int64_t kDotNetEpochOffsetSeconds = 62135596800LL;  // 1970-01-01 之前的秒数

std::int64_t now_dotnet_ticks();

// 单调时钟：世界时间推进只依赖它，不受系统时间调整影响
double steady_seconds_since(std::chrono::steady_clock::time_point origin);

// 读取 /proc/self/status 里的 VmRSS / VmSize / Threads（单位 kB）
struct SelfStats {
    long vm_rss_kb = -1;
    long vm_size_kb = -1;
    long threads = -1;
    long utime = -1;
    long stime = -1;
};

SelfStats read_self_stats();

// 进日志前的清洗：控制字符（含 \n \r \t）换成 '?'，超长截断。
// 用户名是客户端完全可控的字符串，直接打进日志会被拿来伪造日志行。
std::string sanitize_for_log(const std::string& value);

}  // namespace sfs::server
