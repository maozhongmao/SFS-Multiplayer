// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "state_store.hpp"
#include "json_lite.hpp"

#include <cmath>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

namespace sfs::store {

namespace {

// ------------------------------------------------------------ 浮点 <-> JSON
// 存档用字符串表示非有限浮点，必须双向一致，否则 C# 侧读不懂
std::string number_to_json(double v) {
    if (std::isnan(v)) return "\"NaN\"";
    if (std::isinf(v)) return v > 0 ? "\"Infinity\"" : "\"-Infinity\"";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.17g", v);
    return buf;
}

void write_bool(std::ostringstream& out, bool v) { out << (v ? "true" : "false"); }

// JSON 字符串转义（只处理必须转义的字符）
std::string escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
    return out;
}

// UTC 时间戳（存档里的 SavedAtUtc，仅作信息，读回时不解析）
std::string now_utc_iso() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// ------------------------------------------------------------ 部件读写
sfs::protocol::PartState read_part(const jsonlite::Value& v) {
    sfs::protocol::PartState part;
    part.name = v.get_string("Name");
    part.x = v.get_number("X");
    part.y = v.get_number("Y");
    part.orientation_x = v.get_number("OrientationX", 1.0);
    part.orientation_y = v.get_number("OrientationY", 1.0);
    part.orientation_z = v.get_number("OrientationZ", 0.0);
    part.temperature = static_cast<float>(v.get_number("Temperature",
                                                       -std::numeric_limits<double>::infinity()));

    if (const auto* nv = v.find("NumberVariables"); nv && nv->is_object()) {
        for (const auto& [k, val] : *nv->object) part.number_variables.emplace_back(k, val.as_number());
    }
    if (const auto* tv = v.find("ToggleVariables"); tv && tv->is_object()) {
        for (const auto& [k, val] : *tv->object) part.toggle_variables.emplace_back(k, val.as_bool());
    }
    if (const auto* xv = v.find("TextVariables"); xv && xv->is_object()) {
        for (const auto& [k, val] : *xv->object) part.text_variables.emplace_back(k, val.as_string());
    }
    if (const auto* b = v.find("Burns"); b && b->is_object()) {
        sfs::protocol::BurnMarkState burn;
        burn.angle = b->get_number("Angle");
        burn.intensity = b->get_number("Intensity");
        burn.x = b->get_number("X");
        burn.top = b->get_string("Top");
        burn.bottom = b->get_string("Bottom");
        part.burns = std::move(burn);
    }
    return part;
}

void write_part(std::ostringstream& out, const sfs::protocol::PartState& part) {
    out << '{';
    out << "\"Name\":" << escape(part.name);
    out << ",\"X\":" << number_to_json(part.x);
    out << ",\"Y\":" << number_to_json(part.y);
    out << ",\"OrientationX\":" << number_to_json(part.orientation_x);
    out << ",\"OrientationY\":" << number_to_json(part.orientation_y);
    out << ",\"OrientationZ\":" << number_to_json(part.orientation_z);
    out << ",\"Temperature\":" << number_to_json(part.temperature);

    out << ",\"NumberVariables\":{";
    bool first = true;
    for (const auto& [k, val] : part.number_variables) {
        if (!first) out << ',';
        first = false;
        out << escape(k) << ':' << number_to_json(val);
    }
    out << '}';

    out << ",\"ToggleVariables\":{";
    first = true;
    for (const auto& [k, val] : part.toggle_variables) {
        if (!first) out << ',';
        first = false;
        out << escape(k) << ':';
        write_bool(out, val);
    }
    out << '}';

    out << ",\"TextVariables\":{";
    first = true;
    for (const auto& [k, val] : part.text_variables) {
        if (!first) out << ',';
        first = false;
        out << escape(k) << ':' << escape(val);
    }
    out << '}';

    out << ",\"Burns\":";
    if (part.burns.has_value()) {
        const auto& b = *part.burns;
        out << "{\"Angle\":" << number_to_json(b.angle)
            << ",\"Intensity\":" << number_to_json(b.intensity)
            << ",\"X\":" << number_to_json(b.x)
            << ",\"Top\":" << escape(b.top)
            << ",\"Bottom\":" << escape(b.bottom) << '}';
    } else {
        out << "null";
    }
    out << '}';
}

// ------------------------------------------------------------ 火箭读写
sfs::protocol::RocketState read_rocket(const jsonlite::Value& v) {
    sfs::protocol::RocketState r;
    r.rocket_name = v.get_string("RocketName");

    if (const auto* loc = v.find("Location"); loc && loc->is_object()) {
        r.location.x = loc->get_number("X");
        r.location.y = loc->get_number("Y");
        r.location.vx = loc->get_number("Vx");
        r.location.vy = loc->get_number("Vy");
        r.location.address = loc->get_string("Address", "Earth");
    }
    r.rotation = v.get_number("Rotation");
    r.angular_velocity = v.get_number("AngularVelocity");
    r.throttle_on = v.get_bool("ThrottleOn");
    r.throttle_percent = v.get_number("ThrottlePercent");
    r.rcs = v.get_bool("Rcs");

    if (const auto* parts = v.find("Parts"); parts && parts->is_object()) {
        for (const auto& [pid_text, pv] : *parts->object) {
            try {
                r.parts.emplace_back(static_cast<std::int32_t>(std::stoll(pid_text)), read_part(pv));
            } catch (...) {
                // 非法部件 ID 跳过，不让单个坏条目毁掉整份存档
            }
        }
    }

    if (const auto* joints = v.find("Joints"); joints && joints->is_array()) {
        for (const auto& jv : *joints->array) {
            sfs::protocol::JointState j;
            j.part_a = jv.get_int32("PartA");
            j.part_b = jv.get_int32("PartB");
            r.joints.push_back(j);
        }
    }

    if (const auto* stages = v.find("Stages"); stages && stages->is_array()) {
        for (const auto& sv : *stages->array) {
            sfs::protocol::StageState s;
            s.stage_id = sv.get_int32("StageId");
            if (const auto* ids = sv.find("PartIds"); ids && ids->is_array()) {
                for (const auto& idv : *ids->array) s.part_ids.push_back(idv.as_int32());
            }
            r.stages.push_back(std::move(s));
        }
    }
    return r;
}

void write_rocket(std::ostringstream& out, const sfs::protocol::RocketState& r) {
    out << '{';
    out << "\"RocketName\":" << escape(r.rocket_name);
    out << ",\"Location\":{\"X\":" << number_to_json(r.location.x)
        << ",\"Y\":" << number_to_json(r.location.y)
        << ",\"Vx\":" << number_to_json(r.location.vx)
        << ",\"Vy\":" << number_to_json(r.location.vy)
        << ",\"Address\":" << escape(r.location.address) << '}';
    out << ",\"Rotation\":" << number_to_json(r.rotation);
    out << ",\"AngularVelocity\":" << number_to_json(r.angular_velocity);
    out << ",\"ThrottleOn\":";
    write_bool(out, r.throttle_on);
    out << ",\"ThrottlePercent\":" << number_to_json(r.throttle_percent);
    out << ",\"Rcs\":";
    write_bool(out, r.rcs);

    out << ",\"Parts\":{";
    bool first = true;
    for (const auto& [pid, part] : r.parts) {
        if (!first) out << ',';
        first = false;
        out << '"' << pid << "\":";
        write_part(out, part);
    }
    out << '}';

    out << ",\"Joints\":[";
    first = true;
    for (const auto& j : r.joints) {
        if (!first) out << ',';
        first = false;
        out << "{\"PartA\":" << j.part_a << ",\"PartB\":" << j.part_b << '}';
    }
    out << ']';

    out << ",\"Stages\":[";
    first = true;
    for (const auto& s : r.stages) {
        if (!first) out << ',';
        first = false;
        out << "{\"StageId\":" << s.stage_id << ",\"PartIds\":[";
        bool first_id = true;
        for (auto id : s.part_ids) {
            if (!first_id) out << ',';
            first_id = false;
            out << id;
        }
        out << "]}";
    }
    out << ']';
    out << '}';
}

} // namespace

