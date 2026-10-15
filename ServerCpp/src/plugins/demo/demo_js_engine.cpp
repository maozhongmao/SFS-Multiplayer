// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// DemoEngine 实现。整个文件用 SFS_PLUGINS_DEMO 包起来：
// 服务端 build-server-exp.ps1 会递归收集 src 下的 .cpp，未定义该宏时本 TU 为空，
// 既不产生符号也不改变链接产物 —— 默认构建行为不变。

#if defined(SFS_PLUGINS_DEMO) && SFS_PLUGINS_DEMO

#include "plugins/demo/demo_js_engine.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sfs::plugins::demo {
namespace {

// ---------------------------------------------------------------- 值与命令表

enum class ValKind : std::uint8_t { kNone, kNumber, kText, kRocket, kPlayer };

struct Value {
    ValKind kind = ValKind::kNone;
    double num = 0.0;
    std::string text;
    RocketView rocket{};
    PlayerView player{};
};

double val_to_number(const Value& v) {
    if (v.kind == ValKind::kNumber) return v.num;
    if (v.kind == ValKind::kText) {
        char* end = nullptr;
        const double d = std::strtod(v.text.c_str(), &end);
        if (end != nullptr && *end == '\0') return d;
    }
    return 0.0;
}

std::string val_to_text(const Value& v) {
    if (v.kind == ValKind::kText) return v.text;
    if (v.kind == ValKind::kNumber) {
        char buf[48];
        if (std::floor(v.num) == v.num && std::fabs(v.num) < 1e15) {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v.num));
        } else {
            std::snprintf(buf, sizeof(buf), "%g", v.num);
        }
        return std::string(buf);
    }
    if (v.kind == ValKind::kRocket) {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "rocket#%d", v.rocket.rocket_id);
        return std::string(buf);
    }
    return std::string();
}

// 词法：空白分隔，双引号串保留引号（内部可含空格），其余原样。
std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }
        std::string tok;
        if (c == '"') {
            tok.push_back('"');
            std::size_t j = i + 1;
            while (j < s.size()) {
                const char d = s[j];
                if (d == '\\' && j + 1 < s.size()) { tok.push_back(d); tok.push_back(s[j + 1]); j += 2; continue; }
                tok.push_back(d);
                ++j;
                if (d == '"') break;
            }
            i = j;
        } else {
            std::size_t j = i;
            while (j < s.size() && s[j] != ' ' && s[j] != '\t' && s[j] != '\r' && s[j] != '\n') {
                tok.push_back(s[j]);
                ++j;
            }
            i = j;
        }
        out.push_back(std::move(tok));
    }
    return out;
}

std::string strip_quotes(const std::string& tok) {
    if (tok.size() < 2 || tok.front() != '"') return tok;
    std::string out;
    for (std::size_t i = 1; i + 1 < tok.size(); ++i) {
        if (tok[i] == '\\' && i + 2 < tok.size()) {
            const char n = tok[++i];
            out.push_back(n == 'n' ? ' ' : n);
        } else {
            out.push_back(tok[i]);
        }
    }
    return out;
}

std::string join_tokens(const std::vector<std::string>& t, std::size_t from) {
    std::string out;
    for (std::size_t i = from; i < t.size(); ++i) {
        if (!out.empty()) out.push_back(' ');
        out += t[i];
    }
    return out;
}

const char* kind_name(EventKind kind) {
    switch (kind) {
        case kEventWorldSnapshot: return "worldSnapshot";
        case kEventPlayerJoined:  return "playerJoined";
        case kEventPlayerLeft:    return "playerLeft";
        case kEventChatMessage:   return "chatMessage";
        default:                  return "none";
    }
}

EventKind parse_event_kind(const std::string& name) {
    if (name == "worldSnapshot") return kEventWorldSnapshot;
    if (name == "playerJoined")  return kEventPlayerJoined;
    if (name == "playerLeft")    return kEventPlayerLeft;
    if (name == "chatMessage")   return kEventChatMessage;
    return kEventNone;
}

// 路径段："event.rockets[0].rocketId" -> {event}{rockets}{[0]}{rocketId}
struct PathSeg {
    std::string name;
    bool is_index = false;
    int index = 0;
};

