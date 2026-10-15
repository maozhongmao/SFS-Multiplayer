// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/util.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

namespace sfs::server {

std::atomic<bool> g_running{true};

void on_signal(int) { g_running = false; }

std::int32_t now_millis() {
    using namespace std::chrono;
    return static_cast<std::int32_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

std::int64_t now_dotnet_ticks() {
    using namespace std::chrono;
    const auto micros = duration_cast<microseconds>(
        system_clock::now().time_since_epoch()).count();
    return (micros + kDotNetEpochOffsetSeconds * 1000000LL) * 10LL;
}

// 单调时钟：世界时间推进只依赖它，不受系统时间调整影响
double steady_seconds_since(std::chrono::steady_clock::time_point origin) {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now() - origin).count();
}

SelfStats read_self_stats() {
SelfStats s;
#ifndef _WIN32
// Linux：从 /proc 采集。Windows 端暂不采集，保持默认值。
std::ifstream in("/proc/self/status");
std::string line;
while (std::getline(in, line)) {
    auto grab = [&line](const char* key) -> long {
        const std::size_t p = line.find(key);
        if (p == std::string::npos) return -1;
        return std::strtol(line.c_str() + p + std::strlen(key), nullptr, 10);
    };
    if (line.rfind("VmRSS:", 0) == 0) s.vm_rss_kb = grab("VmRSS:");
    else if (line.rfind("VmSize:", 0) == 0) s.vm_size_kb = grab("VmSize:");
    else if (line.rfind("Threads:", 0) == 0) s.threads = grab("Threads:");
}
std::ifstream stat("/proc/self/stat");
std::string tok;
for (int i = 1; stat >> tok; ++i) {
    if (i == 14) s.utime = std::strtol(tok.c_str(), nullptr, 10);
    if (i == 15) { s.stime = std::strtol(tok.c_str(), nullptr, 10); break; }
}
#endif
return s;
}

// 进日志前的清洗：控制字符（含 \n \r \t）换成 '?'，并截断到 48 字节。
// 用户名是客户端完全可控的字符串，直接打进日志会被拿来伪造日志行；
// 而被拒的名字又恰好是最需要记下来的（谁在拿什么名字试探）。
std::string sanitize_for_log(const std::string& value) {
    std::string out;
    out.reserve(value.size() < 48 ? value.size() : 48);
    for (const char ch : value) {
        if (out.size() >= 48) return out + "…";
        const auto c = static_cast<unsigned char>(ch);
        out.push_back((c < 0x20 || c == 0x7F) ? '?' : ch);
    }
    return out;
}

}  // namespace sfs::server
