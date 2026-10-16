# Plugin — 服务端插件示例（QuickJS）

这里放的是**服务端插件示例**：用 JavaScript 写、由 `qjsc` 编译成**字节码**，服务端加载执行。
插件运行在服务端沙箱里：**没有文件、没有网络、没有进程**，只能调用白名单 API 和接收事件。

> 完整说明写在综合示例的注释里：[\`Server/cpp/plugins/test_broadcast.js\`](../Server/cpp/plugins/test_broadcast.js)（注释即文档）。

## 分类示例（每个文件独立可用）

| 文件 | 类别 | 控制台敲什么 |
|---|---|---|
| `example_01_log.js` | 日志 | 不用敲（每 30 秒自动一行）|
| `example_02_message.js` | 消息 | `hi`（全体广播）、`dm`（私聊）|
| `example_03_query.js` | 查询 | `status`（在线人数 + 世界时间 + 服务端信息）|
| `example_04_admin.js` | 管理 | `clean`（清残骸）、`speed2`（2 倍速）、`boot`（踢第一个玩家）|
| `example_05_rocket.js` | 火箭 | 不用敲（每秒提示火箭 API 尚未开放）|

## 编译与运行

```bash
# 1) 编译成字节码 —— **必须加 -b**
#    不加 -b 出来的是 C 源码，服务端会报 engine rejected bytecode
qjsc -b -o example_04_admin.qbc example_04_admin.js

# 2) 起服务端并加载它
SFS_PLUGINS=1 SFS_PLUGIN_PATH=example_04_admin.qbc ./sfs-server-kcp-arm --port 9806

# 3) 在服务端控制台敲该插件支持的命令（见上表）
```

- `qjsc` 随 quickjs 源码一起编译，或用发布包里的同名工具；
- 插件路径也可用环境变量 `SFS_PLUGIN_PATH` 指定，默认是 `plugins/test_broadcast.qbc`。

## API 与事件（一句话版）

- **能调**：`sfs.log` ｜ `sfs.sendMessage`/`sfs.broadcast` ｜ `sfs.players`/`sfs.getWorldTime`/`sfs.serverInfo` ｜ `sfs.kick`/`sfs.clearDebris`/`sfs.setTimeScale` ｜ `sfs.readRocket`/`sfs.writeRocket`（后两个服务端尚未开放）
- **能收**：`worldSnapshot`（每秒一次）｜ `playerJoined`/`playerLeft` ｜ `chatMessage` ｜ `command`（控制台命令）
- **能力位**：读写火箭/发消息/写日志/管理类各占一位，插件必须**显式声明**，否则调用被拒绝。

## 两个坑（照做能省几小时）

1. **`-b` 出的字节码按模块作用域编译** ⇒ 函数声明不是全局，插件末尾必须写 `globalThis.onEvent = onEvent;`。
2. **插件命令名会被服务端内置命令截走**（内置优先：`help/status/say/kick/stop/players` …）⇒ 插件要换名字。
