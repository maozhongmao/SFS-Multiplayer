# C++ 服务端分类日志

适用代码：`ServerCpp/src/util/log.{hpp,cpp}`（日志内核）、`ServerCpp/src/util/config.{hpp,cpp}`（配置文件）、
`ServerCpp/src/main.cpp`（启动时合并配置）。

## 1. 输出格式

```
HH:MM:SS [cat   ] LEVEL message
```

- 时间戳固定 `HH:MM:SS`（本地时间）。
- 分类标签左对齐补到 6 列：`[net   ] [world ] [p2p   ] [plugin] [config] [error ]`。
- 级别左对齐补到 5 列：`DEBUG INFO  WARN  ERROR`。
- `message` 是**原文保持不变**的日志正文，继续带 `[cpp-server]` 前缀，因此老的 grep
  规则（`grep '\[cpp-server\]'`、`grep 'rocket created'`、`grep 'handshake ok'`）全部有效。

实例：

```
19:52:01 [net   ] INFO  [cpp-server] listening on KCP (UDP) port 9806, max_players=16
19:52:01 [config] INFO  [cpp-server] port=9806 max_players=16 state=data/server-state.json p2p=off ...
19:52:01 [world ] INFO  [cpp-server] 存档已加载: 世界时间=0.000 难度=0 火箭=0 部件=0
```

对比：改动前是 `[cpp-server] listening on KCP (UDP) port 9806, max_players=16`。
**正文与今天等价，只是前面多了时间戳/分类/级别**（格式化细节允许变化）。

## 2. 分类

| 分类 | 名称 | 放什么 |
| --- | --- | --- |
| `net` | Net | 连接、握手、收发、KCP、套接字绑定、帧解码、拒绝加入 |
| `world` | World | 火箭创建/销毁/控制、对接/拆解、权威、存档、垃圾清理 |
| `p2p` | P2P | P2P 信令撮合、端口广告、配对建立/撤销 |
| `plugin` | Plugin | 插件系统（配置面已就绪，服务端现有插件输出尚未接入） |
| `config` | Config | 配置加载、启动摘要、控制台/远程命令、日志上传 |
| `error` | Error | 不归属具体子系统的错误（参数错误、fatal error 等） |

## 3. 级别

`debug < info < warn < error`。默认 `info`。

- `warn` / `error` 会被颜色标注（见下）。
- **error 永远显示**：级别过滤和白名单都不会吞掉 `error` 级别的日志，
  除非该类被**显式**关闭（见第 6 节）。默认级别下 `debug` 不显示，其余都显示。

## 4. 颜色

| 级别 | 颜色 | 转义 |
| --- | --- | --- |
| info | 终端默认色 | 无 |
| warn | 黄色 | `\x1b[33m` |
| error | 红色 | `\x1b[31m` |
| debug | 灰色 | `\x1b[90m` |

- 默认 `color: auto`：**只在 stdout 是终端时启用**；输出重定向到文件/管道时自动关闭。
- `on` 强制启用（即使重定向），`off` 永远关闭。
- Windows 下 init 时会调用 `SetConsoleMode(..., ENABLE_VIRTUAL_TERMINAL_PROCESSING)` 打开 ANSI。
- 上色只作用于级别标签，消息正文不带转义序列，便于 grep。

## 5. 配置文件

默认路径：`data/server-config.json`（相对于进程工作目录）。
**文件不存在 = 使用内置默认值，不报错、不退出。** 文件存在但 JSON 非法时打印一条 `config` 分类的
warn，然后继续用内置默认值启动。

```json
{
  "log": {
    "enabled": true,
    "level": "info",
    "categories": {
      "net": true,
      "world": true,
      "p2p": true,
      "plugin": true,
      "config": true
    },
    "color": "auto",
    "quiet_startup": false
  }
}
```

字段语义：

| 字段 | 取值 | 说明 |
| --- | --- | --- |
| `enabled` | bool，默认 `true` | 总开关；false 时一行都不输出 |
| `level` | `debug/info/warn/error`，默认 `info` | 最低级别 |
| `categories` | 对象 | **false = 不显示该类；未列出的类别默认显示** |
| `color` | `auto/on/off`，默认 `auto` | ANSI 颜色模式 |
| `quiet_startup` | bool，默认 `false` | true 时不打印启动配置摘要行 |

