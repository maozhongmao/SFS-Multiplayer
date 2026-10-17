# Spaceflight-simulator-Multiplayer

Spaceflight Simulator 的实时多人联机项目。公共服务器：stch.de5.net

本项目通过客户端模组和独立服务端，让多个玩家进入同一个 SFS 世界，实时同步玩家火箭及其状态，并提供多人联机所需的基础网络功能。

> 使用说明见 [项目维基](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/wiki)；开发进度与后续计划见 [更新计划](ROADMAP.md)。

客户端模组与服务端必须使用相互匹配的版本，各版本文件见 [Releases](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/releases)。

## 项目组成

- `Client/`：客户端联机模组源码。
- `Plugin/`：独立联机服务端插件。
- `Server/`：独立联机服务端源码。
- `.gitignore`：Git忽略文件配置。
- `LICENCE.md`：本项目的 [MPL LICENSE](LICENSE.md)。
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
- **服务端插件系统（QuickJS）**：服务端可加载预编译的 JS 字节码插件（`SFS_PLUGINS=1` 开启，默认关闭；关闭时产物与未引入前逐字节一致）。插件可读写日志、给指定玩家或全体发消息、读在线玩家名单，并接收世界快照/玩家进出/聊天/控制台命令等事件。

## 使用前提

- 正版 Spaceflight Simulator。
- `UITools` 前置模组。
- 与服务端匹配的客户端模组和服务端程序。

## 基本使用方式

1. 安装 SFS 和 `UITools`。
2. 将与服务端匹配的客户端模组放入游戏的 `Mods` 目录。
3. 启动对应版本的独立服务端，按机器架构选择文件，配置见同目录下的 `server.yml`。
4. 在游戏的 Multiplayer 菜单中填写服务器地址和端口。
5. 输入用户名并加入服务器。

服务端提供 Windows（x64 / ARM64）与 Linux（x86-64 / ARM64 / ARMv7 32 位）版本，各平台功能与协议完全一致，可以互相联机。

本项目是独立开发的非官方多人联机工具，与 Spaceflight Simulator 官方团队及其权利人没有隶属、授权或合作关系。

## 文档

- [项目维基](https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer/wiki)：项目结构、联机架构、网络协议、编译发布、部署运维与常见问题。
- [更新计划](ROADMAP.md)：接下来要更新的内容，以及已完成但尚未上传的部分。

## 开源协议

本项目使用 MPL License。详细条款请查看 [LICENSE.md](LICENSE.md)。

## 源码仓库

https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer

## 联系方式

- QQ Group: 679991439
- Email: stch-stuido@stch.de5.net

Copyright (c) 2026 STCH Studio
Developer: maozhongmao / yangchengtong
