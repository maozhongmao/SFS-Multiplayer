// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - 公共基础类型（引擎无关）。
//
// 目标板 armv7/musl、ROM ~20MB、RAM 只有几百 KB 余量，所以这里全程:
//   * POD + 显式数量上限，不用 std::function / 异常 / RTTI 传递边界；
//   * 任何进入 Player.log / 服务端 stdout 的字符串都必须是 ASCII（TMP 无中文字形），
//     宿主在 log 出口统一做 ASCII 化与限长（见 plugin_host.cpp 的 to_ascii）。
//
// 本文件不依赖任何 JS 引擎，QuickJS 只是 IJsEngine 的一个实现。

#pragma once

#include <cstddef>
#include <cstdint>

namespace sfs::plugins {

// 能力位（能力白名单）。插件装载时必须显式声明所需能力位，
// 宿主 API 每次调用还会再校验一遍 —— 声明与实际调用双重约束。
enum Capability : std::uint32_t {
    kCapNone        = 0u,
    kCapReadRocket  = 1u << 0,  // sfs.readRocket(id)
    kCapWriteRocket = 1u << 1,  // sfs.writeRocket(view)
    kCapSendMessage = 1u << 2,  // sfs.sendMessage(playerId, text)
    kCapLog         = 1u << 3,  // sfs.log(text)
    kCapAdmin       = 1u << 4,  // 管理类：kick / clearDebris / setTimeScale
};
struct RocketView;  // 前置声明：HostApi 只用它的指针（完整定义在 plugin_events.hpp）

// 宿主 API 表: 交给引擎绑定层的一组 C 函数指针。ctx 是"每个插件一份"的
// 绑定对象, 能力位绑在它上面 —— 插件 A 拿不到插件 B 的 ctx。
// 每个函数进入真实逻辑前: 查能力位 -> 查参数边界 -> 扣 API 预算。
struct HostApi {
    void* ctx = nullptr;
    std::int32_t (*read_rocket)(void* ctx, std::int32_t rocket_id, RocketView* out) = nullptr;
    std::int32_t (*write_rocket)(void* ctx, const RocketView* view) = nullptr;
    std::int32_t (*send_message)(void* ctx, std::int32_t player_id, const char* utf8_text) = nullptr;
    std::int32_t (*log)(void* ctx, const char* utf8_text) = nullptr;
    // 通用调用通道：op 是固定白名单里的操作名，arg 是参数，out 是可选结果缓冲区。
    // 有了它，新增 API 只需在引擎加一个 JS 绑定 + 在服务端实现一个 op，
    // 不必再改 HostServices/HostApi/trampoline/api_for 这些样板。
    std::int32_t (*call)(void* ctx, const char* op, const char* arg, char* out,
                         std::int32_t out_cap) = nullptr;
};

constexpr std::uint32_t kCapAll =
    kCapReadRocket | kCapWriteRocket | kCapSendMessage | kCapLog | kCapAdmin;

inline bool has_capability(std::uint32_t caps, Capability cap) {
    return (caps & static_cast<std::uint32_t>(cap)) != 0u;
}

// 每个插件的硬上限。与 QuickJS 的映射关系:
//   memory_limit_bytes  -> JS_SetMemoryLimit   （引擎分配器硬上限）
//   max_stack_bytes     -> JS_SetMaxStackSize  （递归/栈深度上限）
//   instruction_budget  -> JS_SetInterruptHandler 的计数阈值（每轮投递）
//   max_api_calls       -> 宿主侧每轮投递允许的宿主 API 调用次数
// 目标设备余量小，默认值刻意保守；服务端可用 PluginDescriptor::limits 覆盖。
struct PluginLimits {
    std::size_t   memory_limit_bytes = 256u * 1024u;  // 每插件堆上限 256 KB
    std::size_t   max_stack_bytes    = 32u * 1024u;   // 栈上限 32 KB
    std::uint64_t instruction_budget = 200000u;       // 每轮投递指令预算
    std::uint32_t max_api_calls      = 1024u;         // 每轮投递 API 调用上限
};

// 服务端能接受的取值上限（防止描述符里写出把目标设备打爆的数字）。
constexpr std::size_t   kMaxMemoryLimitBytes   = 2u * 1024u * 1024u;
constexpr std::size_t   kMaxStackBytes         = 256u * 1024u;
constexpr std::uint64_t kMaxInstructionBudget  = 5000000u;
constexpr std::uint32_t kMaxApiCallsPerDispatch = 4096u;
// 单个插件字节码上限（常驻 RAM）。只跑字节码，不含源码解析。
constexpr std::size_t   kMaxBytecodeBytes      = 32u * 1024u;

// 插件生命周期（单线程：只有服务端主循环线程会改状态）。
enum class PluginState : std::uint8_t {
    kUnloaded = 0,  // 未装载
    kLoaded,        // 字节码已载入，尚未订阅事件
    kRunning,       // 已订阅，event pump 会投递
    kFaulted,       // 触发上限/运行错误，停止投递，等待卸载
};

// ASCII，可直接进日志。
const char* to_string(PluginState state);

// 事件类型位图：粗粒度、批量投递；绝不做每帧回调。
enum EventKind : std::uint32_t {
    kEventNone          = 0u,
    kEventWorldSnapshot = 1u << 0,  // 每秒一次的世界快照（批量）
    kEventPlayerJoined  = 1u << 1,
    kEventPlayerLeft    = 1u << 2,
    kEventChatMessage   = 1u << 3,
    kEventCommand       = 1u << 4,  // 控制台命令（commands.cpp 转发）
};
constexpr std::uint32_t kEventAll =
    kEventWorldSnapshot | kEventPlayerJoined | kEventPlayerLeft | kEventChatMessage | kEventCommand;

// 宿主 API 返回码：0 成功，负数是拒绝。所有入口先做边界检查再执行。
enum HostStatus : std::int32_t {
    kHostOk              = 0,
    kHostErrNoCapability = -1,  // 插件没有声明该能力位
    kHostErrBadArgument  = -2,  // 越界 id / 非法数值 / 超长文本
    kHostErrNotFound     = -3,  // 目标火箭或玩家不存在
    kHostErrBudget       = -4,  // 本轮 API 调用预算用尽
    kHostErrUnavailable  = -5,  // 宿主未接服务端适配层（骨架默认状态）
    kHostErrState        = -6,  // 插件当前不处于可调用状态
};

const char* to_string(HostStatus status);

// 文本上限（含结尾 NUL）。消息/日志都会被宿主按此截断。
constexpr std::size_t kMaxTextBytes = 256;
// 单轮投递里插件最多能提交的火箭写回数（防止一个事件里刷爆世界）。
constexpr std::uint32_t kMaxWritesPerDispatch = 64;
// 待投递的离散事件上限；超出只计数（dropped），不排队，保证内存有界。
constexpr std::uint32_t kMaxPendingEvents = 64;

}  // namespace sfs::plugins