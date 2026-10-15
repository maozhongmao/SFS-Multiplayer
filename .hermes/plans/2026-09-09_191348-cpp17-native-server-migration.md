# C++17 独立服务端迁移计划

> **For Hermes:** Use subagent-driven-development skill to implement this plan task-by-task.

**Goal:** 在不覆盖现有 C# 稳定服务端的前提下，建立一个面向 Luckfox Lume 128MB DDR3 的 C++17 原生服务端；优先压低空载基础内存，不依赖 swap/虚拟内存，并逐步复刻现有协议和行为。

**Architecture:** C# 服务端继续作为只读行为、协议和回归基线。新服务端放在独立 `ServerCpp/`，采用 C++17、原生 socket、单网络事件循环和固定/有界内存结构；先实现 TCP 可靠事件与 UDP 最新状态语义，再逐项补齐世界、控制权和持久化。低频数据压缩仅在新服务端基础内存达标后接入，绝不作为掩盖运行时膨胀的手段。

**Tech Stack:** C++17、CMake、原生 TCP/UDP socket、`poll`/平台 socket 包装、交叉编译工具链（后续由 Luckfox 实机确认 ARM32/ARM64 与 libc）。不引入 C++ 运行时之外的大型框架，不依赖 Go/.NET/Java 运行时。

---

## 0. 当前事实与边界

- C# 稳定基线保留：
  - 服务端：`D:/SFS-Multiplayer/Server/release-V1.1.4.7-p2p-resync`
  - 客户端：`D:/SFS-Multiplayer/Client/release-V1.1.4.7-p2p-resync-light-smoothing`
- 已测 Windows 空世界 C# 服务端工作集约 `34MB`；普通 GC 参数无收益；裁剪版约可省 `3.5MB`，但会破坏 JSON 保存。
- 当前 C# 协议入口参考：
  - `Server/src/SfsMultiplayer.Protocol/SfsMultiplayer.Protocol/TcpTransportCore.cs`
  - `Server/src/SfsMultiplayer.Server/SfsMultiplayer.Server/UdpStateTransport.cs`
  - `Server/src/SfsMultiplayer.Server/SfsMultiplayer.Server/TcpMultiplayerServer.cs`
- 当前传输契约不可擅自改变：
  - TCP：握手、控制权、创建/销毁、聊天、快照请求、离散事件。
  - UDP：仅 `UpdateRocketPrimary` 高频最新状态；过时状态可丢，不可靠事件不得混入。
  - P2P Active 后仍有一次服务器权威快照重同步；不能把状态改回 TCP fallback。
- 不读取、不修改游戏目录；不反编译或复制模组 DLL；只从当前开源 C# 源码、协议测试和真实网络行为建立兼容实现。
- 本计划不开始实现，不创建 `ServerCpp/`，不修改 C# 基线。

## 1. 目录与回滚策略

**拟创建目录（实际实现经确认后才创建）：**

```text
D:/SFS-Multiplayer/ServerCpp/
  CMakeLists.txt
  src/
    main.cpp
    platform/
    net/
    protocol/
    world/
    persistence/
    compression/
  tests/
  fixtures/
  docs/
  artifacts/
```

**隔离规则：**

1. `ServerCpp/` 是唯一允许新增/修改 C++ 源码的位置。
2. `Server/` 的 C# 服务端在迁移期间只作为基线和测试参考；不替换、不删除、不覆盖。
3. 每个 C++ 试验产物单独放入 `ServerCpp/artifacts/`；正式产物未来使用独立 `release-V1.2.0-*` 目录和 ZIP，不覆盖旧包。
4. 删除任何实验文件前先经明确确认并移入回收站；当前阶段默认保留构建日志、协议 fixture 和内存测量证据。

## 2. 阶段一：先确认板端真实运行边界

**Objective:** 不根据 Windows 内存或型号猜测，先取得 Luckfox 实机的真实可用内存、用户空间位数和工具链边界。

