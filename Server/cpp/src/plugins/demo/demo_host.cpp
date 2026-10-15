// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// demo_host - 在 Windows 宿主机上跑通整套插件宿主的可执行示例。
//
// 它演示服务端接入所需的全部动作：
//   1. 实现 HostServices（把插件 API 落到真实世界）；
//   2. 构造 PluginHost + DemoEngineFactory；
//   3. load() example_plugin.js -> bind_host_api() -> subscribe()；
//   4. 每秒 publish 一次 WorldSnapshot，然后 pump() 一次；
//   5. 演示能力拒绝（不声明 log）与预算故障（instruction_budget=2）；
//   6. 优雅 unload() + shutdown()。
//
// 同样用 SFS_PLUGINS_DEMO 包住：默认（服务端）构建里本 TU 为空，
// 不会和 src/main.cpp 抢 main()，产物行为不变。
//
// 编译/运行命令见 docs/plugin-example.md 第 5 节。

#if defined(SFS_PLUGINS_DEMO) && SFS_PLUGINS_DEMO

#include "plugins/demo/demo_js_engine.hpp"
#include "plugins/plugin_host.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sfs::plugins;

namespace {

// 假世界 + 真正的 HostServices 适配层。真实服务端把这里换成世界/连接/日志实现。
class DemoWorld final : public HostServices {
public:
    void set_rocket_count(int n) {
        rockets_.clear();
        for (int i = 0; i < n; ++i) {
            RocketView r{};
            r.rocket_id = 100 + i;
            r.owner_player = (i % 2) + 1;
            r.controlled_by = (i % 2) + 1;
            r.part_count = 10 + i;
            r.pos_x = i * 100.0;
            r.pos_y = 0.0;
            r.pos_z = 0.0;
            r.vel_x = 1.0;
            r.vel_y = 0.0;
            r.vel_z = 0.0;
            r.throttle = 0.5;
            r.has_update_authority = 1;
            rockets_.push_back(r);
        }
    }

    void add_player(std::int32_t id, const char* name) {
        PlayerView p{};
        p.player_id = id;
        p.controlled_rocket = -1;
        std::snprintf(p.name, sizeof(p.name), "%s", name);
        players_.push_back(p);
    }

    WorldSnapshot snapshot(std::uint64_t tick, double world_time) const {
        WorldSnapshot s;
        s.tick = tick;
        s.world_time = world_time;
        s.dropped_rockets = 0;
        s.rockets = rockets_;
        s.players = players_;
        return s;
    }

    // ---- HostServices ----
    bool read_rocket(std::int32_t rocket_id, RocketView* out) override {
        for (const RocketView& r : rockets_) {
            if (r.rocket_id == rocket_id) { *out = r; return true; }
        }
        return false;
    }
    bool write_rocket(const RocketView& view) override {
        for (RocketView& r : rockets_) {
            if (r.rocket_id == view.rocket_id) {
                r = view;
                std::printf("[services] write-back rocket %d throttle=%.2f\n", r.rocket_id, r.throttle);
                return true;
            }
        }
        return false;
    }
    bool send_message(std::int32_t player_id, const char* utf8_text) override {
        std::printf("[services] message -> player %d: %s\n", player_id, utf8_text);
        for (const PlayerView& p : players_) if (p.player_id == player_id) return true;
        return false;
    }
    void log(const char* ascii_text) override { std::printf("[services] %s\n", ascii_text); }

private:
    std::vector<RocketView> rockets_;
    std::vector<PlayerView> players_;
};

bool read_file(const char* path, std::string* out) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return false;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    std::fclose(f);
    return true;
}

