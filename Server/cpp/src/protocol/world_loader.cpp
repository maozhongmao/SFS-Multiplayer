// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "world_loader.hpp"
#include "protocol/models/state.hpp"
#include "protocol/types.hpp"
#include "net/lidgren_buffer.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>

namespace sfs::protocol {
namespace net = sfs::net;

std::optional<WorldSnapshot> load_world_binary(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;

    std::int32_t version = 0;
    in.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (!in || version != kWorldSaveVersion) return std::nullopt;

    WorldSnapshot snapshot;
    in.read(reinterpret_cast<char*>(&snapshot.world_time), sizeof(snapshot.world_time));
    std::uint8_t diff = 0;
    in.read(reinterpret_cast<char*>(&diff), sizeof(diff));
    snapshot.difficulty = static_cast<DifficultyType>(diff);

    std::int32_t rocket_count = 0;
    in.read(reinterpret_cast<char*>(&rocket_count), sizeof(rocket_count));
    if (rocket_count < 0) return std::nullopt;

    for (std::int32_t i = 0; i < rocket_count; ++i) {
        std::int32_t id = 0;
        in.read(reinterpret_cast<char*>(&id), sizeof(id));
        RocketState rs;
        // 简化：二进制加载需要完整的 Reader，这里留存根
        snapshot.rockets.emplace_back(id, std::move(rs));
    }
    return snapshot;
}

bool save_world_binary(const std::filesystem::path& path, const WorldSnapshot& snapshot) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;

    std::int32_t version = kWorldSaveVersion;
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&snapshot.world_time), sizeof(snapshot.world_time));
    std::uint8_t diff = static_cast<std::uint8_t>(snapshot.difficulty);
    out.write(reinterpret_cast<const char*>(&diff), sizeof(diff));
    std::int32_t count = static_cast<std::int32_t>(snapshot.rockets.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& kv : snapshot.rockets) {
        out.write(reinterpret_cast<const char*>(&kv.first), sizeof(kv.first));
        // 完整序列化需要 Writer，存根
    }
    return static_cast<bool>(out);
}

std::optional<WorldSnapshot> load_world_from_json(const std::filesystem::path& world_folder) {
    // 存根：实际项目中用 JSON 解析库（如 simdjson/nlohmann_json）
    return std::nullopt;
}

} // namespace sfs::protocol
