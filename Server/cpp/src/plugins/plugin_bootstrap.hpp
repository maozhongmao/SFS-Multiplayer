// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - 服务端主循环接入点（compile-time gate）。
//
// 默认 SFS_PLUGINS=0：本头文件展开为空，服务端不引用任何插件符号。
// 打开方式（二选一，编译期优先）:
//   * 编译期: build-server-exp.ps1 的 $cflags 里加 "-DSFS_PLUGINS=1"
//   * 运行期: 环境变量 SFS_PLUGINS=1（编译期已打开时才有意义）
//
// 服务端接入只需三处（不改本文件，见 docs/plugin-system.md 第 5 节）:
//   1. #include "plugins/plugin_bootstrap.hpp"
//   2. 启动后: sfs::plugins::install_services(&my_host_services);
//   3. 主循环每轮: sfs::plugins::tick(uptime_now());
//      + 每 1 秒用当前世界构造 WorldSnapshot 并
//        sfs::plugins::global_host()->publish_world_snapshot(snap);
//      + 退出前: sfs::plugins::shutdown();
// SFS_PLUGINS=0 时这三处展开为空，编译产物与未引入插件系统时一致。

#pragma once

#ifndef SFS_PLUGINS
#define SFS_PLUGINS 0
#endif

#if SFS_PLUGINS

#include "plugins/plugin_host.hpp"

// 注意：这个前置声明必须在 sfs::plugins【外面】，否则会变成 sfs::plugins::sfs::server::Server。
namespace sfs::server { class Server; }

namespace sfs::plugins {

// 安装服务端适配层（启动时一次）。传 nullptr = 无适配层，
// 插件的能力调用统一返回 kHostErrUnavailable（骨架默认状态）。
void install_services(HostServices* services);

// 安装 JS 引擎工厂（默认是 NullJsEngineFactory ⇒ load() 必然失败）。
void install_engine_factory(IJsEngineFactory* factory);

// 控制台未知命令转交插件（有插件处理则返回 true）。
bool submit_console_command(const char* command);

// 启动插件系统：注册服务/工厂、读取 .qbc、装载并订阅。返回装载成功的插件数。
int start(const char* plugin_path, sfs::server::Server* server);

// 运行期开关：编译期打开后，仍可用环境变量 SFS_PLUGINS=0/off 关掉。
// 只在第一次调用时读取环境变量。
bool runtime_enabled();

// 全局单线程宿主（懒构造）。未启用时返回 nullptr。
// 插件代码只在主循环线程执行，宿主内部不加锁。
PluginHost* global_host();

// 主循环每轮调用。未启用 / 无插件 / 未到投递节拍时是空操作，
// 返回 true 表示本轮确实向插件投递了事件。
bool tick(double now_seconds);

// 卸载全部插件（退出前调用，幂等）。
void shutdown();

}  // namespace sfs::plugins

#endif  // SFS_PLUGINS
