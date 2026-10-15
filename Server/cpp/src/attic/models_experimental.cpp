// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "experimental.hpp"
#include "protocol/types.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

void ExperimentalAccessPacket::serialize(net::NetWriter& w) const {
    w.write_bool(request);
    write_string_bounded(w, passphrase);
    w.write_bool(granted);
    write_string_bounded(w, message);
}

void ExperimentalAccessPacket::deserialize(net::NetReader& r) {
    request = r.read_bool();
    passphrase = read_string_bounded(r);
    granted = r.read_bool();
    message = read_string_bounded(r);
}

} // namespace sfs::protocol