**Files:**
- Create: `ServerCpp/docs/board-baseline.md`
- Create: `ServerCpp/artifacts/board-baseline-<timestamp>.txt`

**步骤：**

1. 在板子到手后，只读执行：
   ```sh
   uname -m
   getconf LONG_BIT
   cat /proc/meminfo
   free -m
   ldd --version
   cat /etc/os-release
   ```
2. 记录内核、ARM 位数、libc、启动后剩余内存、已有服务和 swap/zram 状态。
3. 先将 C++ 空壳在板端运行并采样 RSS、线程数、文件描述符数。
4. 设定**板端实测**的空载服务预算；在此之前，不把 Windows 的 `34MB` 直接换算成板端目标。

**验收：**
- 有一份可复查的板端基线文件。
- 明确目标 RID/交叉编译三元组，不猜 `linux-arm`、`linux-arm64` 或 libc 类型。

## 3. 阶段二：C++17 空壳先验证基础内存

**Objective:** 证明原生 C++ 服务端的基础占用明显低于 C#，再投入协议迁移。

**Files:**
- Create: `ServerCpp/CMakeLists.txt`
- Create: `ServerCpp/src/main.cpp`
- Create: `ServerCpp/src/platform/socket_platform.hpp`
- Create: `ServerCpp/src/platform/socket_platform.cpp`
- Create: `ServerCpp/tests/startup_smoke.cpp`
- Create: `ServerCpp/tests/CMakeLists.txt`

**设计：**

- C++17，默认静态/最小依赖构建；不引入 Boost、Qt、gRPC、运行时脚本引擎。
- 平台层只处理 Windows Winsock 初始化与 Linux POSIX socket 差异。
- Linux 目标使用一个 `poll` 驱动的事件循环；不采用“一客户端一线程”。最大连接数当前较小，优先稳定、可审计和低常驻内存。
- 空壳只完成：参数解析、TCP listen、UDP bind、信号退出、内存统计日志；不读取世界、不加载插件、不引入 YAML/JSON 反射。

**TDD 步骤：**

1. 写 `startup_smoke`：断言端口可绑定、停止后可立即重绑。
2. 运行测试，预期 RED（目标尚不存在）。
3. 实现最小 socket 平台层和空壳服务。
4. 运行：
   ```sh
   cmake -S ServerCpp -B ServerCpp/build-host -DCMAKE_BUILD_TYPE=Release
   cmake --build ServerCpp/build-host --config Release
   ctest --test-dir ServerCpp/build-host --output-on-failure
   ```
5. 在 Windows 与板端各采样空载 RSS/线程/FD；只与同一环境下的 C# 基线比较。

**通过门槛：**
- 空壳能监听 TCP/UDP、干净退出、端口可复用。
- 基础 RSS 明显低于相同环境中的 C# 空壳/服务端；若没有明确收益，停止后续大迁移并重新评估架构。

## 4. 阶段三：固定协议 fixture，不凭记忆重写

**Objective:** 把当前 C# 协议变成可重复验证的二进制契约，防止 C++ 实现“看起来能连、实际字段错位”。

**Files:**
- Create: `ServerCpp/fixtures/tcp/`
- Create: `ServerCpp/fixtures/udp/`
- Create: `ServerCpp/docs/protocol-contract.md`
- Create: `ServerCpp/tests/protocol_frame_tests.cpp`
- Create: `ServerCpp/tests/packet_codec_tests.cpp`

**步骤：**

1. 从现有 C# `TcpFrameCodec`、各 packet 编解码器和现有测试生成固定输入/输出 fixture。
2. fixture 至少覆盖：`Hello`、认证/连接、`CreateRocket`、`DestroyRocket`、`UpdatePlayerControl`、世界快照请求、`UpdateRocketPrimary`、UDP `Bind/BindAck/Data`。
3. 每份 fixture 附明：来源 C# 文件、字段顺序、字节长度、预期解码值。
4. C++ codec 使用显式字节序、长度上限和边界检查；不依赖结构体内存布局或编译器 padding。

