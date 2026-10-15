// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - bootstrap 实现。
//
// 本翻译单元始终参与编译（SFS_PLUGINS=0 时也没有调用者），这样:
//   * 骨架代码不会因"默认关闭"而腐烂（每次 server 构建都过一遍编译器）；
//   * 又因为没有可达调用者，--gc-sections 会把它们整段回收，产物行为不变。

#include "plugins/plugin_bootstrap.hpp"

#include "plugins/plugin_host.hpp"

#include <cstdlib>

namespace sfs::plugins {

namespace {

// 全部是 POD 全局：没有构造/析构，不会被放进 .init_array，
// 因此函数不可达时可以随 --gc-sections 一起被回收。
HostServices* g_services = nullptr;
PluginHost* g_host = nullptr;
bool g_runtime_checked = false;
bool g_runtime_enabled = false;

bool parse_onoff(const char* value, bool fallback) {
    if (value == nullptr) return fallback;
    const char c = value[0];
    if (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y' || c == 'o' || c == 'O') {
        // 以 'o' 开头既可能是 on 也可能是 off，逐字符判定。
        const char second = value[1];
        if ((c == 'o' || c == 'O') && (second == 'f' || second == 'F')) return false;
        return true;
    }
    if (c == '0' || c == 'f' || c == 'F' || c == 'n' || c == 'N') return false;
    return fallback;
}

}  // namespace

void install_services(HostServices* services) {
    g_services = services;
    if (g_host != nullptr) {
        // 宿主已构造：不重建，仅提示适配层必须在 global_host() 之前安装。
        g_host->shutdown();
        delete g_host;
        g_host = nullptr;
    }
}

bool runtime_enabled() {
    if (!g_runtime_checked) {
        g_runtime_checked = true;
        g_runtime_enabled = true;  // 编译期已打开
        if (const char* v = std::getenv("SFS_PLUGINS")) {
            g_runtime_enabled = parse_onoff(v, true);
        }
    }
    return g_runtime_enabled;
}

PluginHost* global_host() {
    if (!runtime_enabled()) return nullptr;
    if (g_host == nullptr) g_host = new PluginHost(g_services);
    return g_host;
}

bool tick(double now_seconds) {
    PluginHost* host = global_host();
    if (host == nullptr || !host->is_enabled()) return false;
    host->pump(now_seconds);
    return true;
}

void shutdown() {
    if (g_host != nullptr) {
        delete g_host;
        g_host = nullptr;
    }
}

}  // namespace sfs::plugins
