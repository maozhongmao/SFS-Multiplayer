// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/server.hpp"
#include "plugins/plugin_bootstrap.hpp"  // SFS_PLUGINS=0 时展开为空
#include "server/util.hpp"
#include "util/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace sfs::server {

using sfs::log::Cat;

namespace {

std::string trim(const std::string& value) {
    const auto not_space = [](unsigned char c) { return !std::isspace(c); };
    auto begin = std::find_if(value.begin(), value.end(), not_space);
    auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
    if (begin >= end) return std::string();
    return std::string(begin, end);
}

std::vector<std::string> split_ws(const std::string& line) {
    std::vector<std::string> out;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) out.push_back(token);
    return out;
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parse_double(const std::string& text, double* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) return false;
    *out = value;
    return true;
}

bool parse_int(const std::string& text, int* out) {
    if (text.empty()) return false;
    char* end = nullptr;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || (end != nullptr && *end != '\0')) return false;
    *out = static_cast<int>(value);
    return true;
}

}  // namespace

// ---------------------------------------------------------- 控制台命令
Server::CommandResult Server::execute_command(const std::string& command_line) {
    CommandResult result;
    const std::string line = trim(command_line);
    if (line.empty()) return result;

    const auto parts = split_ws(line);
    const std::string command = to_lower(parts[0]);

    if (command == "help") {
        result.message =
            "Common: status, players, network, traffic, say <message>, time <scale|off>, "
            "debris [auto|max parts], world <save|sync>, kick <ID|name>, stop.";
    } else if (command == "status") {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "players=%d rockets=%zu worldTime=%.1f scale=%.2fx",
                      player_count_, rockets_.size(), world_time(), time_scale_);
        result.message = buffer;
    } else if (command == "players") {
        result.message = player_list_text();
    } else if (command == "network") {
        result.message = network_list_text();
    } else if (command == "traffic") {
        result.message = traffic_list_text();
    } else if (command == "say") {
        if (parts.size() < 2) {
            result.message = "usage: say <message>";
            return result;
        }
        const auto space = line.find(' ');
        const std::string text = space == std::string::npos ? std::string() : trim(line.substr(space + 1));
        if (text.empty()) {
            result.message = "usage: say <message>";
            return result;
        }
        sfs::protocol::SendChatMessagePacket packet;
        packet.sender_id = -1;
        packet.message = "[Server] " + text;
        broadcast_packet(static_cast<std::uint8_t>(sfs::protocol::PacketType::kSendChatMessage),
                         packet, -1);
        result.message = "Broadcast sent.";
    } else if (command == "time") {
        // 对齐 C#：允许 1..2500，off 表示恢复 1x
        if (parts.size() == 2 && to_lower(parts[1]) == "off") {
            set_time_scale(1.0, "server ended time warp");
            result.message = "Time scale restored to 1x.";
            return result;
        }
        double scale = 0.0;
        if (parts.size() != 2 || !parse_double(parts[1], &scale) || scale < 1.0 || scale > 2500.0) {
            result.message = "Allowed time scale: 1 to 2500";
            return result;
        }
        set_time_scale(scale, "set by server");
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "Time scale forced to %.2fx.", scale);
        result.message = buffer;
    } else if (command == "debris") {
        if (parts.size() == 2 && to_lower(parts[1]) == "auto") {
            auto_clear_debris_ = !auto_clear_debris_;
            char buffer[160];
            std::snprintf(buffer, sizeof(buffer), "debris auto is now %s, threshold %d parts.",
                          auto_clear_debris_ ? "ON" : "OFF", kAutoClearDebrisMaxParts);
            result.message = buffer;
            return result;
        }
        int max_parts = 3;
        if (parts.size() > 2 || (parts.size() == 2 && (!parse_int(parts[1], &max_parts) || max_parts < 0))) {
            result.message = "usage: debris [auto|max parts] (default 3)";
            return result;
        }
        const int removed = clear_debris(max_parts);
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer),
                      "Cleaned up %d unowned debris with at most %d parts.", removed, max_parts);
        result.message = buffer;
    } else if (command == "world") {
        if (parts.size() != 2) {
            result.message = "usage: world <save|sync>";
            return result;
        }
        const std::string action = to_lower(parts[1]);
        if (action == "save") {
            save_world_to_disk();
            result.message = "World state saved.";
        } else if (action == "sync") {
            for (auto& [fd, c] : conns_) {
                if (c.handshaked) send_world_snapshot(fd);
            }
            result.message = "World snapshot sent to all players.";
        } else {
            result.message = "usage: world <save|sync>";
        }
    } else if (command == "kick") {
        if (parts.size() != 2) {
            result.message = "usage: kick <player ID|name>";
            return result;
        }
        std::string message;
        try_kick(parts[1], &message);
        result.message = message;
    } else if (command == "stop") {
        result.request_shutdown = true;
        result.message = "Saving and stopping the server safely.";
    } else {
#if defined(SFS_PLUGINS) && SFS_PLUGINS
        // 未知命令交给插件系统（插件可以新增命令）。
        if (sfs::plugins::submit_console_command(command.c_str())) {
            result.message = "";
            return result;
        }
#endif
        result.message = "Unknown command. Type help for the command list.";
    }

    return result;
}

