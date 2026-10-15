// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件宿主冒烟测试：不需要 QuickJS，也不需要真实服务端。
// 编译命令（Windows 宿主机，zig 自带 libc++）见 docs/plugin-example.md 第 5 节。
// 退出码 0 = 全部通过，1 = 有失败。所有输出为 ASCII。

#include "plugins/demo/demo_js_engine.hpp"
#include "plugins/plugin_host.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sfs::plugins;

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            ++g_failures;                                                    \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
        }                                                                    \
    } while (0)

// 服务端适配层的替身：记录插件到底调了什么。
class StubServices final : public HostServices {
public:
    void add_rocket(std::int32_t id, std::int32_t parts) {
        RocketView r{};
        r.rocket_id = id;
        r.owner_player = 1;
        r.controlled_by = 1;
        r.part_count = parts;
        r.throttle = 0.1;
        r.has_update_authority = 1;
        rockets.push_back(r);
    }
    void add_player(std::int32_t id, const char* name) {
        PlayerView p{};
        p.player_id = id;
        p.controlled_rocket = -1;
        std::snprintf(p.name, sizeof(p.name), "%s", name);
        players.push_back(p);
    }

    bool read_rocket(std::int32_t id, RocketView* out) override {
        for (const RocketView& r : rockets) {
            if (r.rocket_id == id) { *out = r; return true; }
        }
        return false;
    }
    bool write_rocket(const RocketView& v) override {
        for (RocketView& r : rockets) {
            if (r.rocket_id == v.rocket_id) { r = v; ++writes; return true; }
        }
        return false;
    }
    bool send_message(std::int32_t player_id, const char* text) override {
        ++messages;
        last_message = text;
        for (const PlayerView& p : players) if (p.player_id == player_id) return true;
        return false;
    }
    void log(const char* text) override { ++logs; last_log = text; }

    std::vector<RocketView> rockets;
    std::vector<PlayerView> players;
    int logs = 0, messages = 0, writes = 0;
    std::string last_log, last_message;
};

struct Fixture {
    StubServices svc;
    demo::DemoEngineFactory factory;
    PluginHost host;
    Fixture() : host(&svc, &factory) {}
};

PluginDescriptor make_desc(const char* id, std::uint32_t caps, const char* program,
                           PluginLimits limits = PluginLimits{}) {
    PluginDescriptor d;
    d.id = id;
    d.capabilities = caps;
    d.limits = limits;
    d.bytecode.assign(program, program + std::strlen(program));
    return d;
}

bool load_and_bind(Fixture& fx, const PluginDescriptor& d, demo::DemoEngine** engine, std::string* err) {
    if (!fx.host.load(d, err)) return false;
    *engine = fx.factory.last_engine();
    if (*engine != nullptr) (*engine)->bind_host_api(fx.host.api_for(d.id));
    return true;
}

WorldSnapshot make_snapshot(const StubServices& svc, std::uint64_t tick) {
    WorldSnapshot s;
    s.tick = tick;
    s.world_time = static_cast<double>(tick);
    s.rockets = svc.rockets;
    s.players = svc.players;
    return s;
}

// ---------------------------------------------------------------- 用例

void test_null_engine() {
    StubServices svc;
    PluginHost host(&svc);  // 默认 NullJsEngineFactory
    PluginDescriptor d = make_desc("p1", kCapLog, "// @demo: when worldSnapshot log \"x\"\n");
    std::string err;
    CHECK(!host.load(d, &err));
    CHECK(err == "engine rejected bytecode");
    CHECK(host.plugin_count() == 0);
}

void test_validations() {
    Fixture fx;
    std::string err;
    PluginDescriptor d = make_desc("bad id!", kCapLog, "// @demo: when worldSnapshot log \"x\"\n");
    CHECK(!fx.host.load(d, &err));
    CHECK(err == "invalid id: ascii [A-Za-z0-9_.-], 1..32 chars");

    d = make_desc("ok", kCapLog, "// @demo: when worldSnapshot log \"x\"\n");
    d.bytecode.clear();
    CHECK(!fx.host.load(d, &err));
    CHECK(err == "empty bytecode");

    d = make_desc("ok", 1u << 20, "// @demo: when worldSnapshot log \"x\"\n");
    CHECK(!fx.host.load(d, &err));
    CHECK(err == "unknown capability bit");

    PluginLimits tiny;
    tiny.memory_limit_bytes = 64;  // 程序本身几百字节 -> 引擎按内存预算拒绝
    d = make_desc("ok", kCapLog, "// @demo: when worldSnapshot set a = 1\n// @demo: when worldSnapshot set b = 2\n// @demo: when worldSnapshot set c = 3\n", tiny);
    CHECK(!fx.host.load(d, &err));
    CHECK(err == "engine rejected bytecode");

    d = make_desc("ok", kCapLog, "// @demo: when worldSnapshot log \"x\"\n");
    CHECK(fx.host.load(d, &err));
    CHECK(!fx.host.load(d, &err));
    CHECK(err == "id already loaded");
    demo::DemoEngine* e = fx.factory.last_engine();
    if (e) e->bind_host_api(fx.host.api_for(d.id));
    CHECK(!fx.host.subscribe("nope", kEventWorldSnapshot, &err));
    CHECK(err == "no such plugin");
    CHECK(!fx.host.subscribe("ok", 1u << 20, &err));
    CHECK(err == "unknown event bit");
}

