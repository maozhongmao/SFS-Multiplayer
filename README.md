# Spaceflight-simulator-Multiplayer

Spaceflight Simulator 的实时多人联机项目。公共服务器：stch.de5.net

本项目通过客户端模组和独立服务端，让多个玩家进入同一个 SFS 世界，实时同步玩家火箭及其状态，并提供多人联机所需的基础网络功能。

> 使用说明见 [项目维基](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/wiki)；开发进度与后续计划见 [更新计划](ROADMAP.md)。

客户端模组与服务端必须使用相互匹配的版本，各版本文件见 [Releases](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/releases)。

## 项目组成

- `Android/`：客户端联机补丁源码与安装器源码。
- `Client/`：客户端联机模组源码。
- `Plugin/`：独立联机服务端插件。
- `Server/`：独立联机服务端源码。
- `.gitignore`：垃圾文件。
- `.gitattributes`：垃圾文件。
- `LICENCE.txt`：本项目的 [MPL LICENSE](LICENSE.txt)；手机端的为 [GPL-3.0 LICENSE](Android/LICENSE.txt)。
- `README.md`：本项目的 [README](README.md)。
- `ROADMAP.md`：本项目的 [ROADMAP](ROADMAP.md)。

客户端模组安装在 SFS 的 `Mods` 目录中，服务端作为独立程序运行。客户端与服务端需要使用相互匹配的版本。

## 主要功能

- 多人进入同一个 SFS 世界。
- 玩家和火箭状态同步。
- 火箭创建、销毁和部件状态同步。
- 玩家控制权同步；服务端按玩家与火箭的关系分配状态更新权威，当前控制该火箭的玩家优先。
- 玩家之间直连（P2P）：服务器按玩家位置与网络端点撮合，配对成功后玩家之间使用 UDP 直连传输火箭状态；直连中断时自动回退到服务器中转。
- 火箭对接与解除对接同步与世界时间同步。
- KCP 网络传输；需要可靠送达的业务包与高频状态包按类型分流。
- 服务端权威处理关键多人事件。
- 太空垃圾自动清理：失去控制且部件数低于阈值的碎片会被自动移除，阈值可在服务端配置。
- 服务端插件系统（QuickJS）：服务端可加载预编译的 JS 字节码插件（SFS_PLUGINS=1 开启，默认关闭；关闭时产物与未引入前逐字节一致）。插件可读写日志、给指定玩家或全体发消息、读在线玩家名单，并接收世界快照/玩家进出/聊天/控制台命令等事件。

## 使用前提

- 正版 Spaceflight Simulator。
- `UITools` 前置模组。[这里下载](https://github.com/cucumber-sp/UITools/releases)
- 与服务端匹配的客户端模组和服务端程序。

## 基本使用方式

1. 安装 SFS 和 UITools 。
2. 将与服务端匹配的客户端模组放入游戏的 Mods 目录。
3. 启动对应版本的独立服务端，按机器架构选择文件，配置见同目录下的 server.yml 。
4. 在游戏的 Multiplayer 菜单中填写服务器地址和端口。
5. 输入用户名并加入服务器。

服务端提供 Windows（x64 / ARM64）与 Linux（x86-64 / ARM64 / ARMv7 32 位）版本，各平台功能与协议完全一致，可以互相联机。

本项目是独立开发的非官方多人联机工具，与 Spaceflight Simulator 官方团队及其权利人没有隶属、授权或合作关系。

## Star历史

感谢所有使用SFS-Multiplayer、报告漏洞、请求功能、测试构建并贡献修复的朋友们。

<a href="https://www.star-history.com/?repos=maozhongmao%2Fsfs-multiplayer%2Csfsplayer-sys%2Fspaceflight-simulator-multiplayermod%2Carctic-ice67%2Fspaceflightsimulator-multiplayermod-mobileandpc&type=date&legend=top-right">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=maozhongmao/sfs-multiplayer%2Csfsplayer-sys/spaceflight-simulator-multiplayermod%2Carctic-ice67/spaceflightsimulator-multiplayermod-mobileandpc&type=date&theme=dark&legend=bottom-right" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=maozhongmao/sfs-multiplayer%2Csfsplayer-sys/spaceflight-simulator-multiplayermod%2Carctic-ice67/spaceflightsimulator-multiplayermod-mobileandpc&type=date&legend=bottom-right" />
   <img alt="Star History Chart" src="https://api.star-history.com/chart?repos=maozhongmao/sfs-multiplayer%2Csfsplayer-sys/spaceflight-simulator-multiplayermod%2Carctic-ice67/spaceflightsimulator-multiplayermod-mobileandpc&type=date&legend=bottom-right" />
 </picture>
</a>

## 文档

- [项目维基](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/wiki)：项目结构、联机架构、网络协议、编译发布、部署运维与常见问题。
- [更新计划](ROADMAP.md)：接下来要更新的内容，以及已完成但尚未上传的部分。

## 开源协议

本项目**按目录分协议**，两套协议同时生效：

| 范围 | 协议 | 条款 |
|---|---|---|
| Android/（手机端联机补丁：注入层 libsfsmp.so、跳板 libmain.so、补丁安装器） | **GPL-3.0-or-later** | [Android/LICENSE](Android/LICENSE.txt) |
| 其余部分（Client/、Server/、Plugin/ 等） | **MPL-2.0** | [LICENSE.md](LICENSE.txt) |

- GPL 部分：分发二进制时必须一并提供对应源码；衍生作品同样以 GPL 发布。
- MPL 部分：修改过的 MPL 文件需以 MPL 公开其源码，可与闭源代码组合。

每个源码文件头部都带 `SPDX-License-Identifier` 标识，可据此判断单个文件适用哪份协议。

> 本仓库不包含 Spaceflight Simulator 的任何游戏源码、资源或游戏本体。手机端补丁是在用户自己的设备上，对已安装的正版游戏打补丁。

## 源码仓库

https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer

## 联系方式

- QQ Group: 679991439
- Email: stch-stuido@stch.de5.net

Copyright (c) 2026 STCH Studio
Developer: maozhongmao / yangchengtong
