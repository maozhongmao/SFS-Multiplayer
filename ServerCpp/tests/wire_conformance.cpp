// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 线格式一致性测试：把 C++ 编码结果与 golden/wire.txt 逐字节比对。
//
// wire.txt 由 ServerCpp/tools/VectorDump 调用真实 C# 协议实现导出，
// 是唯一权威。本测试不描述"应该"是什么样，只回答"和 C# 是否一模一样"。
//
// 用法：wire_conformance.exe <golden 目录>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/net/lidgren_buffer.hpp"
#include "../src/protocol/codec.hpp"
#include "../src/protocol/handshake.hpp"
#include "../src/protocol/server_info.hpp"

namespace {

struct Vector {
    std::string group;
    std::string name;
    std::int32_t bits = 0;
    std::string hex;
};

int g_failures = 0;
int g_passed = 0;

std::string ToHex(const std::vector<std::uint8_t>& bytes) {
    static const char* kDigits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        out.push_back(kDigits[byte >> 4]);
        out.push_back(kDigits[byte & 0x0F]);
    }
    return out;
}

void Check(const Vector& v, const std::vector<std::uint8_t>& actual, const char* what) {
    const std::string hex = ToHex(actual);
    if (hex == v.hex) {
        ++g_passed;
        return;
    }
    ++g_failures;
    if (g_failures <= 20) {
        std::cerr << "FAIL " << v.group << "." << v.name << " (" << what << ")\n"
                  << "  期望 " << v.hex << "\n"
                  << "  实际 " << hex << "\n";
    }
}

std::vector<Vector> LoadVectors(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        std::cerr << "无法打开向量文件: " << path << "\n";
        std::exit(2);
    }
    std::vector<Vector> vectors;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        // 格式：group|name|bits|HEX（十六进制不含 '|'，直接切分即可）
        std::istringstream fields(line);
        std::string bits_text;
        Vector v;
        if (!std::getline(fields, v.group, '|')) continue;
        if (!std::getline(fields, v.name, '|')) continue;
        if (!std::getline(fields, bits_text, '|')) continue;
        if (!std::getline(fields, v.hex)) continue;
        v.bits = std::stoi(bits_text);
        vectors.push_back(std::move(v));
    }
    return vectors;
}

// 与导出器里 PayloadBytes 相同的构造：4 个 double + 一个字符串。
std::vector<std::uint8_t> NetLocationPayload(std::int32_t* bits_out) {
    sfs::net::NetWriter w;
    w.write_f64(1.5);
    w.write_f64(-2.25);
    w.write_f64(0.125);
    w.write_f64(1000.5);
    w.write_string("Earth");
    if (bits_out != nullptr) {
        *bits_out = static_cast<std::int32_t>(w.bit_length());
    }
    return std::vector<std::uint8_t>(w.data(), w.data() + w.byte_length());
}

std::vector<std::uint8_t> AsBytes(const sfs::net::NetWriter& w) {
    return std::vector<std::uint8_t>(w.data(), w.data() + w.byte_length());
}

// 返回 false 表示向量名未知——未知必须算失败，绝不能静默跳过。
bool BuildTcpFrame(const std::string& name, sfs::protocol::TcpFrame* frame) {
    std::int32_t bits = 0;
    const auto netloc = NetLocationPayload(&bits);
    if (name == "hello.empty") { *frame = {sfs::protocol::TcpFrameKind::Hello, 0, {}, 0}; return true; }
    if (name == "hello.netlocation") { *frame = {sfs::protocol::TcpFrameKind::Hello, 0, netloc, bits}; return true; }
    if (name == "helloack.empty") { *frame = {sfs::protocol::TcpFrameKind::HelloAck, 0, {}, 0}; return true; }
    if (name == "packet.seq0") { *frame = {sfs::protocol::TcpFrameKind::Packet, 0, netloc, bits}; return true; }
    if (name == "packet.seq12345") { *frame = {sfs::protocol::TcpFrameKind::Packet, 12345, netloc, bits}; return true; }
    if (name == "packet.seq_min") {
        *frame = {sfs::protocol::TcpFrameKind::Packet, INT32_MIN, netloc, bits};
        return true;
    }
    if (name == "packet.bitodd33") {
        // 33 位：bool(1) + i32(32)，字节长度为 5，尾 7 位是补零。
        sfs::net::NetWriter w;
        w.write_bool(true);
        w.write_i32(0x12345678);
        *frame = {sfs::protocol::TcpFrameKind::Packet, 3, AsBytes(w),
                  static_cast<std::int32_t>(w.bit_length())};
        return true;
    }
    if (name == "packet.payload300") {
        std::vector<std::uint8_t> buf(300);
        for (std::size_t i = 0; i < buf.size(); ++i) {
            buf[i] = static_cast<std::uint8_t>(i & 0xFF);
        }
        sfs::net::NetWriter w;
        w.write_bytes(buf.data(), buf.size());
        *frame = {sfs::protocol::TcpFrameKind::Packet, 300, AsBytes(w),
                  static_cast<std::int32_t>(w.bit_length())};
        return true;
    }
    if (name == "ping.seq_max") { *frame = {sfs::protocol::TcpFrameKind::Ping, INT32_MAX, {}, 0}; return true; }
    if (name == "pong.seq_min") { *frame = {sfs::protocol::TcpFrameKind::Pong, INT32_MIN, {}, 0}; return true; }
    if (name == "disconnect.seq_neg1") { *frame = {sfs::protocol::TcpFrameKind::Disconnect, -1, {}, 0}; return true; }
    if (name == "request_world_snapshot.empty") { *frame = {sfs::protocol::TcpFrameKind::RequestWorldSnapshot, 7, {}, 0}; return true; }
    if (name == "request_rocket_snapshot.empty") { *frame = {sfs::protocol::TcpFrameKind::RequestRocketSnapshot, 8, {}, 0}; return true; }
    if (name == "server_info_request.empty") { *frame = {sfs::protocol::TcpFrameKind::ServerInfoRequest, 0, {}, 0}; return true; }
    if (name == "server_info_response.3of16") {
        const auto payload = sfs::protocol::EncodeServerInfoResponse(3, 16);
        *frame = {sfs::protocol::TcpFrameKind::ServerInfoResponse, 0, payload,
                  static_cast<std::int32_t>(payload.size() * 8)};
        return true;
    }
    if (name == "server_info_response.0of1") {
        const auto payload = sfs::protocol::EncodeServerInfoResponse(0, 1);
        *frame = {sfs::protocol::TcpFrameKind::ServerInfoResponse, 0, payload,
                  static_cast<std::int32_t>(payload.size() * 8)};
        return true;
    }
    return false;
}

