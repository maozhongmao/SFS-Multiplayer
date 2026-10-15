// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "part_packets.hpp"
#include "protocol/types.hpp"
#include "protocol/models/state.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

// ------------------------------------------------------------
// DestroyPartPacket
// ------------------------------------------------------------
void DestroyPartPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(create_explosion);
    w.write_u8(reason);
}
void DestroyPartPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    create_explosion = r.read_bool();
    reason = r.read_u8();
}

// ------------------------------------------------------------
// UpdateStagingPacket
// ------------------------------------------------------------
void UpdateStagingPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(static_cast<std::int32_t>(stages.size()));
    for (const auto& s : stages) s.serialize(w);
}
void UpdateStagingPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    stages.resize(r.read_i32());
    for (auto& s : stages) s.deserialize(r);
}

// ------------------------------------------------------------
// UpdatePartEnginePacket
// ------------------------------------------------------------
void UpdatePartEnginePacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(engine_on);
}
void UpdatePartEnginePacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    engine_on = r.read_bool();
}

// ------------------------------------------------------------
// UpdatePartWheelPacket
// ------------------------------------------------------------
void UpdatePartWheelPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(wheel_on);
}
void UpdatePartWheelPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    wheel_on = r.read_bool();
}

// ------------------------------------------------------------
// UpdatePartBoosterPacket
// ------------------------------------------------------------
void UpdatePartBoosterPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(primed);
    w.write_f32(throttle);
    w.write_f32(fuel_percent);
}
void UpdatePartBoosterPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    primed = r.read_bool();
    throttle = r.read_f32();
    fuel_percent = r.read_f32();
}

// ------------------------------------------------------------
// UpdatePartParachutePacket
// ------------------------------------------------------------
void UpdatePartParachutePacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_f32(state);
    w.write_f32(target_state);
}
void UpdatePartParachutePacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    state = r.read_f32();
    target_state = r.read_f32();
}

// ------------------------------------------------------------
// UpdatePartMovePacket
// ------------------------------------------------------------
void UpdatePartMovePacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_f32(time);
    w.write_f32(target_time);
}
void UpdatePartMovePacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    time = r.read_f32();
    target_time = r.read_f32();
}

// ------------------------------------------------------------
// UpdatePartResourcePacket
// ------------------------------------------------------------
void UpdatePartResourcePacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_f64(resource_percent);
    w.write_i32(static_cast<std::int32_t>(part_ids.size()));
    for (auto id : part_ids) w.write_i32(id);
}
void UpdatePartResourcePacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    resource_percent = r.read_f64();
    part_ids.clear();
    std::int32_t count = r.read_i32();
    for (std::int32_t i = 0; i < count; ++i) {
        part_ids.insert(r.read_i32());
    }
}

} // namespace sfs::protocol
