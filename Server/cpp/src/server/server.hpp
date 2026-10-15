// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <chrono>
#include <cstdint>
#include <random>
#include <array>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "net/lidgren_buffer.hpp"
#include "net/kcp_transport.hpp"
#include "protocol/codec.hpp"
#include "protocol/packets.hpp"
#include "server/rocket_blob.hpp"
#include "server/join_guard.hpp"

// ============================================================
// 冷火箭 blob 编译期开关（任务 #7）
// ------------------------------------------------------------
// 默认【关闭】：未定义 SFS_COLD_ROCKET_BLOB 时，blob 接线全部不参与编译，
// 行为与改动前逐字节一致（parts_begin/parts_end 展开为空操作，ensure/commit 零调用）。
// 打开：在 build-server-exp.ps1 的 $cflags 里加 "-DSFS_COLD_ROCKET_BLOB"。
// ============================================================
#if defined(SFS_COLD_ROCKET_BLOB)
#define SFS_COLD_ROCKET_BLOB_ENABLED 1
#else
#define SFS_COLD_ROCKET_BLOB_ENABLED 0
#endif

namespace sfs::server {

// 单进程玩家上限（JoinResponse / ServerInfoResponse 都用它）
constexpr int kMaxPlayers = 16;

// ---- 跨翻译单元共用的校验 / 查询（定义见 server/packets.cpp）----
// C# ValidateRocket：火箭整体是否合法。reason 非空时会写入可读原因（含 ok）。
bool validate_rocket(const sfs::protocol::RocketState& rocket, const char** reason = nullptr);
// C# rocket.Parts.TryGetValue：该 partId 在这枚火箭里则返回指针，否则 nullptr。
sfs::protocol::PartState* find_part(sfs::protocol::RocketState& rocket, std::int32_t part_id);

struct Connection {
    std::uint32_t conv = 0;            // KCP 会话标识（取代原来的 TCP fd）
    std::uint64_t id = 0;
    std::vector<std::uint8_t> recv;    // 待解析的帧字节流
    std::int32_t next_seq = 1;
    std::int32_t player_id = -1;
    std::string username;
    std::string udp_token;
    bool handshaked = false;
    // 心跳：服务端每 2 秒发一次 Ping，客户端原样回 Pong，据此算 RTT 并下发世界时间
    std::int64_t last_ping_ticks = 0;
    std::int64_t last_pong_ticks = 0;
    double rtt_ms = 0.0;
    double previous_rtt_ms = 0.0;
    double jitter_ms = 0.0;
    std::chrono::steady_clock::time_point last_receive = std::chrono::steady_clock::now();
    // 诊断用收发计数：in 停止增长 = 客户端发送侧出问题（曾被误判为服务端 bug）
    std::uint64_t packets_in = 0;
    std::uint64_t packets_out = 0;
    // 按包类型统计（索引 = PacketType 值），用于确认客户端是否真的发出了某个动作
    std::array<std::uint32_t, 64> type_counts{};
    std::string resume_token;  // 断线重连用（对齐 C# ResumeToken）
    // P2P socket 的广告端口（客户端用 Packet_P2PEndpoint 上报；0 = 尚未广告）。
    // 撮合时用它填 offer 的 peer_port：否则对方会打到主传输 socket 上，探测有去无回。
    std::int32_t p2p_port = 0;
    // 加入被拒（对齐 C# connection.Deny(reason)）：Disconnect 帧（含原因）已经发出，
    // 连接进入"等 KCP 有机会重传后再关闭"的状态，期间不再处理任何帧。
    bool denied = false;
    std::string deny_reason;
    std::chrono::steady_clock::time_point denied_at = std::chrono::steady_clock::now();
     // 【已停用 2026-10-06】原 bool experimental_granted（实验性访问授权）已删除：
    // 权限分组以后并进密码系统。消息号 25 保留占位但在本版本不做任何授权判定。
    // 聊天冷却（对齐 C# ConnectedPlayer.LastChatUtc）
     std::chrono::steady_clock::time_point last_chat = std::chrono::steady_clock::now();
    };

// P2P 信令撮合配置（对齐 C# P2PSettings.cs / server.yml 的 p2p: 段）。
// 默认值与 C# 完全一致；可用命令行或环境变量覆盖，见 main.cpp。
struct P2PSettings {
    // 默认关闭：直连链路目前不稳定（NAT/穿透一抖就掉，掉回来还要强制重同步），
    // 下个大版本再修。要用配置文件/命令行开：--p2p on 或环境变量 SFS_SERVER_P2P=on。
    bool enabled = false;
    double proximity_meters = 10000.0;    // 撮合距离阈值（米）
    int validation_interval_seconds = 1;  // 撮合校验周期（秒）
    // 以下三项 C# 服务端只做配置校验、撮合流程里没用到，这里保留同样的配置面
    int peer_timeout_seconds = 3;
    int transition_buffer_seconds = 10;   // 会随 offer 下发给客户端
    int max_group_members = 16;
    int max_direct_peers_per_client = 8;
};

class Server {
public:
    Server(std::uint16_t port, std::int32_t max_players, std::string state_path,
           P2PSettings p2p = P2PSettings{}, JoinPolicy join = JoinPolicy{});