// ------------------------------------------------------------ 加载

namespace {

// 体积探测：必须在读入内存之前做，否则"先整个读进来再判断是否过大"本身就足以让目标设备 OOM。
bool file_too_large(const std::string& path, std::int64_t* out_size, std::string* error) {
    std::ifstream probe(path, std::ios::binary);
    if (!probe) {
        if (error) *error = "存档不存在或无法打开: " + path;
        return true;
    }
    probe.seekg(0, std::ios::end);
    const auto size = static_cast<std::int64_t>(probe.tellg());
    *out_size = size;
    if (size > kMaxStateFileBytes) {
        if (error) {
            *error = "存档体积超限: " + std::to_string(size) + " 字节（上限 " +
                     std::to_string(kMaxStateFileBytes) + "）: " + path;
        }
        return true;
    }
    return false;
}

// 结构自愈：删掉指向不存在零件的 joints / stage 项。
// 这类数据一旦进权威世界，客户端重建火箭时会因为缺零件直接整枚丢弃，表现是"火箭凭空消失"。
// 与其整体拒收整个存档，不如修掉坏引用并如实统计，把丢了多少写进日志。
void repair_rocket(sfs::protocol::RocketState& r, std::size_t* dropped_joints,
                   std::size_t* dropped_stage_parts) {
    auto part_exists = [&r](std::int32_t id) {
        for (const auto& [pid, part] : r.parts) {
            (void)part;
            if (pid == id) return true;
        }
        return false;
    };

    std::vector<sfs::protocol::JointState> ok_joints;
    ok_joints.reserve(r.joints.size());
    for (const auto& j : r.joints) {
        if (part_exists(j.part_a) && part_exists(j.part_b)) {
            ok_joints.push_back(j);
        } else {
            ++*dropped_joints;
        }
    }
    r.joints = std::move(ok_joints);

    for (auto& stage : r.stages) {
        std::vector<std::int32_t> ok_ids;
        ok_ids.reserve(stage.part_ids.size());
        for (auto id : stage.part_ids) {
            if (part_exists(id)) {
                ok_ids.push_back(id);
            } else {
                ++*dropped_stage_parts;
            }
        }
        stage.part_ids = std::move(ok_ids);
    }
}

}  // namespace

