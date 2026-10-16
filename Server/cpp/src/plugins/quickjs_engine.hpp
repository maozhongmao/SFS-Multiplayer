// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 插件系统 - QuickJS(quickjs-ng) 实现（头文件实现，不需要改构建收集规则）。
//
// 约束：每插件一份独立 JSRuntime/JSContext、单线程；只有 load_bytecode（板上不解析源码）；
// 只注册 sfs.* 四个函数（转发 HostApi，能力位/边界/预算在宿主侧检查）；不注册 std/os/bignum。
// 指令预算 = JS_SetInterruptHandler，超预算中断当次 dispatch 并返回 false。

#pragma once

#if defined(SFS_PLUGINS_QUICKJS)

#include "plugins/ijs_engine.hpp"  // 含 plugin_types.hpp ⇒ HostApi 定义可见

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "quickjs.h"
}

namespace sfs::plugins {

class QuickJsEngine final : public IJsEngine {
public:
    QuickJsEngine() = default;
    ~QuickJsEngine() override { unload(); }

    const char* name() const override { return "quickjs-ng"; }
    void set_host_api(const HostApi& api) override { api_ = api; }

    bool load_bytecode(const std::uint8_t* data, std::size_t size) override {
        if (data == nullptr || size == 0) return false;
        rt_ = JS_NewRuntime();
        if (rt_ == nullptr) return false;
        ctx_ = JS_NewContext(rt_);
        if (ctx_ == nullptr) { unload(); return false; }
        JS_SetContextOpaque(ctx_, this);
        if (memory_limit_ != 0) JS_SetMemoryLimit(rt_, memory_limit_);
        if (stack_limit_ != 0) JS_SetMaxStackSize(rt_, stack_limit_);
        install_host_functions();
        JSValue obj = JS_ReadObject(ctx_, data, size, JS_READ_OBJ_BYTECODE);
        if (JS_IsException(obj)) { JS_FreeValue(ctx_, obj); unload(); return false; }
        JSValue fn = JS_EvalFunction(ctx_, obj);
        if (JS_IsException(fn)) {
            std::fprintf(stderr, "[plugin] bytecode eval failed (bad bytecode or version mismatch)\n");
            JS_FreeValue(ctx_, fn);
            unload();
            return false;
        }
        // 字节码的"完成值"不一定是函数（以函数声明结尾的脚本完成值是 undefined）。
        // 这时回退到全局 onEvent —— 插件按契约把它的入口函数放在全局即可。
        if (!JS_IsFunction(ctx_, fn)) {
            JS_FreeValue(ctx_, fn);
            JSValue global = JS_GetGlobalObject(ctx_);
            fn = JS_GetPropertyStr(ctx_, global, "onEvent");
            JS_FreeValue(ctx_, global);
            if (!JS_IsFunction(ctx_, fn)) {
                std::fprintf(stderr, "[plugin] bytecode exposed neither a function value nor a global onEvent\n");
                JS_FreeValue(ctx_, fn);
                unload();
                return false;
            }
        }
        on_event_ = fn;
        return true;
    }

    // 宿主在 load_bytecode【之前】下发上限，所以这里只能先记住，
    // 等 JS_NewRuntime 建好后再真正应用（否则会误判为拒绝）。
    bool set_memory_limit(std::size_t bytes) override { memory_limit_ = bytes; return true; }
    bool set_max_stack_size(std::size_t bytes) override { stack_limit_ = bytes; return true; }
    void set_instruction_budget(std::uint64_t budget) override {
        budget_ = budget; used_ = 0;
        if (rt_ != nullptr) JS_SetInterruptHandler(rt_, &QuickJsEngine::interrupt, this);
    }
    std::uint64_t instructions_used() const override { return used_; }

    bool dispatch(const PluginEvent& event) override {
        if (ctx_ == nullptr || JS_IsUndefined(on_event_)) return false;
        used_ = 0;
        JSValue arg = build_event(event);
        JSValue ret = JS_Call(ctx_, on_event_, JS_UNDEFINED, 1, &arg);
        JS_FreeValue(ctx_, arg);
        const bool ok = !JS_IsException(ret);
        JS_FreeValue(ctx_, ret);
        if (!ok) { JSValue ex = JS_GetException(ctx_); JS_FreeValue(ctx_, ex); }
        return ok;
    }

    void collect_garbage() override { if (rt_ != nullptr) JS_RunGC(rt_); }
    void unload() override {
        if (ctx_ != nullptr) { JS_FreeContext(ctx_); ctx_ = nullptr; }
        if (rt_ != nullptr) { JS_FreeRuntime(rt_); rt_ = nullptr; }
        on_event_ = JS_UNDEFINED; used_ = 0;
    }

private:
    // quickjs-ng 0.17 的 JSInterruptHandler 签名是 (JSContext*, void*)，不是 JSRuntime*。
    static int interrupt(JSContext*, void* opaque) {
        QuickJsEngine* self = static_cast<QuickJsEngine*>(opaque);
        if (self == nullptr) return 0;
        if (self->budget_ != 0 && ++self->used_ > self->budget_) return 1;
        return 0;
    }