    bool start();

    void run();

    void stop();

    // 拒绝加入：给客户端一条明确的 Disconnect 原因（对齐 C# SendDisconnectDirectAsync →
    // TcpFrame(Disconnect, 0, UTF8(reason))），写服务端日志，并把连接标记为 denied；
    // 真正的关闭由主循环延时执行（见 kDenyCloseDelaySeconds）。
    void deny_join(int fd, const std::string& reason);

private:
    // 对齐 C# ValidateJoin 的重名判定：_players.Values.Any(
    //     player => string.Equals(player.Username, username, StringComparison.OrdinalIgnoreCase))
    // C# 的 _players 只有"当前有连接"的会话（干净断开 / 心跳超时后立即移除，仅当 UDP 仍存活时
    // 才保留 20 秒恢复窗口），所以这里只比对在线连接：掉线宽限期内的身份不占用名字。
    // 否则玩家重启游戏后 60 秒内都用不回自己的名字（C# 行为是可用的）。
    // 代价与 C# 相同：名字被离线玩家释放后抢注，随后原玩家带 token 恢复，会出现两个同名玩家；
    // 恢复凭的是有效 token（同一台客户端），比"拒绝合法恢复"更可接受。
    bool name_taken(const std::string& name) const;

    // 拒绝后等待多久再关连接：留时间给 KCP 把 Disconnect 帧重传到客户端，
    // 否则丢一个 UDP 包玩家就只能看到"连接断开"，看不到拒绝原因。
    static constexpr double kDenyCloseDelaySeconds = 0.8;

    // 重名判定里"这个连接还算在线"的静默上限（秒）：服务端每 2 秒 Ping、客户端必回 Pong，
    // 10 秒收不到任何帧就等于客户端已经没了（心跳回收要 30 秒，但名字不该被一个消失的
    // 客户端占住 30 秒 —— C# 那边 TCP 一断就释放名字，玩家崩溃重启后用回原名是常态）。
    static constexpr double kDupNameLivenessSeconds = 10.0;

    // ---------------------------------------------------------- 套接字初始化




    // ---------------------------------------------------------- 连接管理
    void close_conn(int fd, const char* why);

    // KCP 没有 accept：枚举当前所有会话，新出现的建立 Connection，已有的收帧。
    void poll_kcp_sessions();

    // ---------------------------------------------------------- 帧解析
    bool drain_frames(int fd);

    // ---------------------------------------------------------- 帧处理
    void handle_frame(int fd, const sfs::protocol::TcpFrame& frame);

    std::int32_t next_seq(int fd);

    void send_control(int fd, sfs::protocol::TcpFrameKind kind);

    void send_frame(int fd, const sfs::protocol::TcpFrame& frame);


    // ---------------------------------------------------------- UDP
    // ---------------------------------------------------------- 统计
    void report(const char* tag);

        // ============================================================
    // 游戏状态（严格对齐 C# TcpMultiplayerServer 的处理流程）
    // ============================================================
    // 分离/对接刚产生的新火箭，权威给创建者保留多久。
    // 3 秒足够让本地物理把分离瞬间的冲量算完，又不至于让远端的火箭长时间没人模拟。
    static constexpr double kAuthorityHoldSeconds = 3.0;

