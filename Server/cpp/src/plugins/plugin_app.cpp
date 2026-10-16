// 插件系统的服务端落地：定义 HostServices 的真实实现 + 启动时装载插件。
// 只在 SFS_PLUGINS=1 时参与编译（默认关闭时整个文件为空 TU ⇒ 发布产物不变）。

#if defined(SFS_PLUGINS) && SFS_PLUGINS

#include "plugins/plugin_bootstrap.hpp"
#include "plugins/plugin_host.hpp"
#include "plugins/quickjs_engine.hpp"

#include "server/server.hpp"
#include "protocol/packets.hpp"
#include "util/log.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace sfs::plugins {

namespace {

// 把宿主 API 落到真实服务端：插件只能用这四个出口。
class ServerServices final : public HostServices {
public:
    explicit ServerServices(sfs::server::Server* server) : server_(server) {}

    bool read_rocket(std::int32_t, RocketView*) override { return false; }   // 暂不开放
    bool write_rocket(const RocketView&) override { return false; }          // 暂不开放

    bool send_message(std::int32_t player_id, const char* utf8_text) override {
        if (server_ == nullptr || utf8_text == nullptr) return false;
        return server_->plugin_send_chat(player_id, utf8_text);
    }

    // 通用调用通道：op 白名单（能力位已在宿主侧检查过）。
    std::int32_t call(const char* op, const char* arg, char* out, std::int32_t out_cap) override {
        if (op == nullptr || server_ == nullptr) return -2;
        if (std::strcmp(op, "kick") == 0) {
            if (arg == nullptr) return -2;
            const std::string spec(arg);
            const std::size_t bar = spec.find('|');
            const int pid = std::atoi(spec.substr(0, bar).c_str());
            const std::string reason = (bar == std::string::npos) ? "" : spec.substr(bar + 1);
            return server_->plugin_kick(pid, reason.c_str()) ? 0 : -3;
        }
        if (std::strcmp(op, "clearDebris") == 0) {
            return server_->plugin_clear_debris(arg ? std::atoi(arg) : 6);
        }
        if (std::strcmp(op, "worldTime") == 0) {
            return server_->plugin_world_time(out, out_cap);
        }
        if (std::strcmp(op, "setTimeScale") == 0) {
            return server_->plugin_set_time_scale(arg ? std::atof(arg) : 1.0) ? 0 : -2;
        }
        if (std::strcmp(op, "serverInfo") == 0) {
            return server_->plugin_server_info(out, out_cap);
        }
        return -2;
    }

    void log(const char* ascii_text) override {
        if (ascii_text != nullptr) std::printf("[plugin] %s\n", ascii_text); std::fflush(stdout);
    }

private:
    sfs::server::Server* server_ = nullptr;
};

std::vector<std::uint8_t> read_file(const char* path, bool* ok) {
    std::vector<std::uint8_t> data;
    *ok = false;
    FILE* f = std::fopen(path, "rb");
    if (f == nullptr) return data;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size > 0 && size <= 64 * 1024) {
        data.resize(static_cast<std::size_t>(size));
        *ok = (std::fread(data.data(), 1, data.size(), f) == data.size());
    }
    std::fclose(f);
    return data;
}

ServerServices* g_app_services = nullptr;

}  // namespace

