// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// 包级线格式一致性测试：读 golden/packets.txt（由 VectorDump 用 C# 真实实现导出），
// 对每个包做三件事：
//   1. SplitPacket 切出的类型与包体长度必须与向量一致
//   2. C++ 侧构造同一实例编码，必须与向量逐字节相同
//   3. 把向量解码回 C++ 结构再编码，必须仍然逐字节相同（读写互逆）
// 向量里出现但测试没用的条目、或没被向量覆盖的包类型，都判失败。

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../src/net/lidgren_buffer.hpp"
#include "../src/protocol/packets.hpp"

namespace {

namespace proto = sfs::protocol;
namespace net = sfs::net;

struct Golden {
    std::size_t bits = 0;
    std::vector<std::uint8_t> bytes;
};

std::vector<std::uint8_t> FromHex(const std::string& hex) {
    std::vector<std::uint8_t> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return out;
}

std::map<std::string, Golden> LoadGolden(const std::string& path) {
    std::map<std::string, Golden> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto p1 = line.find('|');
        if (p1 == std::string::npos) continue;
        const auto p2 = line.find('|', p1 + 1);
        if (p2 == std::string::npos) continue;
        const auto p3 = line.find('|', p2 + 1);
        if (p3 == std::string::npos) continue;
        const auto name = line.substr(p1 + 1, p2 - p1 - 1);
        Golden g;
        g.bits = static_cast<std::size_t>(std::strtoull(line.substr(p2 + 1, p3 - p2 - 1).c_str(), nullptr, 10));
        g.bytes = FromHex(line.substr(p3 + 1));
        out[name] = g;
    }
    return out;
}

std::string ToHex(const std::vector<std::uint8_t>& v) {
    static const char* k = "0123456789ABCDEF";
    std::string s;
    s.reserve(v.size() * 2);
    for (auto b : v) { s.push_back(k[b >> 4]); s.push_back(k[b & 0x0F]); }
    return s;
}

class Runner {
public:
    explicit Runner(std::map<std::string, Golden> golden) : golden_(std::move(golden)) {}

    // 三个方向一起验：拆分 / 编码对拍 / 解码回编码
    template <typename T>
    void V(const char* name, proto::PacketType type, const T& packet) {
        ++checks_;
        const auto it = golden_.find(name);
        if (it == golden_.end()) { Fail(name, "向量文件里没有这一条"); return; }
        seen_.insert(name);
        types_.insert(static_cast<std::uint8_t>(type));
        const Golden& want = it->second;

        if (want.bits < 8) { Fail(name, "向量位数不足 8 位"); return; }

        // 1) SplitPacket
        proto::PacketBytes body;
        const auto split_type = proto::SplitPacket(want.bytes.data(), want.bits, body);
        if (split_type != type) { Fail(name, "SplitPacket 解出的类型不对"); return; }
        if (body.bits != want.bits - 8) { Fail(name, "SplitPacket 包体位数不对"); return; }
        const std::size_t want_body_bytes = (want.bits - 8 + 7) / 8;
        if (body.bytes.size() != want_body_bytes) { Fail(name, "SplitPacket 包体长不对"); return; }

        // 2) C++ 编码对拍
        const auto enc = proto::EncodePacket(type, packet);
        if (!Same(enc.bits, enc.bytes, want)) { FailDiff(name, "编码对拍不一致", enc.bits, enc.bytes, want); return; }

        // 3) 向量解码回结构再编码
        T decoded{};
        net::NetReader reader(want.bytes.data() + 1, want.bits - 8);
        try {
            decoded.deserialize(reader);
        } catch (const std::exception& e) {
            Fail(name, std::string("解码抛异常: ") + e.what());
            return;
        }
        if (!reader.at_end()) { Fail(name, "解码后未读尽（剩余位未消费）"); return; }
        const auto re = proto::EncodePacket(type, decoded);
        if (!Same(re.bits, re.bytes, want)) { FailDiff(name, "解码回编码不一致", re.bits, re.bytes, want); return; }

        ++passed_;
    }

    bool Same(std::size_t bits, const std::vector<std::uint8_t>& got, const Golden& want) const {
        return bits == want.bits && got == want.bytes;
    }

