// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 世界存档读写（与 C# ServerStateStore 的 JSON 格式互通）。
//
// 格式实测自板端真实存档（data
//   { "SchemaVersion": 1, "SavedAtUtc": "...",
//     "World": { "WorldTime": <double>, "Difficulty": <int>,
//                "Rockets": { "<id>": { "RocketName", "Location":{X,Y,Vx,Vy,Address},
//                                       "Rotation", "AngularVelocity",
//                                       "ThrottleOn", "ThrottlePercent", "Rcs",
//                                       "Parts": { "<id>": { "Name","X","Y",
//                                            "OrientationX","OrientationY","OrientationZ",
//                                            "Temperature","NumberVariables","ToggleVariables",
//                                            "TextVariables","Burns" } },
//                                       "Joints":[{"PartA","PartB"}],
//                                       "Stages":[{"StageId","PartIds":[]}] } } } }
//
// 类型必须来自 protocol/packets.hpp：Lidgren 位流要求序列化顺序确定，因此那边用
// vector<pair> 保序；protocol/models/state.hpp 里那份 unordered_map 版本布局不同，
// 同名类型混用会因 ODR 违规把内存解释错乱（曾直接导致进程崩溃）。
//
// 非有限浮点在存档里是字符串形式："-Infinity" / "Infinity" / "NaN"
// （System.Text.Json 的写法，读写两侧都必须按同一约定处理）。

#pragma once

#include "protocol/packets.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace sfs::store {

struct SavedWorld {
    double world_time = 0.0;
    std::int32_t difficulty = 0;
    std::unordered_map<std::int32_t, sfs::protocol::RocketState> rockets;
};

// ---------------------------------------------------------------- 存档健壮性
// （对齐 server.yml 的 auto_save_seconds 自保逻辑，并补齐 C# ServerStateStore 没有的校验）

// 单份存档的体积上限。目标设备上任何一次异常写出都可能把存档撑大，超过就拒收：
// 目标设备总共只有几十 MB，为了解析一个异常大的文件耗尽内存比丢一次存档更糟。
constexpr std::int64_t kMaxStateFileBytes = 64LL * 1024 * 1024;   // 64 MB

// 支持的存档结构版本。写侧固定写 1；读侧遇到更高版本必须拒收 ——
// 否则未知字段会被静默丢弃，下一次自动保存就把结构更新的世界覆盖回残缺版本。
constexpr std::int32_t kSupportedSchemaVersion = 1;

// Difficulty 的合法取值（对齐 SfsMultiplayer.Protocol.DifficultyType）：
// 0=Normal / 1=Easy / 2=Hard。非法值会让客户端 Difficulty 解析失败。
constexpr std::int32_t kDifficultyMin = 0;
constexpr std::int32_t kDifficultyMax = 2;

// 从 JSON 存档加载。文件不存在或无法解析时返回 nullopt，并把原因写入 error。
// 会做体积上限、SchemaVersion、World 结构、WorldTime/Difficulty 与集合规模的校验；
// 不通过一律返回 nullopt（不返回半截世界）。
std::optional<SavedWorld> load_state(const std::string& path, std::string* error = nullptr);

// 保存为 JSON 存档（先写 .tmp 再原子改名，避免掉电写坏存档）。
bool save_state(const std::string& path, const SavedWorld& world, std::string* error = nullptr);

// ---------------------------------------------------------------- 坏档回退
// 加载结果：区分"用了主档"还是"用了 .bak 备份"，让调用方能如实打日志
// —— 静默回退到备份会让"为什么世界回到一小时前"永远查不出来。
enum class LoadSource {
    kNone,      // 两份都没能用
    kPrimary,   // 主档正常
    kBackup,    // 主档损坏，已回退到 .bak
};

struct LoadResult {
    bool ok = false;
    LoadSource source = LoadSource::kNone;
    std::string path_used;        // 实际读取的那份文件路径
    std::string isolated_path;    // 被隔离开的坏档新名字（没有则空）
    std::string primary_error;
    std::string backup_error;
    // 结构性自愈的统计数：软修复丢掉了多少条数据 —— 必须让调用方知道发生过，
    // 否则"某枚火箭的分级少了一个零件"会被当成服务端 bug 反复排查。
    std::size_t dropped_joints = 0;
    std::size_t dropped_stage_parts = 0;
    SavedWorld world;
};

// 先读主档；主档不存在/损坏/校验不过则自动回退到 <path>.bak。
//
// 回退前必须先把坏档改名隔离（<path>.corrupt-<unix秒>），否则下一次自动保存会把
// 主档（坏的那份）轮转成 .bak，把唯一还完好的备份顶掉 —— 那就不是回退，是二次事故。
LoadResult load_state_with_fallback(const std::string& path);

} // namespace sfs::store
