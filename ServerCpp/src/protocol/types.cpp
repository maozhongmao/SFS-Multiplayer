// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "types.hpp"
#include "net/lidgren_buffer.hpp"

namespace sfs::protocol {
namespace net = sfs::net;

// ------------------------------------------------------------
// 字符串读写边界（对齐 C# ReadStringBounded / MaxStringLength）
// ------------------------------------------------------------
std::string read_string_bounded(net::NetReader& reader) {
    std::string value = reader.read_string();
    if (value.size() > static_cast<std::size_t>(kMaxStringLength)) {
        throw std::invalid_argument("String is too long.");
    }
    return value;
}

void write_string_bounded(net::NetWriter& writer, std::string_view value) {
    if (value.size() > static_cast<std::size_t>(kMaxStringLength)) {
        throw std::invalid_argument("String is too long.");
    }
    writer.write_string(value);
}

// ------------------------------------------------------------
// NetLocation / Color3 的编解码（对齐 C# NetMessageExtensions）
// ------------------------------------------------------------
void write_netlocation(net::NetWriter& w, const NetLocation& loc) {
    w.write_f64(loc.x);
    w.write_f64(loc.y);
    w.write_f64(loc.vx);
    w.write_f64(loc.vy);
    write_string_bounded(w, loc.address);
}

NetLocation read_netlocation(net::NetReader& r) {
    NetLocation loc;
    loc.x = r.read_f64();
    loc.y = r.read_f64();
    loc.vx = r.read_f64();
    loc.vy = r.read_f64();
    loc.address = read_string_bounded(r);
    return loc;
}

void write_color3(net::NetWriter& w, const Color3& c) {
    w.write_f32(c.r);
    w.write_f32(c.g);
    w.write_f32(c.b);
}

Color3 read_color3(net::NetReader& r) {
    Color3 c;
    c.r = r.read_f32();
    c.g = r.read_f32();
    c.b = r.read_f32();
    return c;
}

} // namespace sfs::protocol
