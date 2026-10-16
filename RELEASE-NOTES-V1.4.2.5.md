# 更新日志 · V1.4.2.5

> 上一个公开版本：**V1.3.5**。本版把 V1.4 以来的全部改动 + **服务端插件系统**一起发布。

## 新功能

### 服务端插件系统（QuickJS）

服务端现在可以加载 **JavaScript 插件**。插件由 `qjsc` 编译成**字节码**后下发，服务端只跑字节码 —— 没有源码求值入口，也不给文件、网络、进程。

- **编译期开关 `-DSFS_PLUGINS=1`**；运行期可用 `SFS_PLUGINS=0` 关掉。
- **关闭时零开销**：quickjs 源码完全不参与构建，产物与引入前**逐字节一致**（已实测比对 sha256）。
- 每个插件一份独立运行时，单线程、只在服务端主循环执行；超指令预算或抛异常会被标记 `faulted` 并停止投递，**不影响其他插件与服务端**。
- 插件路径由 `SFS_PLUGIN_PATH` 指定（默认 `plugins/test_broadcast.qbc`）。

**插件可调用的 API（按类别）**

| 类别 | API |
|---|---|
| 日志 | `sfs.log(text)` |
| 消息 | `sfs.sendMessage(playerId, text)`（<0 = 全体）、`sfs.broadcast(text)` |
| 查询 | `sfs.players()`、`sfs.getWorldTime()`、`sfs.serverInfo()` |
| 管理 | `sfs.kick(id, reason)`、`sfs.clearDebris(maxParts)`、`sfs.setTimeScale(x)` |
| 火箭 | `sfs.readRocket(id)`、`sfs.writeRocket(view)`（**接口已就绪，服务端侧尚未开放**）|

**插件能收到的事件**：`worldSnapshot`（每秒一次，含火箭/玩家）· `playerJoined` / `playerLeft` · `chatMessage` · `command`（控制台命令）。

**能力位**：读/写火箭、发消息、写日志、管理类各占一位 —— 插件必须显式声明，否则调用被拒绝。

**示例插件**（每类一个文件，注释即文档）：`Plugin/example_01_log.js` … `example_05_rocket.js`，
综合示例 `Server/cpp/plugins/test_broadcast.js`（命令 `test` / `who`）。

### 服务端配置与日志

- **JSON 配置文件**：默认 `data/server-config.json`，优先级 命令行 > 环境变量 > 配置文件 > 内置默认值。
- **作弊开关策略**：总开关决定客户端是否显示作弊入口，另有十项独立开关逐项放行/拦截；不配置 = 全开 = 与之前一致。
- **日志分类与级别**：可按 net / world / p2p / plugin / config / error 分类开关，支持 `--log-level`、`--log-color` 等覆盖；输出到终端时彩色，重定向到文件自动关闭。

### 其它

- **P2P 直连通道**：信令撮合 + 距离阈值切换（中继仍然可用）。
- 客户端 **作弊策略**：完全按服务端策略显示/隐藏/拦截（单机与未连服时行为不变）。

## 修复

- **联机世界里看不到作弊入口**：我们自己的暂停菜单补丁取代了游戏原生菜单，原版"无条件创建入口"的代码从未执行。现在在自绘菜单里按原版方式补回（策略要求隐藏时不加）。
- **作弊开关"点了数值生效、显示却不变（一直灰着）"**：联机世界 `worldBase.paths` 为空，`SandboxSettings.OnToggle` 第一步写盘抛空引用，界面刷新被整段跳过；现在路径为空时自己走完刷新链并跳过会抛异常的原方法（单机不变）。
- **SFS 1.5 客户端包编译失败**（1.6 独有类型在 1.5 不存在）。
- 服务端 `frames.cpp` 引用未声明的包类型别名导致编译失败。

## 仓库整理

- 目录与文档：`Client/`、`Plugin/`、`Server/` 三个顶层 README 全部重写（现状、目录、怎么构建、怎么用）。
- 删除历史遗留：两个反编译产物目录（123 个 .cs）与一个误入库的编译产物（418KB exe）。
- 公开文档与注释里不再出现内网地址、设备型号、私有路径等部署细节。

## 发布资产

| 文件 | 说明 |
|---|---|
| `SFS-Multiplayer-1.6-V1.4.2.5.dll` | 客户端模组（游戏 1.6）|
| `SFS-Multiplayer-1.5-V1.4.2.5.dll` | 客户端模组（游戏 1.5，老电脑）|
| `SFS-Multiplayer-Server-windows-x64.exe` | 服务端（Windows x64）|
| `SFS-Multiplayer-Server-windows-arm64.exe` | 服务端（Windows arm64）|
| `SFS-Multiplayer-Server-linux-x64` | 服务端（Linux x64）|
| `SFS-Multiplayer-Server-linux-arm64` | 服务端（Linux arm64）|
| `SFS-Multiplayer-Server-linux-armv7` | 服务端（Linux armv7 / musl，低功耗设备）|
| `SHA256SUMS.txt` | 全部资产的 SHA256 |

### 安装

- **客户端**：把 dll 放进 `Mods/SFS-Multiplayer-<游戏版本>-V1.4.2.5/` 目录。
- **服务端**：直接运行对应平台的二进制；`--port`、`--max-players`、`--state` 常用参数见 README。
  想开插件：`SFS_PLUGINS=1 SFS_PLUGIN_PATH=xxx.qbc ./sfs-server-... --port 9806`
