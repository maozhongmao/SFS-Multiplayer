# 插件系统（QuickJS）— 骨架设计与接入

任务 #12 只交付**可编译、可验证的骨架**：不下载/不引入 QuickJS 依赖，
默认关闭时对服务端产物零影响。真正接引擎按文末步骤执行。

## 1. 目标

- 服务端侧插件框架：加载/卸载、生命周期、每插件资源上限、能力白名单。
- 粗粒度事件总线：每秒一次批量世界快照，**不做每帧回调**（插件执行频率与世界规模解耦）。
- 引擎无关：宿主只认 `sfs::plugins::IJsEngine`；QuickJS（quickjs-ng）是它的一个实现。
- 面向 armv7 / musl、ROM ~20MB、RAM 几百 KB 余量：定长 POD、显式上限、无异常/RTTI 依赖。

## 2. 文件与类

| 文件 | 内容 |
|---|---|
| `ServerCpp/src/plugins/plugin_types.hpp` | 能力位 `Capability`、`PluginLimits`、`EventKind`、`HostStatus`、所有硬上限 |
| `ServerCpp/src/plugins/plugin_events.hpp` | `RocketView` / `PlayerView` / `WorldSnapshot` / `PluginEvent`（POD） |
| `ServerCpp/src/plugins/ijs_engine.hpp` | `IJsEngine` 抽象 + `NullJsEngine`（默认空引擎）+ `IJsEngineFactory` |
| `ServerCpp/src/plugins/plugin_host.hpp` | `PluginHost`、`PluginDescriptor`、`HostServices`、`HostApi` |
| `ServerCpp/src/plugins/plugin_host.cpp` | 宿主实现（能力检查/预算/事件投递/ASCII 日志） |
| `ServerCpp/src/plugins/plugin_bootstrap.hpp` | 服务端主循环接入点，`SFS_PLUGINS` 编译期开关（默认 0） |
| `ServerCpp/src/plugins/plugin_bootstrap.cpp` | bootstrap 实现（全局宿主、运行期开关） |
| `ServerCpp/src/plugins/quickjs_engine.hpp` | QuickJS 接入 TODO（默认不引用 quickjs 头） |
| `ServerCpp/src/protocol/PLUGIN_API.md` | 插件可见的宿主 API 契约（JS 侧） |

数据流：服务端 → `publish_world_snapshot()`（覆盖式，不排队）→ `pump()` 每秒限流投递一次
→ 每个订阅插件 `IJsEngine::dispatch()` → 插件经 `HostApi` 读写世界 → 回到 `HostServices`。

## 3. 事件表

| 事件 | 位 | 载荷 | 节拍 |
|---|---|---|---|
| `kEventWorldSnapshot` | 1<<0 | `WorldSnapshot`（全部火箭+玩家） | 每秒 1 次，批量 |
| `kEventPlayerJoined` | 1<<1 | `PlayerView` | 事件发生时入队，随 pump 投递 |
| `kEventPlayerLeft` | 1<<2 | `PlayerView` | 同上 |
| `kEventChatMessage` | 1<<3 | player_id + 已限长文本 | 同上 |

离散事件队列上限 `kMaxPendingEvents=64`，超出只计数不排队，内存有界。

## 4. 能力边界（白名单）

`HostApi` 是插件唯一能碰宿主的通道，每个插件一份 ctx（能力位绑在 ctx 上）：

| 能力位 | API | 行为 |
|---|---|---|
| `kCapReadRocket` | `read_rocket(id, out)` | 读火箭快照，id<0 拒绝 |
| `kCapWriteRocket` | `write_rocket(view)` | 写回；校验 id/part_count/throttle/有限数；每轮 ≤ `kMaxWritesPerDispatch=64` |
| `kCapSendMessage` | `send_message(player, text)` | 发系统消息；拒控制字符、总长 ≤ `kMaxTextBytes-1` |
| `kCapLog` | `log(text)` | 日志出口统一 ASCII 化（非 ASCII → `?`）并加 `[plugin:<id>]` 前缀 |

每次调用顺序：**查 capability → 查参数边界 → 查插件状态 → 扣 API 预算 → 调 `HostServices`**。
未声明能力返回 `kHostErrNoCapability`，未接适配层返回 `kHostErrUnavailable`。

