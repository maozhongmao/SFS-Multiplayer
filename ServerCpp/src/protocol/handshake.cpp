// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "handshake.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace sfs::protocol {
namespace {

void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void AppendU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
    }
}

std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0])
        | (static_cast<std::uint32_t>(p[1]) << 8)
        | (static_cast<std::uint32_t>(p[2]) << 16)
        | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint64_t ReadU64(const std::uint8_t* p) {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(p[i]) << (8 * i);
    }
    return value;
}

void AppendDouble(std::vector<std::uint8_t>& out, double value) {
    // 位模式直拷，避免浮点转换引入差异。
    std::uint64_t bits = 0;
    static_assert(sizeof(double) == sizeof(bits), "double must be 64-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    AppendU64(out, bits);
}

double ReadDouble(const std::uint8_t* p) {
    const std::uint64_t bits = ReadU64(p);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

// 对应 C# WriteString：int32 字节长度 + UTF-8 原始字节。
void AppendString(std::vector<std::uint8_t>& out, const std::string& value) {
    if (value.size() > kMaxHandshakeStringBytes) {
        throw std::invalid_argument("handshake string is too long");
    }
    AppendU32(out, static_cast<std::uint32_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

// 读取游标：每一步都先确认剩余字节足够，绝越界读。
struct Cursor {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;

    void Need(std::size_t count) const {
        if (pos + count > size) {
            throw std::invalid_argument("handshake payload ended early");
        }
    }

    std::uint32_t U32() {
        Need(4);
        const std::uint32_t v = ReadU32(data + pos);
        pos += 4;
        return v;
    }

    double F64() {
        Need(8);
        const double v = ReadDouble(data + pos);
        pos += 8;
        return v;
    }

    std::uint8_t U8() {
        Need(1);
        return data[pos++];
    }

    std::string String() {
        const auto length = static_cast<std::int32_t>(U32());
        if (length < 0 || static_cast<std::size_t>(length) > kMaxHandshakeStringBytes) {
            throw std::invalid_argument("invalid handshake string length");
        }
        Need(static_cast<std::size_t>(length));
        std::string value(reinterpret_cast<const char*>(data + pos), static_cast<std::size_t>(length));
        pos += static_cast<std::size_t>(length);
        return value;
    }
};

void RequireNoTrailing(const Cursor& cursor) {
    if (cursor.pos != cursor.size) {
        throw std::invalid_argument("handshake payload has trailing data");
    }
}

void RequireMagicVersion(Cursor& cursor) {
    if (cursor.U32() != kHandshakeMagic) {
        throw std::invalid_argument("invalid handshake magic");
    }
    if (static_cast<std::int32_t>(cursor.U32()) != kHandshakeVersion) {
        throw std::invalid_argument("unsupported handshake version");
    }
}

}  // namespace

std::vector<std::uint8_t> EncodeHello(const JoinRequest& request) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    AppendU32(out, kHandshakeMagic);
    AppendU32(out, static_cast<std::uint32_t>(kHandshakeVersion));
    AppendString(out, request.username);
    AppendString(out, request.password);
    AppendString(out, request.solar_system_name);
    AppendU32(out, static_cast<std::uint32_t>(request.resume_player_id));
    AppendString(out, request.resume_token);
    return out;
}

std::vector<std::uint8_t> EncodeAck(const JoinResponse& response) {
    std::vector<std::uint8_t> out;
    out.reserve(96);
    AppendU32(out, kHandshakeMagic);
    AppendU32(out, static_cast<std::uint32_t>(kHandshakeVersion));
    AppendU32(out, static_cast<std::uint32_t>(response.player_id));
    AppendDouble(out, response.update_rockets_period);
    AppendDouble(out, response.chat_message_cooldown);
    AppendDouble(out, response.world_time);
    AppendDouble(out, response.send_time);
    out.push_back(response.difficulty);
    AppendString(out, response.solar_system_name);
    AppendString(out, response.udp_session_token);
    AppendString(out, response.resume_token);
    return out;
}

JoinRequest DecodeHello(const std::vector<std::uint8_t>& payload) {
    Cursor cursor{payload.data(), payload.size()};
    RequireMagicVersion(cursor);
    JoinRequest request;
    request.username = cursor.String();
    request.password = cursor.String();
    request.solar_system_name = cursor.String();
    request.resume_player_id = static_cast<std::int32_t>(cursor.U32());
    request.resume_token = cursor.String();
    RequireNoTrailing(cursor);
    return request;
}

JoinResponse DecodeAck(const std::vector<std::uint8_t>& payload) {
    Cursor cursor{payload.data(), payload.size()};
    RequireMagicVersion(cursor);
    JoinResponse response;
    response.player_id = static_cast<std::int32_t>(cursor.U32());
    response.update_rockets_period = cursor.F64();
    response.chat_message_cooldown = cursor.F64();
    response.world_time = cursor.F64();
    response.send_time = cursor.F64();
    response.difficulty = cursor.U8();
    response.solar_system_name = cursor.String();
    response.udp_session_token = cursor.String();
    response.resume_token = cursor.String();
    RequireNoTrailing(cursor);
    return response;
}

}  // namespace sfs::protocol
