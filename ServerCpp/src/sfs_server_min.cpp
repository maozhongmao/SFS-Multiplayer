// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// SFS Multiplayer 服务端 —— C++ / ARM 常驻版（第一阶段）。
//
// 本文件只做“能连上、能握手、能保活”，不含火箭/部件同步业务逻辑。
// 目的：在 Luckfox Lume（ARMv7 / 88MB RAM）上取一份真实的内存与 CPU 基线，
// 与 C# 版（VmRSS 34832 kB / VmSize 245908 kB / 15 线程）逐项对比。
//
// 单线程 epoll，无额外工作线程，不写任何持久化文件。

#include "protocol/codec.hpp"
#include "server/server.hpp"
#include "server/util.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <signal.h>
#include <string>

using sfs::server::Server;
using sfs::server::kMaxPlayers;
using sfs::server::on_signal;
using sfs::protocol::kProtocolVersion;

int main(int argc, char* argv[]) {
    std::uint16_t port = 9806;
    std::int32_t max_players = kMaxPlayers;
    // 默认与 C# 版一致：相对当前工作目录的 data/server-state.json
    std::string state_path = "data/server-state.json";

    // 兼容两种用法：位置参数（端口 人数）+ 具名参数
    int positional = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::printf("[cpp-server] %s 缺少参数值\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (arg == "--state")       { state_path = need_value("--state"); continue; }
        if (arg == "--port")        { port = static_cast<std::uint16_t>(std::atoi(need_value("--port"))); continue; }
        if (arg == "--max-players") { max_players = std::atoi(need_value("--max-players")); continue; }
        if (arg == "--help" || arg == "-h") {
            std::printf("用法: %s [端口] [人数上限] [--state <存档路径>] [--port N] [--max-players N]\n",
                        argv[0]);
            return 0;
        }
        // 位置参数：第 1 个是端口，第 2 个是人数
        ++positional;
        if (positional == 1) port = static_cast<std::uint16_t>(std::atoi(arg.c_str()));
        else if (positional == 2) max_players = std::atoi(arg.c_str());
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);   // Windows 没有 SIGPIPE；send 失败会直接返回错误
#endif

    std::printf("[cpp-server] SFS Multiplayer C++ server\n");
    std::printf("[cpp-server] protocol version = %d, state=%s\n",
                kProtocolVersion, state_path.c_str());
    std::fflush(stdout);

    Server server(port, max_players, state_path);
    if (!server.start()) {
        std::printf("[cpp-server] start failed\n");
        return 1;
    }
    server.run();

    // 退出前落盘，避免丢进度
    server.save_world_to_disk();
    std::printf("[cpp-server] stopped (state saved)\n");
    return 0;
}
