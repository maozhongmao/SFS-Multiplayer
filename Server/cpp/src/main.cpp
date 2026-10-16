// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "plugins/plugin_bootstrap.hpp"  // SFS_PLUGINS=0 时展开为空

#include "util/config.hpp"
#include "util/log.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

static sfs::server::Server* g_server = nullptr;

void signal_handler(int sig) {
    // 信号处理函数里不能抢日志互斥锁（会与主线程死锁），所以用不加锁的 signal_log。
    sfs::log::signal_log(sfs::log::Cat::Net, sfs::log::Level::Info,
                         "[cpp-server] signal %d received, shutting down...", sig);
    if (g_server) g_server->stop();
}

namespace {

void print_usage(const char* exe) {
    std::cout
        << "SFS Multiplayer C++ 服务端\n"
        << "用法:\n"
        << "  " << exe << " [选项]\n"
        << "  " << exe << " <port> [max_players] [state_path]      # 位置参数写法\n"
        << "\n选项:\n"
        << "  --port <端口>            监听端口，默认 9806\n"
        << "  --max-players <人数>     最大玩家数，默认 16\n"
        << "  --state <文件>           存档文件路径，默认 data/server-state.json\n"
        << "  --p2p <on|off>           P2P 信令撮合开关，默认 off（直连尚不稳定；对齐 C# p2p.enabled）\n"
        << "  --p2p-proximity <米>     撮合距离阈值，默认 10000（对齐 proximity_meters）\n"
        << "  --p2p-interval <秒>      撮合校验周期，默认 1（对齐 validation_interval_seconds）\n"
        << "  --p2p-transition <秒>    下发给客户端的切换缓冲，默认 10（对齐 transition_buffer_seconds）\n"
        << "  --password <密码>        连接密码；默认空 = 不校验（任何人可进）\n"
        << "  --block-duplicate-names <on|off>  重名拦截，默认 on（对齐 block_duplicate_player_names）\n"
        << "  --max-username-length <1-128>     用户名长度上限，默认 32（对齐 max_username_length）\n"
        << "  -h, --help               显示帮助\n"
        << "\n日志选项（优先级：命令行 > 环境变量 > 配置文件 > 内置默认值）:\n"
        << "  --config <文件>          配置文件路径，默认 data/server-config.json（不存在则用内置默认值）\n"
        << "  --log-level <debug|info|warn|error>        最低日志级别，默认 info\n"
        << "  --log-categories <a,b,c>                   只显示这些分类（error 级仍显示）\n"
        << "  --log-hide-categories <a,b,c>              不显示这些分类\n"
        << "  --log-color <auto|on|off>                  ANSI 颜色，默认 auto（仅终端启用）\n"
        << "  --log-quiet-startup                        不打印启动配置摘要\n"
        << "\n环境变量（命令行优先）: SFS_SERVER_PASSWORD（与 C# 服务端同名）/\n"
        << "                        SFS_SERVER_BLOCK_DUPLICATE_NAMES / SFS_SERVER_MAX_USERNAME_LENGTH /\n"
        << "                        SFS_SERVER_P2P / SFS_SERVER_P2P_PROXIMITY /\n"
        << "                        SFS_SERVER_P2P_INTERVAL / SFS_SERVER_P2P_TRANSITION /\n"
        << "                        SFS_LOG_LEVEL / SFS_LOG_CATEGORIES / SFS_LOG_HIDE_CATEGORIES /\n"
        << "                        SFS_LOG_COLOR\n"
        << "\n说明：位置参数与选项可混用，选项优先。\n";
}

// on/off/true/false/1/0/yes/no 都接受（配置文件里是 true/false，命令行 hand-friendly）
bool parse_onoff(const std::string& value, bool* out) {
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        *out = true;
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        *out = false;
        return true;
    }
    return false;
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Windows 控制台默认使用本地代码页（简体中文为 GBK），而源码里的中文是 UTF-8，
    // 直接输出会整片乱码（实测：help/监听/存档提示全变问号）。启动时把控制台
    // 输入输出代码页切到 UTF-8；Linux 构建完全不受影响。
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif

