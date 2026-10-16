# Client — 客户端联机模组

Spaceflight Simulator 的联机客户端模组（C# / .NET Framework 4.8，用 Harmony 打补丁、ModGUI 画界面）。
与 [`Server/`](../Server/) 配套：客户端不做权威判定，世界的"真相"在服务端。

## 支持的游戏版本

同一个工程出两个包（`AssemblyName` 按版本区分）：

| 游戏版本 | 构建方式 | 产物 |
|---|---|---|
| **1.6**（默认） | `dotnet build ... -c Release` | `SFS-Multiplayer-1.6-V<版本>.dll` |
| **1.5**（老电脑） | `dotnet build ... -c Release -p:GameVersion=v15` | `SFS-Multiplayer-1.5-V<版本>.dll` |

1.5 缺少 1.6 独有的传送菜单/解锁按钮等类型，相关代码用 `#if !SFS15` 隔开。

## 目录

```
Client/
├─ src/MultiplayerSFS.Client/
│   ├─ MultiplayerSFS.Common/        协议与线格式（每个包的 Serialize/Deserialize）
│   ├─ MultiplayerSFS.Common.Kcp/    纯 C# 的 KCP 实现（UDP 可靠传输）
│   ├─ MultiplayerSFS.Mod/           模组主体（连接管理、世界/火箭同步、插值、调试面板）
│   └─ MultiplayerSFS.Mod.Patches/   Harmony 补丁（对游戏代码的挂钩）
├─ tests/                            测试
└─ refs/                             游戏本体的参考程序集（**版权物，不入库**）
```

## 构建

```bash
# 1.6 包（默认）
dotnet build Client/src/MultiplayerSFS.Client/MultiplayerSFS.Client.csproj -c Release

# 1.5 包
dotnet build Client/src/MultiplayerSFS.Client/MultiplayerSFS.Client.csproj -c Release -p:GameVersion=v15
```

构建需要 `refs/` 下的游戏程序集（自行从游戏安装目录拷入，不随仓库分发）。
完整发布（两个包 + 服务端资产 + 校验和）用 `Server/cpp/build-release.ps1`。

## 安装

把产出的 dll 放到游戏的
`Mods/SFS-Multiplayer-<游戏版本>-<模组版本>/SFS-Multiplayer-<游戏版本>-<模组版本>.dll`。

## 客户端做了什么

- **连接与握手**：UDP/KCP，会话恢复、断线重连、心跳与 RTT 统计；
- **世界同步**：接收服务端快照、按权威分配同步火箭状态、轨迹重建（定延迟插值，不外推）；
- **玩家交互**：玩家列表、控制权认领、时间加速同步；
- **作弊策略**：按服务端下发的策略显示/隐藏作弊入口、逐项放行或拦截（单机行为不变）；
- **调试面板**：游戏内按 **F8** 打开（性能日志、协议层追踪、状态显示）。

## 注意

- 日志里**只允许 ASCII**（游戏字体没有中文字形，非 ASCII 会变成方块）；
- `Client/refs/`、`Client/release*/`、`tests/` 之外的构建产物**不入库**。
