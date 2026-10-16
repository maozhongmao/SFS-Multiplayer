// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - PluginHost 实现（引擎无关）。
//
// 本翻译单元始终参与编译（保证骨架不腐烂），但在默认 SFS_PLUGINS=0 时
// 没有任何调用者，配合 -ffunction-sections -fdata-sections + --gc-sections
// 整段不可达、被链接器回收，产物行为与未引入插件系统时一致。
// 详见 docs/plugin-system.md 第 5 节的验证方法。

#include "plugins/plugin_host.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <utility>

namespace sfs::plugins {

// ---------------------------------------------------------- 枚举字符串（全 ASCII）

const char* to_string(PluginState state) {
    switch (state) {
        case PluginState::kUnloaded: return "unloaded";
        case PluginState::kLoaded:   return "loaded";
        case PluginState::kRunning:  return "running";
        case PluginState::kFaulted:  return "faulted";
    }
    return "unknown";
}

const char* to_string(HostStatus status) {
    switch (status) {
        case kHostOk:              return "ok";
        case kHostErrNoCapability: return "no-capability";
        case kHostErrBadArgument:  return "bad-argument";
        case kHostErrNotFound:     return "not-found";
        case kHostErrBudget:       return "budget-exhausted";
        case kHostErrUnavailable:  return "unavailable";
        case kHostErrState:        return "bad-state";
    }
    return "unknown";
}

namespace {

// 插件 id 只允许 ASCII [A-Za-z0-9_.-]，1..32 字符：它会进日志，不能带非 ASCII。
bool is_ascii_id(const std::string& id) {
    if (id.empty() || id.size() > 32) return false;
    for (char ch : id) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

// 插件给的文本 -> 保证 ASCII 的日志片段。非 ASCII / 控制字符一律换成 '?'，
// 这是"任何进入 stdout/Player.log 的字符串必须是 ASCII"的最后一道闸。
std::string to_ascii(const char* text, std::size_t max_bytes) {
    std::string out;
    if (text == nullptr) return out;
    out.reserve(max_bytes < 96u ? max_bytes : 96u);
    for (std::size_t i = 0; i < max_bytes && text[i] != '\0'; ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x20u && c <= 0x7Eu) out.push_back(static_cast<char>(c));
        else if (c == '\t') out.push_back(' ');
        else out.push_back('?');
    }
    return out;
}

}  // namespace

// ---------------------------------------------------------- 内部结构

struct PluginHost::Binding {
    PluginHost* host = nullptr;
    Plugin* plugin = nullptr;  // 该 API 表归属的插件（每个插件一份 Binding）
};

struct PluginHost::Plugin {
    std::string id;
    std::uint32_t capabilities = kCapNone;
    PluginLimits limits{};
    PluginState state = PluginState::kUnloaded;
    std::uint32_t subscriptions = kEventNone;
    // 每轮投递重置的两个预算计数器。
    std::uint32_t api_calls_in_phase = 0;
    std::uint32_t writes_in_phase = 0;
    std::uint64_t faults = 0;
    Binding binding{};
    std::unique_ptr<IJsEngine> engine;
};

// ---------------------------------------------------------- 生命周期

PluginHost::PluginHost(HostServices* services, IJsEngineFactory* factory)
    : services_(services) {
    if (factory != nullptr) {
        factory_ = factory;
    } else {
        // 默认工厂 = 空引擎：就算有人构造了宿主，load() 也必然失败。
        owned_factory_.reset(new NullJsEngineFactory());
        factory_ = owned_factory_.get();
    }
}

PluginHost::~PluginHost() { shutdown(); }

void PluginHost::shutdown() {
    for (auto& p : plugins_) {
        if (p && p->engine) p->engine->unload();
    }
    plugins_.clear();
    pending_events_.clear();
    pending_world_ = WorldSnapshot{};
    has_pending_world_ = false;
    dropped_events_ = 0;
    next_delivery_seconds_ = 0.0;
}

PluginHost::Plugin* PluginHost::find(const std::string& plugin_id) {
    for (auto& p : plugins_) {
        if (p && p->id == plugin_id) return p.get();
    }
    return nullptr;
}

const PluginHost::Plugin* PluginHost::find(const std::string& plugin_id) const {
    for (const auto& p : plugins_) {
        if (p && p->id == plugin_id) return p.get();
    }
    return nullptr;
}

PluginState PluginHost::state_of(const std::string& plugin_id) const {
    const Plugin* p = find(plugin_id);
    return p ? p->state : PluginState::kUnloaded;
}

std::uint64_t PluginHost::fault_count(const std::string& plugin_id) const {
    const Plugin* p = find(plugin_id);
    return p ? p->faults : 0u;
}

std::int32_t PluginHost::trampoline_call(void* ctx, const char* op, const char* arg, char* out,
                                        std::int32_t out_cap) {
    Binding* b = static_cast<Binding*>(ctx);
    if (b == nullptr || b->host == nullptr || b->plugin == nullptr) return kHostErrState;
    PluginHost* host = b->host;
    Plugin* p = b->plugin;
    if (p->state != PluginState::kRunning) return kHostErrState;

    // op -> 能力位（查询类 op 不需要能力位）
    std::uint32_t need = 0u;
    if (std::strcmp(op, "kick") == 0 || std::strcmp(op, "clearDebris") == 0 ||
        std::strcmp(op, "setTimeScale") == 0) need = kCapAdmin;
    else if (std::strcmp(op, "readRocket") == 0) need = kCapReadRocket;
    else if (std::strcmp(op, "writeRocket") == 0) need = kCapWriteRocket;
    else if (std::strcmp(op, "worldTime") != 0 && std::strcmp(op, "serverInfo") != 0) return kHostErrBadArgument;
    if (need != 0u && (p->capabilities & need) == 0u) return kHostErrNoCapability;

    const std::int32_t budget = host->consume_api_budget(p);
    if (budget != kHostOk) return budget;
    if (host->services_ == nullptr) return kHostErrUnavailable;
    return host->services_->call(op, arg, out, out_cap);
}

HostApi PluginHost::api_for(const std::string& plugin_id) {
    HostApi api;
    // 函数指针始终有效；插件不存在时 ctx=nullptr，各函数返回 kHostErrState。
    api.read_rocket  = &PluginHost::trampoline_read_rocket;
    api.write_rocket = &PluginHost::trampoline_write_rocket;
    api.send_message = &PluginHost::trampoline_send_message;
    api.log          = &PluginHost::trampoline_log;
    api.call         = &PluginHost::trampoline_call;
    Plugin* p = find(plugin_id);
    if (p != nullptr) {
        p->binding.host = this;
        p->binding.plugin = p;
        api.ctx = &p->binding;
    }
    return api;
}

// ---------------------------------------------------------- 装载 / 卸载 / 订阅

bool PluginHost::load(const PluginDescriptor& descriptor, std::string* error) {
    const auto fail = [error](const char* msg) {
        if (error != nullptr) *error = msg;
        return false;
    };

    if (!is_ascii_id(descriptor.id))
        return fail("invalid id: ascii [A-Za-z0-9_.-], 1..32 chars");
    if (find(descriptor.id) != nullptr) return fail("id already loaded");
    if ((descriptor.capabilities & ~kCapAll) != 0u) return fail("unknown capability bit");
    if (descriptor.bytecode.empty()) return fail("empty bytecode");
    if (descriptor.bytecode.size() > kMaxBytecodeBytes) return fail("bytecode too large");
    if (descriptor.limits.memory_limit_bytes == 0u ||
        descriptor.limits.memory_limit_bytes > kMaxMemoryLimitBytes)
        return fail("memory limit out of range");
    if (descriptor.limits.max_stack_bytes == 0u ||
        descriptor.limits.max_stack_bytes > kMaxStackBytes)
        return fail("stack limit out of range");
    if (descriptor.limits.instruction_budget == 0u ||
        descriptor.limits.instruction_budget > kMaxInstructionBudget)
        return fail("instruction budget out of range");
    if (descriptor.limits.max_api_calls == 0u ||
        descriptor.limits.max_api_calls > kMaxApiCallsPerDispatch)
        return fail("api call budget out of range");
    if (factory_ == nullptr) return fail("no engine factory");

    std::unique_ptr<IJsEngine> engine = factory_->create();
    if (!engine) return fail("engine factory returned null");
    // 先下发资源上限，再载入字节码：引擎在解析/建堆之前就知道天花板。
    if (!engine->set_memory_limit(descriptor.limits.memory_limit_bytes))
        return fail("engine rejected memory limit");
    if (!engine->set_max_stack_size(descriptor.limits.max_stack_bytes))
        return fail("engine rejected stack limit");
    if (!engine->load_bytecode(descriptor.bytecode.data(), descriptor.bytecode.size()))
        return fail("engine rejected bytecode");

    std::unique_ptr<Plugin> plugin(new Plugin());
    plugin->id = descriptor.id;
    plugin->capabilities = descriptor.capabilities;
    plugin->limits = descriptor.limits;
    plugin->state = PluginState::kLoaded;
    plugin->binding.host = this;
    plugin->binding.plugin = plugin.get();
    plugin->engine = std::move(engine);

    plugins_.push_back(std::move(plugin));
    // 【顺序很关键】HostApi 必须在插件注册进 plugins_ 之后再交给引擎：
    // api_for() 内部靠 find(plugin_id) 拿绑定对象，早于注册就只会得到 ctx=nullptr，
    // 于是插件所有 sfs.* 调用都静默失败（不报错、无输出，极难发现）。
    if (plugins_.back() && plugins_.back()->engine) {
        plugins_.back()->engine->set_host_api(api_for(descriptor.id));
    }
    return true;
}

bool PluginHost::unload(const std::string& plugin_id, std::string* error) {
    for (auto it = plugins_.begin(); it != plugins_.end(); ++it) {
        if ((*it) && (*it)->id == plugin_id) {
            if ((*it)->engine) (*it)->engine->unload();
            plugins_.erase(it);
            return true;
        }
    }
    if (error != nullptr) *error = "no such plugin";
    return false;
}

bool PluginHost::subscribe(const std::string& plugin_id, std::uint32_t event_mask,
                           std::string* error) {
    const auto fail = [error](const char* msg) {
        if (error != nullptr) *error = msg;
        return false;
    };
    Plugin* p = find(plugin_id);
    if (p == nullptr) return fail("no such plugin");
    if ((event_mask & ~kEventAll) != 0u) return fail("unknown event bit");
    if (p->state != PluginState::kLoaded && p->state != PluginState::kRunning)
        return fail("plugin not subscribable");
    p->subscriptions = event_mask;
    p->state = (event_mask != kEventNone) ? PluginState::kRunning : PluginState::kLoaded;
    return true;
}

// ---------------------------------------------------------- 事件总线（批量、限流）

void PluginHost::publish_world_snapshot(const WorldSnapshot& snapshot) {
    if (plugins_.empty()) return;  // 零开销：没插件连拷贝都不做
    pending_world_ = snapshot;
    has_pending_world_ = true;
}

void PluginHost::publish_player_joined(const PlayerView& player) {
    if (plugins_.empty()) return;
    if (pending_events_.size() >= kMaxPendingEvents) {
        ++dropped_events_;
        return;
    }
    PendingEvent e;
    e.kind = kEventPlayerJoined;
    e.player = player;
    pending_events_.push_back(std::move(e));
}

void PluginHost::publish_player_left(const PlayerView& player) {
    if (plugins_.empty()) return;
    if (pending_events_.size() >= kMaxPendingEvents) {
        ++dropped_events_;
        return;
    }
    PendingEvent e;
    e.kind = kEventPlayerLeft;
    e.player = player;
    pending_events_.push_back(std::move(e));
}

void PluginHost::publish_command(const char* command) {
    if (plugins_.empty() || command == nullptr) return;
    if (pending_events_.size() >= kMaxPendingEvents) { ++dropped_events_; return; }
    PendingEvent e;
    e.kind = kEventCommand;
    std::size_t n = 0;
    while (n + 1u < kMaxTextBytes && command[n] != '\0') ++n;
    e.text.assign(command, n);
    pending_events_.push_back(std::move(e));
}

void PluginHost::publish_chat(std::int32_t player_id, const char* utf8_text) {
    if (plugins_.empty()) return;
    if (pending_events_.size() >= kMaxPendingEvents) {
        ++dropped_events_;
        return;
    }
    PendingEvent e;
    e.kind = kEventChatMessage;
    e.player.player_id = player_id;
    if (utf8_text != nullptr) {
        std::size_t n = 0;
        while (n + 1u < kMaxTextBytes && utf8_text[n] != '\0') ++n;
        e.text.assign(utf8_text, n);
    }
    pending_events_.push_back(std::move(e));
}

void PluginHost::pump(double now_seconds) {
    if (plugins_.empty()) return;

    bool any_running = false;
    for (const auto& p : plugins_) {
        if (p && p->state == PluginState::kRunning && p->subscriptions != kEventNone) {
            any_running = true;
            break;
        }
    }
    if (!any_running) {
        // 没有活着的订阅者：丢掉积压，避免过期事件在插件恢复后突然灌进去。
        pending_world_ = WorldSnapshot{};
        has_pending_world_ = false;
        pending_events_.clear();
        dropped_events_ = 0;
        return;
    }
    // 粗粒度节拍：每秒最多投递一次，世界快照/离散事件在这一刻一起批量交付。
    if (now_seconds < next_delivery_seconds_) return;
    next_delivery_seconds_ = now_seconds + kDefaultDeliveryIntervalSeconds;

    if (has_pending_world_) {
        PluginEvent event;
        event.kind = kEventWorldSnapshot;
        event.world = &pending_world_;
        deliver(event);
        has_pending_world_ = false;
        pending_world_.rockets.clear();
        pending_world_.players.clear();
    }

    for (const PendingEvent& pe : pending_events_) {
        PluginEvent event;
        event.kind = pe.kind;
        // 注意：chat 事件也要带上 player —— 插件靠 player.playerId（控制台为 -1）
        // 区分"控制台命令"和"玩家聊天"。以前这里只填 text，插件永远收不到来源。
        if (pe.kind == kEventChatMessage || pe.kind == kEventCommand) event.text = pe.text.c_str();
        if (pe.kind == kEventCommand) event.command = pe.text.c_str();
        event.player = &pe.player;
        deliver(event);
    }
    pending_events_.clear();
    dropped_events_ = 0;
}

void PluginHost::deliver(const PluginEvent& event) {
    for (auto& holder : plugins_) {
        Plugin* p = holder.get();
        if (p == nullptr || p->state != PluginState::kRunning) continue;
        if ((p->subscriptions & static_cast<std::uint32_t>(event.kind)) == 0u) continue;
        if (!p->engine) continue;

        // 每轮重置两个预算：指令预算交给引擎的中断回调，API 预算在宿主侧扣。
        p->api_calls_in_phase = 0;
        p->writes_in_phase = 0;
        p->engine->set_instruction_budget(p->limits.instruction_budget);
        const bool ok = p->engine->dispatch(event);
        p->engine->collect_garbage();
        if (!ok) {
            p->state = PluginState::kFaulted;
            ++p->faults;
            log_line(std::string("[plugins] plugin=") + p->id +
                     " faulted: instruction budget or runtime error; event dropped");
        }
    }
}

// ---------------------------------------------------------- 宿主 API（能力白名单 + 边界检查）

std::int32_t PluginHost::consume_api_budget(Plugin* p) {
    if (p->api_calls_in_phase >= p->limits.max_api_calls) return kHostErrBudget;
    ++p->api_calls_in_phase;
    return kHostOk;
}

std::int32_t PluginHost::api_read_rocket(Binding* b, std::int32_t rocket_id, RocketView* out) {
    if (b == nullptr || b->plugin == nullptr || b->host == nullptr) return kHostErrState;
    Plugin* p = b->plugin;
    if (!has_capability(p->capabilities, kCapReadRocket)) return kHostErrNoCapability;
    if (rocket_id < 0 || out == nullptr) return kHostErrBadArgument;
    if (p->state != PluginState::kRunning) return kHostErrState;
    if (services_ == nullptr) return kHostErrUnavailable;
    const std::int32_t budget = consume_api_budget(p);
    if (budget != kHostOk) return budget;
    *out = RocketView{};
    if (!services_->read_rocket(rocket_id, out)) return kHostErrNotFound;
    return kHostOk;
}

std::int32_t PluginHost::api_write_rocket(Binding* b, const RocketView* view) {
    if (b == nullptr || b->plugin == nullptr || b->host == nullptr) return kHostErrState;
    Plugin* p = b->plugin;
    if (!has_capability(p->capabilities, kCapWriteRocket)) return kHostErrNoCapability;
    if (view == nullptr || view->rocket_id < 0) return kHostErrBadArgument;
    if (view->part_count < 0 || view->part_count > 4096) return kHostErrBadArgument;
    if (view->throttle < 0.0 || view->throttle > 1.0) return kHostErrBadArgument;
    // 非有限数（NaN/Inf）直接拒绝：它们会顺着物理同步污染所有客户端。
    if (!std::isfinite(view->pos_x) || !std::isfinite(view->pos_y) || !std::isfinite(view->pos_z))
        return kHostErrBadArgument;
    if (!std::isfinite(view->vel_x) || !std::isfinite(view->vel_y) || !std::isfinite(view->vel_z))
        return kHostErrBadArgument;
    if (p->state != PluginState::kRunning) return kHostErrState;
    if (services_ == nullptr) return kHostErrUnavailable;
    if (p->writes_in_phase >= kMaxWritesPerDispatch) return kHostErrBudget;
    const std::int32_t budget = consume_api_budget(p);
    if (budget != kHostOk) return budget;
    if (!services_->write_rocket(*view)) return kHostErrNotFound;
    ++p->writes_in_phase;
    return kHostOk;
}

std::int32_t PluginHost::api_send_message(Binding* b, std::int32_t player_id,
                                          const char* utf8_text) {
    if (b == nullptr || b->plugin == nullptr || b->host == nullptr) return kHostErrState;
    Plugin* p = b->plugin;
    if (!has_capability(p->capabilities, kCapSendMessage)) return kHostErrNoCapability;
    if (player_id < 0 || utf8_text == nullptr) return kHostErrBadArgument;
    // 发给客户端的是 UTF-8（可以带中文），但拒绝控制字符并把总长卡死，
    // 避免插件往聊天/日志里塞不可见控制序列或超长文本。
    std::size_t len = 0;
    while (utf8_text[len] != '\0') {
        if (len + 1u >= kMaxTextBytes) return kHostErrBadArgument;
        if (static_cast<unsigned char>(utf8_text[len]) < 0x20u) return kHostErrBadArgument;
        ++len;
    }
    if (p->state != PluginState::kRunning) return kHostErrState;
    if (services_ == nullptr) return kHostErrUnavailable;
    const std::int32_t budget = consume_api_budget(p);
    if (budget != kHostOk) return budget;
    if (!services_->send_message(player_id, utf8_text)) return kHostErrNotFound;
    return kHostOk;
}

std::int32_t PluginHost::api_log(Binding* b, const char* utf8_text) {
    if (b == nullptr || b->plugin == nullptr || b->host == nullptr) return kHostErrState;
    Plugin* p = b->plugin;
    if (!has_capability(p->capabilities, kCapLog)) return kHostErrNoCapability;
    if (utf8_text == nullptr) return kHostErrBadArgument;
    // 日志出口不要求 services_：没有适配层时退回 stdout，但仍然是 ASCII。
    const std::int32_t budget = consume_api_budget(p);
    if (budget != kHostOk) return budget;
    log_line("[plugin:" + p->id + "] " + to_ascii(utf8_text, kMaxTextBytes - 1u));
    return kHostOk;
}

// ---------------------------------------------------------- HostApi 跳板

std::int32_t PluginHost::trampoline_read_rocket(void* ctx, std::int32_t rocket_id,
                                                RocketView* out) {
    Binding* b = static_cast<Binding*>(ctx);
    if (b == nullptr || b->host == nullptr) return kHostErrState;
    return b->host->api_read_rocket(b, rocket_id, out);
}

std::int32_t PluginHost::trampoline_write_rocket(void* ctx, const RocketView* view) {
    Binding* b = static_cast<Binding*>(ctx);
    if (b == nullptr || b->host == nullptr) return kHostErrState;
    return b->host->api_write_rocket(b, view);
}

std::int32_t PluginHost::trampoline_send_message(void* ctx, std::int32_t player_id,
                                                 const char* utf8_text) {
    Binding* b = static_cast<Binding*>(ctx);
    if (b == nullptr || b->host == nullptr) return kHostErrState;
    return b->host->api_send_message(b, player_id, utf8_text);
}

std::int32_t PluginHost::trampoline_log(void* ctx, const char* utf8_text) {
    Binding* b = static_cast<Binding*>(ctx);
    if (b == nullptr || b->host == nullptr) return kHostErrState;
    return b->host->api_log(b, utf8_text);
}

// ---------------------------------------------------------- 日志出口

void PluginHost::log_line(const std::string& line) {
    if (services_ != nullptr) {
        services_->log(line.c_str());
        return;
    }
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
}

}  // namespace sfs::plugins
