<!--
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

# SFS Multiplayer 线格式（已实测确认）

本文件记录 **实测确认** 的线格式，以及验证方法。凡未实测的推断一律不写进来。

## 一、Lidgren NetBuffer 位格式

C++ 侧实现见 `src/net/lidgren_buffer.hpp` / `.cpp`，测试见 `tests/golden_conformance.cpp`。

| 类型 | 位宽 | 说明 |
|---|---|---|
| `bool` | 1 | LSB-first 位打包 |
| `i8` / `u8` | 8 | 原样 |
| `i16` / `u16` | 16 | 小端 |
| `i32` / `u32` | 32 | 小端 |
| `i64` / `u64` | 64 | 小端 |
| `f32` | 32 | IEEE754 位模式直拷 |
| `f64` | 64 | IEEE754 位模式直拷 |
| `string` | 变长 | `7 位一组变长整数(UTF-8 字节数)` + UTF-8 原始字节，无终止符 |
| `byte[]` | 8×n | **裸拷贝，无长度前缀** |

关键细节（全部由黄金向量实测得出，非推测）：

- **位序 LSB-first**：值从当前位偏移起按低位到高位写入。例如先写 `bool(true)`
  再写 `i32(0x12345678)`，得到 33 位 `F1AC682400`。
- **字符串前缀是 UTF-8 字节数，不是字符数**：`羊城通`（3 字符 / 9 字节）
  → `09` + 9 字节 = 80 位。此点若搞错会静默错位。
- **变长整数是 7 位一组、低位在前、高位组带 `0x80` 继续标志**：
  `127` → `7F`；`128` → `80 01`；`300` → `AC 02`。
- **`Write(byte[])` 不写长度**：需要长度时协议层自己做（用变长整数）。
- **.NET 的 NaN 位模式**：`float.NaN` = `0xFFC00000`，`double.NaN` = `0xFFF8000000000000`。
  C++ 的 `std::nan()` 载荷不保证相同，序列化时必须用精确位模式。

## 二、TCP 帧格式

帧头 9 字节：`[bodyLen][kind][seq][payloadBits][payload]`。

`TcpFrameKind` 共 10 个值（C# 与 C++ 已对齐，见 `src/protocol/codec.hpp`）：

| 值 | 名称 | 用途 |
|---|---|---|
| 1 | `Hello` | 握手请求 |
| 2 | `HelloAck` | 握手应答 |
| 3 | `Packet` | 游戏包（载荷是 Lidgren 位流） |
| 4 | `Ping` | 心跳 |
| 5 | `Pong` | 心跳应答 |
| 6 | `Disconnect` | 断开 |
| 7 | `RequestWorldSnapshot` | 请求世界快照 |
| 8 | `RequestRocketSnapshot` | 请求火箭快照 |
| 9 | `ServerInfoRequest` | 登录前探测，不建立玩家会话 |
| 10 | `ServerInfoResponse` | 登录前探测应答（登录页 Ping / 人数） |

`ProtocolVersion = 32` —— C# `TcpFrameCodec.ProtocolVersion` 与 C++
`kProtocolVersion` 已对齐；登录握手拿它核对版本，改了就新旧客户端连不上。

`payloadBits` **允许不是 8 的倍数**：向量 `tcpframe.packet.bitodd33` 就是 33 位
（`bool` + `i32`），帧里仍带满 5 个字节，尾 7 位补零。

## 三、UDP 信封

`[kind:1][tokenLen:1][token][payload]`，其中 `UdpKind` 为 `Bind` / `BindAck` / `Data`。

注意：UDP 信封在 C# 侧**没有独立的公开编解码类**，它是内联在
`UdpStateTransport` 的发送路径里的，所以这边的向量是按那段代码转写的，
而不是调用某个公开 API。C++ 侧实现见 `src/protocol/codec.cpp`。

## 三之二、握手与服务器探测

两者都**不走 Lidgren 位格式**，而是 `BinaryWriter`/`BinaryReader` 风格：
整数小端定长，字符串是 `int32 UTF-8 字节长度` + UTF-8 原始字节。

C++ 侧：`src/protocol/handshake.hpp` / `.cpp`（对应 C# `SessionHandshakeCodec`）、
`src/protocol/server_info.hpp` / `.cpp`（对应 C# `ServerInfoWire`）。

**握手**：`magic(uint32) + version(uint32) + ...`，`magic` 常量值 `0x31465353`。
⚠️ 注意它在**线序上的字节是 `53 53 46 31`，即可读字符串 `SSF1` 而不是 `SFS1`**
（常量名看起来像 SFS1，但小端写出后是 SSF1）。按字节复刻，不要按常量名推。

