# 服务端插件系统（QuickJS）

> 状态：**已可用**（2026-10-16）。服务端编译期开关 `SFS_PLUGINS=1` 打开；关闭时二进制与未引入前**逐字节一致**。

插件是**预编译的 JS 字节码**（`.qbc`），由服务端在**主循环线程**上执行。插件没有文件、网络、进程，也没有源码求值入口 —— 它能做的只有下面白名单里的几件事。

---

## 1. 五分钟写一个插件

插件**就是一个函数** `onEvent(event)`：收到事件 → 判断 → 调 API。

```js
// my_plugin.js
function onEvent(event) {
  if (event.kind === "command" && event.command === "hello") {
    sfs.broadcast("hello from plugin");
  }
}
globalThis.onEvent = onEvent;   // ← 必须：见"编译"里的说明
onEvent;
```

编译成字节码（**必须是 `-b`**）并用它启动服务端：

```bash
qjsc -b -o my_plugin.qbc my_plugin.js          # -b = 输出原始字节码（不加就是 C 源码，加载必失败）
SFS_PLUGINS=1 SFS_PLUGIN_PATH=my_plugin.qbc ./sfs-server-kcp-arm --port 9806
```

启动后在服务端控制台敲 `hello` 即可。

---

## 2. 插件能调用的 API（`sfs.*`）

| API | 能力位 | 说明 |
|---|---|---|
| `sfs.log(text)` | `log` = `1<<3` | 写服务端日志（非 ASCII 会被替换成 `?`；≤255 字节）|
| `sfs.sendMessage(playerId, text)` | `sendMessage` = `1<<2` | 发给**指定玩家**（`playerId < 0` 表示全体广播）|
| `sfs.broadcast(text)` | `sendMessage` | 等价于 `sendMessage(-1, text)`，**全体广播** |
| `sfs.players()` | 无（数据来自快照）| 返回最近一次世界快照里的在线玩家数组 `[{playerId, controlledRocket, name}]` |
| `sfs.readRocket(rocketId)` | `readRocket` = `1<<0` | 读一枚火箭（**服务端侧尚未开放，当前总返回失败**）|
| `sfs.writeRocket(view)` | `writeRocket` = `1<<1` | 写回火箭（**服务端侧尚未开放**）|

**能力位**：插件在描述符里声明要用的能力；**没声明就调用会返回 `-1`**。这样能防住"插件偷偷干别的"。

---

## 3. 插件能收到的事件

| `event.kind` | 时机 | 字段 |
|---|---|---|
| `"worldSnapshot"` | **每秒一次** | `tick` `worldTime` `rockets[]` `players[]` `droppedRockets` |
| `"playerJoined"` / `"playerLeft"` | 玩家进出 | `player.playerId` `player.name` |
| `"chatMessage"` | 玩家聊天 | `text`、`player.playerId` |
| `"command"` | **服务端控制台命令** | `command`（命令名）、`text` |

事件是**批量、每秒一次**的（不会按帧回调）。**不要指望单个事件带全部信息**：例如玩家名单只在 `worldSnapshot` 里，
命令事件里没有 —— 常见写法是"快照时记下来、命令时用"。

---

## 4. 我们踩过的坑（照做能省几小时）

1. **`qjsc` 默认输出 C 源码，不是字节码** ✗
   ⇒ 必须加 **`-b`**，否则宿主报 `engine rejected bytecode`（而且文件头是 `/* File generated…` 一眼可辨）。
2. **`-b` 出的字节码按模块作用域编译** ✗
   ⇒ 函数声明**不会**自动成为全局，宿主取不到入口 ⇒ 插件末尾必须写 `globalThis.onEvent = onEvent;`（再补一句 `onEvent;` 兼容两种取法）。
3. **宿主的 `HostApi` 必须在插件注册之后再交给引擎**（服务端内部实现细节，插件作者不用管，但改宿主的人会踩）✗
   ⇒ 早于注册时 `ctx = nullptr`，插件所有 `sfs.*` 调用**静默失败**：不报错、不输出，极难排查。
4. **插件命令会被服务端内置命令截走** ✗
   ⇒ 内置优先（`help/status/say/kick/stop/players` …），插件要换名字。想要插件优先需另设计。
5. **服务端日志/输出是行缓冲的** ✗ ⇒ 调试插件时若看不到输出，先确认有没有 `fflush`（宿主侧已处理）。

---

## 5. 沙箱边界（有意为之，别指望放开）

| 不能做 | 原因 |
|---|---|
| 文件读写 / 网络 / 进程线程 | 插件是"可放心的第三方代码"，服务器跑在别人家里 |
| `eval` / 执行源码字符串 | 宿主只接受字节码，板上不做 JS 解析 |
| 高精度计时 | 防止用时间侧信道做奇怪的事 |
| 任意改世界 | 只能通过受限的 `writeRocket`（限流 + 数值校验：NaN/Inf 拒绝）|

每个插件一份独立 `JSRuntime`，超预算/抛异常会被标记 `faulted` 并停止投递，**不影响其他插件与服务端**。

---

## 6. 目录与开关

| 东西 | 位置 |
|---|---|
| 宿主 / 引擎 | `Server/cpp/src/plugins/`（`plugin_host.*` 宿主、`quickjs_engine.hpp` 引擎、`plugin_app.cpp` 服务端适配）|
| 协议契约 | `Server/cpp/src/protocol/PLUGIN_API.md` |
| 示例插件 | `Server/cpp/plugins/test_broadcast.js`（命令 `test` / `who`）|
| quickjs-ng | `Server/cpp/third_party/quickjs/`（MIT，见该目录 `VENDORED.txt`）|
| 编译开关 | `build-server-exp.ps1` 加 `$env:SFS_PLUGINS=1`；运行期 `SFS_PLUGINS=0` 可临时关掉 |
| 插件路径 | 环境变量 `SFS_PLUGIN_PATH`（默认 `plugins/test_broadcast.qbc`）|
