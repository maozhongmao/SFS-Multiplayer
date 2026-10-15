# 插件示例：第一个插件 + 不开 QuickJS 的调试

> 契约以 `ServerCpp/src/protocol/PLUGIN_API.md` 为准，架构见 `docs/plugin-system.md`。
> 本文讲怎么写出并跑通第一个插件，以及没接 QuickJS 时如何调试。

## 1. 文件清单

| 文件 | 作用 |
|---|---|
| `ServerCpp/src/plugins/demo/demo_js_engine.hpp/.cpp` | `DemoEngine`：`IJsEngine` 的最小完整参考实现（命令表，非真 JS） |
| `ServerCpp/src/plugins/demo/example_plugin.js` | 示例插件：真 JS 源码 + `@demo` 可执行镜像 |
| `ServerCpp/src/plugins/demo/demo_host.cpp` | 可运行入口：装载 / 每秒投递 / 能力拒绝 / 预算故障 / 卸载 |
| `ServerCpp/tests/plugins/plugin_host_smoke.cpp` | 84 项冒烟测试，退出码 0 = 全过 |

三个 C++ 文件都用 `SFS_PLUGINS_DEMO` 包住，默认（服务端）构建里展开为空，
所以 `build-server-exp.ps1` 的产物行为不变（第 6 节已实测字节数/sha256 一致）。

## 2. 写出第一个插件

插件 = 一段 JS 字节码 + 一个描述符。字节码求值结果必须是函数 `onEvent(event)`；
`event` 每秒批量投递一次，`kind` 为 `worldSnapshot`/`playerJoined`/`playerLeft`/`chatMessage`。

```cpp
PluginDescriptor d;
d.id = "example-plugin";                                      // ASCII [A-Za-z0-9_.-]，1..32
d.capabilities = kCapReadRocket | kCapSendMessage | kCapLog;  // 先声明，装载后调用才不会被拒
d.limits.memory_limit_bytes = 64 * 1024;                      // 每插件堆上限
d.limits.instruction_budget = 10000;                          // 每轮指令预算
d.limits.max_api_calls = 256;                                 // 每轮宿主 API 调用上限
// d.bytecode = 预编译 .qbc（<=32KB，宿主没有源码求值入口）
host.load(d, &err);
host.subscribe(d.id, kEventWorldSnapshot | kEventPlayerJoined, &err);  // 订阅后才收事件
```

`example_plugin.js` 的逻辑：每秒 `sfs.log` 一行统计；`readRocket` 读第一枚火箭的部件数；
火箭数超过阈值 3 时给每个在线玩家 `sfs.sendMessage`。

## 3. sfs.* 能做与不能做

| 接口 | 能做 | 不能做 |
|---|---|---|
| `sfs.log(text)` | 写服务端日志 | <=255 字节；非 ASCII 变 `?`；需 `log` 位 |
| `sfs.readRocket(id)` | 按 id 读快照，成功返回对象 | id<0 拒绝；不存在返回 -3；需 `readRocket` 位 |
| `sfs.writeRocket(v)` | 写回已存在的火箭 | partCount 0..4096、throttle 0..1、坐标/速度必须有限；每轮 <=64 次；需 `writeRocket` 位 |
| `sfs.sendMessage(id,text)` | 给玩家发系统消息 | id>=0、无控制字符、UTF-8 <=255 字节；需 `sendMessage` 位 |

返回码：`0` 成功、`-1` 无能力、`-2` 参数非法、`-3` 不存在、`-4` 预算用尽、
`-5` 未接适配层、`-6` 状态不对。超指令预算或未捕获异常会让插件进入 `faulted` 终态并停止投递。
明确没有：文件、网络、进程/线程、模块加载、`eval`、高精度计时。

## 4. 不上 QuickJS 也能调试：DemoEngine

