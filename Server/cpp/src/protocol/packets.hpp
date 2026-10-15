// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../net/lidgren_buffer.hpp"

// 游戏包层：与 C# SfsMultiplayer.Protocol 的 PacketModels / PartPacketModels /
// StateModels / DockingPacketModels / TimeWarpPacketModels / P2PPeerOfferPacket /
// ExperimentalAccessPacket 逐字段对应。
//
// 容器表示：C# 侧用 Dictionary / HashSet，枚举顺序在 C# 里没有保证；这里统一用
// 有序 vector 表示，序列化时按 vector 顺序写出。接收方按顺序读回，语义不受影响。

namespace sfs::protocol {

// ---- 与 C# NetMessageExtensions 一致的边界 ----
constexpr std::int32_t kMaxCollectionCount = 100000;
constexpr std::int32_t kMaxStringLength = 1000000;
// P2P 包单独用 256 上限，不是 kMaxCollectionCount
constexpr std::int32_t kMaxP2PRocketCount = 256;

// ---- 枚举 ----

// 取值顺序是线协议的一部分，22 是有意留空的（归档 1.5 客户端如此）
enum class PacketType : std::uint8_t {
    kJoinRequest = 0,
    kJoinResponse = 1,
    kPlayerConnected = 2,
    kPlayerDisconnected = 3,
    kUpdatePlayerControl = 4,
    kUpdatePlayerAuthority = 5,
    kUpdateWorldTime = 6,
    kUpdatePlayerColor = 7,
    kSendChatMessage = 8,
    kCreateRocket = 9,
    kDestroyRocket = 10,
    kUpdateRocketPrimary = 11,
    kUpdateRocketSecondary = 12,
    kDestroyPart = 13,
    kUpdateStaging = 14,
    kUpdatePartEngineModule = 15,
    kUpdatePartWheelModule = 16,
    kUpdatePartBoosterModule = 17,
    kUpdatePartParachuteModule = 18,
    kUpdatePartMoveModule = 19,
    kUpdatePartResourceModule = 20,
    kShowToastMessage = 21,
    // 【启用 2026-10-15】客户端侧早已定义 22 = UpdateCheatStatus（原本归档自 1.5、服务端当非法空位丢弃）。
    // 现在正式启用为「服务端 -> 客户端」的作弊策略下发：bool enabled + uint16 位图（bit0..bit9）。
    // 消息号 22 永不更改；客户端 Packet_UpdateCheatStatus 的线上布局必须与本文件保持一致。
    kUpdateCheatStatus = 22,
    kDockTransaction = 23,
    kTimeWarp = 24,
    // 【已停用 2026-10-06】实验性访问机制已砍掉（权限分组以后并进密码系统）。
    // 消息号保留占位：消息号永不改，重编号会让老客户端错位解析。
    kExperimentalAccess = 25,
    kP2PPeerOffer = 26,
    kRenameRocket = 27,
    kUpdatePartTemperature = 28,
    kUpdateDestructionReason = 29,
    kServerCommand = 30,
    kUploadLog = 31,
    // 【新增 2026-10-10】客户端 -> 服务端：广告本端 P2P socket 实际绑定的 UDP 端口。
    // 不广告就会打错门：服务端撮合填的 peer_port 取自主传输会话观察到的端点，
    // 而 P2P socket 绑的是另一个随机空闲端口 -> 对方探测全部打进主传输 socket，
    // KCP conv 不匹配直接丢包 -> 实测发 39 个收 0 个、永远握手超时。
    kP2PEndpoint = 32,
};

// 客户端 -> 服务端：广告本端 P2P socket 实际绑定的 UDP 端口。
struct P2PEndpointPacket {
    std::int32_t port = -1;
    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

// 服务端 -> 客户端：作弊策略。
//   enabled         总开关。false = 客户端隐藏整个作弊入口（暂停菜单按钮 + 抽屉 + 解锁/购买）。
//   individual_flags 逐项位图，位号与客户端 CheatFlag 完全一致（两端必须同时改，禁止重排）：
//     bit0 InfiniteBuildArea  bit1 PartClipping   bit2 InfiniteFuel  bit3 NoAtmosphericDrag
//     bit4 UnbreakableParts   bit5 NoGravity      bit6 NoHeatDamage  bit7 NoBurnMarks
//     bit8 Teleport           bit9 RefillFuelTanks
//   置 1 = 允许；置 0 = 禁止。bit10..bit15 恒 0。
// 线上布局：[0] bool（1 bit）+ [1..2] uint16（小端）；与 C# 侧 Write(bool)+Write(ushort) 逐位一致。
struct UpdateCheatStatusPacket {
    bool enabled = true;
    std::uint16_t individual_flags = 0x03FFu;   // 默认全开 = 与"没有策略"等价
    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

enum class DifficultyType : std::uint8_t {
    kNormal = 0,
    kEasy = 1,
    kHard = 2,
};

enum class DockTransactionOperation : std::uint8_t {
    kDock = 0,
    kUndock = 1,
};

enum class TimeWarpOperation : std::uint8_t {
    kRequest = 0,
    kVote = 1,
    kResponse = 2,
    kApplied = 3,
    kCancelled = 4,
    kNotice = 5,
};

// C# 的 Enum.IsDefined 等价物：取值表里有才算合法
bool IsValidPacketType(std::uint8_t raw);
bool IsValidDifficulty(std::uint8_t raw);
bool IsValidDockOperation(std::uint8_t raw);
bool IsValidTimeWarpOperation(std::uint8_t raw);

// ---- 基础结构 ----

struct NetLocation {
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    std::string address = "Earth";  // C# NetLocation.Empty 的默认地址

