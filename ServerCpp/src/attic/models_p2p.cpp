// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "p2p.hpp"
#include "protocol/types.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

void P2PPeerOfferPacket::serialize(net::NetWriter& w) const {
    w.write_bool(active);
    w.write_i32(peer_player_id);
    write_string_bounded(w, peer_address);
    w.write_i32(peer_port);
    write_string_bounded(w, pair_token);
    w.write_i32(transition_buffer_seconds);

    if (local_rocket_ids.size() > 256) throw std::invalid_argument("Invalid local P2P rocket count.");
    if (peer_rocket_ids.size() > 256) throw std::invalid_argument("Invalid peer P2P rocket count.");

    w.write_i32(static_cast<std::int32_t>(local_rocket_ids.size()));
    for (auto id : local_rocket_ids) w.write_i32(id);
    w.write_i32(static_cast<std::int32_t>(peer_rocket_ids.size()));
    for (auto id : peer_rocket_ids) w.write_i32(id);
}

void P2PPeerOfferPacket::deserialize(net::NetReader& r) {
    active = r.read_bool();
    peer_player_id = r.read_i32();
    peer_address = read_string_bounded(r);
    peer_port = r.read_i32();
    pair_token = read_string_bounded(r);
    transition_buffer_seconds = r.read_i32();

    std::int32_t local_count = r.read_i32();
    if (local_count < 0 || local_count > 256) throw std::invalid_argument("Invalid local P2P rocket count.");
    local_rocket_ids.resize(local_count);
    for (std::int32_t i = 0; i < local_count; ++i) local_rocket_ids[i] = r.read_i32();

    std::int32_t peer_count = r.read_i32();
    if (peer_count < 0 || peer_count > 256) throw std::invalid_argument("Invalid peer P2P rocket count.");
    peer_rocket_ids.resize(peer_count);
    for (std::int32_t i = 0; i < peer_count; ++i) peer_rocket_ids[i] = r.read_i32();
}

} // namespace sfs::protocol