// 控制台线程：读 stdin 逐行执行；stop 或 EOF 时置 g_running=false
void Server::run_console() {
    std::string line;
    while (g_running && std::getline(std::cin, line)) {
        const CommandResult result = execute_command(line);
        if (!result.message.empty()) {
            SFS_LOGI(Cat::Config, "[cmd] %s", result.message.c_str());
        }
        if (result.request_shutdown) {
            g_running = false;
            break;
        }
    }
}

// 对齐 C# ClearDebris：清除无人控制且部件数 <= max_parts 的火箭
int Server::clear_debris(int max_parts) {
    std::vector<std::int32_t> controlled;
    controlled.reserve(controlled_rocket_.size());
    for (const auto& [pid, rid] : controlled_rocket_) {
        (void)pid;
        if (rid >= 0) controlled.push_back(rid);
    }

    std::vector<std::int32_t> victims;
    for (const auto& [id, entry] : rockets_) {
        if (std::find(controlled.begin(), controlled.end(), id) != controlled.end()) continue;
        // 冷火箭的部件表常驻 blob，state.parts 是空的：用 parts_count()（只数、不解压）
        // 而不是 parts_begin()/parts_end() 来判残骸。后者要解包再回包，既慢，又有致命副作用 ——
        // 解包失败时 ensure_parts() 会清空 state.parts，紧接着 commit_parts() 就把这份空表
        // 写回 blob，一枚活火箭会当场变成 0 部件。只读计数不碰状态，没有这个风险。
        if (static_cast<int>(entry.parts_count()) <= max_parts) victims.push_back(id);
    }

    for (const auto id : victims) {
        rockets_.erase(id);
        sfs::protocol::DestroyRocketPacket packet;
        packet.world_time = world_time();
        packet.rocket_id = id;
        packet.reason = 0;
        broadcast_packet(static_cast<std::uint8_t>(sfs::protocol::PacketType::kDestroyRocket),
                         packet, -1);
    }
    if (!victims.empty()) refresh_authorities();
    return static_cast<int>(victims.size());
}