// 启动：注册服务、装引擎工厂、装载插件。返回装载成功的插件数。
int start(const char* plugin_path, sfs::server::Server* server) {
    if (!runtime_enabled()) {
        std::printf("[plugin] disabled at runtime (SFS_PLUGINS=0)"); std::fflush(stdout);
        return 0;
    }
    g_app_services = new ServerServices(server);
    install_services(g_app_services);
    install_engine_factory(new QuickJsEngineFactory());

    PluginHost* host = global_host();
    if (host == nullptr) return 0;

    bool ok = false;
    std::vector<std::uint8_t> bytecode = read_file(plugin_path, &ok);
    if (!ok || bytecode.empty()) {
        std::printf("[plugin] cannot read bytecode: %s\n", plugin_path); std::fflush(stdout);
        return 0;
    }

    PluginDescriptor desc;
    desc.id = "test-broadcast";
    // 目前由服务端写死（示例插件用满：日志 + 消息 + 查询 + 管理）。
    // 待改进：应当由插件自带描述符文件声明能力位，而不是服务端硬编码。
    desc.capabilities = kCapSendMessage | kCapLog | kCapAdmin | kCapReadRocket | kCapWriteRocket;
    desc.limits.memory_limit_bytes = 1u << 20;       // 1 MB
    desc.limits.max_stack_bytes = 64u * 1024;        // 64 KB
    desc.limits.instruction_budget = 200000;         // 每轮 20 万条（与文档默认一致）
    desc.limits.max_api_calls = 256;
    desc.bytecode = bytecode;

    std::string error;
    if (!host->load(desc, &error)) {
        std::fprintf(stderr, "[plugin] load failed: %s\n", error.c_str()); std::fflush(stderr);
        return 0;
    }
    if (!host->subscribe(desc.id, kEventAll)) {
        std::fprintf(stderr, "[plugin] subscribe failed"); std::fflush(stderr);
        return 0;
    }
    std::printf("[plugin] loaded id=%s bytes=%u capabilities=0x%X",
             desc.id.c_str(), static_cast<unsigned>(bytecode.size()), desc.capabilities); std::fflush(stdout);
    return 1;
}

}  // namespace sfs::plugins

// ---- 以下都是 Server 的公开包装：把插件请求落到真实服务端 ----
namespace sfs::server {

bool Server::plugin_kick(std::int32_t player_id, const char* reason) {
    if (player_id < 0) return false;
    (void)reason;   // 现成的 try_kick 不带原因参数；要带原因需改它（后续）
    std::string message;
    return try_kick(std::to_string(player_id), &message);   // try_kick 支持按数字 id 找人
}

int Server::plugin_clear_debris(int max_parts) { return clear_debris(max_parts); }

int Server::plugin_world_time(char* out, std::int32_t out_cap) {
    if (out == nullptr || out_cap <= 0) return -2;
    const double t = world_time();
    std::snprintf(out, static_cast<std::size_t>(out_cap), "%.3f", t);
    return 0;
}

bool Server::plugin_set_time_scale(double scale) {
    if (!(scale > 0.0) || scale > 1000.0) return false;   // 拒绝 NaN/Inf/离谱值
    set_time_scale(scale, "plugin");
    return true;
}

int Server::plugin_server_info(char* out, std::int32_t out_cap) {
    if (out == nullptr || out_cap <= 0) return -2;
    std::int32_t players = 0;
    for (auto& entry : conns_) if (entry.second.handshaked) ++players;
    std::snprintf(out, static_cast<std::size_t>(out_cap), "players=%d", players);
    return 0;
}

}  // namespace sfs::server

// Server::plugin_send_chat 的实现（声明在 server.hpp，受同一个宏保护）。
// 语义：一次调用 = 给全体玩家广播（用服务端现成的 chat 广播通道，客户端有聊天窗显示）。
namespace sfs::server {
bool Server::plugin_send_chat(std::int32_t player_id, const char* utf8_text) {
    if (utf8_text == nullptr || utf8_text[0] == '\0') return false;
    sfs::protocol::SendChatMessagePacket pkt;
    pkt.sender_id = -1;
    pkt.message = utf8_text;
    const auto type = static_cast<std::uint8_t>(sfs::protocol::PacketType::kSendChatMessage);
    if (player_id < 0) {   // 全体广播
        broadcast_packet(type, pkt, -1);
        return true;
    }
    // 私聊：按 player_id 找到那条连接，只发给它（照 broadcast_packet 的写法）。
    sfs::net::NetWriter body;
    pkt.serialize(body);
    for (auto& entry : conns_) {
        if (!entry.second.handshaked) continue;
        if (entry.second.player_id != player_id) continue;
        send_payload(entry.first, type, body);
        return true;
    }
    return false;   // 该玩家不存在
}
}  // namespace sfs::server

#endif  // SFS_PLUGINS