bool parse_path(const std::string& tok, std::vector<PathSeg>& out) {
    std::size_t i = 0;
    while (i < tok.size()) {
        const unsigned char c = static_cast<unsigned char>(tok[i]);
        if (std::isalpha(c) || c == '_') {
            const std::size_t j0 = i;
            while (i < tok.size() && (std::isalnum(static_cast<unsigned char>(tok[i])) || tok[i] == '_')) ++i;
            PathSeg s;
            s.name = tok.substr(j0, i - j0);
            out.push_back(s);
        } else if (c == '.') {
            ++i;
            if (i >= tok.size() || !(std::isalpha(static_cast<unsigned char>(tok[i])) || tok[i] == '_')) return false;
        } else if (c == '[') {
            const std::size_t j0 = ++i;
            while (i < tok.size() && std::isdigit(static_cast<unsigned char>(tok[i]))) ++i;
            if (i == j0 || i >= tok.size() || tok[i] != ']') return false;
            PathSeg s;
            s.is_index = true;
            s.index = std::atoi(tok.substr(j0, i - j0).c_str());
            out.push_back(s);
            ++i;
        } else {
            return false;
        }
    }
    return !out.empty() && !out.front().is_index;
}

enum class ActionKind : std::uint8_t {
    kSet, kLog, kSendMsg, kSendAll, kGetRocket, kSetThrottle, kWriteBack
};

struct Action {
    ActionKind kind = ActionKind::kLog;
    std::string name;   // set / getrocket / setthrottle / writeback 的目标变量
    std::string expr1;  // 主表达式
    std::string expr2;  // sendmsg 的文本表达式
};

struct Command {
    EventKind when = kEventNone;  // kEventNone = 任意事件
    bool has_condition = false;
    std::string cond_lhs, cond_op, cond_rhs;
    Action action;
};

}  // namespace

// ---------------------------------------------------------------- 引擎内部状态

struct DemoEngineImpl {
    // 配置（由宿主下发）
    std::size_t   memory_limit = 0;  // 0 = 不限
    std::size_t   max_stack = 0;     // 0 = 不限
    std::uint64_t budget = 0;        // 0 = 不限

    // 装载产物
    bool loaded = false;
    bool faulted = false;
    std::vector<Command> commands;

    // 运行期
    HostApi api{};
    bool bound = false;
    const PluginEvent* event = nullptr;
    std::map<std::string, Value> vars;
    std::uint64_t instructions = 0;
    std::uint32_t eval_depth = 0;
    bool eval_error = false;

    DemoEngineStats stats{};

    bool parse_command(const std::string& body, Command* out);
    Value resolve_token(const std::string& tok);
    Value resolve_path(const std::vector<PathSeg>& path);
    Value rocket_field(const RocketView& r, const std::string& f);
    Value player_field(const PlayerView& p, const std::string& f);
    std::string interpolate(const std::string& raw);
    Value parse_add(const std::vector<std::string>& t, std::size_t& i);
    Value parse_mul(const std::vector<std::string>& t, std::size_t& i);
    Value parse_primary(const std::vector<std::string>& t, std::size_t& i);
    Value eval_expr_string(const std::string& expr);
    bool eval_condition(const Command& c);
    bool run_action(const Action& a);
    bool spend(std::uint64_t n);
    void record(std::int32_t status);
    std::int32_t call_log(const std::string& text);
    std::int32_t call_send(std::int32_t player, const std::string& text);
    std::int32_t call_read(std::int32_t rocket_id, RocketView* out);
    std::int32_t call_write(const RocketView& view);
};

// ---------------------------------------------------------------- 命令行解析

