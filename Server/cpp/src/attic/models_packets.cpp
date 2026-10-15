// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "packets.hpp"
#include "protocol/types.hpp"
#include "protocol/models/state.hpp"
#include "protocol/models/part_packets.hpp"
#include "protocol/models/docking.hpp"
#include "protocol/models/timewarp.hpp"
#include "protocol/models/p2p.hpp"
#include "protocol/models/experimental.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

// ------------------------------------------------------------
// PlayerConnectedPacket
// ------------------------------------------------------------
void PlayerConnectedPacket::serialize(net::NetWriter& w) const {
    w.write_i32(player_id);
    write_string_bounded(w, username);
    write_color3(w, icon_color);
    w.write_bool(print_message);
}
void PlayerConnectedPacket::deserialize(net::NetReader& r) {
    player_id = r.read_i32();
    username = read_string_bounded(r);
    icon_color = read_color3(r);
    print_message = r.read_bool();
}

// ------------------------------------------------------------
// PlayerDisconnectedPacket
// ------------------------------------------------------------
void PlayerDisconnectedPacket::serialize(net::NetWriter& w) const { w.write_i32(player_id); }
void PlayerDisconnectedPacket::deserialize(net::NetReader& r) { player_id = r.read_i32(); }

// ------------------------------------------------------------
// UpdatePlayerControlPacket
// ------------------------------------------------------------
void UpdatePlayerControlPacket::serialize(net::NetWriter& w) const {
    w.write_i32(player_id);
    w.write_i32(rocket_id);
}
void UpdatePlayerControlPacket::deserialize(net::NetReader& r) {
    player_id = r.read_i32();
    rocket_id = r.read_i32();
}

// ------------------------------------------------------------
// UpdatePlayerAuthorityPacket
// ------------------------------------------------------------
void UpdatePlayerAuthorityPacket::serialize(net::NetWriter& w) const {
    std::vector<std::int32_t> ids(rocket_ids.begin(), rocket_ids.end());
    w.write_i32(static_cast<std::int32_t>(ids.size()));
    for (auto id : ids) w.write_i32(id);
}
void UpdatePlayerAuthorityPacket::deserialize(net::NetReader& r) {
    rocket_ids.clear();
    auto vec = read_collection<std::int32_t>(r, [&r]() { return r.read_i32(); });
    rocket_ids.insert(vec.begin(), vec.end());
}

// ------------------------------------------------------------
// UpdateWorldTimePacket
// ------------------------------------------------------------
void UpdateWorldTimePacket::serialize(net::NetWriter& w) const { w.write_f64(world_time); }
void UpdateWorldTimePacket::deserialize(net::NetReader& r) { world_time = r.read_f64(); }

// ------------------------------------------------------------
// UpdatePlayerColorPacket
// ------------------------------------------------------------
void UpdatePlayerColorPacket::serialize(net::NetWriter& w) const {
    w.write_i32(player_id);
    write_color3(w, color);
}
void UpdatePlayerColorPacket::deserialize(net::NetReader& r) {
    player_id = r.read_i32();
    color = read_color3(r);
}

// ------------------------------------------------------------
// SendChatMessagePacket
// ------------------------------------------------------------
void SendChatMessagePacket::serialize(net::NetWriter& w) const {
    w.write_i32(sender_id);
    write_string_bounded(w, message);
}
void SendChatMessagePacket::deserialize(net::NetReader& r) {
    sender_id = r.read_i32();
    message = read_string_bounded(r);
}

// ------------------------------------------------------------
// CreateRocketPacket
// ------------------------------------------------------------
void CreateRocketPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(local_id);
    w.write_i32(global_id);
    w.write_bool(for_launch);
    rocket.serialize(w);
}
void CreateRocketPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    local_id = r.read_i32();
    global_id = r.read_i32();
    for_launch = r.read_bool();
    rocket = RocketState{};
    rocket.deserialize(r);
}

// ------------------------------------------------------------
// DestroyRocketPacket
// ------------------------------------------------------------
void DestroyRocketPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    w.write_u8(reason);
}
void DestroyRocketPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    reason = r.read_u8();
}

// ------------------------------------------------------------
// UpdateRocketPrimaryPacket
// ------------------------------------------------------------
void UpdateRocketPrimaryPacket::serialize(net::NetWriter& w) const {
    w.write_f64(world_time);
    w.write_i32(rocket_id);
    write_netlocation(w, location);
    w.write_f32(rotation);
    w.write_f32(angular_velocity);
}
void UpdateRocketPrimaryPacket::deserialize(net::NetReader& r) {
    world_time = r.read_f64();
    rocket_id = r.read_i32();
    location = read_netlocation(r);
    rotation = r.read_f32();
    angular_velocity = r.read_f32();
}

