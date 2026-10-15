// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server_info.hpp"

#include <stdexcept>

namespace sfs::protocol {
namespace {

void WriteInt32Le(std::uint8_t* buffer, std::size_t offset, std::int32_t value) {
    const auto v = static_cast<std::uint32_t>(value);
    buffer[offset] = static_cast<std::uint8_t>(v);
    buffer[offset + 1] = static_cast<std::uint8_t>(v >> 8);
    buffer[offset + 2] = static_cast<std::uint8_t>(v >> 16);
    buffer[offset + 3] = static_cast<std::uint8_t>(v >> 24);
}

std::int32_t ReadInt32Le(const std::uint8_t* p) {
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[3]) << 24));
}

}  // namespace

std::vector<std::uint8_t> EncodeServerInfoResponse(
    std::int32_t current_players,
    std::int32_t max_players) {
    if (current_players < 0 || max_players < 1 || current_players > max_players) {
        throw std::invalid_argument("server info counts are out of range");
    }
    std::vector<std::uint8_t> payload(kServerInfoResponseBytes, 0);
    WriteInt32Le(payload.data(), 0, current_players);
    WriteInt32Le(payload.data(), 4, max_players);
    return payload;
}

std::pair<std::int32_t, std::int32_t> DecodeServerInfoResponse(
    const std::vector<std::uint8_t>& payload) {
    if (payload.size() != kServerInfoResponseBytes) {
        throw std::invalid_argument("server info payload must be 8 bytes");
    }
    return {ReadInt32Le(payload.data()), ReadInt32Le(payload.data() + 4)};
}

}  // namespace sfs::protocol