bool DemoEngineImpl::parse_command(const std::string& body, Command* out) {
    const std::vector<std::string> t = tokenize(body);
    if (t.empty() || out == nullptr) return false;
    std::size_t i = 0;

    if (t[i] == "when") {
        if (i + 1 >= t.size()) return false;
        const EventKind k = parse_event_kind(t[i + 1]);
        const bool any = (t[i + 1] == "any" || t[i + 1] == "*");
        if (k == kEventNone && !any) return false;
        out->when = k;
        i += 2;
    }

    if (i < t.size() && t[i] == "if") {
        if (i + 4 >= t.size() || t[i + 4] != "then") return false;
        out->has_condition = true;
        out->cond_lhs = t[i + 1];
        out->cond_op  = t[i + 2];
        out->cond_rhs = t[i + 3];
        i += 5;
    }
    if (i >= t.size()) return false;

    const std::string kw = t[i++];
    Action a;
    if (kw == "set") {
        if (t.size() < i + 3 || t[i + 1] != "=") return false;
        a.kind = ActionKind::kSet;
        a.name = t[i];
        a.expr1 = join_tokens(t, i + 2);
    } else if (kw == "log") {
        if (i >= t.size()) return false;
        a.kind = ActionKind::kLog;
        a.expr1 = join_tokens(t, i);
    } else if (kw == "sendmsg") {
        if (t.size() < i + 2) return false;
        a.kind = ActionKind::kSendMsg;
        a.expr1 = t[i];
        a.expr2 = join_tokens(t, i + 1);
    } else if (kw == "sendall") {
        if (i >= t.size()) return false;
        a.kind = ActionKind::kSendAll;
        a.expr1 = join_tokens(t, i);
    } else if (kw == "getrocket") {
        if (t.size() < i + 3 || t[i + 1] != "into") return false;
        a.kind = ActionKind::kGetRocket;
        a.expr1 = t[i];
        a.name = t[i + 2];
    } else if (kw == "setthrottle") {
        if (t.size() < i + 2) return false;
        a.kind = ActionKind::kSetThrottle;
        a.name = t[i];
        a.expr1 = join_tokens(t, i + 1);
    } else if (kw == "writeback") {
        if (i >= t.size()) return false;
        a.kind = ActionKind::kWriteBack;
        a.name = t[i];
    } else {
        return false;
    }
    out->action = std::move(a);
    return true;
}

// ---------------------------------------------------------------- 路径求值

Value DemoEngineImpl::rocket_field(const RocketView& r, const std::string& f) {
    Value v;
    v.kind = ValKind::kNumber;
    if (f == "rocketId") v.num = r.rocket_id;
    else if (f == "ownerPlayer") v.num = r.owner_player;
    else if (f == "controlledBy") v.num = r.controlled_by;
    else if (f == "partCount") v.num = r.part_count;
    else if (f == "posX") v.num = r.pos_x;
    else if (f == "posY") v.num = r.pos_y;
    else if (f == "posZ") v.num = r.pos_z;
    else if (f == "velX") v.num = r.vel_x;
    else if (f == "velY") v.num = r.vel_y;
    else if (f == "velZ") v.num = r.vel_z;
    else if (f == "throttle") v.num = r.throttle;
    else if (f == "hasUpdateAuthority") v.num = r.has_update_authority;
    else return Value{};
    return v;
}

Value DemoEngineImpl::player_field(const PlayerView& p, const std::string& f) {
    Value v;
    if (f == "playerId") { v.kind = ValKind::kNumber; v.num = p.player_id; return v; }
    if (f == "controlledRocket") { v.kind = ValKind::kNumber; v.num = p.controlled_rocket; return v; }
    if (f == "name") { v.kind = ValKind::kText; v.text = p.name; return v; }
    return Value{};
}