    struct RocketEntry {
        sfs::protocol::RocketState state;
        // ---- RAM 压缩（方案 B，见 Server/cpp/RAM-COMPRESSION.md）----
        // 冷火箭的部件表以 deflate blob 常驻，state.parts 清空；需要读写的少数路径先 materialize。
        // 只压 parts：热字段（location/rotation/throttle/rcs/name/joints/stages）留在展开态。
        sfs::server::PartsBlob parts_blob;
        bool parts_in_memory = false;     // state.parts 当前是否是有效（展开）内容
        bool parts_dirty = false;         // 展开后是否被改过，需要重新打包
        // 需要读/写部件表时先调它：把 blob 解包进 state.parts（幂等，已在内存就直接返回）。
        std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& ensure_parts() {
            if (!parts_in_memory) {
                // 只有"blob 里有内容、而 state.parts 还没展开"时才解包。
                // 新建 / 整体覆盖过的条目 parts_in_memory 仍是 false 但 state.parts 已有内容，
                // 此时绝不能拿旧 blob 覆盖它（否则新状态会退回上一次压缩前的旧部件表）。
                if (state.parts.empty() && !parts_blob.bytes.empty()) {
                    if (!sfs::server::unpack_parts(parts_blob, state.parts)) {
                        state.parts.clear();   // 解包失败绝不放半成品进世界
                    }
                }
                parts_blob.bytes.clear();
                parts_blob.bytes.shrink_to_fit();
                parts_blob.part_count = static_cast<std::uint32_t>(state.parts.size());
                parts_in_memory = true;
            }
            return state.parts;
        }
        // 改完部件表调它：重新压缩并释放展开态（省内存的关键就在这一步）。
        void commit_parts() {
            // 不能只认 parts_in_memory：新建 / 整体覆盖过的条目它还是 false 而 state.parts 已有内容，
            // 这种情况同样必须回包，否则"冷却"是空操作，下一次访问又会被旧 blob 覆盖。
            if (!parts_in_memory && state.parts.empty()) return;
            parts_blob = sfs::server::pack_parts(state.parts);
            state.parts.clear();
            state.parts.shrink_to_fit();
            parts_in_memory = false;
            parts_dirty = false;
        }
        // 【冷火箭 blob 接线】访问 / 读取部件表之前调用（宏关闭时是空操作，不生成代码）。
        void parts_begin() {
#if SFS_COLD_ROCKET_BLOB_ENABLED
            ensure_parts();
#endif
        }
        // 【冷火箭 blob 接线】访问 / 改完之后调用：重新打包并清空 state.parts（宏关闭时是空操作）。
        void parts_end() {
#if SFS_COLD_ROCKET_BLOB_ENABLED
            commit_parts();
#endif
        }
        // 只数部件个数时用它：不必解压（clear_debris 就靠这个）。
        // 注意：新建 / 整体覆盖过但还没 commit 的条目 state.parts 非空而 parts_in_memory 仍是 false，
        // 所以判据用"state.parts 是否为空"而不是 parts_in_memory，否则会把活火箭数成 0 个部件。
        std::size_t parts_count() const {
            if (!state.parts.empty()) return state.parts.size();
            return static_cast<std::size_t>(parts_blob.part_count);
        }
        // ---- 以上为 RAM 压缩字段 ----
        std::int32_t owner_player = -1;   // 创建者，仅用于权限判定
        // 创建时刻：用于"清理无主火箭"的宽限期判定（见 clear_uncontrolled_rockets）。
        // 从存档载入的世界会得到"载入时刻"，即重启后旧无主火箭还会被多保护一个宽限期。
        std::chrono::steady_clock::time_point created_at = std::chrono::steady_clock::now();
        // ---- 权威保留期 ----
        // 这段时间内 refresh_authorities 不得把这枚火箭判给"最近的玩家"，只能留给创建者。
        // 为什么需要：刚分离出来的火箭，服务端手里的状态是从客户端包重建的，精度不如刚刚
        // 在本地完成分离运算的那台机器。立刻改判会让它按服务端那份不精确的状态瞬移 ——
        // 现场表现就是"刚分离就瞬移、还撞掉原来那枚火箭"。
        std::chrono::steady_clock::time_point authority_hold_until{};
        std::int32_t authority_hold_player = -1;
        // 最近一次收到该火箭的状态更新（来自"正在模拟它的那台客户端"）。
        // 用它区分"被丢弃的旧火箭"和"某人在飞、只是当前没操控的火箭"。
        std::chrono::steady_clock::time_point last_update = std::chrono::steady_clock::now();
    };

