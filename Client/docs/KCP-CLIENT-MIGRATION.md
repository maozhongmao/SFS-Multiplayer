// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

# 客户端 KCP 化集成方案（V1.2.1）

服务端侧已完成（见 `ServerCpp/docs/ADR-001-kcp-transport.md` 的「实施记录」），
本文说明客户端需要怎么改、按什么顺序、以及每一步的验收标准。

## 当前状态

| 端 | 传输 | 状态 |
|---|---|---|
| 服务端 C++ | 单 UDP 端口 + KCP 会话 | ✅ 完成（Linux ARM 9.1 MB / Windows 1.1 MB，两端实测 PASS） |
| 服务端 C# | TCP + 裸 UDP | ❌ 已废弃，不再维护（最后一个版本已开源） |
| **客户端 C#** | **TCP + 裸 UDP 双通道** | ❌ **待改造（本文范围）** |

## 需要覆盖的三条链路

客户端有**三条**独立的数据路径，KCP 化时必须分别处理：

| 链路 | 现状 | 改造后 |
|---|---|---|
| 客户端 → 服务器（控制流） | TCP（`TcpTransportCore`） | **KCP**（会话随握手建立） |
| 客户端 → 服务器（状态流） | 裸 UDP（`UdpClientTransport`，token 绑定） | **并入同一条 KCP 会话**（不再需要 UDP token / Bind） |
| 客户端 ↔ 客户端（P2P 直连） | 裸 UDP（`P2PConnectionManager`，Hello/Ack/State） | **KCP**（每个 peer 一条会话） |

> 注意：P2P 那条与服务端撮合是**两套东西**（详见 `ServerCpp/docs/ADR-002-easytier-networking.md`），
> 它与 EasyTier 组网无关。三条链路都跑本项目自己的协议，共用同一套 C# KCP。

## 实施步骤

### 步骤 1：拆分通信文件（进行中）

`TcpTransportCore.cs`（680 行）内含四个独立的类，拆到 `MultiplayerSFS.Common/Transport/`：

```
Transport/
├── TcpFrame.cs          — TcpFrame 结构 + 帧编解码
├── NetPayloadCodec.cs   — Lidgren 序列化辅助
├── TcpSendQueue.cs      — critical/latest 发送队列
├── UdpTransport.cs      — 原 UdpTransport 拆分
└── TcpTransportCore.cs  — 连接核心（拆分后应显著变小）
```

**要求**：零语义变化，每文件 < 400 行。
**验收**：原调用点不需要修改就能编译（命名空间兼容）。

### 步骤 2：引入纯安全 C# 的 KCP（进行中）

**不能用的方案**（已排除）：

- `new1271/KcpSharpN`（MIT）：虽许可干净，但核心是裸指针 + `delegate* unmanaged[Cdecl]`
  函数指针 + Fody/InlineIL 织入，Unity IL2CPP 下风险高，且与项目已有的 Costura.Fody 冲突。
- `Molth/Kcp-CSharp`、`Cysharp/KcpTransport`：许可为 NOASSERTION（不明），不可用。

**采用方案**：对照我们已有源码的 `ServerCpp/src/third_party/kcp/ikcp.c`，
用**纯安全 C#**（无 unsafe / 无指针 / 无 IL 织入）独立重写，保证 Unity IL2CPP 可编译。

**验收**：本地两个 KcpContext 互通（含 64 KB 跨 MTU 大消息、丢包重传）。

### 步骤 3：写 `KcpTransportCore`，替换 TCP 连接层

对外接口与 `TcpTransportCore` **保持一致**，这样 `ClientManager` 等上层调用点改动最小：

```
Start(host, port) / Stop()
SendPacket(Packet) / TrySendPacket(Packet)
ReceiveQueue（帧队列）
统计属性（SentFrames / ReceivedFrames / SentBytes / ReceivedBytes / LastDisconnectReason …）
```

内部实现换成：一条 UDP socket + 一个 KcpContext + `FrameCodec` 解帧。

**关键差异**：KCP 是**面向流**的，`ikcp_send` 超过 MTU 会分片、接收端一次 `ikcp_recv`
只拿到一个分片。与 C++ 侧一致，**在 KCP 之上用 4 字节小端长度前缀恢复消息边界**
（C++ 侧已在 `KcpTransport` 实现，C# 侧照做）。

### 步骤 4：UDP 状态通道合并

原 `UdpClientTransport` 做三件事，改造后分别处置：

| 原职责 | 处置 |
|---|---|
| UDP 绑定（token + Bind/BindAck + 1 秒心跳重试） | **删除** —— KCP 会话本身就是绑定 |
| 位置流（`UpdateRocketPrimary` 走 UDP 直发） | **改为走 KCP 帧**（与服务端一致） |
| P2P 原始数据报收发（`TrySendPeer` / `TryReceivePeer`） | **保留**，但要改成走各 peer 的 KCP 会话 |

### 步骤 5：P2P 链路 KCP 化

`P2PConnectionManager` 现有自定义握手（Hello/Ack，三次确认）+ 状态包 + Relay 回退。
改造后每个 peer 一条 KCP 会话，握手与状态都走该会话。

**注意**：`P2PPolicies` 里的分组、邻近判定、超时、过渡缓冲等策略**保持不动** ——
它们管的是「谁和谁直连」，与传输无关。

### 步骤 6：协议版本号提升

`TcpFrameCodec.ProtocolVersion` 从 **32 → 33**。

**必须两端同时提升**：C++ 侧 `sfs::protocol::kProtocolVersion` 目前仍是 32
（`ServerCpp/src/protocol/codec.hpp`）。线格式已经真正不兼容（TCP → KCP），
按项目惯例（只有线格式真正不兼容才提升传输协议号）应当提升。

**旧客户端的拒绝路径**：新服务端遇到握手版本不匹配时，必须回一个带可读原因的
Disconnect（而不是静默断开），否则老客户端玩家只会看到「连不上」而不知道为什么。

## 测试计划

| 级别 | 内容 | 通过标准 |
|---|---|---|
| 单元 | C# KCP 互通（含大消息 / 丢包重传） | 全部通过 |
| 联调 A | Windows 服务端 + Windows 客户端 | 能握手、能进世界、位置流正常 |
| 联调 B | 板子服务端（Linux ARM）+ PC 客户端 | 同上，且延迟与 1.1.4 相当或更好 |
| 回归 | 旧客户端（1.1.4）连新服务端 | 收到「版本不匹配」的可读提示，不崩溃 |
| 压力 | 世界内物体数 + 多玩家 | 观察 `bad=0`、内存与 CPU |

## 环境约定

- **9806 = 生产环境**（正常在跑的服务端），**9807 = 测试环境**。改动先在 9807 验证。
- 板子（Luckfox Lume，88 MB RAM）资源紧张：测试优先在 Windows 做，
  大存档（3.9 MB 级）在板子上会把进程撑到 44 MB 并触发 OOM，需注意。
