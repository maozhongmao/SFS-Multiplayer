// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - QuickJS / quickjs-ng 接入点（TODO，任务 #12 只做骨架，不引入外部依赖）。
//
// 默认 SFS_PLUGINS_QUICKJS=0：本文件不引用任何 quickjs 头，默认构建既不需要 SDK、
// 也不下载依赖，二进制里不会多出任何 QuickJS 符号。
//
// 真正接入时:
//   1. 取 quickjs-ng 源码放进 Server/cpp/src/third_party/quickjs（离线放入，不在构建里下载）。
//   2. 交叉编译到 armv7 musl（命令行要点见 docs/plugin-system.md 第 6 节）。
//   3. 把下面 TODO 处补全，并在服务端启动处安装 QuickJsEngineFactory。
//
// 安全基线（与任务 #12 的要求一致）:
//   * 只跑字节码：宿主不暴露 JS_Eval 源码入口，磁盘上只有 compile 后的 .qbc。
//   * 无文件/网络 API：QuickJS 自带 std/os 模块一律不注册，只注册 HostApi 白名单。
//   * 超时中断：JS_SetInterruptHandler 每 N 条指令回调一次，预算用尽返回 1 终止。

#pragma once

#include "plugins/ijs_engine.hpp"

#ifndef SFS_PLUGINS_QUICKJS
#define SFS_PLUGINS_QUICKJS 0
#endif

#if SFS_PLUGINS_QUICKJS

#include "quickjs.h"

namespace sfs::plugins {

// TODO(#12): 按 IJsEngine 实现。要点:
//   class QuickJsEngine final : public IJsEngine {
//       JSRuntime* rt_ = nullptr;
//       JSContext* ctx_ = nullptr;
//       JSValue handler_{};
//       std::uint64_t budget_ = 0, used_ = 0;
//       bool interrupted_ = false;
//   };
//   load_bytecode:
//       handler_ = JS_ReadObject(ctx_, data, size, JS_READ_OBJ_BYTECODE);
//       // 约定: 字节码求值结果必须是函数（模块入口），否则视为非法插件
//   dispatch:
//       used_ = 0; interrupted_ = false;
//       JSValue ev = snapshot_to_js(ctx_, event);   // 从 WorldSnapshot 构造批量事件对象
//       JS_Call(ctx_, handler_, JS_UNDEFINED, 1, &ev);
//       JS_FreeValue(ctx_, ev);
//       return !interrupted_ && !JS_HasException(ctx_);
//   set_memory_limit:      JS_SetMemoryLimit(rt_, bytes)
//   set_max_stack_size:    JS_SetMaxStackSize(rt_, bytes)
//   set_instruction_budget: 保存 budget_；JS_SetInterruptHandler(rt_, interrupt_cb, this)
//       interrupt_cb: ++used_ >= budget_ ? 1 : 0   // 返回 1 = 中断执行
//   collect_garbage:       JS_RunGC(rt_)
//   unload:                JS_FreeValue(handler_) + JS_FreeContext + JS_FreeRuntime

// TODO(#12): class QuickJsEngineFactory final : public IJsEngineFactory { ... };

}  // namespace sfs::plugins

#endif  // SFS_PLUGINS_QUICKJS