Value DemoEngineImpl::resolve_path(const std::vector<PathSeg>& p) {
    Value cur;
    std::size_t i = 1;
    const std::string& root = p[0].name;

    if (root == "event") {
        if (i >= p.size() || p[i].is_index) { eval_error = true; return Value{}; }
        const std::string f = p[i].name;
        const WorldSnapshot* w = (event != nullptr) ? event->world : nullptr;
        ++i;
        if (f == "kind") { cur.kind = ValKind::kText; cur.text = kind_name(event ? event->kind : kEventNone); }
        else if (f == "tick") { cur.kind = ValKind::kNumber; cur.num = w ? static_cast<double>(w->tick) : 0.0; }
        else if (f == "worldTime") { cur.kind = ValKind::kNumber; cur.num = w ? w->world_time : 0.0; }
        else if (f == "droppedRockets") { cur.kind = ValKind::kNumber; cur.num = w ? static_cast<double>(w->dropped_rockets) : 0.0; }
        else if (f == "rockets" || f == "players") {
            const bool rocks = (f == "rockets");
            if (i < p.size() && p[i].is_index) {
                const int idx = p[i].index;
                ++i;
                if (w == nullptr) { eval_error = true; return Value{}; }
                if (rocks) {
                    if (idx < 0 || static_cast<std::size_t>(idx) >= w->rockets.size()) { eval_error = true; return Value{}; }
                    cur.kind = ValKind::kRocket;
                    cur.rocket = w->rockets[static_cast<std::size_t>(idx)];
                } else {
                    if (idx < 0 || static_cast<std::size_t>(idx) >= w->players.size()) { eval_error = true; return Value{}; }
                    cur.kind = ValKind::kPlayer;
                    cur.player = w->players[static_cast<std::size_t>(idx)];
                }
            } else if (i < p.size() && p[i].name == "count") {
                ++i;
                cur.kind = ValKind::kNumber;
                cur.num = w ? static_cast<double>(rocks ? w->rockets.size() : w->players.size()) : 0.0;
            } else {
                eval_error = true;
                return Value{};
            }
        } else {
            eval_error = true;
            return Value{};
        }
    } else if (root == "player") {
        if (event == nullptr || event->player == nullptr) { eval_error = true; return Value{}; }
        cur.kind = ValKind::kPlayer;
        cur.player = *event->player;
    } else if (root == "text") {
        cur.kind = ValKind::kText;
        cur.text = (event != nullptr && event->text != nullptr) ? event->text : "";
    } else {
        const auto it = vars.find(root);
        if (it == vars.end()) { eval_error = true; return Value{}; }
        cur = it->second;
    }

    while (i < p.size()) {
        if (p[i].is_index) { eval_error = true; return Value{}; }
        if (cur.kind == ValKind::kRocket) cur = rocket_field(cur.rocket, p[i].name);
        else if (cur.kind == ValKind::kPlayer) cur = player_field(cur.player, p[i].name);
        else { eval_error = true; return Value{}; }
        if (cur.kind == ValKind::kNone) { eval_error = true; return Value{}; }
        ++i;
    }
    return cur;
}

Value DemoEngineImpl::resolve_token(const std::string& tok) {
    if (tok.empty()) { eval_error = true; return Value{}; }
    if (tok.front() == '"') {
        Value v;
        v.kind = ValKind::kText;
        v.text = interpolate(strip_quotes(tok));
        return v;
    }
    const unsigned char c0 = static_cast<unsigned char>(tok[0]);
    if (std::isdigit(c0) || (tok[0] == '-' && tok.size() > 1 &&
        (std::isdigit(static_cast<unsigned char>(tok[1])) || tok[1] == '.'))) {
        char* end = nullptr;
        const double d = std::strtod(tok.c_str(), &end);
        if (end == nullptr || *end != '\0') { eval_error = true; return Value{}; }
        Value v;
        v.kind = ValKind::kNumber;
        v.num = d;
        return v;
    }
    std::vector<PathSeg> path;
    if (!parse_path(tok, path)) { eval_error = true; return Value{}; }
    return resolve_path(path);
}

std::string DemoEngineImpl::interpolate(const std::string& raw) {
    std::string out;
    std::size_t i = 0;
    while (i < raw.size()) {
        if (raw[i] == '@') {
            if (i + 1 < raw.size() && raw[i + 1] == '@') { out.push_back('@'); i += 2; continue; }
            std::size_t j = i + 1;
            while (j < raw.size()) {
                const unsigned char c = static_cast<unsigned char>(raw[j]);
                if (std::isalnum(c) || c == '_' || c == '.' || c == '[' || c == ']') ++j; else break;
            }
            if (j == i + 1) { out.push_back('@'); ++i; continue; }
            const Value v = resolve_token(raw.substr(i + 1, j - i - 1));
            out += val_to_text(v);
            i = j;
        } else {
            out.push_back(raw[i]);
            ++i;
        }
    }
    return out;
}

// ---------------------------------------------------------------- 表达式求值

