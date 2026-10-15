# PLUGIN_API.md — 插件可见的宿主 API 契约（v0 骨架，未接线）

> 状态：**契约已冻结，实现未接线**。C++ 侧类型见 `../plugins/plugin_host.hpp`；
> QuickJS 绑定在 `../plugins/quickjs_engine.hpp` 里是 TODO（任务 #12 只做骨架）。
> 本文是插件（JS 字节码）与宿主之间的唯一接口定义，接线时以此为准。

## 1. 装载约定

- 插件以**预编译字节码**（quickjs-ng `.qbc`）交付；宿主没有源码求值入口。
- 每个插件一个独立 `JSRuntime`/`JSContext`，单线程，只由服务端主循环线程执行。
- 描述符 `PluginDescriptor`：`id`（ASCII `[A-Za-z0-9_.-]`，1..32）、`capabilities` 位图、
  `limits`（内存/栈/指令/API 调用）、`bytecode`（≤ 32 KB）。
- 字节码求值结果必须是**函数**（见 §3），否则装载失败（`engine rejected bytecode`）。

## 2. 全局对象 `sfs`（能力白名单）

只有以下函数会被注册；QuickJS 的 `std`/`os`/`bignum` 模块一律不注册。
调用返回整数状态码（§4）：`0` 成功，负数拒绝。

| 函数 | 需要能力位 | 参数边界 | 返回 |
|---|---|---|---|
| `sfs.readRocket(rocketId)` | `readRocket` | `rocketId >= 0` 整数 | 成功返回 `RocketView` 对象，否则负数 |
| `sfs.writeRocket(view)` | `writeRocket` | `rocketId>=0`、`partCount 0..4096`、`throttle 0..1`、坐标/速度必须是有限数；每轮 ≤ 64 次 | `0` / 负数 |
| `sfs.sendMessage(playerId, text)` | `sendMessage` | `playerId>=0`、无控制字符、UTF-8 长度 ≤ 255 字节 | `0` / 负数 |
| `sfs.log(text)` | `log` | 长度 ≤ 255 字节；非 ASCII 字符会被替换为 `?` 后进日志 | `0` / 负数 |

能力位名称（描述符里的 `capabilities`）：`readRocket`=`1<<0`、`writeRocket`=`1<<1`、
`sendMessage`=`1<<2`、`log`=`1<<3`。未声明的能力调用一律返回 `-1`。

### RocketView 对象

```json
{ "rocketId": 123, "ownerPlayer": 4, "controlledBy": 4, "partCount": 12,
  "posX": 0, "posY": 0, "posZ": 0, "velX": 0, "velY": 0, "velZ": 0,
  "throttle": 0.0, "hasUpdateAuthority": 1 }
```

写回时只有上表字段有效；`rocketId` 必须已存在，否则返回 `-3`。

## 3. 事件契约（批量，每秒一次）

字节码求值结果必须是函数 `onEvent(event)`，宿主在每次投递时调用它。
**同一个事件对象里已经批量装好整个世界**，插件不要按帧轮询。

```json
{ "kind": "worldSnapshot", "tick": 1024, "worldTime": 33.5,
  "droppedRockets": 0,
  "rockets": [ /* RocketView[] */ ],
  "players": [ { "playerId": 4, "controlledRocket": 123, "name": "alice" } ] }
```

| `kind` | 触发 | 载荷 |
|---|---|---|
| `worldSnapshot` | 每秒 1 次 | `tick`/`worldTime`/`droppedRockets`/`rockets`/`players` |
| `playerJoined` | 玩家加入 | `player` 对象 |
| `playerLeft` | 玩家离开 | `player` 对象 |
| `chatMessage` | 聊天 | `playerId` + `text`（UTF-8，≤255 字节） |

单轮投递的指令预算耗尽或抛出未捕获异常时，宿主把插件置为 `faulted` 并停止投递；
`faulted` 是终态，需控制台重新装载。

## 4. 状态码（`HostStatus`）

| 值 | 名称 | 含义 |
|---|---|---|
| `0` | `ok` | 成功 |
| `-1` | `no-capability` | 描述符里没有声明该能力位 |
| `-2` | `bad-argument` | 参数越界/非法数值/超长 |
| `-3` | `not-found` | 火箭或玩家不存在 |
| `-4` | `budget-exhausted` | 本轮 API 调用预算（默认 1024）或写次数（64）用尽 |
| `-5` | `unavailable` | 服务端未安装 `HostServices` 适配层 |
| `-6` | `bad-state` | 插件不在 `running` 状态 / ctx 无效 |

## 5. 不在白名单里（明确禁止）

文件读写、网络/套接字、进程/线程、动态源码求值（`eval` 由字节码-only 策略天然没有入口）、
模块加载、系统时间以外的高精度计时、任何未在 §2 列出的宿主调用。