**验收：**
- C++ 编码结果逐字节匹配 fixture。
- 非法长度、未知 kind、截断包、过大 payload 都被拒绝且不导致进程退出。
- 不读取模组 DLL；仅使用已拥有的服务端协议源码和自建 fixture。

## 5. 阶段四：最小网络主线，不迁移全部业务

**Objective:** 先复刻一条真实连接主线，确认 C++ 能与现有客户端交互，而不是先搬完整世界逻辑。

**Files:**
- Create: `ServerCpp/src/net/event_loop.*`
- Create: `ServerCpp/src/net/tcp_listener.*`
- Create: `ServerCpp/src/net/udp_transport.*`
- Create: `ServerCpp/src/net/session.*`
- Create: `ServerCpp/src/protocol/tcp_frame.*`
- Create: `ServerCpp/tests/tcp_udp_lifecycle_tests.cpp`

**实现顺序：**

1. TCP listener + 有界 session 表。
2. TCP frame 读写与 `Hello` 兼容检查。
3. UDP bind token 与 `BindAck`。
4. 单连接下 `UpdateRocketPrimary` 通过 UDP 收发；只保留每枚火箭最新未处理状态，绝不建立可靠状态重传队列。
5. 断开、异常包、端口释放和重复 bind。

**不做：**
- 不接入 KCP。
- 不实现动态插件加载。
- 不迁移火箭压缩。
- 不把高频状态塞入可靠有序 TCP。

**验收：**
- 现有客户端可完成最小握手；真实客户端连接成功才算完成。
- UDP 主状态与 C# fixture/黑盒行为一致。
- 压测中 session、发送队列和最新状态槽均有硬上限。

## 6. 阶段五：世界与控制权按高风险顺序迁移

**Objective:** 逐项迁移影响体验和一致性的逻辑，不能“一次性翻译整个 C# 服务端”。

**Files（拟定）：**
- Create: `ServerCpp/src/world/world_state.*`
- Create: `ServerCpp/src/world/authority_allocation.*`
- Create: `ServerCpp/src/world/control_ownership.*`
- Create: `ServerCpp/src/world/rocket_state.*`
- Create: `ServerCpp/src/persistence/state_store.*`
- Create: 对应 `ServerCpp/tests/*_tests.cpp`

**迁移顺序：**

1. 玩家表、创建者专属 `LocalId` 语义。
2. 控制权幂等：重复控制请求只回执请求者，不刷新全局 authority。
3. 创建/销毁和销毁后控制权清空广播。
4. 权威分配：保留“控制者优先、其他火箭稳定分配”的当前行为；不得改为“未控制火箭无状态权威”。
5. 世界快照和 P2P Active 后的一次性服务器权威重同步。
6. 存档导入、服务端状态保存/恢复。

**每项固定流程：**

1. 先从 C# 当前测试或真实 fixture 写 C++ 失败测试。
2. 运行确认 RED。
3. 只写让该测试通过的最小实现。
4. 跑 C++ 单测与现有 C# 对照黑盒。
5. 只有测试和实际客户端主线都通过，才进入下一项。

## 7. 阶段六：低频数据压缩，不依赖虚拟内存

**Objective:** 在基础运行时占用已压低后，把低频数据以压缩驻留方式保存，用有限 CPU 换内存，而不是依赖 DDR3 之外的 swap/page fault。

**Files（拟定）：**
- Create: `ServerCpp/src/compression/cold_store.*`
- Create: `ServerCpp/src/compression/rocket_codec.*`
- Create: `ServerCpp/src/compression/cold_data_scheduler.*`
- Create: `ServerCpp/tests/cold_store_tests.cpp`
- Create: `ServerCpp/tests/compression_roundtrip_tests.cpp`

**规则：**