// 清理"没有人操控的火箭"：只保留此刻有玩家操控的、以及刚发射还在宽限期的。
// 为什么需要：无主火箭会持续被引擎计算碰撞箱，而客户端模组又忽略火箭之间的碰撞对，
// 实测形成"算了又不算/算了又算"的循环把主线程拖死（清空世界即恢复）。发射台依旧不参与碰撞。
int Server::clear_uncontrolled_rockets(std::chrono::seconds grace) {
    std::vector<std::int32_t> controlled;
    for (const auto& [pid, rid] : controlled_rocket_) {
        if (pid < 0 || rid < 0) continue;
        if (players_.find(pid) == players_.end()) continue;   // 玩家已离线，其旧记录不算数
        controlled.push_back(rid);
    }

    const auto now = std::chrono::steady_clock::now();
    std::vector<std::int32_t> victims;
    for (const auto& [id, entry] : rockets_) {
        if (std::find(controlled.begin(), controlled.end(), id) != controlled.end()) continue;
        if (grace.count() > 0 && now - entry.created_at < grace) continue;   // 默认 0 = 不宽限
        // 关键：还在被客户端更新的火箭 = 有人在模拟它（哪怕此刻没操控）—— 比如对方那枚正在飞的。
        // 早期版本只按"有没有操控"判定，导致"我上着自己的火箭，对方那枚自己消失了"。
        if (now - entry.last_update < kRocketKeepAliveSeconds) continue;
        victims.push_back(id);
    }

    for (const auto id : victims) {
        rockets_.erase(id);
        created_local_ids_.erase(static_cast<std::uint64_t>(id));
        sfs::protocol::DestroyRocketPacket packet;
        packet.world_time = world_time();
        packet.rocket_id = id;
        packet.reason = 0;
        broadcast_packet(static_cast<std::uint8_t>(sfs::protocol::PacketType::kDestroyRocket),
                         packet, -1);
    }
    if (!victims.empty()) {
        refresh_authorities();
        SFS_LOGI(Cat::World, "[cpp-server] debris cleanup: removed %d (kept %d)",
                 static_cast<int>(victims.size()), static_cast<int>(rockets_.size()));
    }
    return static_cast<int>(victims.size());
}

// 对齐 C# SetTimeScale：先把已流逝时间按旧倍率结算进基准，再换倍率并广播
void Server::set_time_scale(double scale, const std::string& reason) {
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = std::chrono::duration<double>(now - world_clock_origin_).count();
    world_time_base_ += elapsed * time_scale_;
    world_clock_origin_ = now;
    time_scale_ = scale;

    sfs::protocol::TimeWarpPacket packet;
    packet.operation = sfs::protocol::TimeWarpOperation::kApplied;
    packet.multiplier = scale;
    packet.world_time = world_time();
    packet.approved = true;
    packet.message = reason;
    broadcast_packet(static_cast<std::uint8_t>(sfs::protocol::PacketType::kTimeWarp), packet, -1);
}

// 对齐 C# kick：按 ID 或名字找玩家，断开连接并广播离开事件
bool Server::try_kick(const std::string& target, std::string* out_message) {
    int by_id = -1;
    int parsed = 0;
    const bool numeric = parse_int(target, &parsed);
    if (numeric) by_id = parsed;

    int victim_fd = -1;
    std::int32_t victim_pid = -1;
    std::string victim_name;
    for (auto& [fd, c] : conns_) {
        if (!c.handshaked || c.player_id < 0) continue;
        const bool id_match = numeric && c.player_id == by_id;
        bool name_match = false;
        if (!numeric) {
            name_match = to_lower(c.username) == to_lower(target);
        }
        if (id_match || name_match) {
            victim_fd = fd;
            victim_pid = c.player_id;
            victim_name = c.username;
            break;
        }
    }

    if (victim_fd < 0) {
        if (out_message != nullptr) *out_message = "Player not found.";
        return false;
    }

    // 先发 Disconnect 让客户端知道是被踢，再走正常关闭路径（会广播 PlayerDisconnected）
    send_control(victim_fd, sfs::protocol::TcpFrameKind::Disconnect);
    close_conn(victim_fd, "kicked");
    if (out_message != nullptr) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "Kicked %s (ID %d).",
                      victim_name.c_str(), victim_pid);
        *out_message = buffer;
    }
    return true;
}