    // 清理"没有人操控的火箭"，只保留此刻有玩家操控的（以及刚发射还在宽限期的）。
    // 依据：世界里的无主火箭会被引擎持续计算碰撞箱，而客户端模组又忽略它们之间的碰撞对，
    // 实测会形成"算了又不算/算了又算"的循环，把主线程拖到未响应；清空世界即可恢复。
    int clear_uncontrolled_rockets(std::chrono::seconds grace);
    // 【冷火箭 blob】把"当前没有玩家在操控"的火箭部件表压回 blob（宏关闭时为空实现，定义见 world.cpp）。
    void cool_idle_rockets();
    std::unordered_map<std::int32_t, RocketEntry> rockets_;
    std::string state_path_;          // 存档路径（server.yml 的 state_path）
    std::mt19937 rng_{std::random_device{}()};
    std::chrono::steady_clock::time_point last_autosave_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_uncontrolled_sweep_ = std::chrono::steady_clock::now();
    // 宽限期：0 = 直接删（对齐未改动版本的行为）。要临时放宽就改这个数字。
    // 创建宽限：客户端创建火箭后要先等服务端回执、再请求操控权，这段时间内它既没有
    // 操控记录也没有状态更新，若按"无主"直接删，会形成"建->删->重建"风暴
    // （实测 61 次 CreateRocket / 47 次 DestroyPart，每次重建让客户端掉 100ms 帧）。
    // 60 秒足够完成握手；仍然没有操控也没有状态更新的火箭照旧会被清掉。
    static constexpr std::chrono::seconds kUncontrolledGraceSeconds{60};
    // 状态更新保活窗口：这么久内还在被更新的火箭视为"有人在模拟它"，不清（避免删掉在飞的火箭）
    static constexpr std::chrono::seconds kRocketKeepAliveSeconds{10};

    // 对齐 C# NextRocketId：1..INT_MAX 随机，避开已有 ID。
    // （存档里的 ID 是这种大随机数，用自增 1,2,3 会和存档冲突）
    std::int32_t next_rocket_id();

private:
    // 对齐 C# Convert.ToHexString(RandomNumberGenerator.GetBytes(24))：48 个十六进制字符
    std::string random_token();

public:
    // ---------------------------------------------------------- 加入时的初启状态
    // 严格对齐 C# TcpServer.SendInitialState 的顺序与内容。
    void send_initial_state(int fd, std::int32_t joining_id, const std::string& joining_name);

    // ---------------------------------------------------------- 存档
    void load_world_from_disk();

    void save_world_to_disk();
    // 世界时间 = 基准 + 自基准点起的单调秒数 × 倍率（对齐 C# WorldTime 属性）
    double world_time_base_ = 0.0;
    // 难度（0=Normal / 1=Easy / 2=Hard，见 DifficultyType）。
    // 存档里带着这一项，必须跨重启保住：以前写死 0，玩家用 Hard 开的档重启后会变回 Normal。
    std::int32_t difficulty_ = 0;
    double time_scale_ = 1.0;
    std::chrono::steady_clock::time_point world_clock_origin_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_heartbeat_ = std::chrono::steady_clock::now();

    double world_time() const;
    // 玩家信息表（对齐 C# TcpSession：Username / Color / UdpToken / ResumeToken）
    // 断线重连必须保持身份不变 —— 客户端凭 resume_token 认回原会话；若每次都给新
    // player_id/token，客户端会判定重连失败并无限重连，而每次重连又收一遍完整世界
    // 快照，导致它反复销毁重建全部火箭，最终在 Unity 侧崩溃。
    struct PlayerInfo {
        std::string name;
        sfs::protocol::Color3 color = sfs::protocol::Color3::default_color();
        std::string udp_token;
        std::string resume_token;
        std::int32_t controlled_rocket = -1;
        // 掉线时刻（进程内秒数）；0 表示在线。保留记录是为了让 resume 重连复用身份 ——
        // 原来 close_conn 直接 erase，10 秒心跳超时一触发 resume_token 就没了，
        // 重连必然变成"新玩家"（旧玩家名下的控制权与权威全丢）。
        double offline_since = 0.0;

