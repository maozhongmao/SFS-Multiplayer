// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - 引擎无关的插件宿主 (PluginHost)。
//
// 职责:
//   * 装载/卸载与生命周期 (PluginState)
//   * 每插件内存上限 + 指令预算 + 宿主 API 调用预算
//   * 粗粒度事件总线: 每秒一次批量投递世界快照, 不做每帧回调
//   * 能力白名单: 插件只能调用 HostApi 表里的函数, 每次调用都过边界检查
//
// 默认关闭的保证: 本类默认用 NullJsEngineFactory, load() 必然失败;
// 服务端主循环只有在 SFS_PLUGINS=1 时才引用本类 (见 plugin_bootstrap.hpp)。
// 未启用时这些函数不可达, 链接期被 --gc-sections 整段回收, 产物行为不变。

#pragma once

#include "plugins/ijs_engine.hpp"
#include "plugins/plugin_events.hpp"
#include "plugins/plugin_types.hpp"
#include "plugins/quickjs_engine.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sfs::plugins {

// 插件元数据 + 字节码。id 只允许 ASCII 可打印字符 (宿主会校验),
// 因为它会进日志 (TMP 无中文字形)。
struct PluginDescriptor {
    std::string id;
    std::uint32_t capabilities = kCapNone;
    PluginLimits limits{};
    std::vector<std::uint8_t> bytecode;  // 编译后的字节码, 不是源码
};

// 服务端适配层: 由服务端实现, 把宿主 API 落到真实世界。
// 骨架阶段可以是 nullptr —— 所有能力调用返回 kHostErrUnavailable。
// 必须在主循环线程调用 (插件没有自己的线程)。
class HostServices {
public:
    virtual ~HostServices() = default;

    // 读火箭: 成功填 *out 返回 true; 不存在返回 false。
    virtual bool read_rocket(std::int32_t rocket_id, RocketView* out) = 0;
    // 写回火箭: 仅当该 id 已存在才允许; 返回 false = 拒绝 (越权/不存在)。
    virtual bool write_rocket(const RocketView& view) = 0;
    // 给玩家发系统消息 (text 已由宿主限长)。
    virtual bool send_message(std::int32_t player_id, const char* utf8_text) = 0;
    // 写服务端日志 (ascii_text 已保证 ASCII、已限长)。
    virtual void log(const char* ascii_text) = 0;
};

// HostApi 的定义已移到 plugin_types.hpp（那里不会被循环包含）。

class PluginHost {
public:
    // 默认用 NullJsEngineFactory: load() 必然失败, pump() 不做任何事。
    PluginHost(HostServices* services = nullptr, IJsEngineFactory* factory = nullptr);
    ~PluginHost();
    PluginHost(const PluginHost&) = delete;
    PluginHost& operator=(const PluginHost&) = delete;

    // 主循环投递节拍 (秒)。世界快照等事件最多按此频率进入插件。
    static constexpr double kDefaultDeliveryIntervalSeconds = 1.0;

    // 取该插件的宿主 API 表 (ctx 已绑定到它)。插件不存在时 ctx=nullptr,
    // 调用任一函数返回 kHostErrState。引擎绑定层应在装载时缓存这张表。
    HostApi api_for(const std::string& plugin_id);

    // 装载: 校验 id/能力位/上限/字节码 -> 建引擎 -> 下发内存/栈上限 -> 载入字节码。
    // 失败时不留下半成品, error (可空) 带 ASCII 原因。
    bool load(const PluginDescriptor& descriptor, std::string* error = nullptr);
    bool unload(const std::string& plugin_id, std::string* error = nullptr);

    // 订阅事件位图; 只有 kLoaded/kRunning 的插件能订阅。
    bool subscribe(const std::string& plugin_id, std::uint32_t event_mask,
                   std::string* error = nullptr);

    // 服务端按业务节奏推入"最新世界快照"(覆盖式, 不排队)。
    // 真正的投递发生在 pump(), 且被 kDefaultDeliveryIntervalSeconds 限流。
    void publish_world_snapshot(const WorldSnapshot& snapshot);
    // 离散小事件同样只在 pump() 投递, 保证插件代码只在固定节拍被调用。
    void publish_player_joined(const PlayerView& player);
    void publish_player_left(const PlayerView& player);
    void publish_chat(std::int32_t player_id, const char* utf8_text);

    // 主循环每轮调用一次。没有已订阅插件时直接返回 (一次空判断)。
    void pump(double now_seconds);

    // 卸载全部插件 (服务端退出时调用; 幂等)。
    void shutdown();

    std::size_t plugin_count() const { return plugins_.size(); }
    bool is_enabled() const { return !plugins_.empty(); }
    PluginState state_of(const std::string& plugin_id) const;
    // 自装载以来该插件触发预算/运行错误的次数 (供 status 命令)。
    std::uint64_t fault_count(const std::string& plugin_id) const;

private:
    struct Plugin;
    struct Binding;

    // 待投递的离散事件 (有界)。
    struct PendingEvent {
        EventKind kind = kEventNone;
        PlayerView player{};
        std::string text;  // kEventChatMessage, 已限长
    };

    // HostApi 的 C 函数指针实现: 从 ctx 取回 Binding, 转成员函数。
    static std::int32_t trampoline_read_rocket(void* ctx, std::int32_t rocket_id, RocketView* out);
    static std::int32_t trampoline_write_rocket(void* ctx, const RocketView* view);
    static std::int32_t trampoline_send_message(void* ctx, std::int32_t player_id, const char* utf8_text);
    static std::int32_t trampoline_log(void* ctx, const char* utf8_text);

    Plugin* find(const std::string& plugin_id);
    const Plugin* find(const std::string& plugin_id) const;
    std::int32_t api_read_rocket(Binding* b, std::int32_t rocket_id, RocketView* out);
    std::int32_t api_write_rocket(Binding* b, const RocketView* view);
    std::int32_t api_send_message(Binding* b, std::int32_t player_id, const char* utf8_text);
    std::int32_t api_log(Binding* b, const char* utf8_text);
    std::int32_t consume_api_budget(Plugin* p);

    // 把事件投递给所有订阅了该类型且处于 kRunning 的插件。
    void deliver(const PluginEvent& event);
    // ASCII 日志出口: 有 HostServices 走它, 否则退回 stdout。
    void log_line(const std::string& line);

    HostServices* services_ = nullptr;
    std::unique_ptr<IJsEngineFactory> owned_factory_;
    IJsEngineFactory* factory_ = nullptr;
    std::vector<std::unique_ptr<Plugin>> plugins_;

    WorldSnapshot pending_world_;
    bool has_pending_world_ = false;
    std::vector<PendingEvent> pending_events_;
    std::uint32_t dropped_events_ = 0;
    double next_delivery_seconds_ = 0.0;
};

}  // namespace sfs::plugins
