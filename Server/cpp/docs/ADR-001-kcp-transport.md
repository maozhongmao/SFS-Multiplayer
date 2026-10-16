// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

# ADR-001：传输层统一采用 KCP

- **状态**：已决定，未实施（2026-09-12）
- **影响范围**：服务端（C++、C# 两套实现）、客户端 mod（C#/Unity）、协议文档
- **取代**：当前的"TCP 控制 + 裸 UDP 状态流"双协议架构

## 决策

传输层**全部换成 KCP**，不再保留 TCP + UDP 双协议架构。
所有数据走**单一 UDP 端口**，由 KCP 负责可靠传输。

## 理由（按决策时的实际权衡）

1. **裸 UDP 不可靠**：位置流现在靠"发新包覆盖旧包"来掩盖丢包，一旦丢包率上来，
   远端看到的就是位置跳变和抖动，没有任何补救手段。
2. **TCP 太慢**：TCP 的队头阻塞 + 重传等超时（RTO 通常 200ms 起），
   在弱网下表现为几百毫秒的卡顿。而当前架构里，UDP 打不通时正好会回退到 TCP，
   等于在最坏的链路上用了最坏的协议。
3. **KCP 基于 UDP**：保留 UDP 的"不被队头阻塞卡死"特性，同时有 ARQ 重传、
   快速重传和选择性重传。弱网下明显优于 TCP。
4. **代码可读性（重要）**：一套传输协议，后来人只需要理解一个协议。
   双架构意味着每个改动都要在两条链路上各验证一遍，维护成本翻倍。

## KCP 参数（关键，直接决定性能上限）

**KCP 的默认配置是慢的** —— 默认 `interval = 100ms`，比当前生效的 50ms 还慢一倍。
"KCP 很快"这个印象来自下面这组参数，必须显式配置：

```c
ikcp_nodelay(kcp, nodelay, interval, resend, nc);
```

| 参数 | 含义 | 默认 | 本项目建议 |
|---|---|---|---|
| `nodelay` | 是否启用 nodelay 模式（不延迟发送） | 0（禁用） | **1** |
| `interval` | 协议内部工作间隔（ms） | **100** | **10 ~ 20** |
| `resend` | 快速重传：2 = 2 次 ACK 跨越即直接重传 | 0（关闭） | **2** |
| `nc` | 是否关闭拥塞控制 | 0（不关） | **1**（关闭） |

官方 README 给出的"最快档"是 `ikcp_nodelay(kcp, 1, 20, 2, 1)`；
若要更激进可取 `interval = 10`。**参数取值必须客户端与服务端一致**，
否则行为不可预期。

## 两端实现选型

| 端 | 实现 | 说明 |
|---|---|---|
| **服务端（C++）** | `skywind3000/kcp` 的 `ikcp.c` + `ikcp.h` | 核心只有约 1500 行 C，**无依赖，可直接编进现有静态链接的单文件服务端**，与当前 `sfs_server_full_arm` 的构建方式一致 |
| **客户端（C#/Unity）** | `Molth/Kcp-CSharp`（纯 C#，标注支持 Unity/Godot/.NET） | 备选 `Cysharp/KcpTransport`（纯 C# RUDP）；两者都是纯托管实现，无需 native 插件 |

选型要点：客户端侧**不要引入 native 插件**（Unity 跨平台 + IL2CPP 下 native 插件是额外的部署负担）。

## 单一 UDP 端口带来的附带收益

- **NAT 映射只需打一个端口**：当前 TCP + UDP 双端口在 NAT 上要维护两条映射，
  任何一条失败就会触发回退逻辑。
- **与 easytier 组网（ADR-002）配合更好**：异地组网的虚拟网卡只需放行一个端口。
- 协议识别靠 KCP 的 `conv`（会话 ID）+ 首个应用层包，不需要靠端口号区分。

## 迁移约束

- **协议版本号**：线格式整体更换，`ProtocolVersion`（当前 32）必须提升。
- **旧客户端兼容**：项目一贯要求"保持旧版协议兼容、用当前实际安装的旧客户端交叉验证"。
  传输层更换属于**线格式真正不兼容**的情形，因此允许提升协议号，
  但仍需用旧客户端二进制连接新服务端，确认**拒绝路径**给出可读的版本不匹配提示，
  而不是静默失败或崩溃。
- **端口**：保持单端口，端口号不变（当前 9806）。

## 已知代价（需要接受，不是反对意见）

- **位置流会重传**：全部走 KCP 可靠模式后，位置更新在丢包时会被重传，
  重传的是"已经过时的位置"。在丢包严重的链路上，这会带来额外的缓冲延迟。
  这是"可靠性 + 架构简单"的自觉代价 —— KCP 的快速重传（`resend=2`）仍显著优于 TCP 的等超时。
- **可选的后续调优（不改变单架构）**：KCP 允许在**同一条 UDP 连接上按 `conv` 区分多个会话**，
  因此将来若确实需要，可以给位置流单独一个 `conv` 并配不同的 `nodelay` 参数。
  这仍然是一套协议、一个端口，不构成双架构，**当前不做，记录备查**。

## 实施记录

- **`ikcp_create()` 之后必须调用 `ikcp_setoutput(kcp, output)`。** 该字段不设置时为 NULL，
  `ikcp_flush()` 会调用空函数指针导致**段错误**（本项目实测踩到）。官方头文件仅以注释形式
  提示 `kcp->output = my_udp_output`，容易遗漏。
- 发送回调的签名是 `int (*)(const char* buf, int len, ikcpcb* kcp, void* user)` —— 返回 `int` 而非 `void`。
- **KCP 是面向流的**：`ikcp_send` 超过 MTU 会分片，接收端一次 `ikcp_recv` 只拿到一个分片。
  应用层消息边界必须自己维护。`sfs::net::KcpTransport` 用 4 字节小端长度前缀解决，
  并以 64 KB 消息在 ARMv7 目标设备上验证通过。
- 实现落在 `Server/cpp/src/net/kcp_transport.{hpp,cpp}`（单 UDP 端口 + 按 conv 区分会话 +
  长度前缀重组），KCP 本体在 `Server/cpp/src/third_party/kcp/`（MIT，保留原许可文本）。
- 板端自测：`Server/cpp/tests/kcp_echo_test.cpp`，覆盖绑定、会话建立、双向收发与跨 MTU 大消息。

## 实施时待解决

- KCP 的 MTU 设置（`ikcp_setmtu`）与现有 Lidgren 位流包尺寸的适配
  —— 注意 KCP 是**面向流**的，而现有协议是**按包**的，需要保留包边界（在 KCP 之上再加一层长度前缀）。
- 现有握手/心跳/ServerInfo 三条独立线格式（BinaryWriter 直写、不走 Lidgren）需要一并并入 KCP。
- `UpdateRocketsPeriod`（当前服务端硬编码下发 50ms，见 `TcpMultiplayerServer.cs:207`）
  与 `RocketSyncPolicy.ControlledIntervalMilliseconds`（50ms）在换成 KCP 后应重新评估，
  它们才是当前体感延迟的主要来源，与传输协议无关。
