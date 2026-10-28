# 03 — 不用 IL 运行时：native C++ 直连联机客户端可行性评估

> 目标：评估「不注入 C# / 不跑 IL2CPP 托管代码，直接在一个 native C++ 模块里实现联机客户端」的可行性。
> 只读仓库：`Server/cpp/src`、`Server/cpp/tests`、`Server/cpp/docs`、`Client/src/.../MultiplayerSFS.Common*`、
> `android/src/v43.c`、`android/dump/dump.cs`。本报告不改任何源码。
>
> 结论先行：**协议层完全可行且几乎零风险（服务端已有可复用的 C++ 实现 + 一个能跑的 C++ 客户端探针）；
> 真正的成本不在网络，而在「游戏侧对象的读写」——那部分必须靠 il2cpp 导出 API 反射式调用，
> 不是纯 native 能绕开的。**

---

## 0. 一句话结论

| 维度 | 评价 |
|---|---|
| 传输 / 分包 / 序列化 | **可直接复用**，服务端的 `protocol/` + `net/` 就是一份现成的客户端实现（`kcp_client_probe.cpp` 已经证明它能在客户端方向跑通） |
| 握手 / 心跳 / 时间同步 | **可直接复用**，帧类型与握手编解码都在 `protocol/` 里，逻辑量约 300 行 |
| 玩家位置/姿态同步（收发） | **协议可复用**，但「谁来算」要接游戏里的 Rocket 物理，这部分不能只靠网络层 |
| 游戏对象读写 | **必须走 il2cpp_* API**（反射式字段读写 / 方法调用），不是「纯 native」；这是主要工作量与主要风险 |
| 总体 | 可行，但不是「不用 IL 运行时」——而是「不用托管代码逻辑，仍要 il2cpp 运行时做对象访问」 |

---

## 1. 协议栈清单

### 1.1 分层总览

```
┌──────────────────────────────────────────────────────────┐
│ 游戏包层  PacketType(u8) + Lidgren 位流序列化              │  protocol/packets.cpp
├──────────────────────────────────────────────────────────┤
│ 应用帧层  TcpFrame[kind, seq, payloadBits, payload]        │  protocol/codec.cpp
│           + 4 字节小端长度前缀（KCP 消息边界）              │  net/kcp_transport.cpp
├──────────────────────────────────────────────────────────┤
│ 可靠层    KCP（ikcp.c，conv 区分会话，ARQ/重传/窗口）       │  third_party/kcp/ikcp.c
├──────────────────────────────────────────────────────────┤
│ 传输层    UDP（单 socket，默认端口 9806）                   │  net/kcp_transport.cpp
└──────────────────────────────────────────────────────────┘
```

**重要事实：现在全链路只有 KCP/UDP，没有 TCP。** 名字里带 `Tcp` 的 `TcpFrame` / `TcpFrameCodec` /
`TcpTransportCore` 都是历史命名，实际跑在 KCP 上：服务端 `Server::start()` 只 `kcp_.init(port_)`，
`frames.cpp` 只处理 KCP 会话（`Server/cpp/src/server/lifecycle.cpp:34`、`server/transport.cpp:34`）。

`protocol/codec.hpp` 里那套 `UdpEnvelope [kind][tokenLen][token][payload]`（`UdpKind::Bind/BindAck/Data`）
**是死代码**：全仓库只有 `codec.cpp` 自己定义/解码它，没有任何调用点（`frames.cpp:199` 注释也写了
「全 KCP：UDP 令牌表已废弃」）。native 客户端**不需要实现它**。

### 1.2 KCP 层

用的就是标准 skywind3000/kcp（`src/third_party/kcp/ikcp.c`，24 字节头）：

| 偏移 | 长度 | 字段 | 说明 |
|---|---|---|---|
| 0 | 4 | conv | 会话号，小端。客户端随机生成，服务端按 conv 区分会话 |
| 4 | 1 | cmd | 81=PUSH, 82=ACK, 83=WASK, 84=WINS |
| 5 | 1 | frg | 分片序号（0..255），255 表示还有后续 |
| 6 | 2 | wnd | 接收窗口，小端 |
| 8 | 4 | ts | 时间戳（毫秒，回绕） |
| 12 | 4 | sn | 序列号 |
| 16 | 4 | una | 未确认序号 |
| 20 | 4 | len | 数据长度 |

参数（`net/kcp_transport.hpp:27-37` 与 `KcpTransportPresets.cs` 的 balanced 档一致）：

```
nodelay=1  interval=10ms  fastresend=0  nocwnd=1  mtu=1400  wnd=256/512
```

- `fastresend=0` 是**有意**的：协议不靠重传保证送达，丢的状态包由下一个状态包补，位置偏差大走 200 m 硬同步。
  这四个参数是**发送方本地策略**，两端不必逐值相同（`KcpTransportPresets.cs:19` 明确写了）；
  但客户端若改成 `resend=2`，会在抖动链路上把延迟抖动误判成丢包而抢先重传，形成正反馈
  —— 建议就用 balanced 档。
- KCP 是**消息流**：上层自己加 4 字节小端长度前缀，所以一条 KCP 消息 = 一个完整的 `TcpFrame` 编码。

### 1.3 应用帧 `TcpFrame`

编码（`protocol/codec.cpp:43`，与 C# `TcpFrameCodec` 逐字节一致）：

```
[int32 bodyLen LE] [u8 kind] [int32 sequence LE] [int32 payloadBits LE] [payload ...]
 ^ 不含自身 4 字节      ^ 9 字节 body header 从这里开始
```

- `bodyLen = 9 + payload.size()`，**不含**前面 4 字节长度字段。
- `payloadBits` 是**有效位数**，允许不是 8 的倍数（尾数补零）。业务包尾部经常不是字节对齐的（见 1.9），
  只有握手/探测这类纯字节载荷才是 `len*8`。
- 上限 `kMaxTcpPayloadBytes = 8 MiB`；`bodyLen` 必须 `>= 9`。
- `kind` 合法范围 1..12，越界直接断连。