        bool can_resume(const std::string& token) const {
            return !resume_token.empty() && resume_token == token;
        }
    };
    std::unordered_map<std::int32_t, PlayerInfo> players_;

    // 掉线宽限期（秒）：期间保留 PlayerInfo/resume_token 供重连复用身份
    static constexpr double kResumeGraceSeconds = 60.0;
    std::unordered_map<std::int32_t, double> offline_since_;
    std::chrono::steady_clock::time_point process_start_ = std::chrono::steady_clock::now();
    double uptime_now() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - process_start_).count();
    }

    // (player_id << 32 | local_id) -> 服务端已分配的全局火箭 ID。
    // 客户端在没收到回执时会重发同一个 CreateRocket（实测：会话刚建立时首个业务包容易丢，
    // 第一次发射失败、第二次百分百成功）。重发必须按 (玩家, 本地编号) 去重，
    // 否则同一枚火箭会在服务端变成两枚。
    std::unordered_map<std::uint64_t, std::int32_t> created_local_ids_;

    static std::uint64_t created_local_key(std::int32_t pid, std::int32_t local_id) {
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pid)) << 32) |
               static_cast<std::uint32_t>(local_id);
    }

    // 对齐 C# RandomColor()：HSV(h, 1, 1) → RGB
    sfs::protocol::Color3 random_color();

    // player_id -> 该玩家声明控制的火箭（对应 C# TcpSession.ControlledRocket）
    std::unordered_map<std::int32_t, std::int32_t> controlled_rocket_;
    // player_id -> 该玩家的权威火箭集合（对应 C# TcpSession.UpdateAuthority）
    std::map<std::int32_t, std::set<std::int32_t>> update_authority_;

    // 已下发给每个客户端的权威火箭列表（player_id -> 升序 id 列表）。
    // 只在集合变化时重发 —— 客户端只对"自己有权"的火箭发状态包，不下发就等于谁都不上报位置。
    std::map<std::int32_t, std::vector<std::int32_t>> sent_authority_;
    // traffic 命令的差分基准（上一次调用的时刻 + 当时每连接的 in/out 累计）。
    std::chrono::steady_clock::time_point last_traffic_at_{};
    std::map<std::int32_t, std::pair<std::uint64_t, std::uint64_t>> last_traffic_counts_;
    std::uint32_t authority_resend_tick_ = 0;   // 权威列表的兜底补发节拍（见 refresh_authorities）

    // 权威按距离重分配的周期节拍（见 lifecycle.cpp 里的调用处与 world.cpp 的分配逻辑）。
    // last_authority_distance_pass_ 是上次跑的时刻；authority_distance_recheck_needed_
    // 是"自上次重算以来有没有火箭移动过"的开关 —— 世界完全静止时整个节拍不花任何 CPU。
    std::chrono::steady_clock::time_point last_authority_distance_pass_{};
    bool authority_distance_recheck_needed_ = true;

    // ---------------------------------------------------------- P2P 信令撮合
    // 对齐 C# TcpMultiplayerServer.RefreshP2PPeers：
    // 每个校验周期把\"双方火箭间距 <= proximity_meters\"的玩家两两撮合，向双方各下发一份
    // P2PPeerOffer(Active=true, 对方地址/端口、pairToken、双方火箭 id 列表)；
    // 上一轮存在、这一轮不再匹配的配对补发 Active=false 撤销。
    void refresh_p2p_peers();

    // 某玩家当前拥有权威、且世界快照里真实存在的火箭 id（升序）。
    // 对齐 C# P2PRocketIds：session.UpdateAuthority ∩ world.Rockets，OrderBy(id)。
    std::vector<std::int32_t> p2p_rocket_ids(std::int32_t player_id) const;

    // 下发一份 P2PPeerOffer(Active=true)：告诉收件人\"和 peer_player_id 直连，去这个地址\"。
    void send_p2p_offer(int fd, std::int32_t peer_player_id, const std::string& peer_address,
                        std::uint16_t peer_port, const std::string& pair_token,
                        const std::vector<std::int32_t>& local_rocket_ids,
                        const std::vector<std::int32_t>& peer_rocket_ids);

    // 下发 P2PPeerOffer(Active=false)：作废与 peer_player_id 的直连。
    void send_p2p_revoke(int fd, std::int32_t peer_player_id);

    // player_id -> conns_ 的 key（= KCP conv）；该玩家当前没有在线连接时返回 -1。
    int find_fd_by_player(std::int32_t player_id) const;

    // 配对键：小 id 在高 32 位、大 id 在低 32 位。同一对玩家只有一份配对状态，
    // 且与遍历顺序无关（对齐 C# 的 HashSet<(int First, int Second)> 与 _p2pPairTokens）。
    static std::uint64_t p2p_pair_key(std::int32_t a, std::int32_t b) {
        const std::uint32_t low = static_cast<std::uint32_t>(a < b ? a : b);
        const std::uint32_t high = static_cast<std::uint32_t>(a < b ? b : a);
        return (static_cast<std::uint64_t>(low) << 32) | high;
    }

    P2PSettings p2p_;
    // 加入准入配置（对齐 C# ServerSettings：password / block_duplicate_player_names /
    // max_username_length）。人数上限用 max_players_。
    JoinPolicy join_;
    // 配对键 -> pairToken（24 字节随机数的 48 位大写十六进制，与 C# 生成方式一致）。
    // 配对存续期间复用同一个 token：客户端据此判断\"还是同一条直连\"而不是重建。
    std::unordered_map<std::uint64_t, std::string> p2p_pair_tokens_;
    // 上次给这一对下发报价的时刻：报价原先只在"配对新建"时发一次，而客户端握手 10 秒超时后
    // 会永久停在 Relay Fallback（token 还在，服务端就再也不补发），除非两人拉开再靠近。
    // 允许按周期重发同一个 token 的报价 -> "打洞失败"变成"过一会儿再试一次"。
    std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> p2p_pair_last_offer_;
    std::chrono::steady_clock::time_point last_p2p_refresh_ = std::chrono::steady_clock::now();

    // ---------------------------------------------------------- 底层发送
    void send_payload(int fd, std::uint8_t packet_type, const sfs::net::NetWriter& body);

    template <typename PktT>
    void send_packet(int fd, std::uint8_t packet_type, const PktT& pkt) {
        sfs::net::NetWriter body;
        pkt.serialize(body);
        send_payload(fd, packet_type, body);
    }

    template <typename PktT>
    void broadcast_packet(std::uint8_t packet_type, const PktT& pkt, int exclude_fd) {
        sfs::net::NetWriter body;
        pkt.serialize(body);
        for (auto& [fd, c] : conns_) {
            if (fd == exclude_fd || !c.handshaked) continue;
            send_payload(fd, packet_type, body);
        }
    }
    // ---------------------------------------------------------- UDP 下发

    // UpdateRocketPrimary 是唯一走 UDP 状态通道的包（对齐 C# BroadcastLatest）：
    // 接收方已建 UDP 通道则直发 UDP，否则退回 TCP。
    void broadcast_rocket_primary(const sfs::protocol::UpdateRocketPrimaryPacket& pkt, int exclude_fd);

    // ---------------------------------------------------------- 权威分配
    // 对应 C# AuthorityAllocationPolicy.Allocate：
    //   玩家声明控制某火箭则归其所有，否则按 player_id 排序轮询分配。
    void refresh_authorities();

    // ---------------------------------------------------------- 世界快照
    // 对应 C# SendWorldSnapshot：逐枚火箭发 CreateRocket（GlobalId=火箭ID，LocalId 默认 0）
    void send_world_snapshot(int fd);
    void send_rocket_snapshot(int fd, std::int32_t rocket_id);

    // ---------------------------------------------------------- 业务包处理
    void handle_packet(int fd, const std::vector<std::uint8_t>& payload, std::int32_t payload_bits);

    // ---------------------------------------------------------- 对接 / 拆解
    // 对齐 C# TcpMultiplayerServer.HandleDockTransaction / HandleUndockTransaction。
    //
    // 为什么必须服务端算：客户端从未发送 Committed=true（见 Client PartUpdateSyncing.cs 里
    // "Multiplayer docking is committed by the server" 的注释），合并结果由服务端权威产出
    // 后盖上 Committed 广播；若改成"信任客户端算好的 merged_rocket"，任意玩家都能
    // 直接用假的 id 覆盖/删除别人的火箭。
    void handle_dock_transaction(int fd, std::int32_t player_id, std::uint8_t raw_type,
                                 sfs::protocol::DockTransactionPacket& pkt);

    void handle_undock_transaction(int fd, std::int32_t player_id, std::uint8_t raw_type,
                                   sfs::protocol::DockTransactionPacket& pkt);

    // 双人确认状态：一次对接需要两边各自的控制者都确认过才结算，防止一方擅自合并。
    // 对齐 C# _pendingDocks（Dictionary<(keep,remove,keepPart,removePart), PendingDock>）。
    struct PendingDock {
        std::int32_t keep_rocket_id = -1;
        std::int32_t remove_rocket_id = -1;
        std::int32_t keep_part_id = -1;
        std::int32_t remove_part_id = -1;
        std::vector<std::int32_t> confirmations;
        std::chrono::steady_clock::time_point created_at{};
    };
    std::map<std::array<std::int32_t, 4>, PendingDock> pending_docks_;
    // 超过这个时长就丢弃旧状态重新累计确认：与 C# 的 TimeSpan.FromSeconds(5) 一致。
    static constexpr std::chrono::seconds kPendingDockTimeoutSeconds{5};

    // 当前操控某枚火箭的玩家（C# 里是 _players.Values.FirstOrDefault(v => v.ControlledRocket == id)）；
    // 该火箭此刻无人操控返回 -1。对接/拆解要按它判断"哪些人必须点头"。
    std::int32_t controller_of(std::int32_t rocket_id) const;

    // 权限判定：拥有该火箭权威的玩家才能改它（对齐 C# CanUpdate）。
    bool has_authority(std::int32_t player_id, std::int32_t rocket_id) const;

     // ---------------------------------------------------------- 控制台命令
     // 对齐 C# TcpMultiplayerServer.ExecuteCommand / ServerCommandResult：
     // help / status / players / network / say / time / debris / world / kick / stop
    public:
     struct CommandResult {
         bool request_shutdown = false;
         std::string message;
     };

     // 执行一行控制台命令（线程安全：调用方需自身串行化）
     CommandResult execute_command(const std::string& command_line);

     // 控制台线程入口：读 stdin，逐行执行，收到 stop 则置 g_running=false
     void run_console();

    private:
     // 清除无人控制且部件数 <= max_parts 的太空垃圾（对齐 C# ClearDebris），返回清理数量
     int clear_debris(int max_parts);

     // 设置时间倍率并广播 TimeWarp(Applied)（对齐 C# SetTimeScale）
     void set_time_scale(double scale, const std::string& reason);

     bool try_kick(const std::string& target, std::string* out_message);

     std::string player_list_text();

     std::string network_list_text();
    // traffic 命令：每连接的收发包速率。速率靠"两次调用之间"的差分算，
    // 所以要连续执行两次（第一次只建立基准）。
    std::string traffic_list_text();

    public:
     // 自动清理太空垃圾（对齐 C# _autoClearDebris + DebrisControlRules）
     bool auto_clear_debris_ = false;
     static constexpr int kAutoClearDebrisMaxParts = 5;   // DebrisControlRules.DefaultAutoRemoveMaxParts

    private:
     std::chrono::steady_clock::time_point last_debris_sweep_ = std::chrono::steady_clock::now();

     std::uint16_t port_;
    std::int32_t max_players_;
    sfs::net::KcpTransport kcp_;
    std::uint64_t next_conn_id_ = 0;
    std::int32_t next_player_id_ = 0;
    std::int32_t player_count_ = 0;
    std::unordered_map<int, Connection> conns_;
    // 全 KCP：不再需要 UDP 令牌 → 连接映射
    std::uint64_t packet_count_ = 0;
    std::uint64_t bad_packets_ = 0;
    std::vector<int> pending_disconnects_;
    std::unordered_set<int> connected_fds_;   // 已计入 player_count_ 的连接
    std::uint64_t snapshot_requests_ = 0;
    std::uint64_t unknown_frames_ = 0;
    std::uint64_t udp_datagrams_ = 0;
    std::uint64_t udp_data_ = 0;
    std::uint64_t udp_binds_ = 0;
    std::uint64_t udp_bad_ = 0;
    std::uint8_t last_packet_type_ = 0xFF;
};

}  // namespace sfs::server