std::optional<SavedWorld> load_state(const std::string& path, std::string* error) {
    std::int64_t file_size = 0;
    if (file_too_large(path, &file_size, error)) return std::nullopt;
    if (file_size == 0) {
        if (error) *error = "存档为空: " + path;
        return std::nullopt;
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "存档无法打开: " + path;
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    jsonlite::Value root;
    try {
        root = jsonlite::parse(text);
    } catch (const std::exception& e) {
        if (error) *error = std::string("存档 JSON 解析失败: ") + e.what();
        return std::nullopt;
    }
    if (!root.is_object()) {
        if (error) *error = "存档根节点不是 JSON 对象";
        return std::nullopt;
    }

    // ---- Schema 校验：结构版本不匹配必须拒收，不允许带着未知结构继续跑
    if (const auto* schema = root.find("SchemaVersion"); !schema || !schema->is_number()) {
        if (error) *error = "存档缺少 SchemaVersion 字段";
        return std::nullopt;
    }
    const std::int32_t schema_version = root.get_int32("SchemaVersion");
    if (schema_version != kSupportedSchemaVersion) {
        if (error) {
            *error = "存档 SchemaVersion 不支持: " + std::to_string(schema_version) +
                     "（当前只支持 " + std::to_string(kSupportedSchemaVersion) + "）";
        }
        return std::nullopt;
    }

    const jsonlite::Value* world = root.find("World");
    if (!world || !world->is_object()) {
        if (error) *error = "存档缺少 World 对象";
        return std::nullopt;
    }

    SavedWorld out;
    out.world_time = world->get_number("WorldTime");
    // WorldTime 非有限值会让服务端时钟整个失效：此后每个包盖的时间戳都是 NaN，
    // 客户端插值判定陈旧后会无限请求快照（实测把帧率打到 1）。宁可拒收。
    if (!std::isfinite(out.world_time)) {
        if (error) *error = "存档 WorldTime 非有限值";
        return std::nullopt;
    }
    out.difficulty = world->get_int32("Difficulty");
    if (out.difficulty < kDifficultyMin || out.difficulty > kDifficultyMax) {
        if (error) *error = "存档 Difficulty 非法: " + std::to_string(out.difficulty);
        return std::nullopt;
    }

    const auto max_count = static_cast<std::size_t>(sfs::protocol::kMaxCollectionCount);
    const jsonlite::Value* rockets = world->find("Rockets");
    if (rockets && rockets->is_object()) {
        if (rockets->object->size() > max_count) {
            if (error) *error = "存档火箭数量超过上限";
            return std::nullopt;
        }
        std::size_t dropped_joints = 0;
        std::size_t dropped_stage_parts = 0;
        for (const auto& [rid_text, rv] : *rockets->object) {
            try {
                auto rocket = read_rocket(rv);
                if (rocket.parts.size() > max_count) {
                    if (error) *error = "存档单枚火箭的部件数超过上限";
                    return std::nullopt;
                }
                repair_rocket(rocket, &dropped_joints, &dropped_stage_parts);
                out.rockets[static_cast<std::int32_t>(std::stoll(rid_text))] = std::move(rocket);
            } catch (...) {
                // 单个坏条目跳过，不让一枚坏火箭毁掉整份存档
            }
        }
        if (dropped_joints > 0 || dropped_stage_parts > 0) {
            std::printf("[cpp-server] 存档自愈: 丢弃非法 joints=%zu stage 零件=%zu（路径=%s）\n",
                        dropped_joints, dropped_stage_parts, path.c_str());
            std::fflush(stdout);
        }
    }
    return out;
}

// ------------------------------------------------------------ 保存
bool save_state(const std::string& path, const SavedWorld& world, std::string* error) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"SchemaVersion\": 1,\n";
    out << "  \"SavedAtUtc\": \"" << now_utc_iso() << "\",\n";
    out << "  \"World\": {\n";
    out << "    \"WorldTime\": " << number_to_json(world.world_time) << ",\n";
    out << "    \"Difficulty\": " << world.difficulty << ",\n";
    out << "    \"Rockets\": {";
    bool first = true;
    for (const auto& [rid, r] : world.rockets) {
        if (!first) out << ',';
        first = false;
        out << "\n      \"" << rid << "\": ";
        write_rocket(out, r);
    }
    out << "\n    }\n";
    out << "  }\n";
    out << "}\n";

    // 先写临时文件再原子改名：掉电不会把原存档写坏
    const std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            if (error) *error = "无法写入临时存档: " + tmp;
            return false;
        }
        const std::string text = out.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!f) {
            if (error) *error = "写入临时存档失败: " + tmp;
            return false;
        }
    }

    // 保留上一份为 .bak（与 C# 侧一致），再改名
    std::remove((path + ".bak").c_str());
    std::rename(path.c_str(), (path + ".bak").c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        if (error) *error = "原子改名失败: " + tmp + " -> " + path;
        return false;
    }
    return true;
}