void test_dispatch_and_budget_basics() {
    Fixture fx;
    fx.svc.add_rocket(100, 10);
    fx.svc.add_rocket(101, 11);
    fx.svc.add_rocket(102, 12);
    fx.svc.add_player(7, "alice");

    const char* program =
        "// @demo: when worldSnapshot set n = event.rockets.count\n"
        "// @demo: when worldSnapshot log \"n=@n\"\n"
        "// @demo: when worldSnapshot if n > 2 then sendmsg 7 \"too many\"\n";
    PluginDescriptor d = make_desc("basic", kCapLog | kCapSendMessage, program);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    CHECK(engine != nullptr);
    if (engine == nullptr) return;
    CHECK(fx.host.state_of("basic") == PluginState::kLoaded);
    CHECK(fx.host.subscribe("basic", kEventWorldSnapshot, &err));
    CHECK(fx.host.state_of("basic") == PluginState::kRunning);

    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().dispatches == 1);
    CHECK(engine->stats().command_count == 3);
    CHECK(engine->stats().host_calls == 2);  // log + sendmsg
    CHECK(fx.svc.logs == 1);
    CHECK(fx.svc.last_log == "[plugin:basic] n=3");  // 宿主加 ASCII 前缀
    CHECK(fx.svc.messages == 1);
    CHECK(fx.svc.last_message == "too many");
    CHECK(engine->stats().faults == 0);

    // 限流：同一秒内第二次 pump 不投递，下一秒才再投。
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 2));
    fx.host.pump(0.5);
    CHECK(engine->stats().dispatches == 1);
    fx.host.pump(1.0);
    CHECK(engine->stats().dispatches == 2);

    CHECK(fx.host.unload("basic", &err));
    CHECK(fx.host.state_of("basic") == PluginState::kUnloaded);
}
// ---------------------------------------------------------------- 用例（续）

void test_capability_denial() {
    Fixture fx;
    const char* program = "// @demo: when worldSnapshot log \"hello\"\n";
    PluginDescriptor d = make_desc("nolog", kCapSendMessage, program);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("nolog", kEventWorldSnapshot, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().capability_denials == 1);
    CHECK(engine->stats().last_host_status == kHostErrNoCapability);
    CHECK(fx.svc.logs == 0);
    // 能力拒绝是"调用被拒"，不是引擎故障。
    CHECK(engine->stats().faults == 0);
    CHECK(fx.host.state_of("nolog") == PluginState::kRunning);
}

void test_api_budget() {
    Fixture fx;
    const char* program =
        "// @demo: when worldSnapshot log \"a\"\n"
        "// @demo: when worldSnapshot log \"b\"\n"
        "// @demo: when worldSnapshot log \"c\"\n";
    PluginLimits limits;
    limits.max_api_calls = 2;
    PluginDescriptor d = make_desc("apibudget", kCapLog, program, limits);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("apibudget", kEventWorldSnapshot, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().host_calls == 3);
    CHECK(engine->stats().budget_denials == 1);
    CHECK(engine->stats().last_host_status == kHostErrBudget);
    CHECK(fx.svc.logs == 2);
    CHECK(engine->stats().faults == 0);
    CHECK(fx.host.state_of("apibudget") == PluginState::kRunning);
}

void test_instruction_fault() {
    Fixture fx;
    const char* program =
        "// @demo: when worldSnapshot set a = 1\n"
        "// @demo: when worldSnapshot set b = 2\n"
        "// @demo: when worldSnapshot set c = 3\n"
        "// @demo: when worldSnapshot set dd = 4\n";
    PluginLimits limits;
    limits.instruction_budget = 2;  // 只够前两条
    PluginDescriptor d = make_desc("faulty", kCapNone, program, limits);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("faulty", kEventWorldSnapshot, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().faults == 1);
    CHECK(fx.host.state_of("faulty") == PluginState::kFaulted);
    CHECK(fx.host.fault_count("faulty") == 1);

    // faulted 是终态：再投递也不会进入引擎。
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 2));
    fx.host.pump(1.0);
    CHECK(engine->stats().dispatches == 1);
    CHECK(fx.host.state_of("faulty") == PluginState::kFaulted);
}