    // ---------------------------------------------------------- 日志配置
    // 合并顺序：内置默认值 -> 配置文件 -> 环境变量 -> 命令行。
    // 必须最先做：后面的参数错误、启动信息都要走新日志接口。
    sfs::log::Options log_options;
    std::string config_path = sfs::config::default_config_path();
    std::string config_error;
    // 作弊策略在日志系统就绪前后都要用到：先声明，装载后立刻生效，启动摘要里再打印一次。
    sfs::config::CheatConfig cheats;
    try {
        config_path = sfs::config::find_config_path(argc, argv);
        const sfs::config::LogConfigLoad loaded =
            sfs::config::load_log_config(config_path, &log_options);
        log_options.config_path = config_path;
        log_options.config_note = loaded.note;
        config_error = loaded.error;
        // 【作弊策略 2026-10-15】同一个配置文件里的 cheats 段：
        //   { "cheats": { "enabled": true, "individual": { "no_gravity": false, ... } } }
        // 缺失 / 文件不存在 = 总开关开 + 全项允许 = 与"没有策略"完全等价（行为与今天一致）。
        cheats = sfs::config::load_cheat_config(config_path);
        sfs::config::set_active_cheat_config(cheats);
        sfs::config::apply_env_log_options(&log_options);
        sfs::config::apply_cli_log_options(argc, argv, &log_options);
        sfs::log::init(log_options);
        if (!config_error.empty()) {
            SFS_LOGW(sfs::log::Cat::Config, "[cpp-server] 配置文件问题: %s", config_error.c_str());
        }
        // 【作弊策略】启动即打印生效值，方便排查"为什么客户端看不到作弊入口/某一项点不动"。
        // flags 的位号与客户端 CheatFlag 一致（bit0 无限建造区 … bit9 补满燃料），置 1 = 允许。
        SFS_LOGI(sfs::log::Cat::Config, "[cpp-server] cheats: enabled=%d flags=0x%04X (%s)",
                 cheats.enabled ? 1 : 0, static_cast<unsigned>(cheats.individual_flags), cheats.note.c_str());
    } catch (const std::exception& e) {
        log_options = sfs::log::Options{};
        log_options.config_path = config_path;
        log_options.config_note = "error (built-in defaults)";
        sfs::log::init(log_options);
        SFS_LOGE(sfs::log::Cat::Error, "[cpp-server] 参数错误: %s", e.what());
        print_usage(argv[0]);
        return 2;
    }

    std::uint16_t port = 9806;
    std::int32_t max_players = 16;
    std::string state_path = "data/server-state.json";
    // P2P 信令撮合配置（对齐 C# P2PSettings / server.yml 的 p2p: 段）。
    // 先读环境变量、后读命令行，所以命令行优先。
    sfs::server::P2PSettings p2p;
    // 加入准入配置（对齐 C# ServerSettings）：密码默认空 = 不校验任何人（与改动前的行为一致），
    // 重名拦截默认开、用户名长度上限默认 32（对齐 C# server.yml 的默认值）。
    sfs::server::JoinPolicy join;

