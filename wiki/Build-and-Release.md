# 编译与发布

> 本篇只涉及**已发布版本**的构建方式。开发中的改动与实验性工程不在其列。

## 前置环境

| 用途 | 需要 |
| --- | --- |
| 客户端模组 | .NET SDK（客户端工程目标框架为 **net48**） |
| 服务端 | .NET SDK（版本以服务端工程文件为准） |
| 发布校验 | 计算 SHA-256 的工具（Windows：`certutil` / PowerShell `Get-FileHash`；Linux：`sha256sum`） |

> **客户端需要游戏侧程序集**：客户端模组与客户端测试引用 `Assembly-CSharp.dll`、`Assembly-CSharp-firstpass.dll`、`0Harmony.dll`、`Lidgren.Network.dll`、`UITools.dll`、`UnityEngine.*` 等游戏发行文件。
> 这些属于游戏本体内容，**不随本仓库分发**，需要你从本地游戏安装目录取得；编译时用 `-p:GameReferences=<目录>` 指定这些 DLL 所在目录即可：

```sh
dotnet build Client/src/MultiplayerSFS.Client/MultiplayerSFS.Client.csproj -c Release -p:GameReferences="<游戏程序集目录>"
```

> 客户端测试需要 `Client/refs/v15`、`Client/refs/v16`（SFS 1.5 / 1.6 的程序集）；`LegacyV323ConnectionSmoke` 用于与 SFS 1.5 时代的联机模组做协议互操作测试，需要额外用 `-p:LegacyV323References=<目录>` 指向那份旧模组二进制（见 `Server/THIRD-PARTY-NOTICES.md`）。

## 编译客户端模组

客户端模组是装进游戏 `Mods` 目录的 DLL，用 `dotnet build` 编译**编译产物**即可，**不通过 Unity 编辑器构建**（Unity 用于做游戏，不用来做模组）。

```sh
# 在客户端源码目录下
dotnet build Client/src/MultiplayerSFS.Client/MultiplayerSFS.Client.csproj -c Release
```

产物为单个模组 DLL（文件名含版本号）。发布归档时统一命名为：

```text
SFS-Multiplayer-mod-1.6-<版本>.dll        # 例：SFS-Multiplayer-mod-1.6-V1.1.4.5.dll
```

安装方式：把该 DLL 放进游戏的 `Mods` 目录。**前提是已安装 Mod Loader 与 `UITools` 前置模组。**

## 编译服务端

服务端为**自包含单文件**发布（`PublishSingleFile` + `SelfContained`），不依赖目标机器的 .NET 运行时：

```sh
dotnet publish Server/src/SfsMultiplayer.Server/SfsMultiplayer.Server/SfsMultiplayer.Server.csproj \
  -c Release -r <RID> --self-contained true -p:PublishSingleFile=true
```

按目标平台分别发布：Windows（x64 / ARM64）与 Linux（x86-64 / ARM64 / ARMv7 32 位）。

- 发布目录里除可执行文件外，还需要同目录的 **`server.yml`**（服务端配置，见 [部署与运维](Deployment)）；
- Linux 版可执行文件没有扩展名，这是 Linux 惯例（系统按文件头识别），下载后需先赋予执行权限：

```sh
chmod +x SFS-Multiplayer-Server-linux-<arch>
./SFS-Multiplayer-Server-linux-<arch>
```

## 版本号规则

- **新增一个功能**：版本号第三段 +1（例如 `V1.1.4` → `V1.1.5`）；
- **修复一个缺陷**：版本号第四段 +1（例如 `V1.1.4` → `V1.1.4.1`）；
- 每个版本使用**独立目录/独立产物**，不覆盖既有版本，便于回退；
- 客户端模组与服务端**必须同版本**，发布时一并说明。

## 发布产物命名

| 产物 | 命名 |
| --- | --- |
| 客户端模组 | `SFS-Multiplayer-mod-1.6-<版本>.dll` |
| Windows 服务端 | `SFS-Multiplayer-Server-windows-x64.exe` / `-windows-arm64.exe` |
| Linux 服务端 | `SFS-Multiplayer-Server-linux-x64` / `-linux-arm64` / `-linux-armv7` |

各平台功能与协议完全一致，可以互相联机；发布页面同时给出各文件的 **SHA-256**，供下载后核对。

## 发布流程（推荐做法）

1. **编译**：按上面的命令分别产出客户端模组与各平台服务端；
2. **校验**：对每个产物计算 SHA-256 并记录；
3. **原样上传**：Release 中上传**构建出的原始文件**，不重新打包、不改动二进制；
4. **旧版本保留**：历史版本文件独立保留，不要删除或覆盖，方便玩家按版本回退；
5. **写发布说明**：用完整语义版本号（例如 `V1.1.4`），并列出本版**真实**的新增与修改内容；
6. **核对**：发布后再次核对文件大小与 SHA-256，并验证下载链接可用（必要时用真实客户端/服务端做一次联机验证）。

## 常见问题

- **客户端装了却没生效**：`Mods` 目录里存在**多份**同模组 DLL 会导致重复补丁，请只保留一份；
- **客户端连不上/握手失败**：先检查模组与服务端版本是否匹配；
- **Linux 服务端起不来**：确认已 `chmod +x`，并确认 `server.yml` 与可执行文件在同一目录；
- **端口被占用**：确认没有残留的服务端进程占用同一端口。