`TcpFrameKind`（12 个值，`codec.hpp:16`）：

| 值 | 名称 | 方向 | 用途 |
|---|---|---|---|
| 1 | Hello | C→S | 握手请求（payload 是 `SessionHandshakeCodec` 格式，**不是**位流） |
| 2 | HelloAck | S→C | 握手应答；`sequence` 字段放的是**握手版本号 2**，不是序号 |
| 3 | Packet | 双向 | 业务包，payload = `[u8 PacketType][Lidgren 位流]` |
| 4 | Ping | 双向 | 心跳；payload = 8 字节小端 int64（.NET ticks），服务端原样回显 |
| 5 | Pong | 双向 | 心跳应答，payload 必须原样回显 |
| 6 | Disconnect | 双向 | 断开；S→C 时 payload 是 UTF-8 拒绝原因 |
| 7 | RequestWorldSnapshot | C→S | 请求全量世界快照（服务端 1 秒/连接限流） |
| 8 | RequestRocketSnapshot | C→S | 单枚火箭快照，payload = `int32 rocketId`（按 (fd, rocketId) 限流 1 秒） |
| 9 | ServerInfoRequest | C→S | 登录页人数探测，空 payload，不建玩家会话 |
| 10 | ServerInfoResponse | S→C | `[int32 当前人数][int32 上限]`，固定 8 字节 |
| 11 | ServerVersionRequest | C→S | 登录页版本探测，空 payload |
| 12 | ServerVersionResponse | S→C | `[int32 握手版本][int32 协议版本][int32 串长][UTF-8 版本串][int32 flags]`，flags bit0 = 需要密码 |

`TcpFrameCodec.ProtocolVersion = 32`（C# 与 C++ 两侧常量，`codec.hpp:50`）。

### 1.4 消息类型枚举 `PacketType`（u8，payload 第 0 字节）

取值顺序是线协议的一部分，**永不重编号**（`protocol/packets.hpp:35`）：

| 值 | 名称 | 方向 | 谁发 |
|---|---|---|---|
| 0 | JoinRequest | C→S | 预留（实际走 Hello 帧，位流形态不上线） |
| 1 | JoinResponse | S→C | 预留（实际走 HelloAck 帧） |
| 2 | PlayerConnected | S→C | 广播 |
| 3 | PlayerDisconnected | S→C | 广播 |
| 4 | UpdatePlayerControl | S→C | 广播（谁在开哪枚） |
| 5 | UpdatePlayerAuthority | S→C | 单播（你这几枚火箭归你算） |
| 6 | UpdateWorldTime | S→C | 单播（收到你 Pong 后下发） |
| 7 | UpdatePlayerColor | S→C | 广播 |
| 8 | SendChatMessage | 双向 | 聊天 |
| 9 | CreateRocket | 双向 | 造/回执/权威覆盖 |
| 10 | DestroyRocket | 双向 | 销毁 |
| 11 | UpdateRocketPrimary | 双向 | 位置/旋转/角速度（**权威方 → 服务端 → 其他所有人**） |
| 12 | UpdateRocketSecondary | 双向 | 输入/油门/RCS |
| 13 | DestroyPart | 双向 | 零件销毁 |
| 14 | UpdateStaging | 双向 | 分级 |
| 15 | UpdatePartEngineModule | 双向 | 发动机开关 |
| 16 | UpdatePartWheelModule | 双向 | 轮子 |
| 17 | UpdatePartBoosterModule | 双向 | 助推器 |
| 18 | UpdatePartParachuteModule | 双向 | 降落伞 |
| 19 | UpdatePartMoveModule | 双向 | 可动部件 |
| 20 | UpdatePartResourceModule | 双向 | 资源（油量） |
| 21 | ShowToastMessage | S→C | 提示条 |
| 22 | UpdateCheatStatus | S→C | 作弊策略：`bool enabled + u16 flags` |
| 23 | DockTransaction | 双向 | 对接事务 |
| 24 | TimeWarp | 双向 | 时间加速投票 |
| 25 | ExperimentalAccess | — | **已停用**，占位保留 |
| 26 | P2PPeerOffer | S→C | P2P 撮合 |
| 27 | RenameRocket | 双向 | 改名 |
| 28 | UpdatePartTemperature | 双向 | 零件温度 |
| 29 | UpdateDestructionReason | 双向 | 销毁原因 |
| 30 | ServerCommand | 双向 | 控制台命令 / 输出 |
| 31 | UploadLog | C→S | GZip 日志上传（≤6 MiB） |
| 32 | P2PEndpoint | C→S | 广告本端 P2P UDP 端口 |

### 1.5 序列化格式

两种格式并存，**别混**：

**(a) 握手 / 探测：BinaryWriter 风格**（`protocol/handshake.hpp:7`）

- 整数定长小端（u32/i32/u64/f64）
- 字符串 = `int32 UTF-8 字节长度` + 原始字节（不是 7 位变长）
- 单条字符串上限 4096 字节

**(b) 业务包：Lidgren NetBuffer 位流**（`net/lidgren_buffer.hpp`，已用黄金向量实测）

| 类型 | 位宽 | 说明 |
|---|---|---|
| bool | 1 | LSB-first 位打包（**不是**整字节！） |
| u8/i8 | 8 | 原样 |
| u16/i16 | 16 | 小端 |
| u32/i32 | 32 | 小端 |
| u64/i64 | 64 | 小端 |
| f32/f64 | 32/64 | IEEE754 位模式直拷（NaN 载荷必须用 `0xFFC00000` / `0xFFF8000000000000`） |
| string | 变长 | `7 位一组变长整数(UTF-8 字节数)` + UTF-8 原始字节，**无终止符**；`127→7F`、`128→80 01` |
| byte[] | 8n | 裸拷贝，无长度前缀 |
| 集合 | 变长 | `int32 count` + 逐项；上限 100000 |

