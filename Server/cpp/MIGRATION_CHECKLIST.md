# C# 协议层 → C++ 迁移清单

## 已完成 / 已对齐
- [x] `SessionHandshakeCodec.cs` → `src/protocol/handshake.hpp/.cpp` (握手 magic=0x31465353, version=2)
- [x] `ServerInfoWire.cs` → `src/protocol/server_info.hpp/.cpp` (ServerInfoRequest/Response)
- [x] `ProtocolPrimitives.cs` 基础枚举/常量 → `src/protocol/codec.hpp` (PacketType, TcpFrameKind, kProtocolVersion=32)

## 待转写（21 个源文件）

### 1. 基础类型
- [x] `ProtocolPrimitives.cs` → `src/protocol/types.hpp` (NetLocation, Color3, NetLocationExtensions 等)

### 2. 包模型
- [x] `PacketModels.cs` → `src/protocol/models/packets.hpp` (13 个包: JoinRequest/Response, PlayerConnected/Disconnected, UpdatePlayerControl/Authority, UpdateWorldTime, UpdatePlayerColor, SendChatMessage, CreateRocket, DestroyRocket, UpdateRocketPrimary/Secondary)
- [x] `PartPacketModels.cs` → `src/protocol/models/part_packets.hpp` (6 个部件包)
- [x] `DockingPacketModels.cs` → `src/protocol/models/docking.hpp` (DockTransaction)
- [x] `TimeWarpPacketModels.cs` → `src/protocol/models/timewarp.hpp` (TimeWarp)
- [x] `P2PPeerOfferPacket.cs` → `src/protocol/models/p2p.hpp` (P2PPeerOffer)
- [x] `ExperimentalAccessPacket.cs` → `src/protocol/models/experimental.hpp` (ExperimentalAccess)

### 3. 状态模型
- [x] `StateModels.cs` → `src/protocol/models/state.hpp` (RocketState, PartState, PartCollection, PartModuleState 等)

### 4. 策略 / 加载器（接口存根）
- [x] `SfsWorldLoader.cs` → `src/protocol/world_loader.hpp` (二进制存档格式)
- [x] `AuthorityAllocationPolicy.cs` → `src/protocol/policies/authority.hpp`
- [x] `ControlOwnershipPolicy.cs` → `src/protocol/policies/control_ownership.hpp`
- [x] `DebrisControlRules.cs` → `src/protocol/policies/debris.hpp`
- [x] `P2PProximityPolicy.cs` → `src/protocol/policies/p2p_proximity.hpp`
- [x] `TimeWarpControlRules.cs` → `src/protocol/policies/timewarp.hpp`
- [x] `RocketLatencyCompensation.cs` → `src/protocol/policies/latency_comp.hpp`

### 5. 传输层（替换现有极简版）
- [x] `TcpTransportCore.cs` → `src/net/tcp_transport.hpp/.cpp` (完整 TcpSendQueue + 帧收发 + 心跳 + 重传/可靠性)

## 验收标准
- 每个 `.hpp` 有对应的 `.cpp`，编译无 warning/error
- 所有包/状态的 Serialize/Deserialize 与 C# 逐字节一致（用现有 golden 向量自检）
- ARMv7 交叉编译通过
- 板端真实客户端能连上、握手、收到世界快照、火箭/部件同步、聊天