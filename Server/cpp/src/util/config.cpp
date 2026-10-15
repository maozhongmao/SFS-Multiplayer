// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "util/config.hpp"

#include "json_lite.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace sfs::config {
namespace {

void apply_category_csv(sfs::log::Options* opts, const std::string& csv, bool whitelist) {
    sfs::log::Cat cats[sfs::log::kCategoryCount];
    std::size_t count = 0;
    std::string bad;
    if (!sfs::log::parse_category_list(csv, cats, &count, &bad)) {
        throw std::runtime_error("未知日志分类: " + bad);
    }
    if (whitelist) {
        sfs::log::set_whitelist(opts, cats, count);
    } else {
        sfs::log::add_hidden(opts, cats, count);
    }
}

}  // namespace

std::string default_config_path() { return "data/server-config.json"; }

std::string find_config_path(int argc, char* argv[]) {
    std::string path = default_config_path();
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) throw std::runtime_error("--config 缺少值");
            path = argv[++i];
        }
    }
    return path;
}

LogConfigLoad load_log_config(const std::string& path, sfs::log::Options* opts) {
    LogConfigLoad out;
    out.path = path;

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        // 文件不存在是正常情况（内置默认值），绝不报错。
        out.note = "not found (built-in defaults)";
        return out;
    }
    out.file_found = true;

    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        const sfs::jsonlite::Value root = sfs::jsonlite::parse(text);
        const sfs::jsonlite::Value* logv = root.find("log");
        if (logv != nullptr && logv->is_object()) {
            if (const sfs::jsonlite::Value* v = logv->find("enabled")) {
                opts->enabled = v->as_bool(opts->enabled);
            }
            if (const sfs::jsonlite::Value* v = logv->find("level")) {
                const std::string value = v->as_string("info");
                sfs::log::Level level;
                if (sfs::log::parse_level(value, &level)) {
                    opts->level = level;
                } else {
                    out.error += "log.level 取值非法: " + value + "；";
                }
            }
            if (const sfs::jsonlite::Value* v = logv->find("color")) {
                const std::string value = v->as_string("auto");
                sfs::log::ColorMode mode;
                if (sfs::log::parse_color_mode(value, &mode)) {
                    opts->color_mode = mode;
                } else {
                    out.error += "log.color 取值非法: " + value + "；";
                }
            }
            if (const sfs::jsonlite::Value* v = logv->find("quiet_startup")) {
                opts->quiet_startup = v->as_bool(opts->quiet_startup);
            }
            // categories：false = 不显示该类；未列出的类别保持默认（显示）。
            if (const sfs::jsonlite::Value* cats = logv->find("categories")) {
                if (cats->is_object() && cats->object) {
                    for (const auto& kv : *cats->object) {
                        sfs::log::Cat category;
                        if (!sfs::log::category_from_name(kv.first, &category)) {
                            out.error += "未知日志分类: " + kv.first + "；";
                            continue;
                        }
                        opts->explicit_hidden[static_cast<int>(category)] = !kv.second.as_bool(true);
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        out.parsed = false;
        out.note = "parse error (built-in defaults)";
        out.error = e.what();
        return out;
    }

    out.parsed = true;
    out.note = "loaded";
    return out;
}

namespace {
// 键名 <-> 位号。键名与 docs/cheats-client.md 里的客户端配置键一致，方便两端对照。
struct CheatKeyBit { const char* key; std::uint16_t bit; };
const CheatKeyBit kCheatKeys[] = {
    {"infinite_build_area", 1u << 0},
    {"part_clipping",        1u << 1},
    {"infinite_fuel",        1u << 2},
    {"no_atmospheric_drag",  1u << 3},
    {"unbreakable_parts",    1u << 4},
    {"no_gravity",           1u << 5},
    {"no_heat_damage",       1u << 6},
    {"no_burn_marks",        1u << 7},
    {"teleport",             1u << 8},
    {"refill_fuel_tanks",    1u << 9},
};
}  // namespace

CheatConfig load_cheat_config(const std::string& path) {
    CheatConfig out;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        out.note = "no config file (all cheats allowed)";
        return out;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    try {
        const sfs::jsonlite::Value root = sfs::jsonlite::parse(text);
        const sfs::jsonlite::Value* cheats = root.find("cheats");
        if (cheats == nullptr || !cheats->is_object()) {
            out.note = "no cheats section (all cheats allowed)";
            return out;
        }
        out.found = true;
        if (const sfs::jsonlite::Value* v = cheats->find("enabled")) {
            out.enabled = v->as_bool(out.enabled);
        }
        // individual.<key>=false 才关掉对应位；未列出的键保持允许（最小配置即可用）。
        if (const sfs::jsonlite::Value* ind = cheats->find("individual")) {
            if (ind->is_object() && ind->object) {
                for (const auto& kv : *ind->object) {
                    const bool allow = kv.second.as_bool(true);
                    for (const CheatKeyBit& b : kCheatKeys) {
                        if (kv.first == b.key) {
                            if (allow) {
                                out.individual_flags = static_cast<std::uint16_t>(out.individual_flags | b.bit);
                            } else {
                                out.individual_flags = static_cast<std::uint16_t>(out.individual_flags & ~b.bit);
                            }
                        }
                    }
                }
            }
        }
        out.note = out.enabled ? "loaded (cheats on)" : "loaded (cheats entry hidden)";
        return out;
    } catch (const std::exception& e) {
        out.note = "parse error (all cheats allowed)";
        return out;
    }
}

namespace {
CheatConfig& active_cheat_storage() {
    static CheatConfig config;   // 默认 = 全开，与"没有策略"等价
    return config;
}
}  // namespace

void set_active_cheat_config(const CheatConfig& config) { active_cheat_storage() = config; }
const CheatConfig& active_cheat_config() { return active_cheat_storage(); }

void apply_env_log_options(sfs::log::Options* opts) {
    if (const char* v = std::getenv("SFS_LOG_LEVEL")) {
        sfs::log::Level level;
        if (!sfs::log::parse_level(v, &level)) {
            throw std::runtime_error("SFS_LOG_LEVEL 取值必须是 debug/info/warn/error");
        }
        opts->level = level;
    }
    if (const char* v = std::getenv("SFS_LOG_CATEGORIES")) {
        apply_category_csv(opts, v, true);
    }
    if (const char* v = std::getenv("SFS_LOG_HIDE_CATEGORIES")) {
        apply_category_csv(opts, v, false);
    }
    if (const char* v = std::getenv("SFS_LOG_COLOR")) {
        sfs::log::ColorMode mode;
        if (!sfs::log::parse_color_mode(v, &mode)) {
            throw std::runtime_error("SFS_LOG_COLOR 取值必须是 auto/on/off");
        }
        opts->color_mode = mode;
    }
}

void apply_cli_log_options(int argc, char* argv[], sfs::log::Options* opts) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto take_value = [&](const char* flag) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error(std::string(flag) + " 缺少值");
            return std::string(argv[++i]);
        };

        if (arg == "--log-level") {
            const std::string value = take_value("--log-level");
            sfs::log::Level level;
            if (!sfs::log::parse_level(value, &level)) {
                throw std::runtime_error("--log-level 取值必须是 debug/info/warn/error");
            }
            opts->level = level;
        } else if (arg == "--log-categories") {
            apply_category_csv(opts, take_value("--log-categories"), true);
        } else if (arg == "--log-hide-categories") {
            apply_category_csv(opts, take_value("--log-hide-categories"), false);
        } else if (arg == "--log-color") {
            const std::string value = take_value("--log-color");
            sfs::log::ColorMode mode;
            if (!sfs::log::parse_color_mode(value, &mode)) {
                throw std::runtime_error("--log-color 取值必须是 auto/on/off");
            }
            opts->color_mode = mode;
        } else if (arg == "--log-quiet-startup") {
            opts->quiet_startup = true;
        }
    }
}

}  // namespace sfs::config