    void Fail(const std::string& name, const std::string& why) {
        ++failures_;
        std::printf("  [失败] %s: %s\n", name.c_str(), why.c_str());
        if (failures_ <= 8) {
            const auto it = golden_.find(name);
            if (it != golden_.end()) {
                std::printf("         向量: bits=%zu hex=%s\n", it->second.bits, ToHex(it->second.bytes).c_str());
            }
        }
    }

    void FailDiff(const std::string& name, const std::string& why,
                  std::size_t got_bits, const std::vector<std::uint8_t>& got, const Golden& want) {
        ++failures_;
        std::printf("  [失败] %s: %s\n", name.c_str(), why.c_str());
        if (failures_ > 6) return;
        std::printf("         期望: bits=%zu hex=%s\n", want.bits, ToHex(want.bytes).c_str());
        std::printf("         实际: bits=%zu hex=%s\n", got_bits, ToHex(got).c_str());
        for (std::size_t i = 0; i < got.size() || i < want.bytes.size(); ++i) {
            const int a = i < got.size() ? static_cast<int>(got[i]) : -1;
            const int b = i < want.bytes.size() ? static_cast<int>(want.bytes[i]) : -1;
            if (a != b) {
                std::printf("         首个差异在字节 %zu（期望 0x%02X 实际 0x%02X）\n",
                            i, b < 0 ? 0 : b, a < 0 ? 0 : a);
                break;
            }
        }
    }

    void Report() {
        // 没被任何用例使用的向量 = 漏测
        for (const auto& kv : golden_) {
            if (seen_.find(kv.first) == seen_.end()) {
                Fail(kv.first, "向量存在但测试没用到");
            }
        }
        // 每个合法的包类型都必须被向量覆盖
        for (std::uint16_t t = 0; t <= 26; ++t) {
            if (!proto::IsValidPacketType(static_cast<std::uint8_t>(t))) continue;
            if (types_.find(static_cast<std::uint8_t>(t)) == types_.end()) {
                ++failures_;
                std::printf("  [失败] 包类型 %u 没有向量覆盖\n", static_cast<unsigned>(t));
            }
        }
        // 22 必须仍然非法（归档 1.5 客户端的空洞）
        if (proto::IsValidPacketType(22)) {
            ++failures_;
            std::printf("  [失败] 包类型 22 应当是非法值\n");
        }
        std::printf("\n通过 %d / 失败 %d（共 %d 项用例）\n", passed_, failures_, checks_);
    }

    int failures() const { return failures_; }

private:
    std::map<std::string, Golden> golden_;
    std::set<std::string> seen_;
    std::set<std::uint8_t> types_;
    int checks_ = 0;
    int passed_ = 0;
    int failures_ = 0;
};

