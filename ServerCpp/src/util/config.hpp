// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 服务端配置文件（JSON，无新依赖，复用 src/json_lite.hpp）。
//
// 默认路径 data/server-config.json；不存在时用内置默认值，不报错、不退出。
// 合并优先级：内置默认值 < 配置文件 < 环境变量 < 命令行。
// 目前只读取 \"log\" 段，结构见 docs/server-logging.md。

#pragma once

#include <string>

#include "util/log.hpp"

namespace sfs::config {

std::string default_config_path();

// 扫描 argv 找 --config（最后一次出现的生效）；未给出时返回默认路径。
// --config 缺少值抛 std::runtime_error。
std::string find_config_path(int argc, char* argv[]);

struct LogConfigLoad {
    bool file_found = false;   // 文件存在
    bool parsed = false;       // JSON 解析成功
    std::string path;
    std::string note;          // ASCII：写进启动摘要
    std::string error;         // 非空时由调用方在 init 之后用 warn 打出（可能含中文）
};

// 读取配置文件的 log 段并写进 opts（只覆盖文件里出现的字段）。
LogConfigLoad load_log_config(const std::string& path, sfs::log::Options* opts);

// SFS_LOG_LEVEL / SFS_LOG_CATEGORIES / SFS_LOG_HIDE_CATEGORIES / SFS_LOG_COLOR。
// 取值非法时抛 std::runtime_error。
void apply_env_log_options(sfs::log::Options* opts);

// --log-level / --log-categories / --log-hide-categories / --log-color / --log-quiet-startup。
// 取值非法或缺少值时抛 std::runtime_error。
void apply_cli_log_options(int argc, char* argv[], sfs::log::Options* opts);

// ---- 作弊策略（配置文件的 cheats 段）----
// 位号与客户端 CheatFlag 完全一致，两端必须同时改，禁止重排：
//   bit0 无限建造区 bit1 允许穿模 bit2 无限燃料 bit3 无大气阻力 bit4 无碰撞损伤
//   bit5 无重力     bit6 无热损伤 bit7 无燃烧痕迹 bit8 传送     bit9 补满燃料
// 缺省（没有配置文件 / 没有 cheats 段）= 总开关开 + 全项允许 = 与"没有策略"等价，行为不变。
struct CheatConfig {
    bool found = false;                          // 配置里是否出现 cheats 段
    bool enabled = true;                         // 总开关：false = 隐藏/禁用整个作弊入口
    std::uint16_t individual_flags = 0x03FFu;    // bit0..bit9；置 1 = 允许
    std::string note;                            // ASCII 摘要，给启动日志用
};

CheatConfig load_cheat_config(const std::string& path);

// 进程内生效的作弊策略（main 启动时装载一次；握手成功时下发给每个客户端）。
// 放在 util/config 里，避免跨 TU 的裸全局变量。
void set_active_cheat_config(const CheatConfig& config);
const CheatConfig& active_cheat_config();

}  // namespace sfs::config