Value DemoEngineImpl::parse_mul(const std::vector<std::string>& t, std::size_t& i) {
    Value v = parse_primary(t, i);
    while (!eval_error && i < t.size() && (t[i] == "*" || t[i] == "/")) {
        const std::string op = t[i++];
        const Value r = parse_primary(t, i);
        if (eval_error) break;
        const double a = val_to_number(v);
        const double b = val_to_number(r);
        v.kind = ValKind::kNumber;
        v.num = (op == "*") ? (a * b) : (b == 0.0 ? 0.0 : a / b);
    }
    return v;
}

Value DemoEngineImpl::parse_add(const std::vector<std::string>& t, std::size_t& i) {
    Value v = parse_mul(t, i);
    while (!eval_error && i < t.size() && (t[i] == "+" || t[i] == "-")) {
        const std::string op = t[i++];
        const Value r = parse_mul(t, i);
        if (eval_error) break;
        if (op == "+") {
            if (v.kind == ValKind::kText || r.kind == ValKind::kText) {
                Value s;
                s.kind = ValKind::kText;
                s.text = val_to_text(v) + val_to_text(r);
                v = s;
            } else {
                v.kind = ValKind::kNumber;
                v.num = val_to_number(v) + val_to_number(r);
            }
        } else {
            v.kind = ValKind::kNumber;
            v.num = val_to_number(v) - val_to_number(r);
        }
    }
    return v;
}

Value DemoEngineImpl::parse_primary(const std::vector<std::string>& t, std::size_t& i) {
    if (eval_error) return Value{};
    if (i >= t.size()) { eval_error = true; return Value{}; }
    // 栈预算：每个表达式层按 64 字节记，超过 max_stack 视为运行错误。
    if (max_stack != 0 && (eval_depth + 1u) * 64u > max_stack) { eval_error = true; return Value{}; }
    ++eval_depth;
    Value v;
    const std::string tok = t[i++];
    if (tok == "(") {
        v = parse_add(t, i);
        if (!eval_error) {
            if (i < t.size() && t[i] == ")") ++i;
            else eval_error = true;
        }
    } else {
        v = resolve_token(tok);
    }
    --eval_depth;
    return v;
}

Value DemoEngineImpl::eval_expr_string(const std::string& expr) {
    const std::vector<std::string> t = tokenize(expr);
    std::size_t i = 0;
    Value v = parse_add(t, i);
    if (!eval_error && i != t.size()) eval_error = true;
    return v;
}

bool DemoEngineImpl::eval_condition(const Command& c) {
    const Value l = resolve_token(c.cond_lhs);
    if (eval_error) return false;
    const Value r = resolve_token(c.cond_rhs);
    if (eval_error) return false;
    const std::string& op = c.cond_op;
    if (op == "==") return val_to_text(l) == val_to_text(r);
    if (op == "!=") return val_to_text(l) != val_to_text(r);
    const double x = val_to_number(l);
    const double y = val_to_number(r);
    if (op == ">") return x > y;
    if (op == "<") return x < y;
    if (op == ">=") return x >= y;
    if (op == "<=") return x <= y;
    eval_error = true;  // 未知比较符
    return false;
}

// ---------------------------------------------------------------- 预算与宿主调用

bool DemoEngineImpl::spend(std::uint64_t n) {
    instructions += n;
    if (budget != 0 && instructions > budget) {
        eval_error = true;  // 超预算 = 当次执行中断
        return false;
    }
    return true;
}

void DemoEngineImpl::record(std::int32_t status) {
    ++stats.host_calls;
    stats.last_host_status = status;
    if (status == kHostErrNoCapability) ++stats.capability_denials;
    else if (status == kHostErrBudget) ++stats.budget_denials;
    else if (status == kHostErrNotFound) ++stats.not_found;
}

// 所有"脚本调用"都必须经过这四个包装：先扣指令预算，再走宿主给的函数指针/ctx。
// 引擎不自己判断能力位 —— 能力拒绝由 PluginHost 返回 -1，这里如实记录。
std::int32_t DemoEngineImpl::call_log(const std::string& text) {
    if (!spend(1)) return kHostErrBudget;
    if (api.log == nullptr) { record(kHostErrState); return kHostErrState; }
    const std::int32_t st = api.log(api.ctx, text.c_str());
    record(st);
    return st;
}