⚠️ 最容易踩的三个坑：
1. `bool` 只占 1 位 —— 写完 bool 后所有字段都错位半字节。C++ 侧 `NetWriter` 已经处理，自己写别用 `memcpy` 拼结构体。
2. 字符串长度前缀是**7 位变长**，不是 int32（握手层才是 int32）。
3. 集合计数是 `int32`，逐项紧跟，没有对齐填充。

### 1.6 逐字段布局（native 客户端要用的那些包）

记法：`f64/f32/i32/u8/bool/str/coll<T>`；`NetLocation = f64 x, f64 y, f64 vx, f64 vy, str address`。

```
PlayerConnected      : i32 playerId, str username, f32 r,f32 g,f32 b, bool printMessage
PlayerDisconnected   : i32 playerId
UpdatePlayerControl  : i32 playerId, i32 rocketId          (-1 = 没在开)
UpdatePlayerAuthority: coll<i32> rocketIds
UpdateWorldTime      : f64 worldTime
UpdatePlayerColor    : i32 playerId, f32 r, f32 g, f32 b
SendChatMessage      : i32 senderId, str message
CreateRocket         : f64 worldTime, i32 localId, i32 globalId, bool forLaunch, RocketState
DestroyRocket        : f64 worldTime, i32 rocketId, u8 reason
UpdateRocketPrimary  : f64 worldTime, i32 rocketId, NetLocation, f32 rotation,
                       f32 angularVelocity, i32 structureHash
UpdateRocketSecondary: f64 worldTime, i32 rocketId, f32 inputTurn, f32 rawX, f32 rawY,
                       f32 horizontalX, f32 horizontalY, f32 verticalX, f32 verticalY,
                       f32 throttlePercent, bool throttleOn, bool rcs
DestroyPart          : f64 worldTime, i32 rocketId, i32 partId, bool createExplosion, u8 reason
UpdateStaging        : f64 worldTime, i32 rocketId, coll<Stage>
Stage                : i32 stageId, coll<i32> partIds
UpdatePartEngine     : f64 worldTime, i32 rocketId, i32 partId, bool engineOn
UpdatePartWheel      : f64 worldTime, i32 rocketId, i32 partId, bool wheelOn
UpdatePartBooster    : f64 worldTime, i32 rocketId, i32 partId, bool primed, f32 throttle, f32 fuelPercent
UpdatePartParachute  : f64 worldTime, i32 rocketId, i32 partId, f32 state, f32 targetState
UpdatePartMove       : f64 worldTime, i32 rocketId, i32 partId, f32 time, f32 targetTime
UpdatePartResource   : f64 worldTime, i32 rocketId, f64 resourcePercent, coll<i32> partIds
UpdatePartTemperature: i32 rocketId, i32 partId, f32 temperature
UpdateDestructionReason: i32 rocketId, i32 partId, u8 reason
ShowToastMessage     : str message
UpdateCheatStatus    : bool enabled, u16 flags
TimeWarp             : u8 operation, i32 voteId, i32 requesterId, str requesterName, f64 multiplier,
                       bool approved, f64 worldTime, i32 timeoutSeconds, str message
DockTransaction      : i32 txnId, u8 operation, bool committed, i32 keepRocketId, i32 removeRocketId,
                       i32 keepPartId, i32 removePartId, f64 worldTime,
                       [committed] RocketState merged, i32 secondRocketId, bool hasSecond, [hasSecond] RocketState
RenameRocket         : i32 rocketId, str rocketName
```

`RocketState`：

```
str rocketName, NetLocation, f32 rotation, f32 angularVelocity,
bool throttleOn, f32 throttlePercent, bool rcs,
coll<[i32 partId + PartState]> parts,
coll<Joint {i32 partA, i32 partB}> joints,
coll<Stage> stages
```

`PartState`：

```
str name, f32 x, f32 y, f32 orientationX, f32 orientationY, f32 orientationZ, f32 temperature,
coll<str key + f64 value> numberVariables,
coll<str key + bool value> toggleVariables,
coll<str key + str value> textVariables,
bool burnsIsNull,  [若 false] f32 angle, f32 intensity, f32 x, str top, str bottom
```

注意 `burns` 是「先写一个 bool 表示 null」，语义与 C# `Burns is null` 一致；空 = 写 `true`。

### 1.7 最小握手包（逐字节）

场景：用户名 `Native`、无密码、太阳系 `Default`、非重连（resumePlayerId = -1、空 token）。

payload（41 字节）：

```
53 53 46 31            magic 0x31465353 —— 线序是 "SSF1"，不是 "SFS1"
02 00 00 00            handshake version = 2
06 00 00 00 4E 61 74 69 76 65         str "Native"
00 00 00 00                            str ""   (password)
07 00 00 00 44 65 66 61 75 6C 74      str "Default"
FF FF FF FF                            int32 -1 (resumePlayerId)
00 00 00 00                            str ""   (resumeToken)
```

完整 `TcpFrame`（54 字节，`bodyLen=50`）：

```
32 00 00 00              bodyLen = 50 (9 + 41)
01                       kind = Hello
02 00 00 00              sequence = 2  ← 握手版本号，不是序号
48 01 00 00              payloadBits = 328 (41*8)
53 53 46 31 02 00 00 00 06 00 00 00 4E 61 74 69 76 65
00 00 00 00 07 00 00 00 44 65 66 61 75 6C 74
FF FF FF FF 00 00 00 00
```

送给 KCP 前再加 4 字节长度前缀（KCP 消息边界）：

```
36 00 00 00  32 00 00 00 01 02 00 00 00 48 01 00 00 53 53 46 31 ...
```

（`0x36 = 54`，即整帧长度。这 58 字节就是 `ikcp_send` 的入参；KCP 自己再加 24 字节头。）

### 1.8 连接时序