用 `--config <file>` 指定别的路径。

## 6. 优先级与过滤规则

**内置默认值 < 配置文件 < 环境变量 < 命令行。**

命令行：

```
--config <文件>
--log-level <debug|info|warn|error>
--log-categories <a,b,c>         # 只显示这些分类（白名单）
--log-hide-categories <a,b,c>    # 不显示这些分类（黑名单）
--log-color <auto|on|off>
--log-quiet-startup
```

环境变量（同名对应）：

```
SFS_LOG_LEVEL / SFS_LOG_CATEGORIES / SFS_LOG_HIDE_CATEGORIES / SFS_LOG_COLOR
```

分类列表接受 `all` 表示全部分类。

判定顺序（`util/log.cpp: accepted`）：

1. `enabled=false` -> 不输出；
2. 类别被**显式关闭**（配置文件 `categories: false` 或 `--log-hide-categories`）-> 不输出，**连 error 也压掉**；
3. 级别 = `error` -> **一定输出**（绕过级别阈值与白名单）；
4. 级别低于 `level` -> 不输出；
5. 白名单生效且该类别不在白名单 -> 不输出；
6. 否则输出。

> 注意：`--log-categories net` 只显示 `net` 的非 error 日志，但其它类别的 **error 级**日志仍会显示。
> 要彻底静音某个类别，请用 `--log-hide-categories`（或在配置里写 false）。

## 7. 启动摘要

启动时（`quiet_startup=false`）打印一行 ASCII 的生效配置，方便排查：

```
19:52:01 [config] INFO  [cpp-server] log config: enabled=1 level=info color=off mode=auto quiet_startup=0 config=data/server-config.json (not found (built-in defaults)) categories=net,world,p2p,plugin,config,error
```

这一行**故意绕过过滤**（即使把 `config` 关掉也能看到）；路径做了 ASCII 清洗。

## 8. 示例

只显示 world + p2p：

```bash
./SFS-Multiplayer-Server-windows-x64.exe --state build/tmp/state.json --log-categories world,p2p
```

关掉 net（连接/握手类日志消失，world 仍在）：

```json
{ "log": { "categories": { "net": false } } }
```

提高级别、强制彩色：

```bash
SFS_LOG_LEVEL=debug ./SFS-Multiplayer-Server-windows-x64.exe --log-color on
```

## 9. 在代码里写日志

```cpp
#include "util/log.hpp"
using sfs::log::Cat;

SFS_LOGI(Cat::Net,   "[cpp-server] +conn #%llu conv=%u", id, conv);
SFS_LOGW(Cat::World, "[cpp-server] 拒绝 DestroyRocket: rocket=%d", rid);
SFS_LOGE(Cat::Net,   "[cpp-server] decode error: %s", e.what());
SFS_LOGD(Cat::Config, "[cpp-server] ...");   // 默认不显示
```

- 宏内部是 printf 风格，GCC/Clang 下带 `format(printf)` 检查。
- **信号处理函数**里必须用 `sfs::log::signal_log(...)`（不加锁、无时间戳，用 `write(2)` 直写），
  不能调 `log()`：`log()` 内部有互斥锁，在 SIGINT/SIGTERM 里抢锁可能死锁。

## 10. 已知边界

- 日志统一写 **stdout**（原来 `std::cerr` 的“参数错误 / fatal error”也走 stdout）。
- 未接入新接口的文件（不在本次改动范围）：`src/state_store.cpp`、`src/plugins/*`、
  `src/attic/*`、`src/main_minimal.cpp`、`src/sfs_server_min.cpp`。
  它们仍直接 `printf`，因此不受分类过滤控制。
- `--help` 文本仍直接走 `std::cout`（属于用法说明，不是日志）。
- `ServerCpp/CMakeLists.txt` 未改（本次改动范围不含它）。发布构建 `build-release.ps1`
  用 `Get-ChildItem -Recurse *.cpp` 收集源文件，会自动带上 `util/log.cpp`、`util/config.cpp`；
  若要跑 CMake 构建，需把这两个文件加进 `sfs-server-kcp` 的源列表（以及 `src` 在 include path 上）。