| 方向 | 字段顺序 |
|---|---|
| Hello | `magic, version, username, password, solarSystemName, resumePlayerId(int32), resumeToken` |
| HelloAck | `magic, version, playerId(int32), updateRocketsPeriod(f64), chatMessageCooldown(f64), worldTime(f64), sendTime(f64), difficulty(u8), solarSystemName, udpSessionToken, resumeToken` |

`version = 2`。字符串长度上限 4096 字节（超限 C# 抛 `InvalidDataException`）。

**ServerInfoResponse**：固定 8 字节，两个小端 `int32` = `[当前人数][人数上限]`。
约束 `0 <= 当前人数 <= 人数上限`，`人数上限 >= 1`。

⚠️ **同一份 `JoinResponsePacket` 有两种线格式**，别混：
`HelloAck` 帧（上面这张表，带 `resumeToken`）走 BinaryWriter；
而登录后作为普通 `Packet` 帧发送时走 Lidgren 位流，
**不带 `resumeToken`**。两条路径都必须保留。

## 三之三、两份 Lidgren 的等价性（已实测）

服务端引用 `SpaceWizards.Lidgren.Network.dll`（经 `Server/Directory.Build.props`），
客户端用的是原生 `Lidgren.Network.dll`。两者命名空间同为 `Lidgren.Network` 但程序集名不同。

已用同一套用例分别导出并比对：

- `golden/primitives_native_v15.txt` = 原生 Lidgren 生成
- `golden/primitives.txt` = SpaceWizards 分支生成
- 两者 **SHA-256 完全相同**（`ccf9062a327b98fd` 开头），`sequences.txt` 同样完全一致。

结论：两份 Lidgren 位级等价，任一份导出的向量对另一侧都成立。

## 四、验证方法：黄金向量

**不要靠读 C# 源码猜格式**，也不要靠记忆。做法是用真实 Lidgren 生成标准答案：

```bash
# 1) 用真实 C# 实现导出黄金向量（三个文件：原语 / 序列 / 线格式）
cd Server/cpp/tools/VectorDump
dotnet run -c Release -- "D:/SFS-Multiplayer/Server/cpp/golden"

# 2) C++ 复现并逐字节比对
cd ../..
ZIG=D:/SFS-Multiplayer/tools/zig-x86_64-windows-0.16.0/zig.exe

"$ZIG" c++ -std=c++17 -O2 -o build/golden-conformance.exe \
  tests/golden_conformance.cpp src/net/lidgren_buffer.cpp
./build/golden-conformance.exe golden

"$ZIG" c++ -std=c++17 -O2 -Wall -Wextra -o build/wire-conformance.exe \
  tests/wire_conformance.cpp src/protocol/codec.cpp src/protocol/handshake.cpp \
  src/protocol/server_info.cpp src/net/lidgren_buffer.cpp
./build/wire-conformance.exe golden
```

当前状态（两者都必须 0 失败）：

| 测试 | 用例 | 结果 |
|---|---|---|
| `golden-conformance` | 73 原语 + 10 位偏移序列 + 不变量 | **通过 87 / 失败 0** |
| `wire-conformance` | 16 TCP 帧 + 6 UDP 信封 + 3 握手 + 往返/覆盖度自检 | **通过 53 / 失败 0** |

`VectorDump` 通过 `ProjectReference` 引用 `SfsMultiplayer.Protocol`，
所以生成的是**服务端真实协议实现**的输出。

> `Reference` 不能跨 `ProjectReference` 传递，`Server/Directory.Build.props` 也只作用于
> `Server/` 下的工程——所以 `VectorDump.csproj` 里必须**显式**再引一次
> `SpaceWizards.Lidgren.Network.dll`，否则 Protocol 里的 `Lidgren.Network` 类型解析不了。

`golden/` 下的向量文件是**生成物**，不要手工编辑。改了 Lidgren 版本或需要新增用例时，
改 `tools/VectorDump/Program.cs` 重新导出即可，两边的用例名必须保持一致。

**测试设计上的一条硬规矩**：`wire-conformance` 遇到不认识的向量名或未知分组一律**判失败**，
不静默跳过。另外会自检全部 10 个 `TcpFrameKind` 是否都被向量覆盖。
这样将来新增帧类型时，忘记同步 C++ 侧会立刻暴露，而不是悄悄通过。

## 五、工具链

本机没有 MSVC / g++ / clang / gcc，唯一可用编译器是 **zig 0.16.0**：

```
D:/SFS-Multiplayer/tools/zig-x86_64-windows-0.16.0/zig.exe
```

- 本机 Windows 目标、ARMv7 Linux 交叉编译均已实测可用（静态二进制约 1.34 MB）。
- 注意：zig 是原生 Windows 程序，MSYS 不做路径转换，`/tmp/...` 这类 MSYS 路径
  会失败，请用相对路径或 `D:/...` 形式。
