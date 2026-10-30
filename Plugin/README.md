# Plugin — 服务端插件

用 JavaScript 写，编译成 QuickJS 字节码（`.qbc`）后由服务端加载执行。
插件运行在沙箱里：**没有文件、没有网络、没有进程**，只能调用白名单 API 和接收事件。

## 目录结构

```
Plugin/
├── stats/                    在线时长统计
│   ├── plugin_stats.js       源码
│   └── plugin_stats.qbc      编译产物
├── chat/                     聊天管理
│   ├── plugin_chat.js        源码
│   └── plugin_chat.qbc       编译产物
├── admin/                    管理命令集
│   ├── plugin_admin.js       源码
│   └── plugin_admin.qbc      编译产物
├── aaaexample/               官方分类示例
│   └── 01_log.js … 05_rocket.js   示例文件
├── webui.js                  网页看板（Node 脚本，内嵌插件 + HTML）
└── README.md                 本文件
```

每个插件目录只放两个文件：**源码 `.js` 和编译产物 `.qbc`**。
目录完全自包含，整个拷走就能用。

## 插件清单

| 目录 | 功能 | 控制台命令 |
|---|---|---|
| `stats` | 在线时长统计、进服欢迎、时长排行 | `stats` `statstop` `statsdetail` `statsreset` |
| `chat` | 刷屏拦截、重复拦截、敏感词、聊天日志 | `chatstat` `chatreset` |
| `admin` | 管理命令集（清理/调速/踢人/概况） | `ahelp` `ainfo` `aonline` `aclean` `afast` `aslow` `anormal` `aturbo` `acleanbig` `aboot` |

### stats — 在线时长

记录每个玩家本次连接的上线时间和在线时长。状态存在插件内存，服务端重启清空。

- `stats` — 广播一句话摘要（谁在线、多久）
- `statstop` — 广播在线时长排行（前 5）
- `statsdetail` — 只写日志，输出每个玩家精确秒数
- `statsreset` — 重置计时基准

玩家名字当前服务端不下发，显示为 `#id`。

### chat — 聊天管理

默认参数：10 秒窗口内 4 条警告、8 条踢出；连续 3 次相同内容提醒。

- `chatstat` — 输出统计（总消息数、拦截数、各玩家状态）
- `chatreset` — 清空统计

敏感词表在源码 `BANNED` 数组里，自行增删。重复消息不占用刷屏计数，
避免两个阈值互相误伤。

### admin — 管理命令

- `ainfo` — 服务器概况（在线人数、世界时间、tick）
- `aonline` — 在线玩家列表
- `aclean` / `acleanbig` — 清理残骸（6 / 20 部件以下）
- `aslow` / `anormal` / `afast` / `aturbo` — 时间流速 0.5 / 1 / 2 / 5 倍
- `aboot` — 踢掉第一个在线玩家
- `ahelp` — 命令列表

## 编译

```sh
qjsc -b -o stats/plugin_stats.qbc stats/plugin_stats.js
qjsc -b -o chat/plugin_chat.qbc   chat/plugin_chat.js
qjsc -b -o admin/plugin_admin.qbc admin/plugin_admin.js
```

**`-b` 必须加**。不加 `-b` 出来的是 C 源码，服务端会报 `engine rejected bytecode`。

`qjsc` 随 quickjs 源码一起编译，仓库里在 `Server/cpp/build/qjswin/qjsc.exe`。

## 运行

```sh
SFS_PLUGINS=1 SFS_PLUGIN_PATH=Plugin/stats/plugin_stats.qbc ./sfs-server-kcp-arm --port 9806
```

当前版本一次只能加载一个插件：`plugin_app.cpp` 把 id 写死为 `test-broadcast`，
只读 `SFS_PLUGIN_PATH` 指向的单个文件。

## 官方示例

见 `aaaexample/`，每个文件独立可用：

| 文件 | 类别 | 控制台敲什么 |
|---|---|---|
| `01_log.js` | 日志 | 不用敲（每 30 秒自动一行）|
| `02_message.js` | 消息 | `hi`（全体广播）、`dm`（私聊）|
| `03_query.js` | 查询 | `status`（在线人数 + 世界时间 + 服务端信息）|
| `04_admin.js` | 管理 | `clean`（清残骸）、`speed2`（2 倍速）、`boot`（踢第一个玩家）|
| `05_rocket.js` | 火箭 | 不用敲（每秒提示火箭 API 尚未开放）|

> `03_query` 的 `status` 和 `04_admin` 的 `clean` 会被服务端内置命令截走，实际无法触发。

## API 与事件

- **能调**：`sfs.log` ｜ `sfs.sendMessage` / `sfs.broadcast` ｜ `sfs.players` / `sfs.getWorldTime` / `sfs.serverInfo` ｜ `sfs.kick` / `sfs.clearDebris` / `sfs.setTimeScale` ｜ `sfs.readRocket` / `sfs.writeRocket`（后两个服务端尚未开放）
- **能收**：`worldSnapshot`（每秒一次）｜ `playerJoined` / `playerLeft` ｜ `chatMessage` ｜ `command`（控制台命令）
- **能力位**：读写火箭 / 发消息 / 写日志 / 管理类各占一位，插件必须**显式声明**，否则调用被拒绝。

API 返回码：`0` 成功，`-1` 无能力位，`-2` 参数非法，`-3` 不存在，
`-4` 本轮预算用尽，`-5` 不可用，`-6` 状态不对。

## 四个坑

1. **`-b` 出的字节码按模块作用域编译** ⇒ 函数声明不是全局，
   插件末尾必须写 `globalThis.onEvent = onEvent;`。
2. **插件命令名会被内置命令截走**（内置优先）：
   `help` `status` `say` `kick` `stop` `players` `save` `time` `world`
   `network` `traffic` `sync` `auto` `debris` `kicked` `off`。
3. **`sfs.sendMessage` 的 playerId 不能是负数**，负数直接被拒。
   全体广播只能用 `sfs.broadcast()`。
4. **控制台命令参数会被丢弃**：发 `stats top`，插件收到的只有 `stats`。
   所以子命令都做成了独立命令名。

## 两个写入限制

- `sfs.log` 走 ASCII 化，**非 ASCII 字符（含中文）全部变成 `?`**。日志写英文。
- `sfs.sendMessage` / `sfs.broadcast` 走 UTF-8，**中文正常显示**，
  但单条上限 255 字节，且禁止任何控制字符（`\n` `\t` `\r` 会让整条被拒）。

## 当前数据缺失

世界快照目前只填了 `tick` 和在线玩家 ID：

- `event.rockets[]` 恒为空数组
- 玩家 `name` 恒为空字符串
- `worldTime` 恒为 0，`controlledRocket` 恒为 -1

依赖这些数据的插件暂时拿不到内容，需要服务端在快照构造处补齐。