```
C: UDP socket bind(0)，conv = 随机 u32（非 0）
C: ikcp_create(conv) → ikcp_nodelay(1,10,0,1) / ikcp_setmtu(1400) / ikcp_wndsize(256,512)
C: send TcpFrame(Hello, seq=2, payload=handshake)   ── 每 200ms→2s 指数退避重发，总超时 10s
S: HelloAck(seq=2, payload=SessionHandshakeCodec.EncodeAck)
   └ 同一条 KCP 消息流里紧接着：Packet(22 UpdateCheatStatus)
S: send_initial_state() 按固定顺序：PlayerConnected(+UpdatePlayerControl) 全量
   → CreateRocket 全量（世界快照）→ TimeWarp(Applied)；随后 refresh_authorities() 下发 UpdatePlayerAuthority
C: 每 2s 发 Ping(payload = 8B .NET ticks)
S: 回 Pong(原样回显)；服务端自己的 2s Ping 由客户端回 Pong
S: 收到合法 Pong → Packet(6 UpdateWorldTime)   ← 世界时间只由服务端驱动
C: 业务包（CreateRocket / UpdateRocketPrimary / Secondary / 聊天 …）
```

握手失败时服务端发 `Disconnect`，payload 是 UTF-8 原因文本（`Server is full.` / `Invalid password.` 等），
客户端直接展示这段文本。

### 1.9 已实测的两个坑（本报告亲自编译验证）

用 zig 0.16.0 把 `protocol/` + `net/lidgren_buffer.cpp` 编成本机程序，跑了两个用例：

```
payload=41 frame=54          ← 与 1.7 的字节序列逐字节一致
secondary bytes=46 bits=362  ← UpdateRocketSecondary 只有 362 位，不是 368
```

1. **业务包不是字节对齐的。** `PacketType` 的 u8 写在位偏移 0，所以包头字节对齐；但包体里的
   `bool` 只占 1 位，最后一个 bool 之后整包就停在半个字节上。`UpdateRocketSecondary` 是
   362 位（46 字节，尾 6 位补零）。发出去时 `payloadBits` 必须填 **362**。
   服务端读侧只按字段需要取位，多写 6 个零位也能解出来，但一旦某个包尾部是
   `bool + 后续字段`，位数填错就会整体错位。**结论：位数由序列化器自己算，不要写 `len*8`。**
2. **握手层相反，是纯字节格式**（`payloadBits = len*8`），两者不能共用一套计数。

---

## 2. native 客户端必须实现什么 / 能复用哪些 C++ 文件

### 2.1 可直接复用（文件级，已验证）

下表全部**未改一行**即可编进客户端。我实际用
`zig c++ -target aarch64-linux-musl -std=c++17 -O2 -fPIC` 把这 9 个文件编成了 ARM64 目标文件
（产物在 `tools/out/*-a64.o`），没有一个报错——说明协议层与架构无关，能直接进 Android。

| 文件 | 行数 | 作用 | 复用度 |
|---|---|---|---|
| `src/third_party/kcp/ikcp.c` | 1423 | KCP ARQ 本体 | 原样 |
| `src/net/lidgren_buffer.cpp/.hpp` | 259+134 | Lidgren 位级读写（含 7 位变长串） | 原样 |
| `src/protocol/codec.cpp/.hpp` | 140+75 | `TcpFrame` 编解码 + `TcpFrameKind` | 原样 |
| `src/protocol/handshake.cpp/.hpp` | 186+56 | Hello / HelloAck（BinaryWriter 风格） | 原样 |
| `src/protocol/packets.cpp/.hpp` | 888+600 | 33 种包 + `RocketState`/`PartState` 全字段序列化 | 原样 |
| `src/protocol/types.cpp/.hpp` | 64+70 | 字符串/集合边界 | 原样 |
| `src/protocol/server_info.cpp` | 50 | 登录页人数探测 | 原样（可选） |
| `src/protocol/server_version.cpp` | 74 | 登录页版本探测 | 原样（可选） |
| `src/platform/socket_platform.cpp` | 109 | socket 类型/初始化（Winsock vs POSIX） | 原样 |
| `src/net/kcp_transport.cpp/.hpp` | 577+158 | 单 socket 多 conv 会话、`poll/send/receive` | **可用但偏服务端** |
| `tests/kcp_client_probe.cpp` | 152 | 客户端方向的端到端范例 | **直接当骨架抄** |

`kcp_client_probe.cpp` 的价值最大：它已经证明「客户端侧 `KcpTransport::init(0)` + `connect(conv, host, port)`
+ `EncodeTcpFrame` + `poll/receive`」这条链路能跑通，而且覆盖了 ServerInfo / ServerVersion / Ping-Pong 三段契约。

`KcpTransport` 的两点服务端气味（移植时要改，不用重写）：
- 会话表 `unordered_map<conv, Session>` + `session_convs()` 轮询——客户端只有一个会话，可以直接用单 `ikcpcb*`；
- `kSessionTimeoutMs = 30000`、`kMaxMessageBytes = 8 MiB` 是服务端口径；客户端消息上限实际远小于此。

### 2.2 必须新写的部分

| 模块 | 内容 | 估算 LOC | 依据 |
|---|---|---|---|
| 传输泵 | 收线程 + 发送队列 + `ikcp_update` 10ms 节拍；`poll` 与游戏主线程解耦 | 250 | `TcpTransportCore.cs:206-407` |
| 握手客户端 | conv 随机化、Hello 指数退避（200ms→2s，总 10s）、`HelloAck.Sequence==2` 校验 | 120 | `TcpTransportCore.cs:143-266` |
| 心跳 | 2s 发 Ping（8B ticks）、回 Pong、10s 无收判定掉线 | 90 | `TcpTransportCore.cs:369-407` |
| 断线重连 / Resume | 保存 `playerId + resumeToken`，重连走 Hello 的 resume 字段 | 120 | `handshake.hpp:32`、`frames.cpp:116` |
| 世界状态仓 | `map<rocketId, RocketState>`、玩家表、世界时间基准 | 300 | `WorldState.cs` |
| 快照补包 | `RequestWorldSnapshot` / `RequestRocketSnapshot`（按火箭限流 1s） | 80 | `frames.cpp:309-349` |
| 插值/外推 | 远端火箭位置插值、200 m 硬同步、陈旧补包 | 600–900 | `Interpolator.cs` 1441 行（可砍半） |
| 本地状态采集 | 每帧读本地火箭 → 组 `UpdateRocketPrimary`（位置/旋转/角速度/结构哈希） | 400 | `LocalManager.cs:1200-1400` |
| 远端状态应用 | `physics.SetLocationAndState` + `rb2d` 姿态 + 油门/分级/模块 | 600 | `Interpolator.cs:612-1233` |
| 火箭创建/销毁 | `Instantiate(prefab)` + `PartsLoader.CreatePart` + JointGroup + Stage | 700 | `LocalManager.cs:1250-1337` |
| il2cpp 桥 | `dlopen` + 符号表 + 类/字段/方法缓存 + `il2cpp_string_new` + gchandle | 900 | `v43.c:1663-1720`（已有骨架） |
| 聊天 / UI | 收发 `SendChatMessage`，显示；可复用 v43 的 UI 克隆 | 300 | `ChatWindow.cs` 314 行 |
| P2P（可选） | `P2PPeerOffer` + 独立 KCP 会话 + 打洞 | 800 | `P2PConnectionManager.cs` 707 行 |