## 5. 资源上限（`PluginLimits`，可被描述符覆盖但受全局硬上限约束）

| 字段 | 默认 | 硬上限 | QuickJS 映射 |
|---|---|---|---|
| `memory_limit_bytes` | 256 KB | 2 MB | `JS_SetMemoryLimit` |
| `max_stack_bytes` | 32 KB | 256 KB | `JS_SetMaxStackSize` |
| `instruction_budget` | 200000/轮 | 5000000 | `JS_SetInterruptHandler` 计数中断 |
| `max_api_calls` | 1024/轮 | 4096 | 宿主 `consume_api_budget` |

另有 `kMaxBytecodeBytes=32KB`（每插件字节码常驻 RAM）。超预算的 `dispatch` 置 `kFaulted`、
停止投递并记录 ASCII 日志；**不崩溃服务端**。

## 6. 服务端接入（默认关闭，关闭时零开销）

```cpp
#include "plugins/plugin_bootstrap.hpp"
// 启动后（适配层可选）：
sfs::plugins::install_services(&my_host_services);
// 主循环每轮：
sfs::plugins::tick(uptime_now());            // SFS_PLUGINS=0 时展开为空
// 每 1 秒构造一次快照并投递：
if (sfs::plugins::PluginHost* h = sfs::plugins::global_host())
    h->publish_world_snapshot(build_world_snapshot());
// 退出前：
sfs::plugins::shutdown();
```

开关：编译期 `-DSFS_PLUGINS=1`（推荐，未定义即 0）；运行期环境变量 `SFS_PLUGINS=0/off` 可关。
`SFS_PLUGINS=0` 时 `plugin_bootstrap.hpp` 展开为空；`plugin_host.cpp`/`plugin_bootstrap.cpp`
仍参与编译以防骨架腐烂，但主循环不引用，配合 `-ffunction-sections -fdata-sections + --gc-sections`
整段回收。验证：比较开启前后 `build-server-exp.ps1` 输出的 sha256 / 字节数。

## 7. 把 QuickJS（quickjs-ng）真正接进去

1. **离线放入源码**：`ServerCpp/third_party/quickjs/`（与 `Server/third_party/` 同一惯例；**必须放 D 盘**，见第 10 节）（`quickjs.c`、`quickjs.h`、`libregexp`、
   `libunicode`、`cutils`、`dtoa` 等）。构建脚本已用 `*third_party*` 通配收集 `.c`，无需改脚本。
2. **armv7 musl 交叉编译要点**（工具链同现有：`tools/zig-x86_64-windows-0.16.0/zig.exe`）：
   ```
   zig cc -target arm-linux-musleabihf -std=c11 -Os -DNDEBUG \
       -DCONFIG_VERSION="0.0.0" -DCONFIG_BIGNUM=0 -DCONFIG_STACK_CHECK=1 \
       -ffunction-sections -fdata-sections -c quickjs.c -o quickjs.o
   ```
   要点：`CONFIG_BIGNUM=0` 去掉 BigNum/BigDecimal 省 ROM；`CONFIG_STACK_CHECK=1` 让 `JS_SetMaxStackSize` 真正生效；
   不链接 `libbf`；不开 `JS_NAN_BOXING`（32 位下收益有限）；宏名以所用 quickjs-ng 版本的 Makefile 为准。
3. **实现 `QuickJsEngine`**：按 `plugins/quickjs_engine.hpp` 里的 TODO：
   `JS_NewRuntime`/`JS_NewContext` → `JS_SetMemoryLimit`/`JS_SetMaxStackSize` →
   `JS_ReadObject(..., JS_READ_OBJ_BYTECODE)`（**只跑字节码**）→ `JS_SetInterruptHandler` 检查指令预算 →
   每轮把 `WorldSnapshot` 转成 JS 对象并 `JS_Call` → `JS_RunGC` → `JS_FreeContext`/`JS_FreeRuntime`。
4. **注册宿主白名单**：只把 `HostApi` 四个函数注册成全局 `sfs.*`；QuickJS 自带
   `std`/`os`/`bignum` 模块一律不注册（无文件、无网络、无进程）。
5. **打开开关**：在 `build-server-exp.ps1` 的 `$cflags` 加 `-DSFS_PLUGINS=1`、`-DSFS_PLUGINS_QUICKJS=1`，
   并把工厂装进 `PluginHost`；插件以预编译 `.qbc` 字节码下发。