    static NetLocation empty() { return NetLocation{}; }
};

struct Color3 {
    float r = 0.2f;
    float g = 0.8f;
    float b = 1.0f;

    static Color3 default_color() { return Color3{}; }
};

struct BurnMarkState {
    float angle = 0.0f;
    float intensity = 0.0f;
    float x = 0.0f;
    std::string top;
    std::string bottom;
};

struct PartState {
    std::string name;
    float x = 0.0f;
    float y = 0.0f;
    float orientation_x = 1.0f;
    float orientation_y = 1.0f;
    float orientation_z = 0.0f;
    float temperature = -std::numeric_limits<float>::infinity();
    std::vector<std::pair<std::string, double>> number_variables;
    std::vector<std::pair<std::string, bool>> toggle_variables;
    std::vector<std::pair<std::string, std::string>> text_variables;
    // 空 = 线上写 true（C# 写的是 "Burns is null"）
    std::optional<BurnMarkState> burns;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct JointState {
    std::int32_t part_a = 0;
    std::int32_t part_b = 0;
};

struct StageState {
    std::int32_t stage_id = 0;
    std::vector<std::int32_t> part_ids;
};

struct RocketState {
    std::string rocket_name;
    NetLocation location = NetLocation::empty();
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    bool throttle_on = false;
    float throttle_percent = 0.0f;
    bool rcs = false;
    std::vector<std::pair<std::int32_t, PartState>> parts;
    std::vector<JointState> joints;
    std::vector<StageState> stages;