std::int32_t DemoEngineImpl::call_send(std::int32_t player, const std::string& text) {
    if (!spend(1)) return kHostErrBudget;
    if (api.send_message == nullptr) { record(kHostErrState); return kHostErrState; }
    const std::int32_t st = api.send_message(api.ctx, player, text.c_str());
    record(st);
    return st;
}

std::int32_t DemoEngineImpl::call_read(std::int32_t rocket_id, RocketView* out) {
    if (!spend(1)) return kHostErrBudget;
    if (api.read_rocket == nullptr) { record(kHostErrState); return kHostErrState; }
    const std::int32_t st = api.read_rocket(api.ctx, rocket_id, out);
    record(st);
    return st;
}

std::int32_t DemoEngineImpl::call_write(const RocketView& view) {
    if (!spend(1)) return kHostErrBudget;
    if (api.write_rocket == nullptr) { record(kHostErrState); return kHostErrState; }
    const std::int32_t st = api.write_rocket(api.ctx, &view);
    record(st);
    return st;
}

// ---------------------------------------------------------------- 命令执行

bool DemoEngineImpl::run_action(const Action& a) {
    switch (a.kind) {
        case ActionKind::kSet: {
            const Value v = eval_expr_string(a.expr1);
            if (eval_error) return false;
            vars[a.name] = v;
            return true;
        }
        case ActionKind::kLog: {
            const Value v = eval_expr_string(a.expr1);
            if (eval_error) return false;
            call_log(val_to_text(v));
            return !eval_error;
        }
        case ActionKind::kSendMsg: {
            const Value id = eval_expr_string(a.expr1);
            if (eval_error) return false;
            const Value text = eval_expr_string(a.expr2);
            if (eval_error) return false;
            call_send(static_cast<std::int32_t>(val_to_number(id)), val_to_text(text));
            return !eval_error;
        }
        case ActionKind::kSendAll: {
            const Value text = eval_expr_string(a.expr1);
            if (eval_error) return false;
            const WorldSnapshot* w = (event != nullptr) ? event->world : nullptr;
            if (w != nullptr) {
                for (const PlayerView& p : w->players) {
                    call_send(p.player_id, val_to_text(text));
                    if (eval_error) return false;  // 预算耗尽 -> 中断
                }
            }
            return true;
        }
        case ActionKind::kGetRocket: {
            const Value id = eval_expr_string(a.expr1);
            if (eval_error) return false;
            RocketView rv{};
            const std::int32_t st = call_read(static_cast<std::int32_t>(val_to_number(id)), &rv);
            if (eval_error) return false;
            if (st == kHostOk) {
                Value v;
                v.kind = ValKind::kRocket;
                v.rocket = rv;
                vars[a.name] = v;
            } else {
                // 与 JS 契约一致：失败时读到的是负的状态码，而不是对象。
                Value v;
                v.kind = ValKind::kNumber;
                v.num = static_cast<double>(st);
                vars[a.name] = v;
            }
            return true;
        }
        case ActionKind::kSetThrottle: {
            const auto it = vars.find(a.name);
            if (it == vars.end() || it->second.kind != ValKind::kRocket) { eval_error = true; return false; }
            const Value v = eval_expr_string(a.expr1);
            if (eval_error) return false;
            it->second.rocket.throttle = val_to_number(v);
            return true;
        }
        case ActionKind::kWriteBack: {
            const auto it = vars.find(a.name);
            if (it == vars.end() || it->second.kind != ValKind::kRocket) { eval_error = true; return false; }
            call_write(it->second.rocket);
            return !eval_error;
        }
    }
    eval_error = true;
    return false;
}

// ---------------------------------------------------------------- IJsEngine 实现

DemoEngine::DemoEngine() : impl_(new DemoEngineImpl()) {}
DemoEngine::~DemoEngine() { unload(); }

const char* DemoEngine::name() const { return "demo"; }