std::string Server::player_list_text() {
    std::string out;
    for (auto& [fd, c] : conns_) {
        (void)fd;
        if (!c.handshaked || c.player_id < 0) continue;
        const auto rit = controlled_rocket_.find(c.player_id);
        const std::int32_t controlled = rit == controlled_rocket_.end() ? -1 : rit->second;
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%d: %s control=%d RTT=%.2fms",
                      c.player_id, c.username.c_str(), controlled, c.rtt_ms);
        if (!out.empty()) out += "\n";
        out += buffer;
    }
    if (out.empty()) return "No players online.";
    return out;
}

std::string Server::network_list_text() {
    std::string out = "Player\tPing";
    bool any = false;
    for (auto& [fd, c] : conns_) {
        (void)fd;
        if (!c.handshaked || c.player_id < 0) continue;
        any = true;
        char buffer[256];
        if (c.rtt_ms <= 0.0) {
            std::snprintf(buffer, sizeof(buffer), "\n%s\t--", c.username.c_str());
        } else {
            std::snprintf(buffer, sizeof(buffer), "\n%s\t%.2fms", c.username.c_str(), c.rtt_ms);
        }
        out += buffer;
    }
    if (!any) return "No players online.";
    return out;
}

// 每连接的收发包速率。速率 = 本次与上次调用之间的增量 / 窗口时长；
// 所以第一次调用只建立基准（并给出累计值），第二次起才是速率。
// 用途：调更新频率/查卡顿时，先看"到底谁在发多少"，而不是靠猜。
std::string Server::traffic_list_text() {
    const auto now = std::chrono::steady_clock::now();
    const bool have_prev = last_traffic_at_.time_since_epoch().count() != 0;
    const double window_s = have_prev
        ? std::chrono::duration<double>(now - last_traffic_at_).count()
        : 0.0;
    const bool rate_mode = have_prev && window_s > 0.05;

    std::string out;
    std::uint64_t delta_in = 0;
    std::uint64_t delta_out = 0;

    for (auto& [fd, c] : conns_) {
        (void)fd;
        if (!c.handshaked || c.player_id < 0) continue;

        auto prev = last_traffic_counts_.find(c.player_id);
        const std::uint64_t pin =
            (rate_mode && prev != last_traffic_counts_.end()) ? prev->second.first : c.packets_in;
        const std::uint64_t pout =
            (rate_mode && prev != last_traffic_counts_.end()) ? prev->second.second : c.packets_out;
        const std::uint64_t din = c.packets_in >= pin ? c.packets_in - pin : 0;
        const std::uint64_t dout = c.packets_out >= pout ? c.packets_out - pout : 0;
        delta_in += din;
        delta_out += dout;

        char buffer[320];
        if (rate_mode) {
            std::snprintf(buffer, sizeof(buffer),
                          "%s  in=%.1f/s out=%.1f/s total=%llu/%llu RTT=%.1fms",
                          c.username.c_str(), static_cast<double>(din) / window_s,
                          static_cast<double>(dout) / window_s,
                          static_cast<unsigned long long>(c.packets_in),
                          static_cast<unsigned long long>(c.packets_out), c.rtt_ms);
        } else {
            std::snprintf(buffer, sizeof(buffer),
                          "%s  in=%llu out=%llu RTT=%.1fms",
                          c.username.c_str(),
                          static_cast<unsigned long long>(c.packets_in),
                          static_cast<unsigned long long>(c.packets_out), c.rtt_ms);
        }
        if (!out.empty()) out += "\n";
        out += buffer;
        last_traffic_counts_[c.player_id] = {c.packets_in, c.packets_out};
    }
    last_traffic_at_ = now;

    if (out.empty()) {
        last_traffic_counts_.clear();
        return "No players online.";
    }

    char head[200];
    if (rate_mode) {
        std::snprintf(head, sizeof(head), "window %.1fs  total in=%.1f/s out=%.1f/s\n", window_s,
                      static_cast<double>(delta_in) / window_s,
                      static_cast<double>(delta_out) / window_s);
    } else {
        std::snprintf(head, sizeof(head), "baseline taken (run traffic again for rates)\n");
    }
    return std::string(head) + out;
}

}  // namespace sfs::server
