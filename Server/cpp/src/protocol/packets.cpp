// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "packets.hpp"

#include <algorithm>

namespace sfs::protocol {

using sfs::net::BufferError;
using sfs::net::NetReader;
using sfs::net::NetWriter;

namespace {

// 集合计数在 C# 侧是 message.Write(int)，即 4 字节小端定长，不是 7 位变长。
void WriteCount(NetWriter& w, std::size_t count) {
    if (count > static_cast<std::size_t>(kMaxCollectionCount))
        throw BufferError("集合元素过多。");
    w.write_i32(static_cast<std::int32_t>(count));
}

std::int32_t ReadCount(NetReader& r) {
    const auto count = r.read_i32();
    if (count < 0 || count > kMaxCollectionCount)
        throw BufferError("集合计数非法。");
    return count;
}

std::string ReadStringBounded(NetReader& r) {
    auto value = r.read_string();
    if (value.size() > static_cast<std::size_t>(kMaxStringLength))
        throw BufferError("字符串过长。");
    return value;
}

void WriteNetLocation(NetWriter& w, const NetLocation& value) {
    w.write_f64(value.x);
    w.write_f64(value.y);
    w.write_f64(value.vx);
    w.write_f64(value.vy);
    w.write_string(value.address);
}

NetLocation ReadNetLocation(NetReader& r) {
    NetLocation value;
    value.x = r.read_f64();
    value.y = r.read_f64();
    value.vx = r.read_f64();
    value.vy = r.read_f64();
    value.address = ReadStringBounded(r);
    return value;
}

void WriteColor3(NetWriter& w, const Color3& value) {
    w.write_f32(value.r);
    w.write_f32(value.g);
    w.write_f32(value.b);
}

Color3 ReadColor3(NetReader& r) {
    Color3 value;
    value.r = r.read_f32();
    value.g = r.read_f32();
    value.b = r.read_f32();
    return value;
}

void WriteJoint(NetWriter& w, const JointState& joint) {
    w.write_i32(joint.part_a);
    w.write_i32(joint.part_b);
}

JointState ReadJoint(NetReader& r) {
    JointState joint;
    joint.part_a = r.read_i32();
    joint.part_b = r.read_i32();
    return joint;
}

void WriteStage(NetWriter& w, const StageState& stage) {
    w.write_i32(stage.stage_id);
    WriteCount(w, stage.part_ids.size());
    for (const auto part_id : stage.part_ids) w.write_i32(part_id);
}

StageState ReadStage(NetReader& r) {
    StageState stage;
    stage.stage_id = r.read_i32();
    const auto count = ReadCount(r);
    stage.part_ids.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) stage.part_ids.push_back(r.read_i32());
    return stage;
}

// 定长 rocket_id 列表（P2P 包用，上限 256）。上限只在读侧检查——与 C# 一致：
// P2PPeerOfferPacket.Serialize 直接写 Count 不校验，Deserialize 才检查 0..256。
void WriteRocketIdList(NetWriter& w, const std::vector<std::int32_t>& ids) {
    w.write_i32(static_cast<std::int32_t>(ids.size()));
    for (const auto id : ids) w.write_i32(id);
}

std::vector<std::int32_t> ReadRocketIdList(NetReader& r) {
    const auto count = r.read_i32();
    if (count < 0 || count > kMaxP2PRocketCount)
        throw BufferError("P2P 火箭列表计数非法。");
    std::vector<std::int32_t> ids;
    ids.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) ids.push_back(r.read_i32());
    return ids;
}

}  // namespace

// ---- 枚举合法性 ----

