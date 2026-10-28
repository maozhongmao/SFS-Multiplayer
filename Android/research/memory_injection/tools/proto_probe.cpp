// scratch verification harness (not part of the repo deliverable)
#include "protocol/codec.hpp"
#include "protocol/handshake.hpp"
#include "protocol/packets.hpp"
#include <cstdio>
#include <vector>
#include <string>

int main() {
    sfs::protocol::JoinRequest req;
    req.username = "Native";
    req.password = "";
    req.solar_system_name = "Default";
    req.resume_player_id = -1;
    req.resume_token = "";
    auto payload = sfs::protocol::EncodeHello(req);
    sfs::protocol::TcpFrame f{sfs::protocol::TcpFrameKind::Hello, sfs::protocol::kHandshakeVersion,
                              payload, static_cast<std::int32_t>(payload.size() * 8)};
    auto bytes = sfs::protocol::EncodeTcpFrame(f);
    std::printf("payload=%zu frame=%zu\n", payload.size(), bytes.size());
    for (std::size_t i = 0; i < bytes.size(); ++i) std::printf("%02X ", bytes[i]);
    std::printf("\n");

    // round-trip a CreateRocket-ish packet to prove bit-level writer works here
    sfs::protocol::UpdateRocketSecondaryPacket p;
    p.world_time = 12.5; p.rocket_id = 7;
    p.input_turn = 0.25f; p.throttle_percent = 1.0f; p.throttle_on = true; p.rcs = false;
    sfs::net::NetWriter w;
    w.write_u8(static_cast<std::uint8_t>(sfs::protocol::PacketType::kUpdateRocketSecondary));
    p.serialize(w);
    std::printf("secondary bytes=%zu bits=%zu\n", w.byte_length(), w.get_bit_length());
    sfs::net::NetReader r(w.data(), w.get_bit_length());
    int t = r.read_u8();
    sfs::protocol::UpdateRocketSecondaryPacket q;
    q.deserialize(r);
    std::printf("type=%d rid=%d turn=%.3f throttle=%.3f on=%d rcs=%d rem=%zu\n",
                t, q.rocket_id, q.input_turn, q.throttle_percent, q.throttle_on ? 1 : 0,
                q.rcs ? 1 : 0, r.remaining_bits());
    return 0;
}
