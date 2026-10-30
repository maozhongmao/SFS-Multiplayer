# 更新日志

## V2.0.2（手机端）

**新功能**

- **手机端（Android）联机**：官方 SFS 1.6.00.22 与 PC 玩家进入同一个世界，互相能看到对方的火箭、朝向与运动。手机端与服务端之间使用 KCP over UDP，协议与 PC 端同源。
- **补丁安装器（共存模式）**：安装器在手机上就地给**你自己已安装的官方游戏**打补丁（改写跳板 dex、注入 Bridge/Ticker、追加注入层、重签名），并把包名改成 `com.sfsmp.sfs` 与官方版共存。不覆盖官方、不需要 root、不需要电脑，也**不重新分发游戏本体**。
- **日志落盘（无需 ADB）**：安装器与补丁版游戏的运行日志写入媒体目录，出问题可直接从文件管理器导出：
  - 安装器：`Android/media/com.sfsmp.patcher/log.txt`（含安装会话状态码与异常堆栈）
  - 补丁版游戏：`Android/media/<运行时包名>/log-java.txt`、`log-native.txt`
- **IPv6 支持**：手机端连接地址支持 `[2001:db8::1]:9806`、纯 IPv6（默认端口）、`host:port`、纯 IPv4；逐个候选地址尝试，任一成功即连上。

**同步**

- **传输层与 PC / 服务端完全对齐**（依据服务端 `kcp_transport.hpp` 与 ADR-001）：
  - 显式 MTU 1400、接收窗口 256/512、`nodelay(1, 10, 0, 1)` 与服务端取值一致；
  - 更新调度改为按 `ikcp_check` 返回值驱动，发送后立即 flush（与服务端事件循环同款策略）。
- **位姿同步与 PC 端对齐**：上行主状态包按 50 ms 间隔发送；下行按 120 ms 延迟做插值采样（环形缓冲 + 角度最短弧插值），超过 2 m 偏差做硬对齐，其余走速度追逐，避免与本地物理打架。
- **服务端平台识别**：`Hello` 握手新增可选字节 `platform`（缺失/0 = PC，1 = Android），老客户端不受影响；`players` 指令据此标注 `[pc]` / `[android]` 并统计两端人数，同时对手机端直接丢弃 P2P 报价，避免 PC 发起方空等。

**使用提示**

- 用 adb 安装补丁包请加 `--no-incremental`（增量安装会导致启动时 `dlopen … Timer expired`）。
- 补丁版为共存包（`com.sfsmp.sfs`），与官方游戏可同时安装；请勿卸载重装，以免丢失 `files/sfsmp-server.txt` 里的服务器地址。

---

### 下载哪个版本

按需下载，**客户端、手机端、服务端必须配套使用**（同为 V2.0.2）。

#### 一、手机端 · 补丁安装器（Android 10+）

| 文件 | 体积 | 适用 |
|---|---|---|
| SFS-Multiplayer-2.0.2-armeabi-v7a.apk | 0.29 MB | 32 位 ARM 设备，兼容性最好 |
| SFS-Multiplayer-2.0.2-arm64-v8a.apk | 0.28 MB | 64 位 ARM 设备，速度最快（绝大多数手机选这个）|

> 两个包只带各自架构的注入库，安装器会自动校验设备架构，装错会直接提示换包。

#### 二、PC 客户端 · 模组

| 文件 | 体积 | 适用 |
|---|---|---|
| SFS-Multiplayer-1.5-V2.0.dll | 0.46 MB | SFS 1.5（Windows 7 等只能跑 1.5 的系统）|
| SFS-Multiplayer-1.6-V2.0.dll | 0.46 MB | SFS 1.6（当前游戏版本）|

> 放进游戏目录下的 `Mods` 文件夹即可，需要 0Harmony 前置。

#### 三、服务端 · Linux

| 文件 | 体积 | 适用 |
|---|---|---|
| SFS-Multiplayer-Server-linux-arm64 | 0.76 MB | 树莓派 4+ / 现代 ARM 服务器 |
| SFS-Multiplayer-Server-linux-armv7 | 0.68 MB | ARMv7 32 位开发板 |
| SFS-Multiplayer-Server-linux-x64 | 0.77 MB | 云服务器 / VPS（x86-64）|

> 赋予执行权限后直接运行：`chmod +x 文件名 && ./文件名 --port 9806`

#### 四、服务端 · Windows

| 文件 | 体积 | 适用 |
|---|---|---|
| SFS-Multiplayer-Server-windows-arm64.exe | 0.62 MB | Windows ARM64 |
| SFS-Multiplayer-Server-windows-x64.exe | 0.72 MB | Windows x86-64（普通电脑）|

> 首次运行会在同目录生成 `data/` 存档；防火墙需放行 UDP 9806。

> 客户端、手机端与服务端必须配套使用。
> 手机端补丁只对 **SFS 1.6.00.22 (936)** 生效，官方更新后需等适配。