    // 对应 C# RocketState.Apply(UpdateRocketPrimaryPacket)
    void apply_primary(const NetLocation& loc, float rot, float ang_vel);
    // 对应 C# RocketState.Apply(UpdateRocketSecondaryPacket)
    void apply_secondary(float throttle_pct, bool throttle_on, bool rcs_on);
    // 对应 C# RocketState.RemovePart
    bool remove_part(std::int32_t part_id);

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct WorldSnapshot {
    double world_time = 0.0;
    DifficultyType difficulty = DifficultyType::kNormal;
    std::vector<std::pair<std::int32_t, RocketState>> rockets;
};

// ---- 物理/行星参数（对齐游戏端 PhysicsMode） ----

struct PlanetPhysics {
    std::string name;           // 行星代号（Earth, Moon, Mars...）
    double radius = 0.0;        // 赤道半径 (m)
    double gm = 0.0;            // 标准引力参数 GM (m^3/s^2)
    double rotation_period = 0.0; // 自转周期 (s)
    double atmosphere_height = 0.0; // 大气层高度 (m)
    double surface_pressure = 0.0;  // 地表气压 (Pa)
    double surface_temp = 0.0;      // 地表温度 (K)
    double molar_mass = 0.0;        // 大气摩尔质量 (kg/mol)
    // 大气密度曲线：高度(km) -> 密度(kg/m^3) 采样点
    std::vector<std::pair<double, double>> atmosphere_density_curve;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct PhysicsConstants {
    // 全局物理常量（与游戏端 PhysicsMode 保持一致）
    double gravitational_constant = 6.67430e-11; // G
    double fixed_dt = 0.02;    // 固定时间步 (s) = 1/50
    int max_substeps = 4;      // 最大子步数
    double collision_margin = 0.01; // 碰撞边距 (m)

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

// ---- 包 ----

struct JoinRequestPacket {
    std::string username;
    std::string password;
    std::string solar_system_name;
    std::int32_t resume_player_id = -1;
    std::string resume_token;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct JoinResponsePacket {
    std::int32_t player_id = -1;
    double update_rockets_period = 20.0;
    double chat_message_cooldown = 3.0;
    double world_time = 0.0;
    double send_time = 0.0;
    DifficultyType difficulty = DifficultyType::kNormal;
    std::string solar_system_name;
    std::string udp_session_token;
    // 只在行走 HelloAck 帧（SessionHandshakeCodec）时有值；
    // 作为普通 Packet 帧发送时线上不带这个字段。
    std::string resume_token;

    // 普通 Packet 帧形态（不含 resume_token）
    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct PlayerConnectedPacket {
    std::int32_t player_id = -1;
    std::string username;
    Color3 icon_color = Color3::default_color();
    bool print_message = false;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct PlayerDisconnectedPacket {
    std::int32_t player_id = -1;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePlayerControlPacket {
    std::int32_t player_id = -1;
    std::int32_t rocket_id = -1;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePlayerAuthorityPacket {
    std::vector<std::int32_t> rocket_ids;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdateWorldTimePacket {
    double world_time = 0.0;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePlayerColorPacket {
    std::int32_t player_id = -1;
    Color3 color = Color3::default_color();

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct SendChatMessagePacket {
    std::int32_t sender_id = -1;
    std::string message;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct CreateRocketPacket {
    double world_time = 0.0;
    std::int32_t local_id = -1;
    std::int32_t global_id = -1;
    bool for_launch = false;
    RocketState rocket;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct DestroyRocketPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::uint8_t reason = 0;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdateRocketPrimaryPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    NetLocation location = NetLocation::empty();
    float rotation = 0.0f;
    float angular_velocity = 0.0f;
    // 【结构哈希 2026-10-10】权威方填的"活体零件 ID 集合"哈希。服务端只透传，不解释。
    std::uint32_t structure_hash = 0;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdateRocketSecondaryPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    float input_turn = 0.0f;
    float raw_x = 0.0f;
    float raw_y = 0.0f;
    float horizontal_x = 0.0f;
    float horizontal_y = 0.0f;
    float vertical_x = 0.0f;
    float vertical_y = 0.0f;
    float throttle_percent = 0.0f;
    bool throttle_on = false;
    bool rcs = false;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct DestroyPartPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    bool create_explosion = false;
    std::uint8_t reason = 0;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdateStagingPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::vector<StageState> stages;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartEnginePacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    bool engine_on = false;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartWheelPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    bool wheel_on = false;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartBoosterPacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    bool primed = false;
    float throttle = 0.0f;
    float fuel_percent = 0.0f;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartParachutePacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    float state = 0.0f;
    float target_state = 0.0f;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartMovePacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    float time = 0.0f;
    float target_time = 0.0f;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartResourcePacket {
    double world_time = 0.0;
    std::int32_t rocket_id = -1;
    double resource_percent = 0.0;
    std::vector<std::int32_t> part_ids;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

// 复用客户端 ShowToastMessage（PacketType 21）通道
struct PlayerEventToastPacket {
    std::string message;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct DockTransactionPacket {
    std::int32_t transaction_id = 0;
    DockTransactionOperation operation = DockTransactionOperation::kDock;
    bool committed = false;
    std::int32_t keep_rocket_id = -1;
    std::int32_t remove_rocket_id = -1;
    std::int32_t keep_part_id = -1;
    std::int32_t remove_part_id = -1;
    double world_time = 0.0;
    std::optional<RocketState> merged_rocket;
    std::int32_t second_rocket_id = -1;
    std::optional<RocketState> second_rocket;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct TimeWarpPacket {
    TimeWarpOperation operation = TimeWarpOperation::kRequest;
    std::int32_t vote_id = 0;
    std::int32_t requester_id = -1;
    std::string requester_name;
    double multiplier = 1.0;
    bool approved = false;
    double world_time = 0.0;
    std::int32_t timeout_seconds = 0;
    std::string message;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct ExperimentalAccessPacket {
    bool request = false;
    std::string passphrase;
    bool granted = false;
    std::string message;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct P2PPeerOfferPacket {
    bool active = false;
    std::int32_t peer_player_id = -1;
    std::string peer_address;
    std::int32_t peer_port = 0;
    std::string pair_token;
    std::int32_t transition_buffer_seconds = 10;
    std::vector<std::int32_t> local_rocket_ids;
    std::vector<std::int32_t> peer_rocket_ids;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct RenameRocketPacket {
    std::int32_t rocket_id = -1;
    std::string rocket_name;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdatePartTemperaturePacket {
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    float temperature = 0.0f;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct UpdateDestructionReasonPacket {
    std::int32_t rocket_id = -1;
    std::int32_t part_id = -1;
    std::uint8_t reason = 0;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

struct ServerCommandPacket {
    // 客户端 → 服务端：Command 是控制台命令；服务端 → 客户端：Output 是命令输出
    std::string command;
    std::string output;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

// 客户端 → 服务端：上传日志（GZip 压缩字节流），服务端落盘到自己目录下的 logs/
struct UploadLogPacket {
    std::string file_name;
    std::string comment;
    std::vector<std::uint8_t> data;

    void serialize(sfs::net::NetWriter& w) const;
    void deserialize(sfs::net::NetReader& r);
};

// 单次日志上传的压缩后体积上限（超过直接拒收，避免占满板子存储）
constexpr std::int32_t kMaxLogUploadBytes = 6 * 1024 * 1024;

// ---- 包体组装 / 拆分 ----

struct PacketBytes {
    std::vector<std::uint8_t> bytes;
    std::size_t bits = 0;
};

// 编码成 TCP 帧载荷：首字节是包类型，其余是包体。
// 位偏移 0 处写 8 位类型，所以包体始终字节对齐。
template <typename PacketT>
PacketBytes EncodePacket(PacketType type, const PacketT& packet) {
    sfs::net::NetWriter w;
    w.write_u8(static_cast<std::uint8_t>(type));
    packet.serialize(w);
    PacketBytes out;
    out.bytes.assign(w.data(), w.data() + w.byte_length());
    out.bits = w.bit_length();
    return out;
}

// 从载荷里取出包类型并切出包体（包体不含类型字节）。
// 类型非法或载荷为空都会抛 BufferError。
PacketType SplitPacket(const std::uint8_t* payload, std::size_t payload_bits,
                       PacketBytes& out_body);

}  // namespace sfs::protocol
