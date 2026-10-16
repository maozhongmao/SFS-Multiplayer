// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - JS 引擎抽象（引擎无关边界）。
//
// 宿主框架只认 IJsEngine；QuickJS 只是它的一个实现（TODO，见
// plugins/quickjs_engine.hpp 与 docs/plugin-system.md）。
//
// 设计约束（对齐 armv7/musl 目标板）:
//   * 单运行时、单线程；一个插件一份引擎实例，互不共享堆。
//   * 只提供 load_bytecode，没有"执行源码字符串"的入口 —— 板上不做 JS 解析。
//   * 内存 / 栈 / 指令预算由宿主在装载与每轮 dispatch 时下发。

#pragma once

#include "plugins/plugin_events.hpp"
#include "plugins/plugin_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

namespace sfs::plugins {

class IJsEngine {
public:
    virtual ~IJsEngine() = default;

    // 引擎名（ASCII），用于日志。
    virtual const char* name() const = 0;

    // 宿主 API 表（按值拷贝，由引擎自己持有）。
    virtual void set_host_api(const HostApi& api) = 0;

    // 只接受编译好的字节码（QuickJS: JS_ReadObject + JS_READ_OBJ_BYTECODE）。
    // 返回 false = 字节码非法/版本不符；宿主会把插件置为 kFaulted。
    virtual bool load_bytecode(const std::uint8_t* data, std::size_t size) = 0;

    // 资源上限下发（QuickJS: JS_SetMemoryLimit / JS_SetMaxStackSize）。
    virtual bool set_memory_limit(std::size_t bytes) = 0;
    virtual bool set_max_stack_size(std::size_t bytes) = 0;

    // 每轮投递的指令预算（QuickJS: JS_SetInterruptHandler 按计数中断）。
    // 引擎必须在超预算时中断当前执行，并让当次 dispatch 返回 false。
    virtual void set_instruction_budget(std::uint64_t budget) = 0;
    virtual std::uint64_t instructions_used() const = 0;

    // 投递一个批量事件；返回 false = 运行错误或超预算。
    virtual bool dispatch(const PluginEvent& event) = 0;

    // 一轮结束后的回收（QuickJS: JS_RunGC）。
    virtual void collect_garbage() = 0;

    // 释放运行时；析构前必须调用（析构兜底一次）。
    virtual void unload() = 0;
};

// 空引擎：默认实现。拒绝一切字节码、丢弃一切事件，因此"默认不启用"时
// 插件框架不可能执行任何插件代码。所有方法都不分配内存。
class NullJsEngine final : public IJsEngine {
public:
    const char* name() const override { return "null"; }
    void set_host_api(const HostApi&) override {}
    bool load_bytecode(const std::uint8_t*, std::size_t) override { return false; }
    bool set_memory_limit(std::size_t) override { return true; }
    bool set_max_stack_size(std::size_t) override { return true; }
    void set_instruction_budget(std::uint64_t) override {}
    std::uint64_t instructions_used() const override { return 0; }
    bool dispatch(const PluginEvent&) override { return false; }
    void collect_garbage() override {}
    void unload() override {}
};

// 引擎工厂：每个插件一份独立运行时。默认工厂产出 NullJsEngine，
// 所以即使有人误构造 PluginHost 并 load()，也只会失败、不会跑插件。
class IJsEngineFactory {
public:
    virtual ~IJsEngineFactory() = default;
    virtual std::unique_ptr<IJsEngine> create() = 0;
};

class NullJsEngineFactory final : public IJsEngineFactory {
public:
    std::unique_ptr<IJsEngine> create() override {
        return std::unique_ptr<IJsEngine>(new NullJsEngine());
    }
};

}  // namespace sfs::plugins