**MVP 不含 P2P、不含对接事务（DockTransaction）、不含 TimeWarp 投票**：这三块在服务端都有，
但单机 native MVP 可以只做「中继 + 位置同步 + 聊天」，服务端不会因此拒绝你（它只按包类型分发）。

### 2.3 客户端必须实现的状态机（容易漏的几条）

1. **权威（authority）**：只有收到 `UpdatePlayerAuthority` 里包含的 rocketId，你发的
   `UpdateRocketPrimary/Secondary` 才会被服务端接受，否则**静默丢弃**（`server/packets.cpp:416,437`）。
   所以「本地造完火箭 → 等服务端回执 + 等权威包 → 才开始发状态」是硬顺序。
2. **世界时间只信服务端**：客户端自己积分的 `worldTime` 只能用于本地插值，发出去的包里的
   `worldTime` 会被服务端覆盖（`packets.cpp:421`）。收到 `UpdateWorldTime` 必须重置基准。
3. **重复 Hello 只回 ack**：服务端对同一会话的第二次 Hello **不会**重发初始状态
   （`frames.cpp:231`）。所以客户端重连必须换 conv，否则会「连上但世界是空的」。
4. **`CreateRocket` 的 localId 去重**：没收到回执要原样重发同一个 `localId`，服务端靠
   `(playerId, localId)` 复用 globalId；换 id 重发会造出两枚一样的火箭。
5. **Ping 的 payload 必须原样回显**：服务端只接受「最新一次 Ping 的 ticks」的 Pong
   （`frames.cpp:265`），回错值世界时间就不推进。

### 2.4 时间同步的具体做法

协议只给了两个时间源，别自己发明：

| 来源 | 字段 | 用途 |
|---|---|---|
| `HelloAck` | `worldTime` + `sendTime`（服务端发包时刻，毫秒） | 建立初始基准；`sendTime` 可用来估单程延迟 |
| `Packet(6 UpdateWorldTime)` | `worldTime` | 每次服务端收到你的 Pong 后下发，作为**权威校正点** |

推荐实现（与 C# 侧一致）：

```
本地维护：worldTimeBase（double）、baseLocalMs（单调毫秒）
估算：    worldTimeNow = worldTimeBase + (nowMs - baseLocalMs) * timeScale / 1000

收到 UpdateWorldTime(w)：
    rttHalf = rtt_ms / 2
    drift = w - worldTimeNow            // 含单程延迟带来的偏差
    if |drift| > hardThreshold          // 例如 0.5 s（首次/重连）
        worldTimeBase = w + rttHalf*timeScale/1000; baseLocalMs = now
    else                                // 平滑，避免每 2 秒抖一下
        worldTimeBase += drift * 0.1
```

服务端自己下发时已经补了单程延迟（`frames.cpp:281`：`world_time() + rtt/2000*time_scale`），
所以客户端可以直接把收到的值当作「此刻的服务端时间」，只在偏差超阈值时硬重置。

**不要**每包都写 `WorldTime.main.worldTime`：C# 侧只在必要时写（`ClientManager.cs:754`），
每 2 秒硬写一次会让游戏内时间出现可见的跳变/抖动。
时间倍率固定 1.0（联机不做加速），`WorldTime.main.SetState(1.0, true, false)` 只用作兜底。

---

## 3. 游戏侧必须读写的对象（dump 里的真实类名 / 偏移）

> 重要前提：**安卓包的 `Assembly-CSharp` 是混淆过的**——类名是 `FULMMHJZAEKABZAY` 这种，
> 但**命名空间、字段名、方法名保留了**。PC 模组源码用的是可读名（`Rocket` / `WorldTime` / `Player`），
> 两边必须做一张映射表。`android/src/v43.c:1694` 已经踩通：
> `il2cpp_class_from_name(img, "SFS.World", "FULMMHJZAEKABZAY")`。

### 3.1 核心类型映射