// "字节码" = 一份命令表。只识别 "@demo:" 标记后的文本，其余（真 JS 源码）忽略。
bool DemoEngine::load_bytecode(const std::uint8_t* data, std::size_t size) {
    DemoEngineImpl& s = *impl_;
    if (data == nullptr || size == 0) return false;
    if (size > kMaxBytecodeBytes) return false;

    s.commands.clear();
    s.loaded = false;
    s.faulted = false;
    s.stats = DemoEngineStats{};

    const std::string text(reinterpret_cast<const char*>(data), size);
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t marker = line.find("@demo:");
        if (marker == std::string::npos) continue;
        Command cmd;
        if (!s.parse_command(line.substr(marker + 6), &cmd)) return false;  // 语法错误
        s.commands.push_back(std::move(cmd));
    }
    if (s.commands.empty()) return false;  // 没有可执行镜像

    std::size_t mem = size;
    for (const Command& c : s.commands) {
        mem += sizeof(Command) + c.action.name.size() + c.action.expr1.size() + c.action.expr2.size() +
               c.cond_lhs.size() + c.cond_op.size() + c.cond_rhs.size();
    }
    if (s.memory_limit != 0 && mem > s.memory_limit) return false;  // 超内存上限

    s.stats.program_bytes = size;
    s.stats.command_count = s.commands.size();
    s.stats.memory_used_bytes = mem;
    s.loaded = true;
    return true;
}

bool DemoEngine::set_memory_limit(std::size_t bytes) {
    if (bytes == 0 || bytes > kMaxMemoryLimitBytes) return false;
    impl_->memory_limit = bytes;
    return true;
}

bool DemoEngine::set_max_stack_size(std::size_t bytes) {
    if (bytes == 0 || bytes > kMaxStackBytes) return false;
    impl_->max_stack = bytes;
    return true;
}

void DemoEngine::set_instruction_budget(std::uint64_t budget) { impl_->budget = budget; }

std::uint64_t DemoEngine::instructions_used() const { return impl_->instructions; }

bool DemoEngine::dispatch(const PluginEvent& event) {
    DemoEngineImpl& s = *impl_;
    if (!s.loaded || s.faulted) return false;  // faulted 后不再执行

    s.instructions = 0;
    s.eval_depth = 0;
    s.eval_error = false;
    s.vars.clear();
    s.event = &event;
    ++s.stats.dispatches;

    bool failed = false;
    for (const Command& c : s.commands) {
        if (c.when != kEventNone && c.when != event.kind) continue;
        if (!s.spend(1)) { failed = true; break; }  // 指令预算：每条命令 1 条指令
        if (c.has_condition && !s.eval_condition(c)) {
            if (s.eval_error) { failed = true; break; }
            continue;
        }
        if (!s.run_action(c.action)) { failed = true; break; }
    }

    s.stats.instructions_last_dispatch = s.instructions;
    s.event = nullptr;
    if (failed) {
        s.faulted = true;
        s.stats.faulted = true;
        ++s.stats.faults;
        return false;  // 宿主据此置 kFaulted、停止投递
    }
    return true;
}

void DemoEngine::collect_garbage() {
    // 真实引擎这里是 JS_RunGC；DemoEngine 没有引擎堆，只做防御性复位。
    impl_->eval_depth = 0;
    impl_->eval_error = false;
}

void DemoEngine::unload() {
    DemoEngineImpl& s = *impl_;
    s.loaded = false;
    s.faulted = false;
    s.commands.clear();
    s.vars.clear();
    s.event = nullptr;
    s.api = HostApi{};
    s.bound = false;
    s.stats.api_bound = false;
    s.stats.faulted = false;
    s.instructions = 0;
}

void DemoEngine::bind_host_api(const HostApi& api) {
    impl_->api = api;
    impl_->bound = (api.read_rocket != nullptr || api.write_rocket != nullptr ||
                    api.send_message != nullptr || api.log != nullptr);
    impl_->stats.api_bound = impl_->bound;
}

const DemoEngineStats& DemoEngine::stats() const { return impl_->stats; }

// ---------------------------------------------------------------- 工厂

std::unique_ptr<IJsEngine> DemoEngineFactory::create() {
    std::unique_ptr<DemoEngine> engine(new DemoEngine());
    last_ = engine.get();
    return std::unique_ptr<IJsEngine>(engine.release());
}

}  // namespace sfs::plugins::demo

#endif  // SFS_PLUGINS_DEMO
