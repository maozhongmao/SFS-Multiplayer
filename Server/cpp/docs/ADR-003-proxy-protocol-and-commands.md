// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

# ADR-003：Proxy Protocol（UDP 仅 V2）与更好的命令系统

- **状态**：待实施（待办）
- **优先级**：中（在客户端 KCP 化完成后）
- **影响范围**：服务端 C++（`ServerCpp`）、客户端 C#（配合）

---

## 1. Proxy Protocol（仅 UDP V2）

### 背景与适用场景

**Proxy Protocol 是给「负载均衡器 / 反向代理 / 内网穿透后端」用的**，不是公网直连场景。

典型部署拓扑：

```
客户端 ──公网──→ 负载均衡器（AWS NLB / 阿里云 SLB / Nginx / HAProxy / 内网穿透服务）
                                    │
                                    ↓ 附加 Proxy Protocol V2 头（含真实客户端 IP/端口）
                          服务端（KCP 单 UDP 端口）
```

服务端部署在**私有网络 / 容器 / 虚拟机**后面，前端有 LB/反代/穿透服务时，LB 会把客户端真实 IP/端口通过 Proxy Protocol V2 二进制头透传给后端。服务端需解析该头，还原真实客户端地址，用于：

- 访问控制（IP 白名单/黑名单）
- 日志审计（记录真实客户端 IP）
- 地理位置/延迟估算
- 防刷/限流（按真实 IP 限流）

### 设计约束

| 项目 | 要求 |
|------|------|
| **版本** | **仅 V2（二进制）**，不支持 V1（文本） |
| **适用层** | KCP 之下、应用层之上 —— KCP 会话建立后，第一个包若为 Proxy Protocol V2 头则解析真实 IP 替换会话 peer |
| **触发条件** | 仅当 `server.yml` 配置 `proxy_protocol: true` 时启用 |
| **头格式** | V2 二进制头（16 字节固定 + 可变长 TLV），详见 HAProxy 规范 |
| **地址族** | 仅 IPv4（当前 SFS 只支持 IPv4） |
| **协议命令** | `PROXY` (0x00) —— 标准代理；不支持 `LOCAL` |

### 解析流程

```
KCP 会话建立
    ↓
收到第一个 UDP 数据包
    ↓
检查前 16 字节是否为 Proxy Protocol V2 魔数 (0x0D 0x0A 0x0D 0x0A 0x00 0x0D 0x0A 0x0A ...)
    ↓
是 → 解析 src/dst IP+端口 → 替换 KcpTransport::Session::peer
    ↓
去掉 Proxy Protocol 头 → 剩余 payload 交给 KCP input
    ↓
否 → 直接交给 KCP input
```

### 配置示例

```yaml
# server.yml
proxy_protocol: true
# 可选：信任的 LB CIDR 列表（防伪造），默认全信任
# proxy_trusted_cidrs:
#   - "10.0.0.0/8"
#   - "172.16.0.0/12"
#   - "192.168.0.0/16"
```

---

## 2. 更好的命令系统

### 现状痛点

- 现有 `help/status/players/network/say/time/debris/world/kick/reload/stop` 分散在 `ExecuteCommand` 里
- 无子命令、无参数类型解析、无 Tab 补全、无权限分级
- 扩展新命令要改核心代码，耦合高

### 目标设计

| 特性 | 实现 |
|------|------|
| **命令注册表** | `ICommand` 接口 + 依赖注入容器式注册，插件可动态注册 |
| **子命令** | `world save` / `world load <file>` / `player kick <name> [reason]` |
| **参数解析** | 类型安全（int/string/bool/enum/TimeSpan/IP），自动生成用法提示 |
| **Tab 补全** | 在交互式控制台下按 Tab 列出可用子命令/参数/在线玩家名 |
| **权限分级** | `Admin` / `Moderator` / `Player` 三级，命令声明 `[Permission(Admin)]` |
| **帮助生成** | `help` 自动从命令元数据生成，支持 `help <command>` |
| **历史记录** | 方向键上下翻阅，`Ctrl+R` 搜索 |

### 接口草案

```csharp
// Server/cpp/src/server/command.hpp
namespace sfs::server {

struct CommandContext {
    int player_id;           // -1 = console
    std::string raw_input;
    std::vector<std::string> args;  // 已分词
};

class ICommand {
public:
    virtual ~ICommand() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::string usage() const = 0;        // 如 "player kick <name> [reason]"
    virtual int min_permission() const = 0;       // 0=Player, 1=Moderator, 2=Admin
    virtual std::vector<std::string> completions(const CommandContext& ctx) = 0;
    virtual bool execute(CommandContext& ctx, std::string& out_error) = 0;
};

class CommandRegistry {
public:
    void register_command(std::unique_ptr<ICommand> cmd);
    bool execute(int player_id, std::string_view line, std::string& out_result);
    std::vector<std::string> complete(const std::string& prefix);
};

}
```

### 配置示例

```yaml
# server.yml
commands:
  enabled: true
  history_size: 1000
  # 权限映射（玩家名 -> 级别）
  permissions:
    "admin": 2
    "moderator1": 1
```

---

## 3. 实施顺序

| 顺序 | 任务 | 依赖 |
|------|------|------|
| 1 | Proxy Protocol V2 解析层（C++） | KCP 已完成 |
| 2 | `server.yml` 增加 `proxy_protocol` / `commands` 配置段 | 配置系统 |
| 3 | 命令系统框架（注册表、上下文、基类） | 无 |
| 4 | 内置命令迁移（status/players/kick/world/say/...）| 框架就绪 |
| 4 | 交互式控制台（readline 兼容、Tab 补全、历史）| 框架就绪 |
| 5 | 权限系统对接现有玩家体系 | 玩家管理 |

---

## 4. 验收标准

| 项目 | 通过标准 |
|------|----------|
| Proxy Protocol | 启用后，LB 后端收到的第一个包解析出的 peer IP 与真实客户端一致；未启用时行为不变 |
| 命令系统 | `help` 列出所有命令；`help world` 显示子命令；`player kick <Tab>` 补全在线玩家名 |
| 权限 | 普通玩家输入 `kick` 提示 "Permission denied"；管理员可执行 |
| 补全 | 在交互控制台按 Tab 列出可用命令/子命令/在线玩家 |
| 历史 | 方向键上下翻阅，`Ctrl+R` 搜索 |

---

## 5. 备注

- **不改动**现有网络协议帧格式（Proxy Protocol 仅在第一个包前置，之后正常 KCP）
- 命令系统**仅影响服务端控制台**，不影响客户端协议
- 两项均在 `ServerCpp` 侧实现，客户端无需改动