- 热数据：正在控制、近期收到状态、即将同步、正在参与关键运算的火箭；维持计算精度和可立即访问结构。
- 冷数据：低频校验火箭、低频碎片、长期未变更历史数据、低频持久化块；压缩为版本化二进制块。
- 不是只有火箭可压缩：所有明确低频、可批量恢复的数据都可进入 cold store。
- 不允许每个网络包反复压缩/解压；状态切换必须有滞回和最小驻留时间。
- 压缩 blob 必须带 schema version、长度、校验和；解压失败必须保留原始持久化块或触发可审计的恢复路径。
- 保存精度可以低于运行计算精度，但控制权接管、快照和异常恢复前必须恢复所需精度；精度下调数值必须先由 fixture 和实测决定。
- OS swap、zswap、zram 只能作为系统级兜底研究，不作为服务端性能设计的前提。

**验收：**
- 压缩/解压回环测试覆盖空数据、边界值、损坏数据、版本不匹配。
- 同一世界状态下，对比 RSS、CPU、快照耗时、控制权接管和网络延迟。
- 任何节省内存的方案若使关键交互延迟恶化或世界语义变化，则拒绝采用。

## 8. 阶段七：板端验证与发布门槛

**Objective:** 以实际 Luckfox Lume 运行结果而非主机编译结果决定是否替代 C# 服务端。

**验证场景：**

1. 空载 30 分钟：RSS、线程、FD、CPU、日志增长均稳定。
2. 4 台火箭、碎片、2 台受控/2 台未控：与当前用户观测场景对照。
3. 多客户端连接、控制权切换、创建、销毁、断开重连。
4. P2P 建立后一次性权威快照重同步。
5. 冷数据压缩/恢复期间的快照和控制权接管。
6. 至少 24 小时长稳压测；记录最大 RSS、P95 网络延迟、丢包/异常包、崩溃次数和状态一致性。

**替代门槛：**

- C++ 服务端必须通过协议 fixture、C++ 回归、真实客户端连接和板端长稳测试。
- 内存优势必须是板端实测且可重复的；仅 Windows 数据不够。
- 延迟、控制权、快照和断线恢复不得弱于当前 C# 基线。
- 未达到门槛时，C++ 仅保留为实验，C# 稳定版继续使用。

## 9. 风险与决策点

| 风险 | 处理方式 |
|---|---|
| 板子实际为 ARM32/特定 libc，工具链不匹配 | 到板后先采集 `uname -m`、位数和 libc；再选择交叉编译目标，不预设。 |
| C++ 与现有客户端字段不一致 | 固定二进制 fixture + 真实客户端交叉连接；不凭 C# 类名猜编码。 |
| 为省内存删掉未控制火箭 | 禁止。所有火箭至少保留最小权威状态。 |
| 压缩导致接管卡顿/快照错误 | 热冷分层、滞回、版本化 blob、控制权前解压、实测 P95 延迟。 |
| 动态插件与 NativeAOT/原生服务不兼容 | C# 基线保留；C++ 第一阶段不迁移插件，是否加入以后单独确认。 |
| 配置文件依赖违反便携性要求 | C++ 服务端优先内嵌默认设置并支持 CLI；是否兼容旧 `server.yml` 在实施前单独确认。 |

## 10. 预计变更与回滚

**当前计划阶段：** 只新增本计划文件：

- `D:/SFS-Multiplayer/.hermes/plans/2026-09-09_191348-cpp17-native-server-migration.md`

**未来实现阶段的修改范围：**

- 新增 `D:/SFS-Multiplayer/ServerCpp/`，不修改现有 `Server/` 稳定源码。
- 发布前单独生成新目录和 ZIP，核对大小与 SHA-256。
- 回滚方式：停止使用 C++ 实验服务端，继续运行已保留的 C# `1.1.4.7` 稳定组合；不删除 C# 包。

## 11. 当前结论

C++17 独立服务端是当前最适合解决基础运行时占用的路线，但不能直接把 C# 代码逐行翻译。第一优先级是 C++ 空壳在目标板端的真实 RSS，再是协议 fixture 和最小网络主线；只有基础内存优势被实测证明，才投入完整世界和冷数据压缩迁移。
