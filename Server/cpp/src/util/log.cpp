// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "util/log.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace sfs::log {
namespace {

Options g_options;
std::mutex g_mutex;
bool g_color = false;   // init() 时按 color_mode + 终端检测解析一次

const char* kCategoryNames[kCategoryCount] = {"net", "world", "p2p", "plugin", "config", "error"};

bool stdout_is_tty() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

// Windows 10+ 默认不解析 ANSI 转义序列：必须显式打开 VT 处理，否则颜色会变成乱码。
void enable_windows_vt() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    if (!GetConsoleMode(h, &mode)) return;
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
#endif
}

// info 用终端默认色（空串），其余按级别上色。
const char* level_color(Level l) {
    switch (l) {
        case Level::Debug: return "\x1b[90m";   // 灰
        case Level::Warn:  return "\x1b[33m";   // 黄
        case Level::Error: return "\x1b[31m";   // 红
        case Level::Info:
        default:           return "";            // 默认色
    }
}

// 过滤规则：
//   1) 总开关关闭 -> 全部不输出；
//   2) 类别被【显式】关掉 -> 全部不输出（连 error 也压掉，这是用户明确要求）；
//   3) error 级别 -> 永远输出（不被 level / 白名单过滤）；
//   4) 其余按 level 阈值与白名单判定。
bool accepted(const Options& o, Cat c, Level l) {
    if (!o.enabled) return false;
    const int ci = static_cast<int>(c);
    if (ci < 0 || ci >= kCategoryCount) return false;
    if (o.explicit_hidden[ci]) return false;
    if (l == Level::Error) return true;
    if (static_cast<int>(l) < static_cast<int>(o.level)) return false;
    if (o.whitelist_active && !o.whitelist[ci]) return false;
    return true;
}

void format_timestamp(std::string* out) {
    const std::time_t t = std::time(nullptr);
    const std::tm* tm = std::localtime(&t);
    char buf[16];
    if (tm != nullptr) {
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    } else {
        std::snprintf(buf, sizeof(buf), "??:??:??");
    }
    out->assign(buf);
}

// 摘要行必须是 ASCII 安全的：用户给的路径可能含非 ASCII 字节，这里统一换成 '?'。
std::string ascii_sanitize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (const char ch : in) {
        const unsigned char u = static_cast<unsigned char>(ch);
        out.push_back((u >= 0x20 && u <= 0x7E) ? ch : '?');
    }
    return out;
}

std::string visible_category_list(const Options& o) {
    std::string out;
    for (int i = 0; i < kCategoryCount; ++i) {
        const bool visible = !o.explicit_hidden[i] && (!o.whitelist_active || o.whitelist[i]);
        if (!visible) continue;
        if (!out.empty()) out += ',';
        out += kCategoryNames[i];
    }
    if (out.empty()) out = "none";
    return out;
}

void write_line(const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

}  // namespace

const Options& options() { return g_options; }

void init(const Options& options_in) {
    g_options = options_in;

    bool color = false;
    switch (g_options.color_mode) {
        case ColorMode::On:   color = true; break;
        case ColorMode::Off:  color = false; break;
        case ColorMode::Auto: color = stdout_is_tty(); break;
    }
    g_color = color;
    if (color) enable_windows_vt();

    if (g_options.quiet_startup) return;

    // 启动摘要【故意绕开过滤】：排查"为什么没日志"时最需要看到这一行。
    // 内容全部 ASCII（路径做过清洗），一行打完方便 grep/cut。
    const char* mode_name = "auto";
    if (g_options.color_mode == ColorMode::On) mode_name = "on";
    else if (g_options.color_mode == ColorMode::Off) mode_name = "off";

    std::string line;
    std::lock_guard<std::mutex> lock(g_mutex);
    format_timestamp(&line);
    line += " [config] INFO  [cpp-server] log config: enabled=";
    line += g_options.enabled ? "1" : "0";
    line += " level=";
    switch (g_options.level) {
        case Level::Debug: line += "debug"; break;
        case Level::Info:  line += "info";  break;
        case Level::Warn:  line += "warn";  break;
        case Level::Error: line += "error"; break;
    }
    line += " color=";
    line += color ? "on" : "off";
    line += " mode=";
    line += mode_name;
    line += " quiet_startup=";
    line += g_options.quiet_startup ? "1" : "0";
    line += " config=";
    line += ascii_sanitize(g_options.config_path);
    line += " (";
    line += ascii_sanitize(g_options.config_note);
    line += ") categories=";
    line += visible_category_list(g_options);
    line += '\n';
    write_line(line);
}

bool is_enabled(Cat cat, Level level) { return accepted(g_options, cat, level); }

