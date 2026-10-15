// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "docking.hpp"
#include "protocol/types.hpp"
#include "protocol/models/state.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

void DockTransactionPacket::serialize(net::NetWriter& w) const {
    w.write_i32(transaction_id);
    w.write_u8(static_cast<std::uint8_t>(operation));
    w.write_bool(committed);
    w.write_i32(keep_rocket_id);
    w.write_i32(remove_rocket_id);
    w.write_i32(keep_part_id);
    w.write_i32(remove_part_id);
    w.write_f64(world_time);

    if (committed) {
        if (!merged_rocket.has_value()) {
            throw std::invalid_argument("Committed docking transaction requires a rocket.");
        }
        merged_rocket->serialize(w);
        w.write_i32(second_rocket_id);
        w.write_bool(second_rocket.has_value());
        if (second_rocket.has_value()) {
            second_rocket->serialize(w);
        }
    }
}

void DockTransactionPacket::deserialize(net::NetReader& r) {
    transaction_id = r.read_i32();
    operation = static_cast<DockTransactionOperation>(r.read_u8());
    committed = r.read_bool();
    keep_rocket_id = r.read_i32();
    remove_rocket_id = r.read_i32();
    keep_part_id = r.read_i32();
    remove_part_id = r.read_i32();
    world_time = r.read_f64();

    if (committed) {
        RocketState mr;
        mr.deserialize(r);
        merged_rocket = std::move(mr);
        second_rocket_id = r.read_i32();
        if (r.read_bool()) {
            RocketState sr;
            sr.deserialize(r);
            second_rocket = std::move(sr);
        }
    }
}

} // namespace sfs::protocol