| 语义 | PC / 模组里的名字 | dump 里的名字（安卓） | TypeDefIndex | 出处 |
|---|---|---|---|---|
| 火箭 | `Rocket` | `FULMMHJZAEKABZAY`（`SFS.World`） | 1062 | dump.cs:35913，v43.c:1694 实证 |
| 火箭基类 | `WorldObject`? | `DXYTHXTABGUDBUFN`（抽象） | 592 | dump.cs:21648 |
| 位置组件 | `Location`（MonoBehaviour） | `FNVQPOCIQHTEDVSZ` | 902 | dump.cs:30939，v43.c:1708 |
| 位置值类型 | `Location`（struct） | `Location`（**名字没混淆**） | 238 | dump.cs:7398 |
| 二维向量 | `Double2` | `Double2` | 762 | dump.cs:26874 |
| 世界时间 | `WorldTime` | `VCQSQMYGGZECNBPE`（`SFS.World`） | 1628 | dump.cs:52373 |
| 世界（火箭表） | `World` / `RocketManager`? | `QHQPUKFMUOSOOWES`（`SFS.World`） | 823 | dump.cs:28692 |
| 火箭工厂 | `RocketManager` | `WQPAXLGGCOAUZEOA`（`SFS.World`） | 628 | dump.cs:22853 |
| 零件（运行时） | `Part` | `Part`（**名字没混淆**） | 647 | dump.cs:23472 |
| 零件加载 | `PartsLoader` | `PartsLoader` | 341 | dump.cs:10236 |
| 零件存档 | `PartSave` | `PartSave` | 1227 | dump.cs:40433 |
| 分级模块 | `Staging` | `FIRJPZZYVBGXOYNA` | 266 | dump.cs:8095 |
| 油门模块 | `Throttle` | `AKQXNLQKSRJXPQGE` | 1376 | dump.cs:44665 |
| 物理体 | `Physics` | `UFLBGBGGAEJQCJDW` | 183 | dump.cs:5431 |
| 关节组 | `JointGroup` | `JointGroup` | 1440 | dump.cs:46969 |
| 关节 | `PartJoint` | `PartJoint` | 1264 | dump.cs:41333 |
| 分级 | `Stage` | `Stage` | 1348 | dump.cs:43610 |
| 本地玩家 | `Player.controlledRocket` | **未找到**（见 3.5） | — | — |

### 3.2 火箭 `FULMMHJZAEKABZAY` 字段（dump.cs:35913）

| 偏移 | 字段 | 类型 | 用途 |
|---|---|---|---|
| 0x38 | `location` | `FNVQPOCIQHTEDVSZ*` | **读写位置**（继承自基类 `DXYTHXTABGUDBUFN`） |
| 0x58 | `mass` | `Mass_Calculator*` | 质量（只读，可用于校验） |
| 0x60 | `rb2d` | `Rigidbody2D*` | 姿态/角速度/bodyType |
| 0x68 | `partHolder` | `PartHolder*` | 零件表容器 |
| 0x78 | `arrowkeys` | `WMCZPVIROBWPMJFG*` | RCS 输入（`.rcs.Value`） |
| 0x80 | `throttle` | `AKQXNLQKSRJXPQGE*` | 油门（`.throttleOn.Value` / `.throttlePercent.Value`） |
| 0x88 | `staging` | `FIRJPZZYVBGXOYNA*` | 分级 |
| 0x90 | `resources` | `MKGUBMRBOIFPUAEJ*` | 资源/油量 |
| 0xA8 | `timeManager` | `GameObject*` | — |
| 0xB0 | `partManager` | `GameObject*` | — |
| 0xB8 | `rocketName` | `string*` | 火箭名 |
| 0xC0 | `jointsGroup` | `JointGroup*` | 关节 |
| 0xC8 | `collisionImmunity` | `float` | — |
| 0xF8 | `physics` | `UFLBGBGGAEJQCJDW*` | **`SetLocationAndState` 在这里** |

方法：`GetRotation()`（v43.c:1705 已用）、`SetJointGroup(JointGroup)`（dump.cs:36098）、
`UseParts(bool, ValueTuple<Part,PolygonData>[])`（静态，造零件用）、
`SetPlayerToBestControllable(Rocket[])`（dump.cs:36068）。

### 3.3 位置链（这是同步的核心）

```
rocket.location             @0x38   → FNVQPOCIQHTEDVSZ*
  .planet                   @0x20   → Planet_Local*  (Obs<Planet>)
  .position                 @0x28   → Double2_Local* (Obs<Double2>)
  .velocity                 @0x30   → Double2_Local*
  get_Value() / set_Value(Location)  → 值类型 Location（读一次拿全）

Location (struct, dump.cs:7398)
  +0x10 time        : f64
  +0x18 planet      : Planet*
  +0x20 position    : Double2 {x@0, y@8}
  +0x30 velocity    : Double2 {x@0, y@8}
```

`NetLocation{x,y,vx,vy,address}` 的映射：
- `x,y ← location.Value.position.x/.y`（单位 m，double，直接对应）
- `vx,vy ← location.Value.velocity.x/.y`
- `address ← location.Value.planet.codeName`（`Planet` 的 `codeName` 字段；C# 侧
  `Interpolator.cs:600` 用的就是 `live.planet.codeName`）

写回：`rocket.physics.SetLocationAndState(loc, physicsMode:true)`（dump.cs:5500，签名
`SetLocationAndState(Location, bool)`），姿态另外写
`rocket.rb2d.transform.eulerAngles = (0,0,rot)` + `rocket.rb2d.angularVelocity = angVel`
（`Interpolator.cs:612-613`）。**两条都要写**：只写 Location 不改 transform，画面不会动。

### 3.4 世界时间 `VCQSQMYGGZECNBPE`（dump.cs:52373）

| 偏移 | 字段 | 类型 |
|---|---|---|
| 0x0 | `main`（静态） | `VCQSQMYGGZECNBPE*` |
| 0x20 | `timewarpIndex` | `int` |
| 0x28 | `worldTime` | `double` |
| 0x30 | `timewarpSpeed` | `double` |
| 0x38 | `realtimePhysics` | `Bool_Local*` |

方法：`SetState(double worldTime, bool realtimePhysics, bool applyToPhysics)`（dump.cs:52398）。
C# 模组的两处用法：
- 收包写时间：`WorldTime.main.worldTime = packet.WorldTime;`（`ClientManager.cs:754`）
- 兜底把时间倍率拉回 1：`WorldTime.main.SetState(1.0, true, false)`（`ClientManager.cs:443`）

⚠️ 联机模型**不做时间加速**，`timewarpSpeed` 必须保持正值；写成 -1 会让 `worldTime` 反向积分
（`ClientManager.cs:437` 的血泪注释）。

### 3.5 玩家 / 控制权