`DemoEngine` 不解析 JavaScript，只执行 `example_plugin.js` 里带 `@demo:` 前缀的注释行；
这些行逐条镜像真 JS 逻辑，接上 QuickJS 后就是普通注释。命令表：
`set n = <expr>` / `log <expr>` / `sendmsg <id> <text>` / `sendall <text>` / `getrocket <id> into <var>` / `setthrottle <var> <expr>` / `writeback <var>` / `when <kind> <action>` / `if <a> <op> <b> then <action>`；
字符串里 `@n`/`@a.b` 插值，表达式支持 `+ - * /` 与括号。所有"脚本调用"仍走
`PluginHost` 的 `HostApi` 表，能力拒绝（-1）与预算（-4）照常生效。

**接线注意**：`IJsEngine` 没有绑定宿主 API 的方法，`PluginHost::load()` 成功后
`api_for(id)` 的 `ctx` 才有效。`DemoEngineFactory` 记录最近创建的引擎，装载后调用
`engine->bind_host_api(host.api_for(id))` 完成这一步（见 `demo_host.cpp`）。

## 5. 编译与运行（Windows 宿主机）

zig 缓存/临时目录先指到 `ServerCppuild` 下，避免 ACL 问题；`-DSFS_PLUGINS_DEMO=1` 打开 demo。

```powershell
$root='D:\SFS-Multiplayer'; $zig="$root\tools\zig-x86_64-windows-0.16.0\zig.exe"; $b="$root\ServerCpp\build"
$env:ZIG_GLOBAL_CACHE_DIR="$b\zig-cache\global"; $env:ZIG_LOCAL_CACHE_DIR="$b\zig-cache\local"
$env:TEMP="$b\zig-cache\tmp"; $env:TMP="$b\zig-cache\tmp"; $inc="$root\ServerCpp\src"
$cc=@("-target","x86_64-windows-gnu","-std=c++17","-w","-DSFS_PLUGINS_DEMO=1","-I",$inc)
$lib=@("$inc\plugins\plugin_host.cpp","$inc\plugins\demo\demo_js_engine.cpp")
# demo_host（从仓库根运行，路径会自动找到示例插件；也可把 .js 路径作为参数传入）
& $zig c++ @cc @lib "$inc\plugins\demo\demo_host.cpp" -o "$b\demo\demo_host.exe"
& "$b\demo\demo_host.exe"
# 冒烟测试：84 项
& $zig c++ @cc @lib "$root\ServerCpp\tests\plugins\plugin_host_smoke.cpp" -o "$b\demo\plugin_host_smoke.exe"
& "$b\demo\plugin_host_smoke.exe" "$inc\plugins\demo\example_plugin.js"
```

`demo_host.exe` 预期输出（节选）：`loaded ... state=running`、每秒 `[plugin:example-plugin] tick=...`、
`message -> player 1: world has 4 rockets`、`nolog stats: capability_denials=2`、
`budget state: state=faulted`、`done: host shutdown`。
`plugin_host_smoke.exe` 预期最后一行 `checks=84 failures=0 -> PASS`，退出码 0。

## 6. 将来接上 QuickJS 只改一处

宿主、事件总线、预算、白名单、日志出口全部不动。唯一要换的是**引擎工厂**：把
`demo::DemoEngineFactory` 换成 `QuickJsEngineFactory`（补全 `quickjs_engine.hpp` 的 TODO）。插件侧把
`example_plugin.js` 用 `qjsc -c -o example_plugin.qbc example_plugin.js` 编译成字节码，描述符的
`bytecode` 换成 `.qbc` 即可。交叉编译与安全基线见 `docs/plugin-system.md` 第 6/7 节。

## 7. 仍是 TODO

- `quickjs_engine.hpp` 的 `QuickJsEngine`/`QuickJsEngineFactory` 未实现（只有接口注释）。
- `sfs.*` 的 QuickJS 绑定层（`WorldSnapshot` -> JS 对象、`HostApi` -> `sfs.*`）未实现。
- 服务端主循环尚未调用 `install_services` / `global_host` / `publish_world_snapshot` / `tick`。
- `writeRocket` 的每轮 64 次上限、预算与 ASCII 日志闸在宿主已生效，但尚无真实服务端适配层接入。