    static QuickJsEngine* self(JSContext* ctx) {
        return static_cast<QuickJsEngine*>(JS_GetContextOpaque(ctx));
    }

    static JSValue js_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr || s->api_.log == nullptr || argc < 1) return JS_NewInt32(ctx, -1);
        const char* text = JS_ToCString(ctx, argv[0]);
        if (text == nullptr) return JS_NewInt32(ctx, -2);
        const std::int32_t rc = s->api_.log(s->api_.ctx, text);
        JS_FreeCString(ctx, text);
        return JS_NewInt32(ctx, rc);
    }
    static JSValue js_send_message(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr || s->api_.send_message == nullptr || argc < 2) return JS_NewInt32(ctx, -1);
        std::int32_t pid = -1;
        if (JS_ToInt32(ctx, &pid, argv[0]) != 0) return JS_NewInt32(ctx, -2);
        const char* text = JS_ToCString(ctx, argv[1]);
        if (text == nullptr) return JS_NewInt32(ctx, -2);
        const std::int32_t rc = s->api_.send_message(s->api_.ctx, pid, text);
        JS_FreeCString(ctx, text);
        return JS_NewInt32(ctx, rc);
    }
    // sfs.broadcast(text)：全体广播。映射到宿主 send_message 的 player_id < 0 语义。
    static JSValue js_broadcast(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr || s->api_.send_message == nullptr || argc < 1) return JS_NewInt32(ctx, -1);
        const char* text = JS_ToCString(ctx, argv[0]);
        if (text == nullptr) return JS_NewInt32(ctx, -2);
        const std::int32_t rc = s->api_.send_message(s->api_.ctx, -1, text);
        JS_FreeCString(ctx, text);
        return JS_NewInt32(ctx, rc);
    }

    // sfs.players()：最近一次世界快照里的在线玩家（结构化，插件不必自己从事件里翻）。
    static JSValue js_players(JSContext* ctx, JSValueConst, int, JSValueConst*) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr) return JS_NewArray(ctx);
        JSValue arr = JS_NewArray(ctx);
        for (std::size_t i = 0; i < s->last_players_.size(); ++i)
            JS_SetPropertyUint32(ctx, arr, static_cast<std::uint32_t>(i), s->player_to_js(s->last_players_[i]));
        return arr;
    }

    static JSValue js_read_rocket(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr || s->api_.read_rocket == nullptr || argc < 1) return JS_NewInt32(ctx, -1);
        std::int32_t rid = -1;
        if (JS_ToInt32(ctx, &rid, argv[0]) != 0) return JS_NewInt32(ctx, -2);
        RocketView view{};
        const std::int32_t rc = s->api_.read_rocket(s->api_.ctx, rid, &view);
        if (rc != 0) return JS_NewInt32(ctx, rc);
        return s->rocket_to_js(view);
    }
    static JSValue js_write_rocket(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
        QuickJsEngine* s = self(ctx);
        if (s == nullptr || s->api_.write_rocket == nullptr || argc < 1) return JS_NewInt32(ctx, -1);
        RocketView view{};
        JSValue v = argv[0];
        JSValue p0 = JS_GetPropertyStr(ctx, v, "rocketId");  JS_ToInt32(ctx, &view.rocket_id, p0);   JS_FreeValue(ctx, p0);
        JSValue p1 = JS_GetPropertyStr(ctx, v, "partCount"); JS_ToInt32(ctx, &view.part_count, p1);  JS_FreeValue(ctx, p1);
        JSValue p2 = JS_GetPropertyStr(ctx, v, "throttle");  JS_ToFloat64(ctx, &view.throttle, p2);  JS_FreeValue(ctx, p2);
        const std::int32_t rc = s->api_.write_rocket(s->api_.ctx, &view);
        return JS_NewInt32(ctx, rc);
    }

    void install_host_functions() {
        JSValue global = JS_GetGlobalObject(ctx_);
        JSValue sfs = JS_NewObject(ctx_);
        JS_SetPropertyStr(ctx_, sfs, "log", JS_NewCFunction(ctx_, &QuickJsEngine::js_log, "log", 1));
        JS_SetPropertyStr(ctx_, sfs, "sendMessage", JS_NewCFunction(ctx_, &QuickJsEngine::js_send_message, "sendMessage", 2));
        JS_SetPropertyStr(ctx_, sfs, "readRocket", JS_NewCFunction(ctx_, &QuickJsEngine::js_read_rocket, "readRocket", 1));
        JS_SetPropertyStr(ctx_, sfs, "writeRocket", JS_NewCFunction(ctx_, &QuickJsEngine::js_write_rocket, "writeRocket", 1));
        JS_SetPropertyStr(ctx_, sfs, "broadcast", JS_NewCFunction(ctx_, &QuickJsEngine::js_broadcast, "broadcast", 1));
        JS_SetPropertyStr(ctx_, sfs, "players", JS_NewCFunction(ctx_, &QuickJsEngine::js_players, "players", 0));
        JS_SetPropertyStr(ctx_, global, "sfs", sfs);
        JS_FreeValue(ctx_, global);
    }

    // 注意：这两个是【成员函数】（要用 ctx_），不能加 static。
    JSValue rocket_to_js(const RocketView& v) const {
        JSValue o = JS_NewObject(ctx_);
        JS_SetPropertyStr(ctx_, o, "rocketId", JS_NewInt32(ctx_, v.rocket_id));
        JS_SetPropertyStr(ctx_, o, "ownerPlayer", JS_NewInt32(ctx_, v.owner_player));
        JS_SetPropertyStr(ctx_, o, "controlledBy", JS_NewInt32(ctx_, v.controlled_by));
        JS_SetPropertyStr(ctx_, o, "partCount", JS_NewInt32(ctx_, v.part_count));
        JS_SetPropertyStr(ctx_, o, "posX", JS_NewFloat64(ctx_, v.pos_x));
        JS_SetPropertyStr(ctx_, o, "posY", JS_NewFloat64(ctx_, v.pos_y));
        JS_SetPropertyStr(ctx_, o, "posZ", JS_NewFloat64(ctx_, v.pos_z));
        JS_SetPropertyStr(ctx_, o, "velX", JS_NewFloat64(ctx_, v.vel_x));
        JS_SetPropertyStr(ctx_, o, "velY", JS_NewFloat64(ctx_, v.vel_y));
        JS_SetPropertyStr(ctx_, o, "velZ", JS_NewFloat64(ctx_, v.vel_z));
        JS_SetPropertyStr(ctx_, o, "throttle", JS_NewFloat64(ctx_, v.throttle));
        JS_SetPropertyStr(ctx_, o, "hasUpdateAuthority", JS_NewInt32(ctx_, v.has_update_authority));
        return o;
    }
    JSValue player_to_js(const PlayerView& p) const {
        JSValue o = JS_NewObject(ctx_);
        JS_SetPropertyStr(ctx_, o, "playerId", JS_NewInt32(ctx_, p.player_id));
        JS_SetPropertyStr(ctx_, o, "controlledRocket", JS_NewInt32(ctx_, p.controlled_rocket));
        JS_SetPropertyStr(ctx_, o, "name", JS_NewString(ctx_, p.name));
        return o;
    }

    JSValue build_event(const PluginEvent& event) {
        JSValue o = JS_NewObject(ctx_);
        const char* kind = "none";
        switch (event.kind) {
            case kEventWorldSnapshot: kind = "worldSnapshot"; break;
            case kEventPlayerJoined:  kind = "playerJoined";  break;
            case kEventPlayerLeft:    kind = "playerLeft";    break;
            case kEventChatMessage:   kind = "chatMessage";   break;
            case kEventCommand:       kind = "command";       break;
            default: break;
        }
        JS_SetPropertyStr(ctx_, o, "kind", JS_NewString(ctx_, kind));
        if (event.command != nullptr) JS_SetPropertyStr(ctx_, o, "command", JS_NewString(ctx_, event.command));
        if (event.text != nullptr) JS_SetPropertyStr(ctx_, o, "text", JS_NewString(ctx_, event.text));
        if (event.player != nullptr) JS_SetPropertyStr(ctx_, o, "player", player_to_js(*event.player));
        if (event.world == nullptr) return o;
        const WorldSnapshot& w = *event.world;
        last_players_ = w.players;   // 缓存给 sfs.players()
        JS_SetPropertyStr(ctx_, o, "tick", JS_NewInt64(ctx_, static_cast<std::int64_t>(w.tick)));
        JS_SetPropertyStr(ctx_, o, "worldTime", JS_NewFloat64(ctx_, w.world_time));
        JS_SetPropertyStr(ctx_, o, "droppedRockets", JS_NewInt32(ctx_, static_cast<std::int32_t>(w.dropped_rockets)));
        JSValue rockets = JS_NewArray(ctx_);
        for (std::size_t i = 0; i < w.rockets.size(); ++i)
            JS_SetPropertyUint32(ctx_, rockets, static_cast<std::uint32_t>(i), rocket_to_js(w.rockets[i]));
        JS_SetPropertyStr(ctx_, o, "rockets", rockets);
        JSValue players = JS_NewArray(ctx_);
        for (std::size_t i = 0; i < w.players.size(); ++i)
            JS_SetPropertyUint32(ctx_, players, static_cast<std::uint32_t>(i), player_to_js(w.players[i]));
        JS_SetPropertyStr(ctx_, o, "players", players);
        return o;
    }

    JSRuntime* rt_ = nullptr;
    JSContext* ctx_ = nullptr;
    JSValue on_event_ = JS_UNDEFINED;
    HostApi api_{};
    std::uint64_t budget_ = 0;
    std::uint64_t used_ = 0;
    std::size_t memory_limit_ = 0;
    std::size_t stack_limit_ = 0;
    std::vector<PlayerView> last_players_;   // 最近快照的玩家（sfs.players() 用）
};

class QuickJsEngineFactory final : public IJsEngineFactory {
public:
    std::unique_ptr<IJsEngine> create() override {
        return std::unique_ptr<IJsEngine>(new QuickJsEngine());
    }
};

}  // namespace sfs::plugins

#endif  // SFS_PLUGINS_QUICKJS