PC 模组用 `Player.controlledRocket.Value`（`LocalManager.cs` 多处），但**安卓 dump 里没有这个成员**
（`grep controlledRocket` = 0 命中，`Player` 类只匹配到 Google Play Games 的同名类）。
两种可能：① 该静态成员在安卓包被改名成混淆名；② 安卓版本走的是另一套选择逻辑。
**标注为未解决**，替代路径（按可行性排序）：
1. `rocket.isPlayer` / `rocket.hasControl`（基类 `DXYTHXTABGUDBUFN` 的 0x48 / 0x50，`Bool_Local*`）
   —— 直接读「哪枚火箭被本地玩家控制」，不依赖 `Player` 类；
2. `QHQPUKFMUOSOOWES.main.rockets`（@0x40，`List<Rocket>`）遍历，配合 1 找当前控制的那枚；
3. 反射扫 `Assembly-CSharp` 里所有带 `Obs<Rocket>` 字段的静态类（v43.c:1741 已有遍历 image 的代码）。

### 3.6 造/拆火箭需要的托管对象

远端 `CreateRocket` 到达后必须复刻一枚，服务端不会帮你建。C# 的路径
（`LocalManager.cs:1250-1313`）逐句翻成 il2cpp 调用是：

```
Object.Instantiate(RocketManager.rocketPrefab)          // WQPAXLGGCOAUZEOA.rocketPrefab @0x20
rocket.rocketName = state.rocketName                    // 0xB8
rocket.throttle.throttleOn.Value = ...                  // 0x80 → AKQXNLQKSRJXPQGE
rocket.throttle.throttlePercent.Value = ...
rocket.arrowkeys.rcs.Value = ...                        // 0x78
foreach part: PartsLoader.CreatePart(PartSave, null, null, Allow, out ownership)
              ↑ PartSave{name@0x10, position@0x18, orientation@0x20, temperature@0x28,
                         NUMBER_VARIABLES@0x30, TOGGLE_VARIABLES@0x38, TEXT_VARIABLES@0x40,
                         burns@0x48}
new PartJoint(a, b, b.Position - a.Position)            // 1264
rocket.SetJointGroup(new JointGroup(list, allParts))    // 36098 / 1440
rocket.rb2d.transform.eulerAngles = (0,0,rotation)
rocket.physics.SetLocationAndState(loc, true)           // 5500
rocket.rb2d.angularVelocity = angVel
rocket.staging.InsertStage(new Stage(id, parts), false) // 8125 / 1348
```

**这是整个移植里最贵的一段**：`PartSave` 里三个 `Dictionary<string,T>` 都要在托管堆上
`il2cpp_object_new` + 逐个 `Dictionary.Add` 调用；75 个零件的火箭就是 75 次 CreatePart
+ 225 个字典插入。C# 侧注释说「75 部件 Instantiate 一次几百毫秒」（`StructureSync.cs:16`），
native 侧不会更快——因为开销在 Unity 侧，不在 IL 侧。

`UpdateStaging` / `DestroyPart` / 模块包反过来简单：都是改已存在零件的字段或调用
`staging.InsertStage` / `Part.aboutToDestroy`，不需要新建零件。

### 3.7 读本地火箭 → 组包（每帧）

需要读的字段（全部来自 `Interpolator.cs` / `LocalManager.cs` 的实际用法）：

| 包字段 | 游戏来源 |
|---|---|
| `location.{x,y,vx,vy}` | `rocket.location.Value.position/.velocity` |
| `location.address` | `location.Value.planet.codeName` |
| `rotation` | `rocket.GetRotation()`（v43.c 已调通） |
| `angularVelocity` | `rocket.rb2d.angularVelocity` |
| `throttlePercent` / `throttleOn` | `rocket.throttle.throttlePercent.Value` / `.throttleOn.Value` |
| `rcs` | `rocket.arrowkeys.rcs.Value` |
| `parts[].{name,x,y,orientation,temperature}` | `Part` 的 Unity 对象名（`PartSave(Part)` 取的就是它）/ 相对火箭原点的局部坐标 / `Part.orientation@0x58` / `Part.temperature@0xA0` |
| `structureHash` | 活体零件 ID 集合的哈希（客户端自定义，服务端只透传） |

`Part` 的字段偏移（dump.cs:23472）：
`displayName@0x28`、`mass@0x40`、`orientation@0x58`（`OrientationModule`，三轴 float）、
`variablesModule@0x60`（`VariablesModule`，即 `PartState` 的三类变量来源）、`temperature@0xA0`。

---

## 4. 工作量与风险

### 4.1 先纠正一个前提

「不用 IL 运行时」在**注入式**方案里做不到，也不必要：

- 游戏进程里 `libil2cpp.so` 一定在跑，`Rocket`/`Part` 都是托管对象；
- native 代码要读写它们，只能走 `il2cpp_*` 导出 API（`v43.c` 已经这么干了）；
- 真正能省掉的是 **C# 模组那一层**（Harmony patch、`MultiplayerSFS.Mod.dll`、HybridCLR 解释器），
  不是 il2cpp 运行时本身。

所以本报告评估的是「**native 直连 + il2cpp 反射**」，不是「脱离 il2cpp 的独立进程客户端」。
后者要自己重写 SFS 的物理/星球/零件系统，**不可行**（那是整个游戏）。

### 4.2 工作量（单人，熟悉该仓库 C++ 与 il2cpp）