bool IsValidPacketType(std::uint8_t raw) {
    switch (static_cast<PacketType>(raw)) {
        case PacketType::kJoinRequest:
        case PacketType::kJoinResponse:
        case PacketType::kPlayerConnected:
        case PacketType::kPlayerDisconnected:
        case PacketType::kUpdatePlayerControl:
        case PacketType::kUpdatePlayerAuthority:
        case PacketType::kUpdateWorldTime:
        case PacketType::kUpdatePlayerColor:
        case PacketType::kSendChatMessage:
        case PacketType::kCreateRocket:
        case PacketType::kDestroyRocket:
        case PacketType::kUpdateRocketPrimary:
        case PacketType::kUpdateRocketSecondary:
        case PacketType::kDestroyPart:
        case PacketType::kUpdateStaging:
        case PacketType::kUpdatePartEngineModule:
        case PacketType::kUpdatePartWheelModule:
        case PacketType::kUpdatePartBoosterModule:
        case PacketType::kUpdatePartParachuteModule:
        case PacketType::kUpdatePartMoveModule:
        case PacketType::kUpdatePartResourceModule:
        case PacketType::kShowToastMessage:
        case PacketType::kDockTransaction:
        case PacketType::kTimeWarp:
        case PacketType::kExperimentalAccess:
        case PacketType::kP2PPeerOffer:
            return true;
        case PacketType::kRenameRocket:
        case PacketType::kUpdatePartTemperature:
        case PacketType::kUpdateDestructionReason:
        case PacketType::kServerCommand:
        case PacketType::kUpdateCheatStatus:
        case PacketType::kP2PEndpoint:
        case PacketType::kUploadLog:
            return true;
        }
        return false;  // 尤其要挡住 22，那是归档客户端留下的空位
        }

bool IsValidDifficulty(std::uint8_t raw) { return raw <= 2; }

bool IsValidDockOperation(std::uint8_t raw) { return raw <= 1; }

bool IsValidTimeWarpOperation(std::uint8_t raw) { return raw <= 5; }

// ---- PartState ----

void PartState::serialize(NetWriter& w) const {
    w.write_string(name);
    w.write_f32(x);
    w.write_f32(y);
    w.write_f32(orientation_x);
    w.write_f32(orientation_y);
    w.write_f32(orientation_z);
    w.write_f32(temperature);

    WriteCount(w, number_variables.size());
    for (const auto& [key, value] : number_variables) {
        w.write_string(key);
        w.write_f64(value);
    }

    WriteCount(w, toggle_variables.size());
    for (const auto& [key, value] : toggle_variables) {
        w.write_string(key);
        w.write_bool(value);
    }

    WriteCount(w, text_variables.size());
    for (const auto& [key, value] : text_variables) {
        w.write_string(key);
        w.write_string(value);
    }

    // 先写一个 "是否为空"，为真时后面没有内容
    w.write_bool(!burns.has_value());
    if (burns.has_value()) {
        w.write_f32(burns->angle);
        w.write_f32(burns->intensity);
        w.write_f32(burns->x);
        w.write_string(burns->top);
        w.write_string(burns->bottom);
    }
}

void PartState::deserialize(NetReader& r) {
    name = ReadStringBounded(r);
    x = r.read_f32();
    y = r.read_f32();
    orientation_x = r.read_f32();
    orientation_y = r.read_f32();
    orientation_z = r.read_f32();
    temperature = r.read_f32();

    number_variables.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i)
        number_variables.emplace_back(ReadStringBounded(r), r.read_f64());

    toggle_variables.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i)
        toggle_variables.emplace_back(ReadStringBounded(r), r.read_bool());

    text_variables.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) {
        auto key = ReadStringBounded(r);
        text_variables.emplace_back(std::move(key), ReadStringBounded(r));
    }

    if (r.read_bool()) {
        burns.reset();
    } else {
        BurnMarkState burn;
        burn.angle = r.read_f32();
        burn.intensity = r.read_f32();
        burn.x = r.read_f32();
        burn.top = ReadStringBounded(r);
        burn.bottom = ReadStringBounded(r);
        burns = std::move(burn);
    }
}

// ---- RocketState ----

void RocketState::apply_primary(const NetLocation& loc, float rot, float ang_vel) {
    location = loc;
    rotation = rot;
    angular_velocity = ang_vel;
}

void RocketState::apply_secondary(float throttle_pct, bool throttle_enabled, bool rcs_on) {
    throttle_percent = throttle_pct;
    throttle_on = throttle_enabled;
    rcs = rcs_on;
}

bool RocketState::remove_part(std::int32_t part_id) {
    joints.erase(std::remove_if(joints.begin(), joints.end(),
                                [part_id](const JointState& j) {
                                    return j.part_a == part_id || j.part_b == part_id;
                                }),
                 joints.end());
    for (auto& stage : stages) {
        stage.part_ids.erase(
            std::remove(stage.part_ids.begin(), stage.part_ids.end(), part_id),
            stage.part_ids.end());
    }
    const auto it = std::find_if(parts.begin(), parts.end(),
                                 [part_id](const auto& entry) { return entry.first == part_id; });
    if (it == parts.end()) return false;
    parts.erase(it);
    return true;
}