bool BuildUdpEnvelope(
    const std::string& name,
    sfs::protocol::UdpKind* kind,
    std::string* token,
    std::vector<std::uint8_t>* payload) {
    if (name == "bind.tok3_empty") { *kind = sfs::protocol::UdpKind::Bind; *token = "tok"; return true; }
    if (name == "bind.emptytoken") { *kind = sfs::protocol::UdpKind::Bind; *token = ""; return true; }
    if (name == "bindack.token32") { *kind = sfs::protocol::UdpKind::BindAck; *token = std::string(32, 'a'); return true; }
    if (name == "data.emptytoken_payload") { *kind = sfs::protocol::UdpKind::Data; *token = ""; *payload = NetLocationPayload(nullptr); return true; }
    if (name == "data.token255_payload") { *kind = sfs::protocol::UdpKind::Data; *token = std::string(255, 'b'); *payload = NetLocationPayload(nullptr); return true; }
    if (name == "data.utf8token_cn_payload") { *kind = sfs::protocol::UdpKind::Data; *token = "羊城通"; *payload = NetLocationPayload(nullptr); return true; }
    return false;
}

// 握手与 ServerInfo 的往返自检：编码结果必须能原样解回来。
void RoundTripHandshake() {
    sfs::protocol::JoinRequest hello;
    hello.username = "羊城通";
    hello.password = "";
    hello.solar_system_name = "Earth";
    hello.resume_player_id = -1;
    hello.resume_token = "";
    const auto decoded_hello = sfs::protocol::DecodeHello(sfs::protocol::EncodeHello(hello));
    if (decoded_hello.username != hello.username
        || decoded_hello.resume_player_id != hello.resume_player_id) {
        ++g_failures;
        std::cerr << "FAIL handshake.hello 往返不一致\n";
    } else {
        ++g_passed;
    }

    sfs::protocol::JoinResponse ack;
    ack.player_id = 7;
    ack.update_rockets_period = 20.0;
    ack.chat_message_cooldown = 3.0;
    ack.world_time = 12345.678;
    ack.send_time = 98765.4321;
    ack.difficulty = 2;
    ack.solar_system_name = "Earth";
    ack.udp_session_token = "token-0123456789abcdef";
    ack.resume_token = "resume-xyz";
    const auto decoded_ack = sfs::protocol::DecodeAck(sfs::protocol::EncodeAck(ack));
    if (decoded_ack.udp_session_token != ack.udp_session_token || decoded_ack.world_time != ack.world_time) {
        ++g_failures;
        std::cerr << "FAIL handshake.ack 往返不一致\n";
    } else {
        ++g_passed;
    }

    // 非法输入必须抛异常，不能静默返回 0。
    for (const auto& bad : std::vector<std::vector<std::uint8_t>>{
             {}, {0x00}, {0x53, 0x53, 0x46, 0x31}, {0x53, 0x53, 0x46, 0x31, 0x09, 0x00, 0x00, 0x00}}) {
        bool threw = false;
        try {
            (void)sfs::protocol::DecodeHello(bad);
        } catch (const std::exception&) {
            threw = true;
        }
        if (!threw) {
            ++g_failures;
            std::cerr << "FAIL handshake 非法载荷未抛异常 (size=" << bad.size() << ")\n";
        } else {
            ++g_passed;
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "golden";
    const auto vectors = LoadVectors(dir + "/wire.txt");
    if (vectors.empty()) {
        std::cerr << "向量为空，拒绝给出通过结论\n";
        return 2;
    }

    // 覆盖度自检：每个 TcpFrameKind 都必须至少在向量里出现一次。
    bool kind_seen[11] = {};
    int tcp_frames = 0;
    int udp_envelopes = 0;
    int handshakes = 0;

    for (const auto& v : vectors) {
        if (v.group == "tcpframe") {
            ++tcp_frames;
            sfs::protocol::TcpFrame frame{};
            if (!BuildTcpFrame(v.name, &frame)) {
                ++g_failures;
                std::cerr << "FAIL 未知的 tcpframe 向量: " << v.name << "\n";
                continue;
            }
            kind_seen[static_cast<std::uint8_t>(frame.kind)] = true;
            Check(v, sfs::protocol::EncodeTcpFrame(frame), "EncodeTcpFrame");

            const auto decoded = sfs::protocol::DecodeTcpFrame(
                sfs::protocol::EncodeTcpFrame(frame));
            if (decoded.kind != frame.kind || decoded.sequence != frame.sequence
                || decoded.payload_bits != frame.payload_bits
                || decoded.payload != frame.payload) {
                ++g_failures;
                std::cerr << "FAIL tcpframe." << v.name << " 解码往返不一致\n";
            } else {
                ++g_passed;
            }
        } else if (v.group == "udp") {
            ++udp_envelopes;
            sfs::protocol::UdpKind kind{};
            std::string token;
            std::vector<std::uint8_t> payload;
            if (!BuildUdpEnvelope(v.name, &kind, &token, &payload)) {
                ++g_failures;
                std::cerr << "FAIL 未知的 udp 向量: " << v.name << "\n";
                continue;
            }
            Check(v, sfs::protocol::EncodeUdpEnvelope(kind, token, payload), "EncodeUdpEnvelope");

            const auto decoded = sfs::protocol::DecodeUdpEnvelope(
                sfs::protocol::EncodeUdpEnvelope(kind, token, payload));
            if (decoded.kind != static_cast<std::uint8_t>(kind) || decoded.token != token
                || decoded.payload != payload) {
                ++g_failures;
                std::cerr << "FAIL udp." << v.name << " 解码往返不一致\n";
            } else {
                ++g_passed;
            }
        } else if (v.group == "handshake") {
            ++handshakes;
            if (v.name == "hello.basic") {
                sfs::protocol::JoinRequest hello;
                hello.username = "羊城通";
                hello.solar_system_name = "Earth";
                hello.resume_player_id = -1;
                Check(v, sfs::protocol::EncodeHello(hello), "EncodeHello");
            } else if (v.name == "hello.resume") {
                sfs::protocol::JoinRequest hello;
                hello.username = "maozhongmao";
                hello.password = "pw-123";
                hello.solar_system_name = "Jupiter";
                hello.resume_player_id = 42;
                hello.resume_token = "resume-token-abc";
                Check(v, sfs::protocol::EncodeHello(hello), "EncodeHello");
            } else if (v.name == "ack.basic") {
                sfs::protocol::JoinResponse ack;
                ack.player_id = 7;
                ack.update_rockets_period = 20.0;
                ack.chat_message_cooldown = 3.0;
                ack.world_time = 12345.678;
                ack.send_time = 98765.4321;
                ack.difficulty = 2;
                ack.solar_system_name = "Earth";
                ack.udp_session_token = "token-0123456789abcdef";
                ack.resume_token = "resume-xyz";
                Check(v, sfs::protocol::EncodeAck(ack), "EncodeAck");
            } else {
                ++g_failures;
                std::cerr << "FAIL 未知的 handshake 向量: " << v.name << "\n";
            }
        } else {
            ++g_failures;
            std::cerr << "FAIL 未知的向量分组: " << v.group << "\n";
        }
    }

    RoundTripHandshake();

    int missing_kinds = 0;
    for (int kind = 1; kind <= 10; ++kind) {
        if (!kind_seen[kind]) {
            ++missing_kinds;
            std::cerr << "FAIL 向量缺少 TcpFrameKind = " << kind << "\n";
        }
    }
    g_failures += missing_kinds;

    std::cout << "向量总数 : " << vectors.size()
              << " (tcpframe=" << tcp_frames
              << " udp=" << udp_envelopes
              << " handshake=" << handshakes << ")\n";
    std::cout << "通过     : " << g_passed << "\n";
    std::cout << "失败     : " << g_failures << "\n";
    if (g_failures == 0) {
        std::cout << "结果     : 与 C# 实现逐字节一致 ✓\n";
        return 0;
    }
    std::cout << "结果     : 存在不一致 ✗\n";
    return 1;
}
