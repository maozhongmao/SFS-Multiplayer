// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// DemoEngine — 一个最小但完整的 IJsEngine 参考实现（不依赖任何 JS 解释器）。
//
// 目的：在没有 QuickJS 的情况下，把"宿主如何驱动一个引擎"完整跑通，
// 并演示下面四件引擎必须自己管的事：
//   1. 装载：只吃字节，装载失败返回 false（宿主会拒绝装载）。
//   2. 预算：每轮 dispatch 前由宿主下发指令预算；超预算必须中断当次执行并返回 false。
//   3. 白名单：所有"脚本调用"都走 PluginHost 给的那张 HostApi 函数表 + ctx，
//      引擎自己不碰服务端，能力拒绝（-1）也要如实记录。
//   4. 回收：collect_garbage / unload 的生命周期。
//
// 它不识真正的 JavaScript：load_bytecode 把输入当成"命令表"，
// 只执行以 "// @demo:" 开头的行（见 demo/example_plugin.js 顶部说明）。
// 这样同一份 .js 里既有给 QuickJS 用的真源码，又有 DemoEngine 能跑的镜像。
// 真正的 QuickJS 接入点见 src/plugins/quickjs_engine.hpp 与 docs/plugin-system.md 第 7 节。

#pragma once

#include "plugins/ijs_engine.hpp"
#include "plugins/plugin_host.hpp"  // HostApi

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sfs::plugins::demo {

// 引擎自省数据：测试与 demo_host 用它验证"预算/白名单/故障"确实生效。
struct DemoEngineStats {
    std::size_t   program_bytes = 0;      // 最近一次装载的输入字节数
    std::size_t   command_count = 0;      // 解析出的可执行命令条数
    std::size_t   memory_used_bytes = 0;  // DemoEngine 口径的常驻内存
    std::uint64_t dispatches = 0;         // 成功进入的投递次数
    std::uint64_t instructions_last_dispatch = 0;  // 最近一轮消耗的指令数
    std::uint32_t host_calls = 0;         // 调过多少次宿主 API
    std::int32_t  last_host_status = 0;   // 最近一次宿主 API 返回码
    std::uint32_t capability_denials = 0; // 返回 kHostErrNoCapability 的累计次数
    std::uint32_t budget_denials = 0;     // 返回 kHostErrBudget 的累计次数
    std::uint32_t not_found = 0;          // 返回 kHostErrNotFound 的累计次数
    std::uint32_t faults = 0;             // 超预算/运行错误导致 dispatch 失败的次数
    bool api_bound = false;               // 是否已 bind_host_api()
    bool faulted = false;                 // 引擎自身是否进入终态
};

class DemoEngine final : public IJsEngine {
public:
    DemoEngine();
    ~DemoEngine() override;
    DemoEngine(const DemoEngine&) = delete;
    DemoEngine& operator=(const DemoEngine&) = delete;

    // ---- IJsEngine ----
    const char* name() const override;
    bool load_bytecode(const std::uint8_t* data, std::size_t size) override;
    bool set_memory_limit(std::size_t bytes) override;
    bool set_max_stack_size(std::size_t bytes) override;
    void set_instruction_budget(std::uint64_t budget) override;
    std::uint64_t instructions_used() const override;
    bool dispatch(const PluginEvent& event) override;
    void collect_garbage() override;
    void unload() override;

    // 绑定宿主能力白名单。PluginHost::load() 成功后 ctx 才有效，
    // 所以这是 IJsEngine 之外的"装载后接线"一步：见 docs/plugin-example.md 第 4 节。
    void bind_host_api(const HostApi& api);

    const DemoEngineStats& stats() const;

private:
    std::unique_ptr<struct DemoEngineImpl> impl_;
};

// 引擎工厂：每个插件一份独立引擎实例，与 IJsEngine 的"一插件一运行时"一致。
// 同时记录最近创建的引擎，方便装载后把 host.api_for(id) 接上去。
class DemoEngineFactory final : public IJsEngineFactory {
public:
    std::unique_ptr<IJsEngine> create() override;
    // 最近一次 create() 产出的引擎（仍由 PluginHost 持有，unload 后失效）。
    DemoEngine* last_engine() const { return last_; }
    void clear() { last_ = nullptr; }

private:
    DemoEngine* last_ = nullptr;
};

}  // namespace sfs::plugins::demo
