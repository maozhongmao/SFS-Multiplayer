// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "state.hpp"
#include "protocol/types.hpp"
#include "protocol/models/packets.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

// ------------------------------------------------------------
// BurnMarkState
// ------------------------------------------------------------
void BurnMarkState::serialize(net::NetWriter& w) const {
    w.write_f32(angle);
    w.write_f32(intensity);
    w.write_f32(x);
    write_string_bounded(w, top);
    write_string_bounded(w, bottom);
}

void BurnMarkState::deserialize(net::NetReader& r) {
    angle = r.read_f32();
    intensity = r.read_f32();
    x = r.read_f32();
    top = read_string_bounded(r);
    bottom = read_string_bounded(r);
}

// ------------------------------------------------------------
// PartState
// ------------------------------------------------------------
void PartState::serialize(net::NetWriter& w) const {
    write_string_bounded(w, name);
    w.write_f32(x);
    w.write_f32(y);
    w.write_f32(orientation_x);
    w.write_f32(orientation_y);
    w.write_f32(orientation_z);
    w.write_f32(temperature);

    // number_variables: unordered_map<string, double>
    w.write_i32(static_cast<std::int32_t>(number_variables.size()));
    for (const auto& kv : number_variables) {
        write_string_bounded(w, kv.first);
        w.write_f64(kv.second);
    }

    // toggle_variables: unordered_map<string, bool>
    w.write_i32(static_cast<std::int32_t>(toggle_variables.size()));
    for (const auto& kv : toggle_variables) {
        write_string_bounded(w, kv.first);
        w.write_bool(kv.second);
    }

    // text_variables: unordered_map<string, string>
    w.write_i32(static_cast<std::int32_t>(text_variables.size()));
    for (const auto& kv : text_variables) {
        write_string_bounded(w, kv.first);
        write_string_bounded(w, kv.second);
    }

    if (burns.has_value()) {
        w.write_bool(false);
        burns->serialize(w);
    } else {
        w.write_bool(true);
    }
}

void PartState::deserialize(net::NetReader& r) {
    name = read_string_bounded(r);
    x = r.read_f32();
    y = r.read_f32();
    orientation_x = r.read_f32();
    orientation_y = r.read_f32();
    orientation_z = r.read_f32();
    temperature = r.read_f32();

    // number_variables
    number_variables.clear();
    std::int32_t nv_count = r.read_i32();
    for (std::int32_t i = 0; i < nv_count; ++i) {
        std::string key = read_string_bounded(r);
        double val = r.read_f64();
        number_variables.emplace(std::move(key), val);
    }

    // toggle_variables
    toggle_variables.clear();
    std::int32_t tv_count = r.read_i32();
    for (std::int32_t i = 0; i < tv_count; ++i) {
        std::string key = read_string_bounded(r);
        bool val = r.read_bool();
        toggle_variables.emplace(std::move(key), val);
    }

    // text_variables
    text_variables.clear();
    std::int32_t tx_count = r.read_i32();
    for (std::int32_t i = 0; i < tx_count; ++i) {
        std::string key = read_string_bounded(r);
        std::string val = read_string_bounded(r);
        text_variables.emplace(std::move(key), std::move(val));
    }

    bool is_null = r.read_bool();
    if (is_null) {
        burns.reset();
    } else {
        BurnMarkState b;
        b.deserialize(r);
        burns = std::move(b);
    }
}

// ------------------------------------------------------------
// JointState
// ------------------------------------------------------------
void JointState::serialize(net::NetWriter& w) const {
    w.write_i32(part_a);
    w.write_i32(part_b);
}
void JointState::deserialize(net::NetReader& r) {
    part_a = r.read_i32();
    part_b = r.read_i32();
}

// ------------------------------------------------------------
// StageState
// ------------------------------------------------------------
void StageState::serialize(net::NetWriter& w) const {
    w.write_i32(stage_id);
    w.write_i32(static_cast<std::int32_t>(part_ids.size()));
    for (auto id : part_ids) w.write_i32(id);
}
void StageState::deserialize(net::NetReader& r) {
    stage_id = r.read_i32();
    part_ids.resize(r.read_i32());
    for (auto& id : part_ids) id = r.read_i32();
}

// ------------------------------------------------------------
// RocketState
// ------------------------------------------------------------
void RocketState::apply(const UpdateRocketPrimaryPacket& pkt) {
    location = pkt.location;
    rotation = pkt.rotation;
    angular_velocity = pkt.angular_velocity;
}
void RocketState::apply(const UpdateRocketSecondaryPacket& pkt) {
    throttle_percent = pkt.throttle_percent;
    throttle_on = pkt.throttle_on;
    rcs = pkt.rcs;
}
bool RocketState::remove_part(std::int32_t part_id) {
    auto it = parts.find(part_id);
    if (it == parts.end()) return false;
    parts.erase(it);
    joints.erase(
        std::remove_if(joints.begin(), joints.end(),
            [part_id](const JointState& j) {
                return j.part_a == part_id || j.part_b == part_id;
            }),
        joints.end());
    for (auto& stage : stages) {
        stage.part_ids.erase(
            std::remove(stage.part_ids.begin(), stage.part_ids.end(), part_id),
            stage.part_ids.end());
    }
    return true;
}

void RocketState::serialize(net::NetWriter& w) const {
    write_string_bounded(w, rocket_name);
    write_netlocation(w, location);
    w.write_f32(rotation);
    w.write_f32(angular_velocity);
    w.write_bool(throttle_on);
    w.write_f32(throttle_percent);
    w.write_bool(rcs);

    // parts
    w.write_i32(static_cast<std::int32_t>(parts.size()));
    for (const auto& kv : parts) {
        w.write_i32(kv.first);
        kv.second.serialize(w);
    }

    // joints
    w.write_i32(static_cast<std::int32_t>(joints.size()));
    for (const auto& j : joints) j.serialize(w);

    // stages
    w.write_i32(static_cast<std::int32_t>(stages.size()));
    for (const auto& s : stages) s.serialize(w);
}

void RocketState::deserialize(net::NetReader& r) {
    rocket_name = read_string_bounded(r);
    location = read_netlocation(r);
    rotation = r.read_f32();
    angular_velocity = r.read_f32();
    throttle_on = r.read_bool();
    throttle_percent = r.read_f32();
    rcs = r.read_bool();

    parts.clear();
    std::int32_t parts_count = r.read_i32();
    for (std::int32_t i = 0; i < parts_count; ++i) {
        std::int32_t id = r.read_i32();
        PartState ps;
        ps.deserialize(r);
        parts.emplace(id, std::move(ps));
    }

    joints.resize(r.read_i32());
    for (auto& j : joints) j.deserialize(r);

    stages.resize(r.read_i32());
    for (auto& s : stages) s.deserialize(r);
}

// ------------------------------------------------------------
// WorldSnapshot
// ------------------------------------------------------------
void WorldSnapshot::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_u8(static_cast<std::uint8_t>(difficulty));
    w.write_i32(static_cast<std::int32_t>(rockets.size()));
    for (const auto& kv : rockets) {
        w.write_i32(kv.first);
        kv.second.serialize(w);
    }
}

void WorldSnapshot::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    difficulty = static_cast<DifficultyType>(r.read_u8());
    rockets.clear();
    std::int32_t count = r.read_i32();
    for (std::int32_t i = 0; i < count; ++i) {
        std::int32_t id = r.read_i32();
        RocketState rs;
        rs.deserialize(r);
        rockets.emplace(id, std::move(rs));
    }
}

} // namespace sfs::protocol