| # | 工作项 | 人日 | 说明 |
|---|---|---|---|
| 1 | 协议层接进 Android 构建（NDK；zig 只当交叉编译器，Android 目标仍需 NDK sysroot），握手跑通 | 3 | 已实测 9 个文件可编 aarch64；剩链接与 `JNI_OnLoad` |
| 2 | 传输泵 / 心跳 / 掉线判定 / 重连 resume | 3 | 抄 `kcp_client_probe.cpp` + `TcpTransportCore.cs` |
| 3 | 状态仓 + 包路由 + 快照补包 | 2 | 纯 C++，无游戏依赖 |
| 4 | il2cpp 桥（符号表、类/字段/方法缓存、string、gchandle、异常检查） | 5 | `v43.c:1663-1720` 是现成骨架，但要做成可复用层 |
| 5 | 读本地火箭 → `UpdateRocketPrimary/Secondary`（含等权威） | 4 | 见 3.7 |
| 6 | 远端已有火箭的状态应用（位置/姿态/油门/分级） | 3 | `SetLocationAndState` + `rb2d` |
| 7 | 远端火箭创建（Instantiate + PartSave + JointGroup + Stage） | 6 | 最贵的一段，见 3.6 |
| 8 | 插值 / 外推 / 200 m 硬同步 / 陈旧补包 | 4 | 可先做「直接贴位置」，后续再优化 |
| 9 | 世界时间、玩家列表、聊天、UI | 3 | UI 可复用 v43 的克隆按钮 |
| 10 | 真机联调（双客户端、丢包、重连、后台切回） | 5 | 必须有第二台设备/PC 客户端做对端 |
| | **小计** | **38** | |
| | 余量 25%（混淆名、崩溃定位、未知 API） | 9 | |
| | **合计** | **≈47 人日（约 9–10 周）** | |

砍到 **MVP（≈31 人日 / 6 周）** 的做法：
- 只做 1–7 + 10，插值先用「收到就贴」（第 8 项砍到 1 天）；
- 不做聊天/UI（用日志代替）、不做 P2P、不做对接、不做 TimeWarp。

对照参考：被替代的 C# 模组本体是 **7730 行**（`MultiplayerSFS.Mod/*.cs`），
其中 `LocalManager` 1748 行、`Interpolator` 1441 行、`ClientManager` 943 行。
native 版估算 **4600–5400 行**（不含 P2P），行数少一些是因为协议/序列化那 4800 行直接复用了。

### 4.3 风险

| 风险 | 等级 | 具体表现 | 对策 |
|---|---|---|---|
| **主线程限制** | **致命** | Unity 的 `Transform`/`Rigidbody2D`/`Instantiate` 只能在主线程调；网络线程里调 = 随机崩溃或静默失效 | 网络线程只碰 KCP + 队列；所有游戏对象操作在主线程 tick 里做。`v43.c:1895-1906` 已证明可以换 `ScreenManager.Update` 的方法指针拿到每帧主线程回调 |
| **序列化对齐** | **高** | `bool` 只占 1 位；`UpdateRocketSecondary` = 362 位不是 368；字符串是 7 位变长；NaN 位模式 | 复用 `lidgren_buffer.cpp`，**不要**自己按结构体 memcpy；用 `golden/` 向量回归（现成 87+53 用例） |
| **KCP 定时器** | **高** | `ikcp_update` 必须 ~10ms 一次；`ikcp_input/send/flush` 非线程安全；从 Unity 帧驱动（16.7ms）会加抖动 | 独立线程 + 互斥锁保护 ikcp；`steady_clock` 单调毫秒；`Check()` 返回值决定 sleep，别用固定 10ms 死等 |
| **混淆名漂移** | 中高 | 游戏一更新，`FULMMHJZAEKABZAY` 这类名字全变；硬编码偏移也会变 | 类名/偏移做成**运行时查表**（`il2cpp_class_get_field_from_name` + `field_get_offset`，v43.c 就是这么做的，不写死偏移）；查不到时降级并打日志，不要崩 |
| **托管对象生命周期** | 中 | `PartSave`/`JointGroup`/`Stage` 建出来若没有引用会被 GC 回收 | 每个跨帧持有的托管对象 `il2cpp_gchandle_new`；用完 `gchandle_free` |
| **创建火箭的性能** | 中 | 75 零件 × (`CreatePart` + 3 个字典) 在主线程 = 几百毫秒卡顿 | 每帧只建一枚（C# 也是这么节流的，`LocalManager.cs:563`）；建完再放进世界 |
| **服务端静默丢弃** | 中 | 没权威时 `UpdateRocketPrimary` 被丢，客户端表现为「别人看不见我」 | 严格按 2.3 的顺序；本地打点记录「发了多少 / 收到多少」；必要时 `RequestRocketSnapshot` 自检 |
| **Android UDP 被系统掐** | 中 | 后台切回、ICMP port unreachable 会让 socket 读端永久报错 | 用未连接 UDP socket + `sendto`；参考 `UdpSocketGuard.cs` 的 `SIO_UDP_CONNRESET` 处理 |
| **调试困难** | 中 | il2cpp 里崩了没有 C# 栈 | 沿用 `v43.c` 的 `log_app()` 落盘日志；每个 il2cpp 调用后检查 `exc` |

### 4.4 结论与建议路径

1. **协议层零风险**：服务端已经有一份能跑的 C++ 客户端探针，`protocol/` + `net/` 直接复用，
   本报告已实测能编 aarch64。这一步**不需要重新设计任何东西**。
2. **风险全部集中在「游戏侧读写」**：il2cpp 反射 + 主线程调度 + 造火箭。这部分和工作量
   （≈30 人日）就是整个项目的真实成本，与「是不是 native」无关——用 C# 模组同样要面对
   `Instantiate` 几百毫秒的卡顿。
3. **建议先做一个 3 天的 spike**：在 `v43.c` 现有骨架上加「遍历 `World.main.rockets` →
   读 `rocket.location.Value` → 打印 x/y」，能跑通就说明第 3 节的位置链成立，
   再投入剩下的 30+ 人日；跑不通就先解决 3.5 的 `Player.controlledRocket` 缺失问题。
4. **不建议**做的：自己实现物理、自己实现零件渲染、脱离 il2cpp 的独立进程客户端。

---

## 附：本次验证产物

| 路径 | 内容 |
|---|---|
| `tools/proto_probe.cpp` | 用仓库真实代码编 Hello 帧 + `UpdateRocketSecondary` 往返的验证程序 |
| `tools/out/proto_probe.exe` | 本机运行结果（payload=41 / frame=54 / 362 bits） |
| `tools/out/*-a64.o` | 9 个复用文件交叉编译到 `aarch64-linux-musl` 的目标文件 |

> 说明：`tools/` 下只有验证用的临时文件，不属于交付物；`D:/SFS-Multiplayer/1` 与任何源码均未修改。
