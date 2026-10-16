// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/models/state.hpp"
#include <string>
#include <optional>
#include <filesystem>

namespace sfs::protocol {

// ------------------------------------------------------------
// 世界加载/保存接口（嵌入式板端用二进制格式，不解析 JSON）
// 对应 C# SfsWorldLoader.cs，但去掉了 System.Text.Json 依赖
// ------------------------------------------------------------

// 二进制存档版本（用于向前兼容）
constexpr std::int32_t kWorldSaveVersion = 1;

// 从二进制文件加载世界快照
// 格式：
//   int32  version
//   int64  world_time
//   uint8  difficulty
//   int32  rocket_count
//   [RocketState x rocket_count]
std::optional<WorldSnapshot> load_world_binary(const std::filesystem::path& path);

// 将世界快照保存为二进制文件
bool save_world_binary(const std::filesystem::path& path, const WorldSnapshot& snapshot);

// 兼容层：从 C# JSON 存档转换为二进制（仅开发期用，板端不需要）
// 实际部署时由开发机跑一次，把生成的二进制文件拷到目标设备上
std::optional<WorldSnapshot> load_world_from_json(const std::filesystem::path& world_folder);

} // namespace sfs::protocol
