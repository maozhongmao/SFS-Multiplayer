// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "codec.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

namespace sfs::protocol {
namespace {

constexpr std::size_t kTcpBodyHeaderBytes = 9;

void WriteInt32Le(std::vector<std::uint8_t>& output, std::size_t offset, std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    output[offset] = static_cast<std::uint8_t>(bits);
    output[offset + 1] = static_cast<std::uint8_t>(bits >> 8u);
    output[offset + 2] = static_cast<std::uint8_t>(bits >> 16u);
    output[offset + 3] = static_cast<std::uint8_t>(bits >> 24u);
}

std::int32_t ReadInt32Le(const std::vector<std::uint8_t>& input, std::size_t offset) {
    const auto bits = static_cast<std::uint32_t>(input[offset])
        | (static_cast<std::uint32_t>(input[offset + 1]) << 8u)
        | (static_cast<std::uint32_t>(input[offset + 2]) << 16u)
        | (static_cast<std::uint32_t>(input[offset + 3]) << 24u);
    return static_cast<std::int32_t>(bits);
}

bool IsValidTcpKind(std::uint8_t value) {
    // 对应 C# 的 Enum.IsDefined(kind)：本枚举是 1..12 连续取值。
    // 新增帧类型时必须跟着改，否则服务端会把合法帧判成非法帧并直接断连
    // （ServerVersionRequest 就这么被漏过一次，登录页版本探测永远收不到回包）。
    return value >= static_cast<std::uint8_t>(TcpFrameKind::Hello)
        && value <= static_cast<std::uint8_t>(TcpFrameKind::ServerVersionResponse);
}

}  // namespace

std::vector<std::uint8_t> EncodeTcpFrame(const TcpFrame& frame) {
    if (frame.payload.size() > kMaxTcpPayloadBytes) {
        throw std::invalid_argument("TCP payload is too large");
    }
    if (frame.payload_bits < 0
        || static_cast<std::uint64_t>(frame.payload_bits)
            > static_cast<std::uint64_t>(frame.payload.size()) * 8u) {
        throw std::invalid_argument("TCP payload bit length is invalid");
    }

    const auto body_length = kTcpBodyHeaderBytes + frame.payload.size();
    const auto total_length = sizeof(std::int32_t) + body_length;
    if (body_length > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        throw std::invalid_argument("TCP frame is too large");
    }

    std::vector<std::uint8_t> encoded(total_length);
    WriteInt32Le(encoded, 0, static_cast<std::int32_t>(body_length));
    encoded[4] = static_cast<std::uint8_t>(frame.kind);
    WriteInt32Le(encoded, 5, frame.sequence);
    WriteInt32Le(encoded, 9, frame.payload_bits);
    std::copy(frame.payload.begin(), frame.payload.end(), encoded.begin() + 13);
    return encoded;
}

TcpFrame DecodeTcpFrame(const std::vector<std::uint8_t>& encoded) {
    if (encoded.size() < sizeof(std::int32_t) + kTcpBodyHeaderBytes) {
        throw std::invalid_argument("TCP frame is truncated");
    }

    const auto body_length = ReadInt32Le(encoded, 0);
    if (body_length < static_cast<std::int32_t>(kTcpBodyHeaderBytes)
        || body_length > static_cast<std::int32_t>(kTcpBodyHeaderBytes + kMaxTcpPayloadBytes)) {
        throw std::invalid_argument("TCP frame length is invalid");
    }
    const auto expected_size = sizeof(std::int32_t) + static_cast<std::size_t>(body_length);
    if (encoded.size() != expected_size) {
        throw std::invalid_argument("TCP frame has trailing or missing bytes");
    }

    const auto kind = encoded[4];
    if (!IsValidTcpKind(kind)) {
        throw std::invalid_argument("TCP frame kind is invalid");
    }
    const auto sequence = ReadInt32Le(encoded, 5);
    const auto payload_bits = ReadInt32Le(encoded, 9);
    const auto payload_length = static_cast<std::size_t>(body_length) - kTcpBodyHeaderBytes;
    if (payload_bits < 0
        || static_cast<std::uint64_t>(payload_bits)
            > static_cast<std::uint64_t>(payload_length) * 8u) {
        throw std::invalid_argument("TCP payload bit length is invalid");
    }

    std::vector<std::uint8_t> payload(payload_length);
    std::copy(encoded.begin() + 13, encoded.end(), payload.begin());
    return {
        static_cast<TcpFrameKind>(kind), sequence, std::move(payload), payload_bits};
}

std::vector<std::uint8_t> EncodeUdpEnvelope(
    UdpKind kind,
    const std::string& token,
    const std::vector<std::uint8_t>& payload) {
    if (token.size() > std::numeric_limits<std::uint8_t>::max()) {
        throw std::invalid_argument("UDP token is too long");
    }
    std::vector<std::uint8_t> encoded(2u + token.size() + payload.size());
    encoded[0] = static_cast<std::uint8_t>(kind);
    encoded[1] = static_cast<std::uint8_t>(token.size());
    std::copy(token.begin(), token.end(), encoded.begin() + 2);
    std::copy(payload.begin(), payload.end(), encoded.begin() + 2 + token.size());
    return encoded;
}

UdpEnvelope DecodeUdpEnvelope(const std::vector<std::uint8_t>& encoded) {
    if (encoded.size() < 2u) {
        throw std::invalid_argument("UDP envelope is truncated");
    }
    const auto kind = encoded[0];
    if (kind != static_cast<std::uint8_t>(UdpKind::Bind)
        && kind != static_cast<std::uint8_t>(UdpKind::BindAck)
        && kind != static_cast<std::uint8_t>(UdpKind::Data)) {
        throw std::invalid_argument("UDP kind is invalid");
    }
    const auto token_length = static_cast<std::size_t>(encoded[1]);
    const auto payload_offset = 2u + token_length;
    if (encoded.size() < payload_offset) {
        throw std::invalid_argument("UDP token is truncated");
    }
    UdpEnvelope result{
        kind,
        std::string(encoded.begin() + 2, encoded.begin() + payload_offset),
        std::vector<std::uint8_t>(encoded.begin() + payload_offset, encoded.end()),
    };
    return result;
}

}  // namespace sfs::protocol