// ------------------------------------------------------------ 坏档回退
LoadResult load_state_with_fallback(const std::string& path) {
    LoadResult out;

    std::string primary_error;
    if (auto world = load_state(path, &primary_error); world.has_value()) {
        out.ok = true;
        out.source = LoadSource::kPrimary;
        out.path_used = path;
        out.world = std::move(*world);
        return out;
    }
    out.primary_error = primary_error;

    // 主档读不出来：先把它挪走隔离，再试 .bak。
    // 不隔离的话下一次自动保存会把这份坏档轮转成 .bak，把唯一完好的备份顶掉。
    const std::string backup = path + ".bak";
    std::ifstream probe(path, std::ios::binary);
    const bool primary_exists = probe.good();
    probe.close();
    if (primary_exists) {
        // 隔离文件名必须唯一：Windows 的 rename 不覆盖已存在的目标文件，
        // 同一秒内第二次隔离会因为撞名直接失败 —— 于是坏档留在原地，
        // 下一次自动保存就把它轮转成 .bak，把唯一完好的备份顶掉（这正是要防的那件事）。
        const auto stamp = static_cast<long long>(std::time(nullptr));
        bool isolated_ok = false;
        for (int attempt = 0; attempt < 100 && !isolated_ok; ++attempt) {
            std::string candidate = path + ".corrupt-" + std::to_string(stamp);
            if (attempt > 0) candidate += "-" + std::to_string(attempt);
            std::ifstream clash(candidate, std::ios::binary);
            if (clash.good()) continue;               // 目标已存在，换下一个后缀
            clash.close();
            if (std::rename(path.c_str(), candidate.c_str()) == 0) {
                out.isolated_path = candidate;
                isolated_ok = true;
            }
        }
        if (!isolated_ok) {
            // 改名失败（权限/跨文件系统等）不能算致命：继续回退，但如实记录。
            out.isolated_path.clear();
            std::printf("[cpp-server] 警告: 坏档无法隔离，下一次自动保存可能覆盖 .bak（路径=%s）\n",
                        path.c_str());
            std::fflush(stdout);
        }
    }

    std::string backup_error;
    if (auto world = load_state(backup, &backup_error); world.has_value()) {
        out.ok = true;
        out.source = LoadSource::kBackup;
        out.path_used = backup;
        out.world = std::move(*world);
        return out;
    }
    out.backup_error = backup_error;

    // 两份都没能用：明确返回失败，由调用方决定是空世界开局还是拒绝启动。
    // 绝不在这一步删除任何文件 —— 数据还在盘上，人可以介入。
    return out;
}

} // namespace sfs::store