    // 位置参数：<port> [max_players] [state_path]
    // 选项参数：--port/--max-players/--state/--p2p*/--password/--block-duplicate-names/
    //          --max-username-length（优先于位置参数）；--config/--log-* 已在上面消费。
    std::vector<std::string> positional;
    try {
        if (const char* v = std::getenv("SFS_SERVER_P2P")) {
            bool on = false;
            if (!parse_onoff(v, &on)) throw std::runtime_error("SFS_SERVER_P2P 取值必须是 on/off");
            p2p.enabled = on;
        }
        // 与 C# 同名的环境变量：SFS_SERVER_PASSWORD 优先于其它来源（对齐 Program.cs:39）
        if (const char* v = std::getenv("SFS_SERVER_PASSWORD")) {
            join.password = v;
        }
        if (const char* v = std::getenv("SFS_SERVER_BLOCK_DUPLICATE_NAMES")) {
            bool on = false;
            if (!parse_onoff(v, &on))
                throw std::runtime_error("SFS_SERVER_BLOCK_DUPLICATE_NAMES 取值必须是 on/off");
            join.block_duplicate_player_names = on;
        }
        if (const char* v = std::getenv("SFS_SERVER_MAX_USERNAME_LENGTH")) {
            join.max_username_length = std::atoi(v);
        }
        if (const char* v = std::getenv("SFS_SERVER_P2P_PROXIMITY")) p2p.proximity_meters = std::atof(v);
        if (const char* v = std::getenv("SFS_SERVER_P2P_INTERVAL")) {
            p2p.validation_interval_seconds = std::atoi(v);
        }
        if (const char* v = std::getenv("SFS_SERVER_P2P_TRANSITION")) {
            p2p.transition_buffer_seconds = std::atoi(v);
        }

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto take_value = [&](const char* flag) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error(std::string(flag) + " 缺少值");
                }
                return std::string(argv[++i]);
            };
            if (arg == "--port") {
                port = static_cast<std::uint16_t>(std::atoi(take_value("--port").c_str()));
            } else if (arg == "--max-players") {
                max_players = static_cast<std::int32_t>(std::atoi(take_value("--max-players").c_str()));
            } else if (arg == "--state") {
                state_path = take_value("--state");
            } else if (arg == "--p2p") {
                bool on = false;
                const std::string value = take_value("--p2p");
                if (!parse_onoff(value, &on)) throw std::runtime_error("--p2p 取值必须是 on/off");
                p2p.enabled = on;
            } else if (arg == "--p2p-proximity") {
                p2p.proximity_meters = std::atof(take_value("--p2p-proximity").c_str());
            } else if (arg == "--p2p-interval") {
                p2p.validation_interval_seconds = std::atoi(take_value("--p2p-interval").c_str());
            } else if (arg == "--p2p-transition") {
                p2p.transition_buffer_seconds = std::atoi(take_value("--p2p-transition").c_str());
            } else if (arg == "--password") {
                // 密码也可以给空串（等于关闭校验）；含空格的密码加引号即可
                join.password = take_value("--password");
            } else if (arg == "--block-duplicate-names") {
                bool on = false;
                const std::string value = take_value("--block-duplicate-names");
                if (!parse_onoff(value, &on))
                    throw std::runtime_error("--block-duplicate-names 取值必须是 on/off");
                join.block_duplicate_player_names = on;
            } else if (arg == "--max-username-length") {
                join.max_username_length = std::atoi(take_value("--max-username-length").c_str());
            } else if (arg == "--config") {
                (void)take_value("--config");   // 上面已经加载过
            } else if (arg == "--log-level") {
                (void)take_value("--log-level");
            } else if (arg == "--log-categories") {
                (void)take_value("--log-categories");
            } else if (arg == "--log-hide-categories") {
                (void)take_value("--log-hide-categories");
            } else if (arg == "--log-color") {
                (void)take_value("--log-color");
            } else if (arg == "--log-quiet-startup") {
                // 无值
            } else if (arg == "-h" || arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else {
                positional.push_back(arg);
            }
        }

        // 范围校验与 C# ServerSettings.ValidateP2P 完全一致：越界直接拒绝启动，
        // 免得错值悄悄变成另一种行为（NaN 会被第一个条件拦下）
        if (!(p2p.proximity_meters >= 0.0) || p2p.proximity_meters > 1000000.0) {
            throw std::runtime_error("p2p 距离阈值必须在 0 .. 1000000 米之间");
        }
        if (p2p.validation_interval_seconds < 1 || p2p.validation_interval_seconds > 3600) {
            throw std::runtime_error("p2p 校验周期必须在 1 .. 3600 秒之间");
        }
        if (p2p.transition_buffer_seconds < 0 || p2p.transition_buffer_seconds > 3600) {
            throw std::runtime_error("p2p 切换缓冲必须在 0 .. 3600 秒之间");
        }
        // 加入准入的取值校验，区间与 C# ServerSettings.Validate 一致：
        //   MaxUsernameLength 1..128（ServerSettings.cs:58-59）、Password 长度 <= 256（:62-63）
        if (join.max_username_length < 1 || join.max_username_length > 128) {
            throw std::runtime_error("用户名长度上限必须在 1 .. 128 之间");
        }
        if (join.password.size() > 256) {
            throw std::runtime_error("连接密码不能超过 256 个字符");
        }
    } catch (const std::exception& e) {
        SFS_LOGE(sfs::log::Cat::Error, "[cpp-server] 参数错误: %s", e.what());
        print_usage(argv[0]);
        return 2;
    }

    // 位置参数仅在对应选项未给出时才生效（选项优先）
    bool port_from_option = false;
    bool players_from_option = false;
    bool state_from_option = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--port") port_from_option = true;
        else if (arg == "--max-players") players_from_option = true;
        else if (arg == "--state") state_from_option = true;
    }
    if (!port_from_option && positional.size() >= 1)
        port = static_cast<std::uint16_t>(std::atoi(positional[0].c_str()));
    if (!players_from_option && positional.size() >= 2)
        max_players = static_cast<std::int32_t>(std::atoi(positional[1].c_str()));
    if (!state_from_option && positional.size() >= 3)
        state_path = positional[2];

    // 人数上限：区间与 C# ServerSettings.Validate（MaxConnections 1..256）一致。
    // 这里必须真正拦住：0/负数会让"满员判定"变成永远满员或永不生效。
    if (max_players < 1 || max_players > 256) {
        SFS_LOGE(sfs::log::Cat::Error,
                 "[cpp-server] 参数错误: max_players 必须在 1 .. 256 之间（对齐 C# MaxConnections）");
        print_usage(argv[0]);
        return 2;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
    #if defined(SFS_PLUGINS) && SFS_PLUGINS
    // 装载预编译插件（.qbc）；路径可用 SFS_PLUGIN_PATH 覆盖。
    static const char* kPluginDefault = "plugins/test_broadcast.qbc";
    const char* plugin_path = std::getenv("SFS_PLUGIN_PATH");
    if (plugin_path == nullptr) plugin_path = kPluginDefault;
#endif
    sfs::server::Server server(port, max_players, state_path, p2p, join);
#if defined(SFS_PLUGINS) && SFS_PLUGINS
    sfs::plugins::start(plugin_path, &server);
#endif
        g_server = &server;
        if (!server.start()) return 1;
        SFS_LOGI(sfs::log::Cat::Config,
                 "[cpp-server] port=%u max_players=%d state=%s p2p=%s p2p_proximity=%g "
                 "password=%s block_duplicate_names=%s max_username_length=%d",
                 static_cast<unsigned>(port), max_players, state_path.c_str(),
                 p2p.enabled ? "on" : "off", p2p.proximity_meters,
                 join.password.empty() ? "off" : "on",
                 join.block_duplicate_player_names ? "on" : "off", join.max_username_length);
        SFS_LOGI(sfs::log::Cat::Config,
                 "[cpp-server] 输入 help 查看服务端命令，输入 stop 安全保存并退出。");
        // 控制台命令线程：与网络主循环并行，stdin 阻塞不影响收包
        std::thread console_thread([&server]() { server.run_console(); });
        console_thread.detach();
        server.run();
    } catch (const std::exception& e) {
        SFS_LOGE(sfs::log::Cat::Error, "[cpp-server] fatal error: %s", e.what());
        return 1;
    }
    return 0;
}
