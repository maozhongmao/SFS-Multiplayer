# 项目总览

## 这是什么

SFS Multiplayer 让多名玩家进入**同一个 Spaceflight Simulator 1.6 世界**：彼此的火箭、部件、控制权、对接状态与世界时间都实时同步，并配套提供聊天、玩家列表、服务器信息等联机基础功能。

它是**独立开发的非官方联机工具**，与 Spaceflight Simulator 官方团队及其权利人没有隶属、授权或合作关系；不修改游戏本体文件，也不会改动玩家已有的 SFS 存档。

## 它由什么组成

| 部分 | 位置 | 作用 |
| --- | --- | --- |
| 客户端模组 | [`Client/`](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/tree/main/Client) | 以 DLL 形式装入游戏的 `Mods` 目录，在游戏内接入联机逻辑 |
| 独立服务端 | [`Server/`](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/tree/main/Server) | 独立进程，不依赖游戏进程；负责会话、世界状态与权威裁决 |
| 发布产物 | [Releases](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/releases) | 按版本归档的成品：客户端模组 DLL，以及 Windows / Linux 各架构的服务端可执行文件 |
| 安全策略 | [`SECURITY.md`](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/blob/main/SECURITY.md) | 漏洞报告方式与安全约定 |
| 许可证 | [`LICENSE.md`](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/blob/main/LICENSE.md) | MPL-2.0 |

## 一次联机里，谁在做什么

```text
玩家 A 的游戏 ─┐
               ├── 客户端模组 ── 网络 ── 独立服务端（世界状态与权威）
玩家 B 的游戏 ─┘                     │
                                     └── 玩家之间可建立直连通道（P2P），
                                         直连建立后两人之间的火箭状态不再绕经服务端
```

- **客户端模组**负责采集本机玩家的火箭与部件状态、上报给服务端，并把外来状态应用到游戏对象上；
- **独立服务端**负责会话管理（握手、玩家 ID、人数与输入边界校验）、维护世界状态（火箭、部件、阶段、控制权、时间），并按玩家与火箭的关系分配**状态更新权威**；
- **玩家直连（P2P）**在配对成功后直接在两名玩家之间传输火箭状态；直连中断会自动回退到服务端中转，界面会显示当前通道状态。

## 使用前提

- **Steam 版 Spaceflight Simulator 1.6**；
- 支持 SFS 1.6 的 **Mod Loader**；
- **`UITools` 前置模组**（客户端模组的界面依赖它）；
- **相互匹配**的客户端模组与服务端程序：版本不匹配会出现握手失败或行为异常。

## 玩家视角的最小流程

1. 安装 SFS 1.6、Mod Loader 与 `UITools`；
2. 把与服务端匹配的客户端模组 DLL 放进游戏的 `Mods` 目录；
3. 启动服务端（见 [部署与运维](Deployment)）；
4. 游戏内 Multiplayer 菜单填写服务器地址与端口；
5. 输入用户名加入，房间内即可看到其他玩家及其火箭。

## 服务端可以跑在哪

服务端是独立程序，与游戏进程无关。已发布版本提供以下架构：Windows（x64 / ARM64）、Linux（x86-64 / ARM64 / ARMv7 32 位）。各平台**功能与协议完全一致，可以互相联机**；ARMv7 版本面向低内存设备，部署注意事项见 [部署与运维](Deployment)。

## 版本与兼容性

- 客户端模组与服务端**必须同版本**；
- 同一版本内，各平台服务端之间可以互相联机；
- 升级时请同时替换客户端模组与服务端程序，不要混用不同版本。

## 下一步

- 想了解模块划分与数据流 → [联机架构](Architecture)
- 想自己编译一份 → [编译与发布](Build-and-Release)
- 想了解网络报文 → [网络协议](Protocol)
