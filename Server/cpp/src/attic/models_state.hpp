// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/types.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <algorithm>

namespace sfs::protocol {

// ------------------------------------------------------------
// 前向声明
// ------------------------------------------------------------
struct UpdateRocketPrimaryPacket;
struct UpdateRocketSecondaryPacket;

// ------------------------------------------------------------
// 烧痕状态（对齐 C# BurnMarkState）
// ------------------------------------------------------------
struct BurnMarkState {
    float angle = 0.0f;
    float intensity = 0.0f;
    float x = 0.0f;
    std::string top;
    std::string bottom;

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

// ------------------------------------------------------------
// 部件状态（对齐 C# PartState）
// ------------------------------------------------------------
struct PartState {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float orientation_x = 1.0f;
    float orientation_y = 1.0f;
    float orientation_z = 0.0f;
    float temperature = -std::numeric_limits<float>::infinity();

    std::unordered_map<std::string, double> number_variables;
    std::unordered_map<std::string, bool> toggle_variables;
    std::unordered_map<std::string, std::string> text_variables;
    std::optional<BurnMarkState> burns;

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

// ------------------------------------------------------------
// 关节状态（对齐 C# JointState）
// ------------------------------------------------------------
struct JointState {
    std::int32_t part_a = 0;
    std::int32_t part_b = 0;

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

// ------------------------------------------------------------
// 分级状态（对齐 C# StageState）
// ------------------------------------------------------------
struct StageState {
    std::int32_t stage_id = 0;
    std::vector<std::int32_t> part_ids;

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

// ------------------------------------------------------------
// 火箭状态（对齐 C# RocketState）
// ------------------------------------------------------------
struct RocketState {
    std::string rocket_name;
    NetLocation location = NetLocation::Empty();
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    bool throttle_on = false;
    float throttle_percent = 0.0f;
    bool rcs = false;

    std::unordered_map<std::int32_t, PartState> parts;
    std::vector<JointState> joints;
    std::vector<StageState> stages;

    void apply(const UpdateRocketPrimaryPacket& pkt);
    void apply(const UpdateRocketSecondaryPacket& pkt);
    bool remove_part(std::int32_t part_id);

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

// ------------------------------------------------------------
// 世界快照（对齐 C# WorldSnapshot）
// ------------------------------------------------------------
struct WorldSnapshot {
    double world_time = 0.0;
    DifficultyType difficulty = DifficultyType::Normal;
    std::unordered_map<std::int32_t, RocketState> rockets;

    void serialize(net::NetWriter& w) const;
    void deserialize(net::NetReader& r);
};

} // namespace sfs::protocol