std::string resolve_plugin_path(int argc, char** argv) {
    if (argc > 1) return std::string(argv[1]);
    const char* candidates[] = {
        "Server/cpp/src/plugins/demo/example_plugin.js",
        "src/plugins/demo/example_plugin.js",
        "../src/plugins/demo/example_plugin.js",
        "example_plugin.js",
    };
    for (const char* c : candidates) {
        std::string probe;
        if (read_file(c, &probe)) return std::string(c);
    }
    return std::string("Server/cpp/src/plugins/demo/example_plugin.js");
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("== SFS plugin demo: DemoEngine reference host (no QuickJS) ==\n");

    const std::string path = resolve_plugin_path(argc, argv);
    std::string program;
    if (!read_file(path.c_str(), &program)) {
        std::printf("FAIL: cannot read plugin source '%s'\n", path.c_str());
        return 2;
    }
    std::printf("plugin source : %s (%llu bytes)\n", path.c_str(),
                static_cast<unsigned long long>(program.size()));

    DemoWorld world;
    world.set_rocket_count(2);
    world.add_player(1, "alice");
    world.add_player(2, "bob");

    demo::DemoEngineFactory factory;
    PluginHost host(&world, &factory);

    PluginDescriptor desc;
    desc.id = "example-plugin";
    desc.capabilities = kCapReadRocket | kCapSendMessage | kCapLog;
    desc.limits.memory_limit_bytes = 64u * 1024u;
    desc.limits.max_stack_bytes = 8u * 1024u;
    desc.limits.instruction_budget = 10000u;
    desc.limits.max_api_calls = 256u;
    desc.bytecode.assign(program.begin(), program.end());

    std::string err;
    if (!host.load(desc, &err)) {
        std::printf("FAIL: load rejected: %s\n", err.c_str());
        return 3;
    }
    demo::DemoEngine* engine = factory.last_engine();
    if (engine != nullptr) engine->bind_host_api(host.api_for(desc.id));
    if (!host.subscribe(desc.id, kEventWorldSnapshot | kEventPlayerJoined, &err)) {
        std::printf("FAIL: subscribe: %s\n", err.c_str());
        return 4;
    }
    std::printf("loaded        : id=%s engine=%s state=%s\n", desc.id.c_str(),
                engine != nullptr ? engine->name() : "?", to_string(host.state_of(desc.id)));

    // ---- 每秒一次快照，共 5 轮；t=1 时加一个玩家来演示离散事件 ----
    for (int t = 0; t < 5; ++t) {
        world.set_rocket_count(2 + t);  // t>=2 时超过阈值 3，触发 sendMessage
        host.publish_world_snapshot(world.snapshot(static_cast<std::uint64_t>(t), t * 1.0));
        if (t == 1) {
            world.add_player(3, "carol");
            PlayerView p{};
            p.player_id = 3;
            p.controlled_rocket = -1;
            std::snprintf(p.name, sizeof(p.name), "%s", "carol");
            host.publish_player_joined(p);
        }
        host.pump(t * 1.0);  // 每秒最多投递一次，这里是第 t 秒
    }
    if (engine != nullptr) {
        const demo::DemoEngineStats& st = engine->stats();
        std::printf("engine stats  : cmds=%llu mem=%llu bytes host_calls=%u last_status=%d faults=%u\n",
                    static_cast<unsigned long long>(st.command_count),
                    static_cast<unsigned long long>(st.memory_used_bytes),
                    st.host_calls, st.last_host_status, st.faults);
    }
    host.unload(desc.id, &err);
    std::printf("unloaded      : state=%s\n\n", to_string(host.state_of(desc.id)));

    // ---- 能力白名单：不声明 log 的插件，sfs.log 一律返回 -1 ----
    PluginDescriptor nolog = desc;
    nolog.id = "example-plugin-nolog";
    nolog.capabilities = kCapReadRocket | kCapSendMessage;  // 故意没有 kCapLog
    if (host.load(nolog, &err)) {
        demo::DemoEngine* e2 = factory.last_engine();
        if (e2 != nullptr) e2->bind_host_api(host.api_for(nolog.id));
        host.subscribe(nolog.id, kEventWorldSnapshot, &err);
        world.set_rocket_count(5);
        host.publish_world_snapshot(world.snapshot(10, 10.0));
        host.pump(10.0);
        if (e2 != nullptr) {
            std::printf("nolog stats   : capability_denials=%u last_status=%d sendMessage still ok\n",
                        e2->stats().capability_denials, e2->stats().last_host_status);
        }
        host.unload(nolog.id, &err);
    } else {
        std::printf("FAIL: nolog load rejected: %s\n", err.c_str());
    }
    std::printf("\n");

    // ---- 指令预算：只给 2 条命令的预算，第 3 条超预算 -> kFaulted 并停止投递 ----
    const char* tiny =
        "// @demo: when worldSnapshot set a = 1\n"
        "// @demo: when worldSnapshot set b = 2\n"
        "// @demo: when worldSnapshot set c = 3\n";
    PluginDescriptor budget;
    budget.id = "budget-demo";
    budget.capabilities = kCapNone;
    budget.limits.memory_limit_bytes = 16u * 1024u;
    budget.limits.max_stack_bytes = 4u * 1024u;
    budget.limits.instruction_budget = 2u;  // 只够 2 条
    budget.limits.max_api_calls = 16u;
    budget.bytecode.assign(tiny, tiny + std::strlen(tiny));
    if (host.load(budget, &err)) {
        demo::DemoEngine* e3 = factory.last_engine();
        if (e3 != nullptr) e3->bind_host_api(host.api_for(budget.id));
        host.subscribe(budget.id, kEventWorldSnapshot, &err);
        world.set_rocket_count(1);
        host.publish_world_snapshot(world.snapshot(20, 20.0));
        host.pump(20.0);
        std::printf("budget state  : state=%s faults=%llu (expected faulted)\n",
                    to_string(host.state_of(budget.id)),
                    static_cast<unsigned long long>(host.fault_count(budget.id)));
        host.publish_world_snapshot(world.snapshot(21, 21.0));
        host.pump(21.0);  // faulted 之后不再投递
        std::printf("budget state  : after another pump state=%s\n",
                    to_string(host.state_of(budget.id)));
        host.unload(budget.id, &err);
    } else {
        std::printf("FAIL: budget load rejected: %s\n", err.c_str());
    }

    host.shutdown();
    std::printf("\ndone: host shutdown, plugin_count=%llu\n",
                static_cast<unsigned long long>(host.plugin_count()));
    return 0;
}

#endif  // SFS_PLUGINS_DEMO
