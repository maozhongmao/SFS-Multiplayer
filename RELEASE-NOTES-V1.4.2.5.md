# 更新日志

## V1.4.2.5

**服务端插件系统（可用）**

- 插件系统不再是骨架：**服务端现在真的能跑 JavaScript 插件了**。插件用 `qjsc` 编译成**字节码**后下发，服务端只执行字节码 —— 没有源码求值入口。
- **沙箱边界**：没有文件、没有网络、没有进程线程、没有高精度计时；只能调用白名单 API 并接收事件。每个插件一份独立运行时，只占服务端主循环线程。
- **能力位**：读火箭 / 写火箭 / 发消息 / 写日志 / 管理类各占一位，插件必须显式声明，未声明的调用一律被拒绝。
- **预算与隔离**：每插件独立的内存上限、栈上限、每轮指令预算与 API 调用预算；超预算或抛异常会被标记为故障并停止投递，**不影响其他插件与服务端**。
- **关闭时零开销**：编译期 `SFS_PLUGINS=1` 打开，运行期 `SFS_PLUGINS=0` 可临时关掉；关闭编译开关时 quickjs 源码完全不参与构建，产物与未引入时**逐字节一致**。

**插件 API 与事件（按类别）**

| 类别 | API |
|---|---|
| 日志 | `sfs.log(text)` |
| 消息 | `sfs.sendMessage(playerId, text)`（`playerId < 0` 为全体广播）、`sfs.broadcast(text)` |
| 查询 | `sfs.players()`、`sfs.getWorldTime()`、`sfs.serverInfo()` |
| 管理 | `sfs.kick(id, reason)`、`sfs.clearDebris(maxParts)`、`sfs.setTimeScale(x)` |
| 火箭 | `sfs.readRocket(id)`、`sfs.writeRocket(view)`（**接口已就绪，服务端侧尚未开放**）|

- **事件**：`worldSnapshot`（每秒一次，含火箭与在线玩家）、`playerJoined` / `playerLeft`、`chatMessage`、`command`（服务端控制台命令）。
- **示例插件随仓库附带**：按类别五个（`Plugin/example_01_log.js` … `example_05_rocket.js`），外加综合示例（`Server/cpp/plugins/test_broadcast.js`，提供 `test` / `who` 两个命令）；注释即文档。

**仓库整理**

- `Client/`、`Plugin/`、`Server/` 三个顶层 README 全部重写：现状、目录结构、怎么构建、怎么用。
- 删除历史遗留：两个反编译产物目录（123 个 .cs 文件）与一个误入库的编译产物（418 KB 可执行文件）。
- 公开文档与代码注释中不再出现内网地址、设备型号、私有部署路径。

---

### 下载哪个版本

| 顺序 | 文件 | 说明 |
|---|---|---|
| 1 | `SFS-Multiplayer-1.5-V1.4.2.5.dll` | 客户端模组 · SFS **1.5**（Windows 7 等只能跑 1.5 的系统）|
| 2 | `SFS-Multiplayer-1.6-V1.4.2.5.dll` | 客户端模组 · SFS **1.6**（当前主流版本）|
| 3 | `SFS-Multiplayer-Server-windows-x64.exe` | 服务端 · Windows 64 位 |
| 4 | `SFS-Multiplayer-Server-linux-x64` | 服务端 · Linux 64 位（VPS 常见）|
| 5 | `SFS-Multiplayer-Server-linux-arm64` | 服务端 · Linux ARM64（树莓派 4+ 等）|
| 6 | `SFS-Multiplayer-Server-linux-armv7` | 服务端 · Linux ARMv7（musl 静态，低功耗设备）|
| 7 | `SFS-Multiplayer-Server-windows-arm64.exe` | 服务端 · Windows ARM64 |
| — | `SHA256SUMS.txt` | 以上文件的 SHA256 校验和 |

**客户端安装**：把 dll 放进 `Mods/SFS-Multiplayer-<游戏版本>-V1.4.2.5/` 目录。
**服务端运行**：直接执行对应平台的二进制，常用参数 `--port`、`--max-players`、`--state`；
想启用插件：`SFS_PLUGINS=1 SFS_PLUGIN_PATH=xxx.qbc ./sfs-server-... --port 9806`。
