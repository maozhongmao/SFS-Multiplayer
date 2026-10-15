// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "../src/protocol/codec.hpp"
#include "../src/protocol/server_version.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

int main() {
    const auto frame = sfs::protocol::TcpFrame::Packet(
        0x01020304,
        std::vector<std::uint8_t>{0xAA, 0xBB},
        13);
    const auto encoded = sfs::protocol::EncodeTcpFrame(frame);

    const std::vector<std::uint8_t> expected{
        11, 0, 0, 0,       // body length: 9-byte header + 2-byte payload
        3,                  // TcpFrameKind::Packet
        4, 3, 2, 1,         // sequence, little endian
        13, 0, 0, 0,        // payload bit length
        0xAA, 0xBB};
    assert(encoded == expected);

    const auto decoded = sfs::protocol::DecodeTcpFrame(encoded);
    assert(decoded.kind == sfs::protocol::TcpFrameKind::Packet);
    assert(decoded.sequence == 0x01020304);
    assert(decoded.payload_bits == 13);
    assert(decoded.payload == std::vector<std::uint8_t>({0xAA, 0xBB}));

    const auto udp = sfs::protocol::EncodeUdpEnvelope(
        sfs::protocol::UdpKind::Bind, "token", {0x10, 0x20});
    assert(udp == std::vector<std::uint8_t>({1, 5, 't', 'o', 'k', 'e', 'n', 0x10, 0x20}));

    const auto udp_decoded = sfs::protocol::DecodeUdpEnvelope(udp);
    assert(udp_decoded.kind == sfs::protocol::UdpKind::Bind);
    assert(udp_decoded.token == "token");
    assert(udp_decoded.payload == std::vector<std::uint8_t>({0x10, 0x20}));

    // 版本探测载荷的字节布局必须和客户端 ServerVersionCodec / C# ServerVersionWire 逐字节一致
    const auto version_payload = sfs::protocol::EncodeServerVersionResponse(2, 32, "1.2.3");
    const std::vector<std::uint8_t> expected_version{
        2, 0, 0, 0,          // handshake version
        32, 0, 0, 0,         // protocol version
        5, 0, 0, 0,          // version string byte length
        '1', '.', '2', '.', '3'};
    assert(version_payload == expected_version);

    std::int32_t handshake = 0;
    std::int32_t protocol = 0;
    std::string server_version;
    assert(sfs::protocol::DecodeServerVersionResponse(
        version_payload, &handshake, &protocol, &server_version));
    assert(handshake == 2);
    assert(protocol == 32);
    assert(server_version == "1.2.3");

    // 长度前缀比实际载荷长：必须判失败，不能越界读
    auto short_payload = version_payload;
    short_payload.resize(14);
    assert(!sfs::protocol::DecodeServerVersionResponse(
        short_payload, &handshake, &protocol, &server_version));
    return 0;
}