void RocketState::serialize(NetWriter& w) const {
    w.write_string(rocket_name);
    WriteNetLocation(w, location);
    w.write_f32(rotation);
    w.write_f32(angular_velocity);
    w.write_bool(throttle_on);
    w.write_f32(throttle_percent);
    w.write_bool(rcs);

    WriteCount(w, parts.size());
    for (const auto& [id, part] : parts) {
        w.write_i32(id);
        part.serialize(w);
    }

    WriteCount(w, joints.size());
    for (const auto& joint : joints) WriteJoint(w, joint);

    WriteCount(w, stages.size());
    for (const auto& stage : stages) WriteStage(w, stage);
}

void RocketState::deserialize(NetReader& r) {
    rocket_name = ReadStringBounded(r);
    location = ReadNetLocation(r);
    rotation = r.read_f32();
    angular_velocity = r.read_f32();
    throttle_on = r.read_bool();
    throttle_percent = r.read_f32();
    rcs = r.read_bool();

    parts.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) {
        const auto id = r.read_i32();
        PartState part;
        part.deserialize(r);
        parts.emplace_back(id, std::move(part));
    }

    joints.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) joints.push_back(ReadJoint(r));

    stages.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) stages.push_back(ReadStage(r));
}

// ---- 包实现 ----

void JoinRequestPacket::serialize(NetWriter& w) const {
    // resume_player_id / resume_token 不上线，走握手帧（SessionHandshakeCodec）
    w.write_string(username);
    w.write_string(password);
    w.write_string(solar_system_name);
}

void JoinRequestPacket::deserialize(NetReader& r) {
    username = ReadStringBounded(r);
    password = ReadStringBounded(r);
    solar_system_name = ReadStringBounded(r);
}

void JoinResponsePacket::serialize(NetWriter& w) const {
    // 普通 Packet 帧形态：不带 resume_token
    w.write_i32(player_id);
    w.write_f64(update_rockets_period);
    w.write_f64(chat_message_cooldown);
    w.write_f64(world_time);
    w.write_f64(send_time);
    w.write_u8(static_cast<std::uint8_t>(difficulty));
    w.write_string(solar_system_name);
    w.write_string(udp_session_token);
}

void JoinResponsePacket::deserialize(NetReader& r) {
    player_id = r.read_i32();
    update_rockets_period = r.read_f64();
    chat_message_cooldown = r.read_f64();
    world_time = r.read_f64();
    send_time = r.read_f64();
    difficulty = static_cast<DifficultyType>(r.read_u8());
    solar_system_name = ReadStringBounded(r);
    udp_session_token = ReadStringBounded(r);
}

void PlayerConnectedPacket::serialize(NetWriter& w) const {
    w.write_i32(player_id);
    w.write_string(username);
    WriteColor3(w, icon_color);
    w.write_bool(print_message);
}

void PlayerConnectedPacket::deserialize(NetReader& r) {
    player_id = r.read_i32();
    username = ReadStringBounded(r);
    icon_color = ReadColor3(r);
    print_message = r.read_bool();
}

void PlayerDisconnectedPacket::serialize(NetWriter& w) const { w.write_i32(player_id); }

void PlayerDisconnectedPacket::deserialize(NetReader& r) { player_id = r.read_i32(); }

void UpdatePlayerControlPacket::serialize(NetWriter& w) const {
    w.write_i32(player_id);
    w.write_i32(rocket_id);
}

void UpdatePlayerControlPacket::deserialize(NetReader& r) {
    player_id = r.read_i32();
    rocket_id = r.read_i32();
}

void UpdatePlayerAuthorityPacket::serialize(NetWriter& w) const {
    WriteCount(w, rocket_ids.size());
    for (const auto id : rocket_ids) w.write_i32(id);
}

void UpdatePlayerAuthorityPacket::deserialize(NetReader& r) {
    rocket_ids.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) rocket_ids.push_back(r.read_i32());
}

void UpdateWorldTimePacket::serialize(NetWriter& w) const { w.write_f64(world_time); }

void UpdateWorldTimePacket::deserialize(NetReader& r) { world_time = r.read_f64(); }

void UpdatePlayerColorPacket::serialize(NetWriter& w) const {
    w.write_i32(player_id);
    WriteColor3(w, color);
}

void UpdatePlayerColorPacket::deserialize(NetReader& r) {
    player_id = r.read_i32();
    color = ReadColor3(r);
}

