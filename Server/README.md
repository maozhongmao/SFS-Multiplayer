<!--
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

# SFS Multiplayer Server 1.2.0-preview

Windows x64 独立实时联机服务端原型，面向 Spaceflight Simulator 的
Multiplayer-SFS/Lidgren 线协议。服务端不依赖游戏进程，不修改导入的 SFS 存档。

## 当前交付范围

已完成：

- 独立 UDP/Lidgren 服务端；
- 玩家握手、密码、重名、人数和输入边界校验；
- 世界快照、聊天、火箭/部件实时状态转发；
- 服务端分配玩家 ID 和火箭更新权限；
- 同一火箭互斥控制，原控制者释放后才允许接管；
- JSON 状态原子保存及 `.bak` 回退；
- 只读导入 SFS 1.5/1.6 世界；
- Windows x64 自包含单文件发布。

尚未完成：

- **SFS 1.6 客户端模组。** 仅运行这个 exe 不能让未安装兼容模组的原版
  SFS 1.6 加入服务器。
- 本机 1.6.00.16 安装的 `Spaceflight Simulator_Data/Managed` 目录只有一个
  指回自身目录的 `Managed.lnk`，没有 `Assembly-CSharp.dll` 等编译程序集；因此
  当前无法在不猜 API 的前提下构建和验证 1.6 客户端模组。
- 原 1.5 Multiplayer-SFS 客户端的 Lidgren DLL 已通过协议互通工具验证，但这不等于
  原 1.5 `Mod.dll` 可以直接装入 1.6。

## 直接运行

发布目录只有：

```text
SFS-Multiplayer-Server.exe
```

检查真实世界但不监听网络：

```text
SFS-Multiplayer-Server.exe --world "C:\path\to\World" --check
```

启动服务器：

```text
SFS-Multiplayer-Server.exe --world "C:\path\to\World" --port 9806
```

首次启动会只读导入 `WorldSettings.txt`、`Persistent/WorldState.txt` 和
`Persistent/Rockets.txt`。后续优先加载 exe 旁的 `data/server-state.json`，不会覆盖
原 SFS 世界。按 **Ctrl+C** 会先保存状态，再等待网络线程停止并退出。强制结束进程、
系统崩溃或断电不能保证最后一帧落盘；自动保存默认每 30 秒执行一次。

## 配置文件

复制仓库根目录的 `server.example.json`，例如：

```text
SFS-Multiplayer-Server.exe --config server.json
```

相对的 `WorldPath` 和 `StatePath` 以配置文件目录为基准。命令行参数覆盖配置：

```text
--world <目录>
--state <文件>
--port <1-65535>
--max-players <1-256>
--autosave <0-86400>
--check
--help
```

密码建议放在环境变量中，不写进 JSON 或命令行：

```text
SFS_SERVER_PASSWORD=你的密码 SFS-Multiplayer-Server.exe --config server.json
```

## 网络

- 默认 UDP 端口：`9806`
- Lidgren Application ID：`multiplayersfs`
- 公网使用时只放行实际选择的 UDP 端口；不要开放无关 TCP 端口。

## 构建与测试

需要 .NET SDK 8：

```text
dotnet restore SfsMultiplayer.sln
dotnet test SfsMultiplayer.sln --no-restore
dotnet publish src/SfsMultiplayer.Server/SfsMultiplayer.Server/SfsMultiplayer.Server.csproj \
  -c Release -r win-x64 --self-contained true \
  -p:PublishSingleFile=true \
  -o dist/SFS-Multiplayer-Server-0.1.1-win-x64
```

当前回归套件覆盖存档解析、真实 UDP 握手、密码拒绝、快照、实时同步、控制权、
越权拦截、持久化和网络线程安全关停。

## 许可证

本项目采用 Mozilla Public License Version 2.0（MPL-2.0）。第三方组件及固定版本见 `THIRD-PARTY-NOTICES.md` 和
`third_party/`。发布物不包含 Spaceflight Simulator 游戏文件或资产。