6. **验证**：同一份 `.qbc` 在 x64 上跑通后，板上核对 RSS 增量与单轮 `instructions_used()`。

## 8. 安全考量

- **字节码-only**：宿主不暴露源码求值入口，磁盘上只有编译产物，避免板上解析器攻击面。
- **无文件/网络 API**：不注册 QuickJS 标准模块，插件能做的只有白名单里的四件事。
- **超时/死循环中断**：`JS_SetInterruptHandler` 按指令预算中断；`kFaulted` 后不再投递。
- **内存/栈天花板**：`JS_SetMemoryLimit` 触发引擎 OOM 而非进程 OOM；`max_stack_bytes` 防深递归。
- **能力最小化**：装载时声明、调用时复核；写火箭还要过数值有限性与每轮写次数上限。
- **日志 ASCII 闸**：插件文本进 stdout/Player.log 前统一 ASCII 化并限长，并加 `[plugin:<id>]` 前缀便于溯源。
- **单线程**：插件只在主循环线程执行，不引入并发数据竞争；单插件故障不影响其他插件与服务端。

## 9. 验收判据

1. `ServerCpp/build-server-exp.ps1` 编译 0 错误（含 `plugins/*.cpp`）。
2. `SFS_PLUGINS=0`（默认）时产物行为不变：二进制 sha256/字节数与引入前一致（或仅链接顺序等非行为差异）。
3. `-DSFS_PLUGINS=1` 时两个 TU 仍 0 错误（已用 `zig c++ -target arm-linux-musleabihf -std=c++17` 验证）。
4. 文档给出 QuickJS 接入的完整步骤（第 7 节）与安全边界（第 8 节）。

## 10. 前置依赖与路径（**必须放在 D 盘**）

> ⚠️ **本机 C 盘不可写**：zig 默认缓存 `C:\Users\<用户>\AppData\Local\zig` 会报 `AccessDenied`，
> 子进程写 `%TEMP%` 同样被拒。**所有下载 / 解压 / 构建 / 缓存路径一律落在 D 盘**，否则构建必然失败。

| 依赖 | 建议路径（全部在 D 盘） | 说明 |
|---|---|---|
| QuickJS（quickjs-ng）源码 | `D:\SFS-Multiplayer\ServerCpp\third_party\quickjs\` | 离线放入。文件：`quickjs.c/.h`、`libregexp.c/.h`、`libunicode.c/.h`、`cutils.c/.h`、`libbf.c/.h` |
| armv7 musl 构建产物（上板用） | `...\third_party\quickjs\build-armv7\libquickjs.a` | 与板上服务端同 ABI（musl 静态） |
| Windows x64 构建产物（宿主机 demo 用） | `...\third_party\quickjs\build-win64\quickjs.lib` | 想在 Windows 上跑真 JS 时才需要 |
| zig 缓存与临时目录 | `D:\SFS-Multiplayer\ServerCpp\build\{zigcache,tmp}` | 构建前设 `ZIG_GLOBAL_CACHE_DIR`、`ZIG_LOCAL_CACHE_DIR`、`TEMP`、`TMP` |

交叉编译要点（细节以 quickjs-ng 官方 Makefile 为准）：用 `zig cc -target arm-linux-musleabihf -O2 -DNDEBUG`
分别编译 `quickjs.c libregexp.c libunicode.c cutils.c libbf.c`，再 `zig ar rcs libquickjs.a *.o`；
**不要**引入 QuickJS 的标准模块（`quickjs-libc`），插件只走 `sfs.*` 白名单。

接入服务端构建：在 `ServerCpp\build-server-exp.ps1` 的 `$cflags` 增加
`-DSFS_PLUGINS=1 -DSFS_PLUGINS_QUICKJS=1`，并追加 `-I <quickjs 源码>` 与 `libquickjs.a` 的绝对路径（D 盘）。

**边界说明**：① 空引擎 / DemoEngine 路径**不需要 QuickJS** —— 示例插件与宿主机 demo 今天就能跑；
② QuickJS 只是"接真 JS 插件"的前置依赖；③ 目标板是 armv7 musl、宿主机是 Windows x64，
**两套产物都要放 D 盘**（`build-armv7` / `build-win64`）。