void SendChatMessagePacket::serialize(NetWriter& w) const {
    w.write_i32(sender_id);
    w.write_string(message);
}

void SendChatMessagePacket::deserialize(NetReader& r) {
    sender_id = r.read_i32();
    message = ReadStringBounded(r);
}

void CreateRocketPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(local_id);
    w.write_i32(global_id);
    w.write_bool(for_launch);
    rocket.serialize(w);
}

void CreateRocketPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    local_id = r.read_i32();
    global_id = r.read_i32();
    for_launch = r.read_bool();
    rocket.deserialize(r);
}

void DestroyRocketPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_u8(reason);
}

void DestroyRocketPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    reason = r.read_u8();
}

void UpdateRocketPrimaryPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    WriteNetLocation(w, location);
    w.write_f32(rotation);
    w.write_f32(angular_velocity);
    w.write_i32(static_cast<std::int32_t>(structure_hash));
}

void UpdateRocketPrimaryPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    location = ReadNetLocation(r);
    rotation = r.read_f32();
    angular_velocity = r.read_f32();
    structure_hash = static_cast<std::uint32_t>(r.read_i32());
}

void UpdateRocketSecondaryPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_f32(input_turn);
    w.write_f32(raw_x);
    w.write_f32(raw_y);
    w.write_f32(horizontal_x);
    w.write_f32(horizontal_y);
    w.write_f32(vertical_x);
    w.write_f32(vertical_y);
    w.write_f32(throttle_percent);
    w.write_bool(throttle_on);
    w.write_bool(rcs);
}

void UpdateRocketSecondaryPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    input_turn = r.read_f32();
    raw_x = r.read_f32();
    raw_y = r.read_f32();
    horizontal_x = r.read_f32();
    horizontal_y = r.read_f32();
    vertical_x = r.read_f32();
    vertical_y = r.read_f32();
    throttle_percent = r.read_f32();
    throttle_on = r.read_bool();
    rcs = r.read_bool();
}

void DestroyPartPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(create_explosion);
    w.write_u8(reason);
}

void DestroyPartPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    create_explosion = r.read_bool();
    reason = r.read_u8();
}

void UpdateStagingPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    WriteCount(w, stages.size());
    for (const auto& stage : stages) WriteStage(w, stage);
}

void UpdateStagingPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    stages.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) stages.push_back(ReadStage(r));
}

void UpdatePartEnginePacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(engine_on);
}

void UpdatePartEnginePacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    engine_on = r.read_bool();
}

void UpdatePartWheelPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(wheel_on);
}

void UpdatePartWheelPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    wheel_on = r.read_bool();
}

void UpdatePartBoosterPacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_bool(primed);
    w.write_f32(throttle);
    w.write_f32(fuel_percent);
}

void UpdatePartBoosterPacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    primed = r.read_bool();
    throttle = r.read_f32();
    fuel_percent = r.read_f32();
}

void UpdatePartParachutePacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_f32(state);
    w.write_f32(target_state);
}

void UpdatePartParachutePacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    state = r.read_f32();
    target_state = r.read_f32();
}

void UpdatePartMovePacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_f32(time);
    w.write_f32(target_time);
}

void UpdatePartMovePacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    time = r.read_f32();
    target_time = r.read_f32();
}

void UpdatePartResourcePacket::serialize(NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_f64(resource_percent);
    WriteCount(w, part_ids.size());
    for (const auto id : part_ids) w.write_i32(id);
}

void UpdatePartResourcePacket::deserialize(NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    resource_percent = r.read_f64();
    part_ids.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) part_ids.push_back(r.read_i32());
}

void PlayerEventToastPacket::serialize(NetWriter& w) const { w.write_string(message); }

void PlayerEventToastPacket::deserialize(NetReader& r) { message = ReadStringBounded(r); }

void DockTransactionPacket::serialize(NetWriter& w) const {
    w.write_i32(transaction_id);
    w.write_u8(static_cast<std::uint8_t>(operation));
    w.write_bool(committed);
    w.write_i32(keep_rocket_id);
    w.write_i32(remove_rocket_id);
    w.write_i32(keep_part_id);
    w.write_i32(remove_part_id);
    w.write_f64(world_time);
    if (!committed) return;
    // 已提交的事务必须带合并后的火箭，否则接收方无法复现
    if (!merged_rocket.has_value()) throw BufferError("已提交的对接事务缺少合并火箭。");
    merged_rocket->serialize(w);
    w.write_i32(second_rocket_id);
    w.write_bool(second_rocket.has_value());
    if (second_rocket.has_value()) second_rocket->serialize(w);
}