// ------------------------------------------------------------
// UpdateRocketSecondaryPacket
// ------------------------------------------------------------
void UpdateRocketSecondaryPacket::serialize(net::NetWriter& w) const {
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
void UpdateRocketSecondaryPacket::deserialize(net::NetReader& r) {
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

// ------------------------------------------------------------
// PlayerEventToastPacket
// ------------------------------------------------------------
void PlayerEventToastPacket::serialize(net::NetWriter& w) const {
    write_string_bounded(w, message);
}
void PlayerEventToastPacket::deserialize(net::NetReader& r) {
    message = read_string_bounded(r);
}

// ------------------------------------------------------------
// 统一编解码入口
// ------------------------------------------------------------
template <typename T>
inline void serialize_packet(net::NetWriter& w, const T& pkt) {
    pkt.serialize(w);
}

template <typename T>
inline void deserialize_packet(net::NetReader& r, T& pkt) {
    pkt.deserialize(r);
}

// 将字节流按 PacketType 解码到具体结构
bool decode_packet(std::uint8_t packet_type, net::NetReader& r, void* out) {
    using PT = sfs::protocol::PacketType;
    switch (static_cast<PT>(packet_type)) {
        case PT::PlayerConnected: {
            auto* p = static_cast<PlayerConnectedPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::PlayerDisconnected: {
            auto* p = static_cast<PlayerDisconnectedPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerControl: {
            auto* p = static_cast<UpdatePlayerControlPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerAuthority: {
            auto* p = static_cast<UpdatePlayerAuthorityPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateWorldTime: {
            auto* p = static_cast<UpdateWorldTimePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerColor: {
            auto* p = static_cast<UpdatePlayerColorPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::SendChatMessage: {
            auto* p = static_cast<SendChatMessagePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::CreateRocket: {
            auto* p = static_cast<CreateRocketPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DestroyRocket: {
            auto* p = static_cast<DestroyRocketPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateRocketPrimary: {
            auto* p = static_cast<UpdateRocketPrimaryPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateRocketSecondary: {
            auto* p = static_cast<UpdateRocketSecondaryPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DestroyPart: {
            auto* p = static_cast<DestroyPartPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateStaging: {
            auto* p = static_cast<UpdateStagingPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_EngineModule: {
            auto* p = static_cast<UpdatePartEnginePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_WheelModule: {
            auto* p = static_cast<UpdatePartWheelPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_BoosterModule: {
            auto* p = static_cast<UpdatePartBoosterPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_ParachuteModule: {
            auto* p = static_cast<UpdatePartParachutePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_MoveModule: {
            auto* p = static_cast<UpdatePartMovePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_ResourceModule: {
            auto* p = static_cast<UpdatePartResourcePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::ShowToastMessage: {
            auto* p = static_cast<PlayerEventToastPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DockTransaction: {
            auto* p = static_cast<DockTransactionPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::TimeWarp: {
            auto* p = static_cast<TimeWarpPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::ExperimentalAccess: {
            auto* p = static_cast<ExperimentalAccessPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::P2PPeerOffer: {
            auto* p = static_cast<P2PPeerOfferPacket*>(out);
            p->deserialize(r);
            return true;
        }
        default:
            return false;
    }
}

void encode_packet(std::uint8_t packet_type, const void* pkt, net::NetWriter& w) {
    using PT = sfs::protocol::PacketType;
    switch (static_cast<PT>(packet_type)) {
        case PT::PlayerConnected: {
            const auto* p = static_cast<const PlayerConnectedPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::PlayerDisconnected: {
            const auto* p = static_cast<const PlayerDisconnectedPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePlayerControl: {
            const auto* p = static_cast<const UpdatePlayerControlPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePlayerAuthority: {
            const auto* p = static_cast<const UpdatePlayerAuthorityPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdateWorldTime: {
            const auto* p = static_cast<const UpdateWorldTimePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePlayerColor: {
            const auto* p = static_cast<const UpdatePlayerColorPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::SendChatMessage: {
            const auto* p = static_cast<const SendChatMessagePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::CreateRocket: {
            const auto* p = static_cast<const CreateRocketPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::DestroyRocket: {
            const auto* p = static_cast<const DestroyRocketPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdateRocketPrimary: {
            const auto* p = static_cast<const UpdateRocketPrimaryPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdateRocketSecondary: {
            const auto* p = static_cast<const UpdateRocketSecondaryPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::DestroyPart: {
            const auto* p = static_cast<const DestroyPartPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdateStaging: {
            const auto* p = static_cast<const UpdateStagingPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_EngineModule: {
            const auto* p = static_cast<const UpdatePartEnginePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_WheelModule: {
            const auto* p = static_cast<const UpdatePartWheelPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_BoosterModule: {
            const auto* p = static_cast<const UpdatePartBoosterPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_ParachuteModule: {
            const auto* p = static_cast<const UpdatePartParachutePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_MoveModule: {
            const auto* p = static_cast<const UpdatePartMovePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::UpdatePart_ResourceModule: {
            const auto* p = static_cast<const UpdatePartResourcePacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::ShowToastMessage: {
            const auto* p = static_cast<const PlayerEventToastPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::DockTransaction: {
            const auto* p = static_cast<const DockTransactionPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::TimeWarp: {
            const auto* p = static_cast<const TimeWarpPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::ExperimentalAccess: {
            const auto* p = static_cast<const ExperimentalAccessPacket*>(pkt);
            p->serialize(w);
            return;
        }
        case PT::P2PPeerOffer: {
            const auto* p = static_cast<const P2PPeerOfferPacket*>(pkt);
            p->serialize(w);
            return;
        }
        default:
            return;
    }
}

} // namespace sfs::protocol
