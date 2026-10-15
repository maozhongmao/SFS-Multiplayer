// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server_version.hpp"

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

std::vector<std::uint8_t> EncodeServerVersionResponse(
    std::int32_t handshake_version,
    std::int32_t protocol_version,
    std::string_view server_version,
    std::int32_t flags) {
    if (server_version.size() > kServerVersionStringMaxBytes) {
        throw std::invalid_argument("server version string is too long");
    }
    std::vector<std::uint8_t> payload(12 + server_version.size() + kServerVersionFlagsBytes, 0);
    WriteInt32Le(payload.data(), 0, handshake_version);
    WriteInt32Le(payload.data(), 4, protocol_version);
    WriteInt32Le(payload.data(), 8, static_cast<std::int32_t>(server_version.size()));
    for (std::size_t i = 0; i < server_version.size(); ++i) {
        payload[12 + i] = static_cast<std::uint8_t>(server_version[i]);
    }
    WriteInt32Le(payload.data(), 12 + server_version.size(), flags);
    return payload;
}

bool DecodeServerVersionResponse(
    const std::vector<std::uint8_t>& payload,
    std::int32_t* handshake_version,
    std::int32_t* protocol_version,
    std::string* server_version,
    std::int32_t* flags) {
    if (payload.size() < 12) return false;
    const std::int32_t length = ReadInt32Le(payload.data() + 8);
    if (length < 0 || static_cast<std::size_t>(length) > kServerVersionStringMaxBytes) return false;
    if (payload.size() < 12 + static_cast<std::size_t>(length)) return false;
    if (handshake_version != nullptr) *handshake_version = ReadInt32Le(payload.data());
    if (protocol_version != nullptr) *protocol_version = ReadInt32Le(payload.data() + 4);
    if (server_version != nullptr) {
        server_version->assign(reinterpret_cast<const char*>(payload.data() + 12),
                               static_cast<std::size_t>(length));
    }
    if (flags != nullptr) {
        const std::size_t flags_offset = 12 + static_cast<std::size_t>(length);
        *flags = payload.size() >= flags_offset + kServerVersionFlagsBytes
            ? ReadInt32Le(payload.data() + flags_offset)
            : 0;
    }
    return true;
}

}  // namespace sfs::protocol
