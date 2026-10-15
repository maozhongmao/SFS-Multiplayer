// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 分类日志：分类 + 级别 + ANSI 颜色 + 可过滤（配置文件 / 环境变量 / 命令行）。
// 用法与配置说明见 docs/server-logging.md。
//
// 输出格式（稳定、可 grep）：
//   HH:MM:SS [net   ] INFO  [cpp-server] listening on KCP (UDP) port 9806, max_players=16
// 分类名左对齐补到 6 列、级别名左对齐补到 5 列，时间戳固定 HH:MM:SS。
// 消息正文（[cpp-server] 前缀、中文、既有关键字）原样保留，因此老的 grep 规则仍然有效。

#pragma once

#include <cstddef>
#include <string>

namespace sfs::log {

// 分类：配置文件 / 命令行里的字符串名就是下面这些（net/world/p2p/plugin/config/error）。
enum class Cat {
    Net = 0,   // 连接 / 握手 / 收发 / KCP
    World,     // 火箭 / 世界状态 / 权威 / 存档
    P2P,       // P2P 信令撮合
    Plugin,    // 插件系统
    Config,    // 配置 / 控制台命令 / 启动摘要
    Error,     // 不归属具体子系统的错误
};
inline constexpr int kCategoryCount = 6;

enum class Level { Debug = 0, Info, Warn, Error };

// 颜色模式：auto = 仅当 stdout 是终端时启用；on = 强制启用；off = 关闭。
enum class ColorMode { Auto, On, Off };

struct Options {
    bool enabled = true;               // 总开关：false 时一行都不输出
    Level level = Level::Info;         // 低于该级别的日志不输出（error 除外）
    ColorMode color_mode = ColorMode::Auto;
    bool quiet_startup = false;        // true = 不打印那条启动配置摘要

    // explicit_hidden[c] = true：该类被【显式】关闭（配置 categories 里写 false，
    // 或 --log-hide-categories / SFS_LOG_HIDE_CATEGORIES）。显式关闭连 error 级别也不再输出。
    bool explicit_hidden[kCategoryCount] = {false, false, false, false, false, false};
    // 白名单（--log-categories a,b,c / SFS_LOG_CATEGORIES）：active 时未列出的类别不输出，
    // 但 error 级别照常输出（除非该类别被显式 hide）。
    bool whitelist_active = false;
    bool whitelist[kCategoryCount] = {true, true, true, true, true, true};

    // 仅用于启动摘要行：
    std::string config_path = "data/server-config.json";
    std::string config_note = "not found (built-in defaults)";
};

// 安装最终生效的选项。内部会解析颜色（终端检测 / Windows VT），
// 并打印一行 ASCII 的启动摘要（quiet_startup=true 时跳过）。
// 必须在任何日志线程启动之前调用一次。
void init(const Options& options);

const Options& options();

// 是否允许输出。供调用方在格式化开销大时提前判断；log() 内部同样会判。
bool is_enabled(Cat cat, Level level);

// 主日志入口，printf 风格格式化。
// 注意：内部有互斥锁，禁止在信号处理函数里调用（那里请用 signal_log）。
void log(Cat cat, Level level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// 信号处理函数专用：不加锁，用 ::write / _write 直接写 stdout，因此没有时间戳前缀。
// 格式：`[net   ] INFO  message`。避免在 SIGINT/SIGTERM 里抢日志互斥锁造成死锁。
void signal_log(Cat cat, Level level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

const char* category_name(Cat cat);
const char* level_name(Level level);
bool category_from_name(const std::string& name, Cat* out);
bool parse_level(const std::string& name, Level* out);
bool parse_color_mode(const std::string& name, ColorMode* out);

// 解析 "net,world,p2p" 形式的分类列表。未识别的名字返回 false 并写入 bad（供报错）。
// 接受特殊名字 "all"（全部分类）。count 上限 kCategoryCount。
bool parse_category_list(const std::string& csv, Cat* out, std::size_t* count, std::string* bad);

// 选项修改（命令行与环境变量共用，两者调用同一套逻辑）。
void set_whitelist(Options* o, const Cat* cats, std::size_t count);
void add_hidden(Options* o, const Cat* cats, std::size_t count);

}  // namespace sfs::log

// 便捷宏：SFS_LOGI(Cat::Net, "...", args...)
#define SFS_LOGD(cat, ...) ::sfs::log::log((cat), ::sfs::log::Level::Debug, __VA_ARGS__)
#define SFS_LOGI(cat, ...) ::sfs::log::log((cat), ::sfs::log::Level::Info, __VA_ARGS__)
#define SFS_LOGW(cat, ...) ::sfs::log::log((cat), ::sfs::log::Level::Warn, __VA_ARGS__)
#define SFS_LOGE(cat, ...) ::sfs::log::log((cat), ::sfs::log::Level::Error, __VA_ARGS__)