void DockTransactionPacket::deserialize(NetReader& r) {
    transaction_id = r.read_i32();
    operation = static_cast<DockTransactionOperation>(r.read_u8());
    committed = r.read_bool();
    keep_rocket_id = r.read_i32();
    remove_rocket_id = r.read_i32();
    keep_part_id = r.read_i32();
    remove_part_id = r.read_i32();
    world_time = r.read_f64();
    merged_rocket.reset();
    second_rocket.reset();
    if (!committed) return;
    RocketState merged;
    merged.deserialize(r);
    merged_rocket = std::move(merged);
    second_rocket_id = r.read_i32();
    if (r.read_bool()) {
        RocketState second;
        second.deserialize(r);
        second_rocket = std::move(second);
    }
}

void TimeWarpPacket::serialize(NetWriter& w) const {
    w.write_u8(static_cast<std::uint8_t>(operation));
    w.write_i32(vote_id);
    w.write_i32(requester_id);
    w.write_string(requester_name);
    w.write_f64(multiplier);
    w.write_bool(approved);
    w.write_f64(world_time);
    w.write_i32(timeout_seconds);
    w.write_string(message);
}

void TimeWarpPacket::deserialize(NetReader& r) {
    operation = static_cast<TimeWarpOperation>(r.read_u8());
    vote_id = r.read_i32();
    requester_id = r.read_i32();
    requester_name = ReadStringBounded(r);
    multiplier = r.read_f64();
    approved = r.read_bool();
    world_time = r.read_f64();
    timeout_seconds = r.read_i32();
    message = ReadStringBounded(r);
}

void ExperimentalAccessPacket::serialize(NetWriter& w) const {
    w.write_bool(request);
    w.write_string(passphrase);
    w.write_bool(granted);
    w.write_string(message);
}

void ExperimentalAccessPacket::deserialize(NetReader& r) {
    request = r.read_bool();
    passphrase = ReadStringBounded(r);
    granted = r.read_bool();
    message = ReadStringBounded(r);
}

void P2PPeerOfferPacket::serialize(NetWriter& w) const {
    w.write_bool(active);
    w.write_i32(peer_player_id);
    w.write_string(peer_address);
    w.write_i32(peer_port);
    w.write_string(pair_token);
    w.write_i32(transition_buffer_seconds);
    WriteRocketIdList(w, local_rocket_ids);
    WriteRocketIdList(w, peer_rocket_ids);
}

void P2PPeerOfferPacket::deserialize(NetReader& r) {
    active = r.read_bool();
    peer_player_id = r.read_i32();
    peer_address = ReadStringBounded(r);
    peer_port = r.read_i32();
    pair_token = ReadStringBounded(r);
    transition_buffer_seconds = r.read_i32();
    local_rocket_ids = ReadRocketIdList(r);
    peer_rocket_ids = ReadRocketIdList(r);
}

// ---- P2PEndpoint ----
void P2PEndpointPacket::serialize(NetWriter& w) const {
    w.write_i32(port);
}

void P2PEndpointPacket::deserialize(NetReader& r) {
    port = r.read_i32();
}

// ---- UpdateCheatStatus ----
// 与 C# 侧 Packet_UpdateCheatStatus.Serialize 逐位对齐：Write(bool) 是 1 bit，Write(ushort) 是 16 bit 小端。
void UpdateCheatStatusPacket::serialize(NetWriter& w) const {
    w.write_bool(enabled);
    w.write_u16(individual_flags);
}

void UpdateCheatStatusPacket::deserialize(NetReader& r) {
    enabled = r.read_bool();
    individual_flags = r.read_u16();
}

// ---- PlanetPhysics ----
void PlanetPhysics::serialize(NetWriter& w) const {
    w.write_string(name);
    w.write_f64(radius);
    w.write_f64(gm);
    w.write_f64(rotation_period);
    w.write_f64(atmosphere_height);
    w.write_f64(surface_pressure);
    w.write_f64(surface_temp);
    w.write_f64(molar_mass);
    WriteCount(w, atmosphere_density_curve.size());
    for (const auto& [h, d] : atmosphere_density_curve) {
        w.write_f64(h);
        w.write_f64(d);
    }
}