proto::RocketState RichRocket() {
    proto::PartState engine{};
    engine.name = "Engine";
    engine.x = 1.5f;
    engine.y = -2.5f;
    engine.orientation_x = 1.0f;
    engine.orientation_y = 0.0f;
    engine.orientation_z = 0.5f;
    engine.temperature = 320.25f;
    engine.number_variables.emplace_back("fuel", 0.75);
    engine.toggle_variables.emplace_back("active", true);
    engine.text_variables.emplace_back("label", "主引擎");
    engine.burns = proto::BurnMarkState{0.5f, 0.25f, 1.5f, "top", "bottom"};

    proto::PartState empty{};
    empty.name = "Empty";

    proto::RocketState r{};
    r.rocket_name = "羊城通号";
    r.location = proto::NetLocation{123.5, -456.25, 1.5, -2.5, "Earth"};
    r.rotation = 1.25f;
    r.angular_velocity = -0.75f;
    r.throttle_on = true;
    r.throttle_percent = 0.875f;
    r.rcs = false;
    r.parts.emplace_back(10, engine);
    r.parts.emplace_back(11, empty);
    r.joints.push_back(proto::JointState{10, 11});
    r.stages.push_back(proto::StageState{0, {10, 11}});
    return r;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string dir = argc > 1 ? argv[1] : "golden";
    auto golden = LoadGolden(dir + "/packets.txt");
    if (golden.empty()) {
        std::printf("读不到向量文件: %s/packets.txt\n", dir.c_str());
        return 2;
    }
    std::printf("载入 %zu 条包向量\n\n", golden.size());

    Runner run(std::move(golden));
    const auto rich = RichRocket();

    run.V("joinrequest.utf8", proto::PacketType::kJoinRequest,
          proto::JoinRequestPacket{"羊城通", "", "Earth", -1, ""});
    run.V("joinrequest.ascii", proto::PacketType::kJoinRequest,
          proto::JoinRequestPacket{"maozhongmao", "pw-123", "Jupiter", 42, "resume-token-abc"});

    run.V("joinresponse.basic", proto::PacketType::kJoinResponse,
          proto::JoinResponsePacket{7, 20.0, 3.0, 12345.678, 98765.4321, proto::DifficultyType::kHard,
                                    "Earth", "token-0123456789abcdef", "resume-xyz"});
    run.V("joinresponse.defaults", proto::PacketType::kJoinResponse, proto::JoinResponsePacket{});

    run.V("playerconnected.basic", proto::PacketType::kPlayerConnected,
          proto::PlayerConnectedPacket{3, "羊城通", proto::Color3{0.1f, 0.2f, 0.3f}, true});
    run.V("playerconnected.defaults", proto::PacketType::kPlayerConnected, proto::PlayerConnectedPacket{});

    run.V("playerdisconnected.basic", proto::PacketType::kPlayerDisconnected,
          proto::PlayerDisconnectedPacket{9});
    run.V("playerdisconnected.neg1", proto::PacketType::kPlayerDisconnected, proto::PlayerDisconnectedPacket{});

    run.V("updateplayercontrol.basic", proto::PacketType::kUpdatePlayerControl,
          proto::UpdatePlayerControlPacket{5, 42});
    run.V("updateplayercontrol.neg", proto::PacketType::kUpdatePlayerControl,
          proto::UpdatePlayerControlPacket{});

    run.V("updateplayerauthority.empty", proto::PacketType::kUpdatePlayerAuthority,
          proto::UpdatePlayerAuthorityPacket{});
    run.V("updateplayerauthority.three", proto::PacketType::kUpdatePlayerAuthority,
          proto::UpdatePlayerAuthorityPacket{{1, 2, 3}});

    run.V("updateworldtime.basic", proto::PacketType::kUpdateWorldTime,
          proto::UpdateWorldTimePacket{12345.678});
    run.V("updateworldtime.negative", proto::PacketType::kUpdateWorldTime,
          proto::UpdateWorldTimePacket{-999.5});

    run.V("updateplayercolor.basic", proto::PacketType::kUpdatePlayerColor,
          proto::UpdatePlayerColorPacket{4, proto::Color3{0.9f, 0.5f, 0.25f}});

    run.V("sendchatmessage.utf8", proto::PacketType::kSendChatMessage,
          proto::SendChatMessagePacket{2, "hello 世界"});
    run.V("sendchatmessage.empty", proto::PacketType::kSendChatMessage,
          proto::SendChatMessagePacket{1, ""});

    run.V("createrocket.rich", proto::PacketType::kCreateRocket,
          proto::CreateRocketPacket{100.25, 3, 77, true, rich});
    run.V("createrocket.emptyrocket", proto::PacketType::kCreateRocket,
          proto::CreateRocketPacket{0.0, -1, -1, false, proto::RocketState{}});

    run.V("destroyrocket.basic", proto::PacketType::kDestroyRocket,
          proto::DestroyRocketPacket{55.5, 7, 4});
    run.V("destroyrocket.reason0", proto::PacketType::kDestroyRocket,
          proto::DestroyRocketPacket{0.0, -1, 0});

    run.V("updaterocketprimary.basic", proto::PacketType::kUpdateRocketPrimary,
          proto::UpdateRocketPrimaryPacket{12.5, 7, proto::NetLocation{1.5, 2.5, -3.5, 4.5, "Moon"}, 0.5f, -0.125f});

    run.V("updaterocketsecondary.basic", proto::PacketType::kUpdateRocketSecondary,
          proto::UpdateRocketSecondaryPacket{12.5, 7, 0.5f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 0.875f, true, false});

    run.V("destroypart.basic", proto::PacketType::kDestroyPart,
          proto::DestroyPartPacket{30.5, 5, 10, true, 4});
    run.V("destroypart.noexplosion", proto::PacketType::kDestroyPart,
          proto::DestroyPartPacket{0.0, -1, -1, false, 0});

    run.V("updatestaging.empty", proto::PacketType::kUpdateStaging,
          proto::UpdateStagingPacket{1.0, 2, {}});
    run.V("updatestaging.two", proto::PacketType::kUpdateStaging,
          proto::UpdateStagingPacket{2.0, 3, {proto::StageState{0, {10, 11}}, proto::StageState{1, {12}}}});

    run.V("updatepartengine.basic", proto::PacketType::kUpdatePartEngineModule,
          proto::UpdatePartEnginePacket{1.5, 2, 3, true});
    run.V("updatepartwheel.basic", proto::PacketType::kUpdatePartWheelModule,
          proto::UpdatePartWheelPacket{1.5, 2, 4, false});
    run.V("updatepartbooster.basic", proto::PacketType::kUpdatePartBoosterModule,
          proto::UpdatePartBoosterPacket{1.5, 2, 5, true, 0.5f, 0.625f});
    run.V("updatepartparachute.basic", proto::PacketType::kUpdatePartParachuteModule,
          proto::UpdatePartParachutePacket{1.5, 2, 6, 0.25f, 0.75f});
    run.V("updatepartmove.basic", proto::PacketType::kUpdatePartMoveModule,
          proto::UpdatePartMovePacket{1.5, 2, 7, 1.25f, 2.5f});

    run.V("updatepartresource.basic", proto::PacketType::kUpdatePartResourceModule,
          proto::UpdatePartResourcePacket{1.5, 2, 0.875, {8, 9}});
    run.V("updatepartresource.empty", proto::PacketType::kUpdatePartResourceModule,
          proto::UpdatePartResourcePacket{0.0, -1, 0.0, {}});

    run.V("showtoast.utf8", proto::PacketType::kShowToastMessage,
          proto::PlayerEventToastPacket{"已进入轨道 🚀"});
    run.V("showtoast.empty", proto::PacketType::kShowToastMessage, proto::PlayerEventToastPacket{});

    run.V("docktransaction.uncommitted", proto::PacketType::kDockTransaction,
          proto::DockTransactionPacket{3, proto::DockTransactionOperation::kUndock, false, 1, 2, 10, 20, 5.5, {}, -1, {}});
    run.V("docktransaction.committed_nosecond", proto::PacketType::kDockTransaction,
          proto::DockTransactionPacket{4, proto::DockTransactionOperation::kDock, true, 1, 2, 10, 20, 6.5, rich, -1, {}});
    run.V("docktransaction.committed_withsecond", proto::PacketType::kDockTransaction,
          proto::DockTransactionPacket{5, proto::DockTransactionOperation::kDock, true, 1, 2, 10, 20, 7.5, rich, 3, rich});

    run.V("timewarp.notice", proto::PacketType::kTimeWarp,
          proto::TimeWarpPacket{proto::TimeWarpOperation::kNotice, 0, -1, "", 1.0, false, 0.0, 0, ""});
    run.V("timewarp.request", proto::PacketType::kTimeWarp,
          proto::TimeWarpPacket{proto::TimeWarpOperation::kRequest, 11, 2, "羊城通", 10.0, true, 4321.5, 30, "加速到 10x"});

    run.V("experimentalaccess.request", proto::PacketType::kExperimentalAccess,
          proto::ExperimentalAccessPacket{true, "", false, ""});
    run.V("experimentalaccess.granted", proto::PacketType::kExperimentalAccess,
          proto::ExperimentalAccessPacket{false, "口令", true, "已开启"});

    run.V("p2ppeeroffer.inactive", proto::PacketType::kP2PPeerOffer, proto::P2PPeerOfferPacket{});
    run.V("p2ppeeroffer.active", proto::PacketType::kP2PPeerOffer,
          proto::P2PPeerOfferPacket{true, 2, "192.168.1.10", 21899, "pair-abc", 10, {1, 2, 3}, {4, 5}});

    run.Report();
    return run.failures() == 0 ? 0 : 1;
}