void test_read_write() {
    Fixture fx;
    fx.svc.add_rocket(42, 12);
    const char* program =
        "// @demo: when worldSnapshot getrocket 42 into r\n"
        "// @demo: when worldSnapshot setthrottle r 0.75\n"
        "// @demo: when worldSnapshot writeback r\n"
        "// @demo: when worldSnapshot log \"parts=@r.partCount\"\n";
    PluginDescriptor d = make_desc("rw", kCapReadRocket | kCapWriteRocket | kCapLog, program);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("rw", kEventWorldSnapshot, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(fx.svc.writes == 1);
    CHECK(fx.svc.rockets.size() == 1);
    if (fx.svc.rockets.size() == 1) CHECK(fx.svc.rockets[0].throttle == 0.75);
    CHECK(fx.svc.last_log == "[plugin:rw] parts=12");
    CHECK(engine->stats().last_host_status == kHostOk);

    // 非法写回（throttle 越界）必须被宿主拒绝，且不落到适配层。
    Fixture fx2;
    fx2.svc.add_rocket(42, 12);
    const char* bad =
        "// @demo: when worldSnapshot getrocket 42 into r\n"
        "// @demo: when worldSnapshot setthrottle r 2.0\n"
        "// @demo: when worldSnapshot writeback r\n";
    PluginDescriptor d2 = make_desc("rw2", kCapReadRocket | kCapWriteRocket, bad);
    demo::DemoEngine* engine2 = nullptr;
    CHECK(load_and_bind(fx2, d2, &engine2, &err));
    if (engine2 == nullptr) return;
    CHECK(fx2.host.subscribe("rw2", kEventWorldSnapshot, &err));
    fx2.host.publish_world_snapshot(make_snapshot(fx2.svc, 1));
    fx2.host.pump(0.0);
    CHECK(fx2.svc.writes == 0);
    CHECK(engine2->stats().last_host_status == kHostErrBadArgument);
}

void test_not_found() {
    Fixture fx;
    fx.svc.add_player(1, "alice");
    const char* program =
        "// @demo: when worldSnapshot sendmsg 99 \"hi\"\n"
        "// @demo: when worldSnapshot getrocket 12345 into r\n";
    PluginDescriptor d = make_desc("nf", kCapSendMessage | kCapReadRocket, program);
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("nf", kEventWorldSnapshot, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().not_found == 2);
    CHECK(engine->stats().last_host_status == kHostErrNotFound);
    CHECK(fx.svc.messages == 1);
}

void test_api_for_missing() {
    Fixture fx;
    HostApi api = fx.host.api_for("ghost");
    CHECK(api.ctx == nullptr);
    RocketView rv{};
    CHECK(api.read_rocket(api.ctx, 1, &rv) == kHostErrState);
    CHECK(api.log(api.ctx, "x") == kHostErrState);
}

void test_example_plugin(const char* path) {
    if (path == nullptr) {
        std::printf("SKIP example_plugin.js case (pass the .js path as argv[1])\n");
        return;
    }
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        ++g_checks;
        ++g_failures;
        std::printf("FAIL cannot open %s\n", path);
        return;
    }
    std::string program;
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), file)) > 0) program.append(buf, n);
    std::fclose(file);

    Fixture fx;
    fx.svc.add_rocket(100, 10);
    fx.svc.add_rocket(101, 11);
    fx.svc.add_rocket(102, 12);
    fx.svc.add_rocket(103, 13);
    fx.svc.add_player(1, "alice");
    fx.svc.add_player(2, "bob");

    PluginDescriptor d;
    d.id = "example-plugin";
    d.capabilities = kCapReadRocket | kCapSendMessage | kCapLog;
    d.bytecode.assign(program.begin(), program.end());
    std::string err;
    demo::DemoEngine* engine = nullptr;
    CHECK(load_and_bind(fx, d, &engine, &err));
    if (engine == nullptr) return;
    CHECK(fx.host.subscribe("example-plugin", kEventWorldSnapshot | kEventPlayerJoined, &err));
    fx.host.publish_world_snapshot(make_snapshot(fx.svc, 1));
    fx.host.pump(0.0);
    CHECK(engine->stats().faults == 0);
    CHECK(engine->stats().command_count == 8);
    CHECK(fx.svc.messages == 2);  // 4 枚 > 阈值 3，给 2 个玩家各一条
    CHECK(engine->stats().last_host_status == kHostOk);

    PlayerView carol{};
    carol.player_id = 3;
    carol.controlled_rocket = -1;
    std::snprintf(carol.name, sizeof(carol.name), "%s", "carol");
    fx.svc.players.push_back(carol);
    fx.host.publish_player_joined(carol);
    fx.host.pump(1.0);
    CHECK(engine->stats().dispatches == 2);
    CHECK(fx.svc.logs >= 3);
}

}  // namespace

int main(int argc, char** argv) {
    std::printf("== plugin host smoke test ==\n");
    test_null_engine();
    test_validations();
    test_dispatch_and_budget_basics();
    test_capability_denial();
    test_api_budget();
    test_instruction_fault();
    test_read_write();
    test_not_found();
    test_api_for_missing();
    test_example_plugin(argc > 1 ? argv[1] : nullptr);
    std::printf("checks=%d failures=%d -> %s\n", g_checks, g_failures,
                g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
