// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "tcp_transport.hpp"
#include "protocol/models/packets.hpp"
#include "protocol/models/part_packets.hpp"
#include "protocol/models/state.hpp"
#include "protocol/models/docking.hpp"
#include "protocol/models/timewarp.hpp"
#include "protocol/models/p2p.hpp"
#include "protocol/models/experimental.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::net {

// ------------------------------------------------------------
// NetPayload 编码实现
// ------------------------------------------------------------
NetPayload encode_payload(std::uint8_t packet_type, const void* packet_ptr) {
    NetWriter w;
    w.write_u8(packet_type);

    using PT = sfs::protocol::PacketType;
    switch (static_cast<PT>(packet_type)) {
        case PT::PlayerConnected: {
            const auto* p = static_cast<const sfs::protocol::PlayerConnectedPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::PlayerDisconnected: {
            const auto* p = static_cast<const sfs::protocol::PlayerDisconnectedPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePlayerControl: {
            const auto* p = static_cast<const sfs::protocol::UpdatePlayerControlPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePlayerAuthority: {
            const auto* p = static_cast<const sfs::protocol::UpdatePlayerAuthorityPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdateWorldTime: {
            const auto* p = static_cast<const sfs::protocol::UpdateWorldTimePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePlayerColor: {
            const auto* p = static_cast<const sfs::protocol::UpdatePlayerColorPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::SendChatMessage: {
            const auto* p = static_cast<const sfs::protocol::SendChatMessagePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::CreateRocket: {
            const auto* p = static_cast<const sfs::protocol::CreateRocketPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::DestroyRocket: {
            const auto* p = static_cast<const sfs::protocol::DestroyRocketPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdateRocketPrimary: {
            const auto* p = static_cast<const sfs::protocol::UpdateRocketPrimaryPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdateRocketSecondary: {
            const auto* p = static_cast<const sfs::protocol::UpdateRocketSecondaryPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::DestroyPart: {
            const auto* p = static_cast<const sfs::protocol::DestroyPartPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdateStaging: {
            const auto* p = static_cast<const sfs::protocol::UpdateStagingPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_EngineModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartEnginePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_WheelModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartWheelPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_BoosterModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartBoosterPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_ParachuteModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartParachutePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_MoveModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartMovePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::UpdatePart_ResourceModule: {
            const auto* p = static_cast<const sfs::protocol::UpdatePartResourcePacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::ShowToastMessage: {
            const auto* p = static_cast<const sfs::protocol::PlayerEventToastPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::DockTransaction: {
            const auto* p = static_cast<const sfs::protocol::DockTransactionPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::TimeWarp: {
            const auto* p = static_cast<const sfs::protocol::TimeWarpPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::ExperimentalAccess: {
            const auto* p = static_cast<const sfs::protocol::ExperimentalAccessPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        case PT::P2PPeerOffer: {
            const auto* p = static_cast<const sfs::protocol::P2PPeerOfferPacket*>(packet_ptr);
            p->serialize(w);
            break;
        }
        default:
            return NetPayload{};
    }

    return NetPayload{w.get_bytes(), static_cast<std::int32_t>(w.get_bit_length())};
}

bool decode_payload(std::uint8_t packet_type, const NetPayload& payload, void* out) {
    NetReader r(payload.data.data(), payload.bit_length);
    if (r.at_end()) return false;
    // 跳过已读的 packet_type 字节
    std::uint8_t read_type = r.read_u8();
    if (read_type != packet_type) return false;

    using PT = sfs::protocol::PacketType;
    switch (static_cast<PT>(packet_type)) {
        case PT::PlayerConnected: {
            auto* p = static_cast<sfs::protocol::PlayerConnectedPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::PlayerDisconnected: {
            auto* p = static_cast<sfs::protocol::PlayerDisconnectedPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerControl: {
            auto* p = static_cast<sfs::protocol::UpdatePlayerControlPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerAuthority: {
            auto* p = static_cast<sfs::protocol::UpdatePlayerAuthorityPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateWorldTime: {
            auto* p = static_cast<sfs::protocol::UpdateWorldTimePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePlayerColor: {
            auto* p = static_cast<sfs::protocol::UpdatePlayerColorPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::SendChatMessage: {
            auto* p = static_cast<sfs::protocol::SendChatMessagePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::CreateRocket: {
            auto* p = static_cast<sfs::protocol::CreateRocketPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DestroyRocket: {
            auto* p = static_cast<sfs::protocol::DestroyRocketPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateRocketPrimary: {
            auto* p = static_cast<sfs::protocol::UpdateRocketPrimaryPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateRocketSecondary: {
            auto* p = static_cast<sfs::protocol::UpdateRocketSecondaryPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DestroyPart: {
            auto* p = static_cast<sfs::protocol::DestroyPartPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdateStaging: {
            auto* p = static_cast<sfs::protocol::UpdateStagingPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_EngineModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartEnginePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_WheelModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartWheelPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_BoosterModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartBoosterPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_ParachuteModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartParachutePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_MoveModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartMovePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::UpdatePart_ResourceModule: {
            auto* p = static_cast<sfs::protocol::UpdatePartResourcePacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::ShowToastMessage: {
            auto* p = static_cast<sfs::protocol::PlayerEventToastPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::DockTransaction: {
            auto* p = static_cast<sfs::protocol::DockTransactionPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::TimeWarp: {
            auto* p = static_cast<sfs::protocol::TimeWarpPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::ExperimentalAccess: {
            auto* p = static_cast<sfs::protocol::ExperimentalAccessPacket*>(out);
            p->deserialize(r);
            return true;
        }
        case PT::P2PPeerOffer: {
            auto* p = static_cast<sfs::protocol::P2PPeerOfferPacket*>(out);
            p->deserialize(r);
            return true;
        }
        default:
            return false;
    }
}

} // namespace sfs::net