void PlanetPhysics::deserialize(NetReader& r) {
    name = ReadStringBounded(r);
    radius = r.read_f64();
    gm = r.read_f64();
    rotation_period = r.read_f64();
    atmosphere_height = r.read_f64();
    surface_pressure = r.read_f64();
    surface_temp = r.read_f64();
    molar_mass = r.read_f64();
    atmosphere_density_curve.clear();
    for (std::int32_t i = 0, n = ReadCount(r); i < n; ++i) {
        double h = r.read_f64();
        double d = r.read_f64();
        atmosphere_density_curve.emplace_back(h, d);
    }
}

// ---- PhysicsConstants ----
void PhysicsConstants::serialize(NetWriter& w) const {
    w.write_f64(gravitational_constant);
    w.write_f64(fixed_dt);
    w.write_i32(max_substeps);
    w.write_f64(collision_margin);
}

void PhysicsConstants::deserialize(NetReader& r) {
    gravitational_constant = r.read_f64();
    fixed_dt = r.read_f64();
    max_substeps = r.read_i32();
    collision_margin = r.read_f64();
}


// ---- RenameRocketPacket ----
void RenameRocketPacket::serialize(NetWriter& w) const {
    w.write_i32(rocket_id);
    w.write_string(rocket_name);
}

void RenameRocketPacket::deserialize(NetReader& r) {
    rocket_id = r.read_i32();
    rocket_name = ReadStringBounded(r);
}

// ---- UpdatePartTemperaturePacket ----
void UpdatePartTemperaturePacket::serialize(NetWriter& w) const {
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_f32(temperature);
}

void UpdatePartTemperaturePacket::deserialize(NetReader& r) {
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    temperature = r.read_f32();
}

// ---- UpdateDestructionReasonPacket ----
void UpdateDestructionReasonPacket::serialize(NetWriter& w) const {
    w.write_i32(rocket_id);
    w.write_i32(part_id);
    w.write_u8(reason);
}

void UpdateDestructionReasonPacket::deserialize(NetReader& r) {
    rocket_id = r.read_i32();
    part_id = r.read_i32();
    reason = r.read_u8();
}

// ---- ServerCommandPacket ----
void ServerCommandPacket::serialize(NetWriter& w) const {
    w.write_string(command);
    w.write_string(output);
}

void ServerCommandPacket::deserialize(NetReader& r) {
    command = ReadStringBounded(r);
    output = ReadStringBounded(r);
}

// ---- UploadLogPacket ----
// 线格式必须与客户端 Packet_UploadLog 一致：string(fileName) + string(comment) + i32(len) + 裸字节
void UploadLogPacket::serialize(NetWriter& w) const {
    w.write_string(file_name);
    w.write_string(comment);
    w.write_i32(static_cast<std::int32_t>(data.size()));
    if (!data.empty()) w.write_bytes(data.data(), data.size());
}

void UploadLogPacket::deserialize(NetReader& r) {
    file_name = ReadStringBounded(r);
    comment = ReadStringBounded(r);
    const std::int32_t length = r.read_i32();
    if (length <= 0) {
        data.clear();
        return;
    }
    if (length > kMaxLogUploadBytes) {
        throw std::invalid_argument("log upload exceeds size limit");
    }
    if (static_cast<std::size_t>(length) * 8 > r.remaining_bits()) {
        throw std::invalid_argument("log upload payload truncated");
    }
    data.resize(static_cast<std::size_t>(length));
    r.read_bytes(data.data(), data.size());
}

// ---- 拆分 ----


PacketType SplitPacket(const std::uint8_t* payload, std::size_t payload_bits,
                       PacketBytes& out_body) {
    if (payload == nullptr) throw BufferError("载荷为空。");
    if (payload_bits < 8) throw BufferError("载荷不足一个包类型字节。");

    // 类型写在位偏移 0 的 8 位里，也就是载荷第 0 字节
    const std::uint8_t raw_type = payload[0];
    if (!IsValidPacketType(raw_type)) throw BufferError("未知的包类型。");

    out_body.bits = payload_bits - 8;
    const std::size_t body_bytes = (out_body.bits + 7) / 8;
    out_body.bytes.assign(payload + 1, payload + 1 + body_bytes);
    return static_cast<PacketType>(raw_type);
}

}  // namespace sfs::protocol