void log(Cat cat, Level level, const char* fmt, ...) {
    if (!accepted(g_options, cat, level)) return;

    char message[4096];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    const bool color = g_color;
    const char* color_code = color ? level_color(level) : "";

    std::string line;
    line.reserve(std::strlen(message) + 48);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        format_timestamp(&line);
        char head[32];
        std::snprintf(head, sizeof(head), " [%-6s] ", category_name(cat));
        line += head;
        if (color_code[0] != '\0') line += color_code;
        char label[8];
        std::snprintf(label, sizeof(label), "%-5s", level_name(level));
        line += label;
        if (color_code[0] != '\0') line += "\x1b[0m";
        line += ' ';
        line += message;
        line += '\n';
        write_line(line);
    }
}

// 异步信号安全：不用 std::string 之外的动态设施，不抢互斥锁，只走 write(2)。
void signal_log(Cat cat, Level level, const char* fmt, ...) {
    if (!accepted(g_options, cat, level)) return;

    char message[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    char head[48];
    std::snprintf(head, sizeof(head), "[%-6s] %-5s ", category_name(cat), level_name(level));

    char line[640];
    std::snprintf(line, sizeof(line), "%s%s\n", head, message);
    const std::size_t len = std::strlen(line);
#ifdef _WIN32
    ::_write(_fileno(stdout), line, static_cast<unsigned>(len));
#else
    std::size_t written = 0;
    while (written < len) {
        const ssize_t n = ::write(STDOUT_FILENO, line + written, len - written);
        if (n <= 0) break;
        written += static_cast<std::size_t>(n);
    }
#endif
}

const char* category_name(Cat cat) {
    const int i = static_cast<int>(cat);
    if (i < 0 || i >= kCategoryCount) return "???";
    return kCategoryNames[i];
}

const char* level_name(Level level) {
    switch (level) {
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "INFO";
}

bool category_from_name(const std::string& name, Cat* out) {
    for (int i = 0; i < kCategoryCount; ++i) {
        if (name == kCategoryNames[i]) {
            *out = static_cast<Cat>(i);
            return true;
        }
    }
    return false;
}

bool parse_level(const std::string& name, Level* out) {
    if (name == "debug") { *out = Level::Debug; return true; }
    if (name == "info")  { *out = Level::Info;  return true; }
    if (name == "warn" || name == "warning") { *out = Level::Warn; return true; }
    if (name == "error") { *out = Level::Error; return true; }
    return false;
}

bool parse_color_mode(const std::string& name, ColorMode* out) {
    if (name == "auto") { *out = ColorMode::Auto; return true; }
    if (name == "on" || name == "always" || name == "true") { *out = ColorMode::On; return true; }
    if (name == "off" || name == "never" || name == "false") { *out = ColorMode::Off; return true; }
    return false;
}

bool parse_category_list(const std::string& csv, Cat* out, std::size_t* count, std::string* bad) {
    *count = 0;
    std::size_t start = 0;
    while (start <= csv.size()) {
        std::size_t end = csv.find(',', start);
        if (end == std::string::npos) end = csv.size();
        std::string token = csv.substr(start, end - start);
        // trim 空格
        std::size_t b = 0;
        while (b < token.size() && (token[b] == ' ' || token[b] == '\t')) ++b;
        std::size_t e = token.size();
        while (e > b && (token[e - 1] == ' ' || token[e - 1] == '\t')) --e;
        token = token.substr(b, e - b);
        if (!token.empty()) {
            if (token == "all") {
                for (int i = 0; i < kCategoryCount; ++i) {
                    if (*count < static_cast<std::size_t>(kCategoryCount)) out[(*count)++] = static_cast<Cat>(i);
                }
            } else {
                Cat c;
                if (!category_from_name(token, &c)) {
                    if (bad != nullptr) *bad = token;
                    return false;
                }
                if (*count >= static_cast<std::size_t>(kCategoryCount)) {
                    if (bad != nullptr) *bad = token;
                    return false;
                }
                out[(*count)++] = c;
            }
        }
        if (end == csv.size()) break;
        start = end + 1;
    }
    return true;
}

void set_whitelist(Options* o, const Cat* cats, std::size_t count) {
    o->whitelist_active = true;
    for (int i = 0; i < kCategoryCount; ++i) o->whitelist[i] = false;
    for (std::size_t i = 0; i < count; ++i) {
        const int ci = static_cast<int>(cats[i]);
        if (ci < 0 || ci >= kCategoryCount) continue;
        o->whitelist[ci] = true;
        // 白名单来自命令行/环境变量（优先级高于配置文件），列入即视为不再显式隐藏。
        o->explicit_hidden[ci] = false;
    }
}

void add_hidden(Options* o, const Cat* cats, std::size_t count) {
    for (std::size_t i = 0; i < count; ++i) {
        const int ci = static_cast<int>(cats[i]);
        if (ci < 0 || ci >= kCategoryCount) continue;
        o->explicit_hidden[ci] = true;
    }
}

}  // namespace sfs::log
