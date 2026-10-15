// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "timewarp.hpp"
#include "protocol/types.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

void TimeWarpPacket::serialize(net::NetWriter& w) const {
    w.write_u8(static_cast<std::uint8_t>(operation));
    w.write_i32(vote_id);
    w.write_i32(requester_id);
    write_string_bounded(w, requester_name);
    w.write_f64(multiplier);
    w.write_bool(approved);
    w.write_f64(world_time);
    w.write_i32(timeout_seconds);
    write_string_bounded(w, message);
}

void TimeWarpPacket::deserialize(net::NetReader& r) {
    operation = static_cast<TimeWarpOperation>(r.read_u8());
    vote_id = r.read_i32();
    requester_id = r.read_i32();
    requester_name = read_string_bounded(r);
    multiplier = r.read_f64();
    approved = r.read_bool();
    world_time = r.read_f64();
    timeout_seconds = r.read_i32();
    message = read_string_bounded(r);
}

} // namespace sfs::protocol
