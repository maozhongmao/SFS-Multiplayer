# 游玩体验优化 · 总方案（2026-10-01）

> **本文件是单一来源。** 早先的「优化计划」与「实施方案」两份已合并到此处，不再保留副本。
> 状态：本轮 4 项已进入实施；实施进展见第 7 节「执行记录」。

---

## 0. 决定记录

| 项 | 决定 |
|---|---|
| 本轮范围 | **只做 4 个小的**：B1 密码框、E1 命令权限、D3 日志降噪、A1 补标注 |
| 版本号 | **V1.3.0**（功能版，作者决定）。前两位 1.2 → 1.3，**强制全服同时升级**，见第 2 节；密码行修复另出 **V1.3.0.1** |
| E1 档位 | 最小档（不引入管理员口令分级） |
| C1 频率 | 先做成**服务端可配**，实测流量后再定值 |
| TAB 键 | 用 TAB（作者确认与游戏原版按键无冲突） |
| E3 时间加速 | **B 方案：每人独立、限制 1~5 倍** |
| E3 本地时钟 | **① 会话内不拉回**（默认采用；②「退出即拉回」会让快进到火星白飞） |
| traffic 统计 | **加**（C1 调参的前置条件，放后续轮次） |
| 已知问题 #1~#3 | 本轮不做，下一轮单独开「稳定性修复」，先修 #1 |

不做的：A1 恢复游戏内聊天（改用 QQ）、B2 服务器列表（只有两个服且互不兼容）、
B3 界面中文化（必须英文）、B5 须知滚动、D1 管道崩溃（现版本无管道部件）、
E2 MOTD/封禁/白名单/插件、E4 P2P 默认开启（此前有 bug 才关）。

---

## 1. 本轮要做的 4 项

### 1.1 B1 登录页按需显示密码框

**问题**：服务端支持 `--password`、握手带密码字段，但登录页没有密码输入，
`JoinInfo.password` 恒为空 → **设了密码的服，连服主自己都进不去**。

**协议**：复用帧 12 `ServerVersionResponse`，载荷尾部追加 4 字节 flags（小端 int32），bit0 = 需要密码。

```
[握手版本 int32][协议版本 int32][版本字符串长度 int32][版本字符串 UTF-8][flags int32]
```

**兼容性**：两端解码器都只判「够不够长」，天然容忍尾字节 ——
客户端 `ServerVersionCodec.DecodeResponse` 只校验 `payload.Length < 12 + length`，
服务端 `DecodeServerVersionResponse` 同样只判 `&lt;`。
→ 新服务端 + 旧客户端一切照旧；旧服务端 + 新客户端读不到 flags，按「不需要密码」处理。

**客户端改动**

| 文件 | 改动 |
|---|---|
| `Common/ServerVersionCodec.cs` | 加 `ServerPolicyFlagPasswordRequired = 1`；`DecodeResponse` 返回元组加第 4 项 `int Flags`（缺尾字节给 0）；`EncodeResponse` 加 `int flags = 0` |
| `Mod/JoinMenu.cs` | `ServerStatus` 加 `Flags` / `PasswordRequired`；探测分支读 flags；密码行一次创建、用 `Active` 切换；`CheckAndJoin` 里需要密码却为空 → 标红 + 提示；**密码只留内存，不写 PlayerPrefs** |

**布局**：登录页窗口 1000x742 是手工对出来的，多一行（50 + 20 = 70）会把按钮顶出可视区。
做法**不是**改死窗口高度，而是让**声明区让出这 70**：
`statementWindow.Size = new Vector2(ContentWidth, needPassword ? StatementExpandedHeight - 70 : StatementExpandedHeight)`，
再 `LayoutRebuilder.ForceRebuildLayoutImmediate(...)`。声明区本身可滚动，缩小它没有副作用，
而窗口总高不变 → 零裁剪、零重新定位风险。（`ClosableWindow.Size` 已在参考 ui/02 §6.2 里核实可写。）

**服务端改动**

| 文件 | 改动 |
|---|---|
| `protocol/server_version.hpp/.cpp` | `EncodeServerVersionResponse` 加 `std::int32_t flags = 0`（给默认值，现有测试不用改）；`DecodeServerVersionResponse` 加可选 `std::int32_t* flags = nullptr` |
| `server/frames.cpp` | `ServerVersionRequest` 分支传入 `join_.password.empty() ? 0 : 1` |

**验收**：不带密码的服没有密码行；带密码的服出现密码行、错密码被拒；旧客户端连新服仍能显示人数与版本。

### 1.2 E1 命令权限（最小档）

**问题**：远程命令入口的「实验性口令」**两端都硬编码默认 `abcde`**，而 `stop` 可经远程通道关服。
位置：NetworkDebugOverlay.cs:21、ServerCpp/src/server/packets.cpp 的实验性授权与 `kServerCommand` 分支。

**改动**

| 位置 | 改动 |
|---|---|
| `NetworkDebugOverlay.cs` | `experimentalPassphrase` 由 `"abcde"` 改成 `string.Empty`（不预填） |
| `packets.cpp` 实验性授权 | `SFS_SERVER_EXPERIMENTAL_PASSPHRASE` **未设置或为空时关闭远程命令通道**（`granted=false` + 明确提示），不再回落到 `abcde`；去掉那个 `static` lambda 的缓存写法 |
| `packets.cpp` `kServerCommand` | 取首 token 小写比对，命中 `stop` 直接回「该命令只能从服务器控制台执行」——**远程清单拿掉 stop** |

不改帧号、不加协议、不影响控制台命令。

### 1.3 D3 日志降噪

**问题**：NetworkDebugOverlay 每满 1 秒 `Debug.Log` 一行 `[SFS-MP][perf]`，发行版常驻（V1.2.4 已知问题之一）。

**改动**：加开关（F8 面板勾选框 + PlayerPrefs，**默认关**）；关闭时整段跳过 —— 连
`GC.CollectionCount` x3 与 `string.Format` 都不执行，顺带省掉每秒的分配；
**保留**所有 `Debug.LogError` / `LogWarning`（真正的故障日志一条不动）—— 这就是「只记错误」。
按参考 ui/05 §10，**保持 IMGUI 定点修改**，不迁移到 ModGUI。

### 1.4 A1 聊天注释补「已停用 + 原因」

聊天不恢复（改用 QQ），但《开发约定》要求「旧实现若被停用，标注清楚已停用 + 保留原因」。
把 Main.cs 里那批裸注释改成带标记的注释，说明：收发链路与界面实现都还在、随时可恢复、
恢复时要同时打开 `Mod.Patches/EditUI.cs` 里那条输入互斥补丁。**只改注释，不删代码。**

---

## 2. 版本号 V1.3.0

### 2.1 必改清单（少改一处就会全员进不去）

| # | 位置 | 现值 | 改成 | 不改的后果 |
|---|---|---|---|---|
| 1 | ServerCpp/src/protocol/server_version.hpp | `kServerVersionString = "1.2.3.4"` | `"1.3.0"` | **最致命**：新版客户端看到服务端报 1.2 → 前两位不匹配 → Join 直接禁用，**连服主都进不去** |
| 2 | Client csproj AssemblyName x3 | `...-V1.2.5.2` | `-V1.3.0` | 登录页版本号、产物文件名还是旧的 |
| 3 | Client csproj `<Version>` | `1.2.5.2` | `1.3.0` | 程序集版本不一致 |
| 4 | Client csproj `<OutputPath>` | `release-V1.2.5.2\` | `release-V1.3.0\` | 覆盖旧目录，违反「禁止覆盖」 |
| 5 | （若仍发布）Server/*.csproj | `1.2.3.4` | `1.3.0` | 旧 C# 服务端被新版客户端拒绝 |

客户端「Client V...」那行从程序集名 `-V` 后缀自动解析（Main.ModVersionInfo.Extract），**不用手改代码**。

### 2.2 明确不要动的

`kProtocolVersion = 32`（codec.hpp）与 `kHandshakeVersion = 2`（handshake.hpp）。
B1 只是复用既有报文体、尾部加 4 字节且两端容忍，**线格式并没有不兼容**；
《开发约定》「只有线格式真正不兼容时才提升协议版本」。

### 2.3 术语澄清

| 兼容性级别 | 这次 |
|---|---|
| 源代码兼容 | ✅ 1.5 / 1.6 双包照旧 |
| 二进制兼容 | ✅ 与本次无关 |
| **协议兼容（线格式不变）** | ✅ B1 尾部加 4 字节、两端容忍 |
| **版本字符串兼容（前两位一致才允许 Join）** | ❌ 1.2 → 1.3，**唯一的不兼容来源** |

→ **协议没变，是我们主动把版本门抬高了一档。** 要写进发布说明。

### 2.4 发布前检查清单

- [ ] 完整版本号在**所有**位置一致：Release 标题、DLL 元数据、Mods 列表、README、压缩包名；
- [ ] 新建 `Client/release-V1.3.0/` 与 `artifacts/release-V1.3.0/`，旧的 `release-V1.2.5.2/` 原样留作回退点；
- [ ] DLL / 依赖 / 压缩包都记 SHA-256（沿用 V1.2.4 的 `SHA256SUMS.txt` 做法）；
- [ ] 压缩包层级与 DLL 文件名跟 Mods 子目录名一致；
- [ ] 压缩包**不含** `Assembly-CSharp.dll` / `UnityEngine.dll` 等共享程序集；
- [ ] 依赖版本已记录（UITools：声明 1.1.5、实测装 1.1.6，参考已核对 SHA-256 相同）；
- [ ] Loader 发现 → `Early_Load` → `Load` → 场景回调 → 具体功能，**逐级取证**；
- [ ] 未实机验证的项在发布说明里**明确标注未验证**（B1 密码流程就是那条）；
- [ ] 回滚路径写进发布说明。

---

## 3. 后续轮次

### 3.1 E3 时间加速：每人独立 1~5 倍（B 方案）

**为什么可以这么做（我先前的判断是错的）**：SFS 的火箭状态是**行星相对**的，网络只传
`position + velocity + 行星名`（NetLocation），绝对坐标是渲染时用**本机时钟**现算的
（`GetSolarSystemPosition(time) = position + planet.GetSolarSystemPosition(time)`）。
所以每个客户端都在自己的参照系里自洽渲染，**不存在坐标系不兼容**。

**拆两个时钟**

| 时钟 | 谁用 | 加速时 |
|---|---|---|
| 同步时钟 `ClientManager.world` | 包时间戳比较、插值、到期判定、断流判定、快照请求 | **保持 1x，永不跟随** |
| 本地游戏时钟 `WorldTime.main` | 本机物理、画面、行星相位 | **跟随玩家 1~5x** |

**改动清单**

1. 服务端 `kTimeWarp` 分支：钳制上限 20000 → **5**；**删掉落全局时钟那段**；
   `broadcast_packet` → `send_packet`（只回请求者）；`time_scale_` 恒为 1.0；
   控制台 `time` 保留为管理员全局覆盖。
2. `DisableTimewarp`：倍率表 `{1,2,3,5,25,100,500,2500}` → **`{1,2,3,5}`**。
3. `TimeWarpVoteUI`：只调 `WorldTime.main.SetState(multiplier, realtimePhysics: true, showMsg: false)`；
   **不再调用** `ClientManager.world.SetTimeScale(...)`。
4. `ClientManager.OnPacket_UpdateWorldTime`：本地倍率 ≠ 1 时不覆盖 `WorldTime.main.worldTime`。
5. 协议不用改。

**三个「拉回来」必须分开对待**

| # | 拉回对象 | 现状 | 怎么办 |
|---|---|---|---|
| 1 | 远端火箭位置 | 每个新包覆盖外推结果，自动纠正 | **不用改**。外推走 `DelayedWorldTime - lastPacket.WorldTime` 与 `Time.unscaledDeltaTime`，**整条远端渲染链路本来就与 `Time.timeScale` 无关** |
| 2 | 同步时钟 | **只靠「有人加速时的全局广播」顺带校准** | ⚠ **B 之后这条路没了，必须补**：把心跳 `UpdateWorldTime` 也用于校准同步时钟，且要**平滑纠偏**（几十秒抹平），不能硬跳 |
| 3 | 本地游戏时钟 | 心跳每 2 秒硬覆盖 | 加速期间**绝不能拉回**；退出后的策略选 **①（会话内不拉回）** |

**第 2 条是 B 独有的隐藏坑**：`ClientManager.world.initWorldTime` 全仓库只有加入/重连与 TimeWarp Applied 两处会写，
而包时间戳是服务端的、插值判定用的是客户端的 —— 一旦漂开，`IsPacketDue`、插值延迟、断流判定全跟着偏。

**诚实代价**：别人火箭看起来变慢（这正是「我快进、他没快进」的物理事实）；
加速中同处一地的对接/碰撞不可靠（建议加软限制）；星际转移在别人眼里「瞄偏」（纯观感）；重连时本地时钟回退一次。

### 3.2 C1 可配的更新频率 + 流量测量

现状：服务端把 `update_rockets_period` **写死 50.0**（=20Hz，同行 20ms/50Hz），
客户端拿它当发送定时器周期，上限因此锁死。**客户端已有自适应档位框架**（优/良/中/差），
缺的只是「基准由服务端下发」+「打散」。

1. 服务端加 `--sync-hz`（或 `--rocket-period`，环境变量同名），下发基准间隔；不填保持 50。
2. 客户端档位**按基准推导**：controlled = 基准 x1、moving = x4、idle = x20（今天的 50/200/1000 正好是 x1/x4/x20，向后兼容）。
3. **打散**：每枚火箭一个持久随机相位 + 间隔 x0.85~1.15，避免同 tick 齐发；定时器 tick 取基准的一半。
4. **流量测量**（你定的前置条件）：客户端 F8 已有收发字节/帧统计；服务端补一条 `traffic` 命令（每人收发字节/秒）。
   然后在真机上按 50 / 20 / 16 ms 各跑一段比带宽与 CPU。
5. 附带收益：插值平滑常数 = controlled 间隔 x0.2，频率提高后画面更跟手。

### 3.3 A2 TAB 菜单 / A3 常驻状态 HUD

- 新建 `PlayerListMenu.cs`：世界场景加载后创建、默认隐藏、TAB 切换（已确认无冲突）。
- 显示：玩家名（用其颜色）、正在控制的火箭名、在线人数；底部一行自己的 RTT / 抖动 / P2P 状态 / 权威数。
- 数据源现成：LocalManager.players、ClientManager.world.rockets、P2PConnectionManager.Status、TcpClientTransport.RoundTripMs/JitterMs。
- **生命周期、防重复、清引用、限频刷新一律按第 6 节规范**。文案英文。
- A3 在这个菜单稳定后，把最关键的两三项做成常驻小角标。

---

## 4. 已知问题（当前源码里仍存在，已核对到行）

| # | 问题 | 证据 | 玩家感觉 | 建议 |
|---|---|---|---|---|
| 1 | 客户端**就地删掉**服务端下发的权威 id | LocalManager.cs:924-926 | 自己的火箭突然定住/不受控、拆件无效 | **下一轮优先修** |
| 2 | 权威易主后本地**空循环**没收尾 | ClientManager.cs:436-444 | 分离/对接后控制权不自动跟过来 | 与 #1 同区域，可一起做 |
| 3 | 加载期后台线程写字段的竞态 | DivertLoading.cs:41-46、61-62 | 进世界有概率留一条无上下文红字；persistent world 偶尔为空 | 影响面小，可最后 |
| 4 | 远程命令默认口令（已被 E1 处理） | — | — | 本轮修 |
| 5 | 每秒性能日志常驻（已被 D3 处理） | — | — | 本轮修 |
| 6 | 时间加速无任何限制 | packets.cpp:745-757 | 任何人可把全服倍率拉到 2500x | 见 3.1 |
| 7 | 登录页探测每秒新建一次 KCP 会话 | JoinMenu.QueryServerStatusAsync | 菜单开着时每秒一次握手 | 低优先 |

已从清单删除：管道部件（Pipe/CustomPipe）崩溃 —— 现版本游戏无该部件。

---

## 5. 参考包：docs/sfs-1.6-development

已整包拷入仓库（30 个文件 / 373 KB，含 `SKILL.md`）。四个独立系列：
`ui/`(01-05)、`mod/`(01-06)、`research/`(01-04)、`native/`(01-08)。
按 `SKILL.md` 的 `Reference Loading Rule` **按任务只读相关那一篇，不要全量载入**。

### 5.1 场景绑定窗口骨架（A2 / A3 必须照抄）

```csharp
private static GameObject holder;
private static Window window;
private static readonly int WindowId = Builder.GetRandomID();

public override void Load()          // 不是 Early_Load
{
    SceneHelper.OnWorldSceneLoaded += CreateUI;
    SceneHelper.OnWorldSceneUnloaded += DestroyUI;
}

private static void CreateUI()
{
    if (holder != null) return;       // 用 holder 守卫，不要 bool 标志
    holder = Builder.CreateHolder(Builder.SceneToAttach.CurrentScene, "...");
}

private static void DestroyUI()
{
    if (holder != null) UnityEngine.Object.Destroy(holder);
    holder = null; window = null; statusLabel = null;   // 清掉每一个引用
}
```

三条硬要求：① 持有点用 `holder != null`；② 销毁时清掉每一个缓存引用，之后更新前都要判空；
③ **常驻 updater（`DontDestroyOnLoad`）不得持有未保护的场景控件引用** ——
NetworkDebugOverlay 现在就是这个形态，A2 接进去时必须按这条改。

⚠ 现有 `ChatWindow.CreateUI` 用 `while (LocalManager.Player == null) await Task.Yield();` 自旋等待，别再抄那个写法。

### 5.2 只刷新控件，不重建

构造时绑定回调一次；`RefreshUI()` 只改 `Text` / `SetEnabled` / `Active`；
频繁变化的状态限频刷新（`nextRefreshTime + 0.2f`）；状态值放 UI 类之外，UI 只读快照。

### 5.3 隐藏内容改变布局高度时必须重建

`Active = false` 只是隐藏，布局高度会变 → 隐藏/显示后要 `LayoutRebuilder.ForceRebuildLayoutImmediate(...)`。
（1.1 的密码行正是按这条处理的。）

### 5.4 不持久化秘密

参考明列：**密码 / 令牌 / 服务端密钥 / 管理凭据 / 他人私人地址 / 原始包日志** 都不要落盘。
→ 确认 B1 的「密码只留内存」。

### 5.5 D3 保持 IMGUI

参考 ui/05 §10：「已有的 `OnGUI` / `GUI.Window` 不要顺手迁移」，只做定点修改；每个功能只选一条路。

### 5.6 设置页选项（后续可用）

`ConfigurationMenu.Add(...)` + `ModSettings<T>` 可做正规设置页；TAB 键是否可配置、D3 的日志开关都能放进去。
⚠ 设置页里不放任何凭据。

### 5.7 参考已验证、我们本来就做对的

- JoinMenu 的 `disableChildSizeControl`、滚动 `y=1 顶部 / y=0 底部`、
  「滚到底才能 Join」的 `PercentPosition.y <= 0.0001f` 判据；
- Home 场景克隆按钮；
- 游戏内长文本用英文/ASCII。

---

## 6. 执行记录

### 2026-10-01 · 本轮 4 项已实现，编译/链接验证通过

| 项 | 状态 | 改动文件 |
|---|---|---|
| B1 登录页密码框 | 已实现，**待实机验证** | Common/ServerVersionCodec.cs、Mod/JoinMenu.cs、protocol/server_version.hpp/.cpp、server/frames.cpp |
| E1 命令权限最小档 | 已实现 | Mod/NetworkDebugOverlay.cs、server/packets.cpp |
| D3 日志降噪 | 已实现 | Mod/NetworkDebugOverlay.cs |
| A1 聊天补标注 | 已实现 | Mod/Main.cs、Mod/ClientManager.cs |
| 版本号 V1.3.0 | 已实现 | SFS-Multiplayer.Client.csproj、protocol/server_version.hpp |

**验证结果**

| 验证项 | 结果 |
|---|---|
| 客户端 Release（v16 / SFS 1.6 包） | **0 错误**（5 条既有警告，均不在本次改动文件内） |
| 客户端 Release（v15 / SFS 1.5 包） | **0 错误** |
| 产物 | `Client/release-V1.3.0/` 下 1.5 与 1.6 两个 dll + pdb |
| DLL 确实含新代码 | 按字节检索确认：密码提示串、日志开关串、`multiplayersfs.verbose.perflog`、版本串均在 |
| C++ 改动的 3 个 TU | zig c++ 独立编译 **0 错误** |
| 服务端完整链接 | **成功** → `artifacts/release-V1.3.0/SFS-Multiplayer-Server-windows-x64.exe`（1,334,784 字节） |
| 服务端冒烟 | `--help` 正常输出；二进制内含 `"1.3.0"`、**已无** `"1.2.3.4"`、含新提示「只能从服务器控制台执行」 |
| **实机游戏验证** | **未做**（本机没有 SFS） |

**本次构建的 SHA-256**（仅供回退比对，不是发布哈希 —— 发布前要按 2.4 的清单重算并归档）

```
客户端 1.6        892C79B46ABB3BA07BD887C42E862F23399422AD707AE31DB728E368F2AC89E5
客户端 1.5        F1D45F9FFC568AE710465DD7F41E0943BAA5F9170786231E3805BAE88F179CA6
服务端 win-x64    033BE6BD394FD2333C35D516F187B4C0D408A1F0AE74BACC249D8F5FCF418670
```

**过程中发现、但本轮未修的两个问题**

1. **两个客户端包共用同一个 `OutputPath`，MSBuild 的增量清理会删掉另一个。**
   先编 v16 再编 v15，release 目录里的 v16 dll 会消失（本次实测）。
   本次用「分别构建 → 暂存到 `artifacts/release-V1.3.0/` → 再合并回 release 目录」绕过。
   正规做法：给 v15/v16 分配不同的中间+输出目录，或写一个固定的构建脚本 —— 现在仓库里**没有任何构建脚本**，
   说明这件事一直是手工做的，容易漏包。
2. **`ServerCpp/src/net/tcp_transport.cpp` 是 KCP 改造前的遗留文件，已经编不过**
   （引用 protocol 里已不存在的旧枚举名）。当前服务端不编译它，但留在树里会误导后人。建议移入 `attic/`。

**明确标注为未验证的部分**

- B1 的密码流程（需要在真实服务端 + 两个客户端上跑：不带密码不显示密码行、带密码显示、错密码被拒、旧客户端仍能显示人数）；
- D3 的 F8 开关实际表现；
- 两个包在游戏里能否被 Loader 正常加载。

### 2026-10-01 · 修正：密码行改为常驻

实机试过：服务端 `password=on`，但登录页**没有出现 Password 行**，所以没法输入密码（服务端日志里那条
`拒绝加入: 原因=Invalid password.` 就是这么来的）。作者决定不要「自动出现」这套逻辑。

改动（`Mod/JoinMenu.cs`）：

- 密码行**创建即为可见**，删掉 `SetPasswordRowVisible` 与 `passwordRowShown`；
- 窗口高度不加高，改为**声明区永久让出** `RowHeight + RowGap = 70`（320 → 250）：
  总内容高仍是 622，按钮不会被顶出可视区，`windowSize` 那个手工常量不用动；
- `CheckAndJoin` 不再按「服务端要密码」拦空值（服务端不要密码时留空本来就是对的），一律把输入框的值发出去；
- 服务端版本帧里的 flags 仍解析，只用于状态行附加一句 `Password: required`，不改布局。

**未查到底的问题**：flags 那条链路（服务端 `frames.cpp` 写入 → 客户端 `ServerVersionCodec` 解析 →
登录页读取）在实机上表现为「拿到 0」，即客户端从未收到「需要密码」。功能上已经绕开（行常驻），
所以状态行那句提示**可能永远不显示** —— 它是纯附加信息。要么以后查，要么把这段也删掉。

**产物与版本纪律的修正**

第一版修复我图省事，用**同一个版本号 V1.3.0** 覆盖了产物 —— 这违反两条约定：
「修复缺陷：版本号第四段 +1」和「每个版本使用独立目录与独立产物，**禁止覆盖既有版本**」。
更实际的害处是：两份不同的二进制都叫 V1.3.0，以后没法分辨谁装的是哪一份。

已按约定改为 **V1.3.0.1**，独立目录重建：

| 产物 | 位置 | sha256 前 16 位 |
|---|---|---|
| 客户端 1.6 | `Client/release-V1.3.0.1/SFS-Multiplayer-1.6-V1.3.0.1.dll`（428,032 字节） | `E7E46CCD60270014` |
| 客户端 1.5 | 同目录 `SFS-Multiplayer-1.5-V1.3.0.1.dll`（428,544 字节） | — |
| 服务端 windows-x64 | `artifacts/release-V1.3.0.1/` | — |
| 服务端 linux-armv7 | 同目录（已推上板子） | `89330BF4E7E2CB32` |

清理与落地：

- 被覆盖过的 `release-V1.3.0`（客户端、artifacts 各一份）已挪到 `.trash/…-mislabeled-…`，不再冒充 1.3.0；
- 游戏内安装目录换成 `Mods\SFS-Multiplayer-1.6-V1.3.0.1\`（蓝图 `.BlueprintPersistent` 已带过去），
  原来的 1.3.0 目录移进 `Rollback-Mods/`；
- 板子已更新到 V1.3.0.1：`/opt/sfs/sfs-server-kcp-arm`，上一版备份为 `sfs-server-kcp-arm.bak-1300`；
- 服务端版本串 `kServerVersionString` 同步为 `"1.3.0.1"`，登录页两端显示的版本号一致。

### 2026-10-01 · V1.3.0.2：布局改回原样

V1.3.0.1 里为了给密码行腾地方，我把声明区从 320 压到了 250 —— 实测不好看：底部那块留白还在，
声明区反而矮了一截。作者指出底部本来就有余量，多这一行正好落进去。

改动（`Mod/JoinMenu.cs`）：

- 声明区高度恢复 `StatementExpandedHeight`（不扣 `RowHeight + RowGap`）；
- 除密码行外的布局与 `windowSize` 常量**一个字没动**。

一条经验：窗口的纵向布局组是 `childAlignment: TextAnchor.MiddleCenter`，剩余空间上下均分 —— 
所以"内容高 vs 窗口高"直接相减**不是**真实边距，`windowSize` 注释里那个"标题栏约 65"也只是估算。
以后判断会不会溢出，要么实际量，要么直接看结果，别用注释里的算术推。

产物（独立目录，未覆盖任何既有版本）：

| 产物 | 位置 | 备注 |
|---|---|---|
| 客户端 1.6 | `Client/release-V1.3.0.2/SFS-Multiplayer-1.6-V1.3.0.2.dll`（428,032 字节） | 待装入游戏 |
| 客户端 1.5 | 同目录 | — |
| 服务端 linux-armv7 | `artifacts/release-V1.3.0.2/`，sha256 `EE92ADDE8420FDAB…` | **已上板子** |
| 服务端 windows-x64 | 同目录 | — |

板子：`/opt/sfs/sfs-server-kcp-arm` 已换成 V1.3.0.2（版本串核对通过、9806 在听、`password=on` 生效），
上一版备份 `sfs-server-kcp-arm.bak-1301`。客户端因游戏正在运行尚未替换。

### 2026-10-01 · V1.3.0.3：世界进入失败改为「留在加载屏 + 自动重试」

**先纠正一个误诊**（它被写进了源码注释，也被写进过审计记录，很可能是这个坑反复复发的原因）：

> "现场表现：unityTimeScale=1.000 但 gameTimeScale=-1.000，世界时间不动"

看日志那行的实参（`NetworkDebugOverlay.cs`）：

```csharp
Time.timeScale,                                          // {10} unityTimeScale
WorldTime.main == null ? -1f : WorldTime.main.TimeScale,  // {11} gameTimeScale
```

**`-1` 是"WorldTime.main 取不到"的哨兵值，不是时间倍率。** 而 `EnforceRunningTimeScale` 据此去查
`WorldTime.main.TimeScale <= 0f` —— 那个属性在 `!realtimePhysics` 时恒返回 1f，
且 `timewarpSpeed` 只会被赋成 {1,2,3,5} 或服务端钳过 ≥1 的值，**取不到 -1**。
也就是说那道兜底守的是一个从未发生的状态，而真正出现的状态（`WorldTime.main` 为 null）它恰好跳过
（第一句就是 `if (WorldTime.main != null)`）。

**这次日志的实锤**：`[SFS-MP] 进入世界超时，强制关闭加载屏。`
`CloseWorldLoadingScreen()` 只在 `OnLoadWorld()` 里被调用 → 超时能触发，说明 `OnLoadWorld()` 没跑到
→ 世界初始化没完成。而旧兜底**只关屏、不修世界、不报状态**，于是"卡在加载屏"（看得见）
变成了"进去了但世界完全不更新"（看不见）。

**改动**

- `LocalManager.Update`：超时不再关屏，改为调 `ClientManager.OnWorldEntryTimedOut()`；
- `ClientManager`：新增 `WorldEntryMaxAttempts = 5`、`worldEntryAttempts`、`lastJoinResponse`；
  把"拉起世界场景"那一步抽成 `EnterWorldScene()`（重试只重跑这一步，不重建 `ClientManager.world`，
  否则会把已收到的世界状态整份丢掉）；超时则重试并在加载屏上显示第几次；
- 5 次仍失败 → `GiveUpWorldEntry()`：报错、提示、退回主菜单，**不把坏世界留给玩家**；
- 每次超时都打一条带状态的日志：`scene= / WorldTime.main=ok|NULL / Player= / worldRockets= / synced= / pending=`。
  下次再出问题，一眼就能看出是哪一步没起来。

**产物**（独立目录，未覆盖 1.3.0.2）：客户端 `Client/release-V1.3.0.3/`（1.6 包 429,568 字节）、
服务端 `artifacts/release-V1.3.0.3/`（armv7 sha256 `1545F77FAF42630E…`）。

**部署状态**：客户端因游戏正在运行**未安装**；服务端**未推上板子** ——
本轮服务端只有版本串变化（无功能改动），且推板子会重启服务端、打断正在进行的测试。
前两位相同，1.3.0.3 客户端配 1.3.0.2 服务端**完全兼容**。

### 执行记录 V1.3.0.4 — 世界进入重试的回退修复（客户端）

**问题**：1.3.0.3 的"卡在加载屏 + 自动重试"引入回退 —— 玩家放弃加入后，
`worldLoadingStartedTime` 没人清，重试在玩家已经身处 `Build_PC`/`Hub_PC` 时仍每 10 秒调一次
`EnterWorldScene()`。日志实证：`scene=Build_PC` 后接连 3 次 `scene=Hub_PC`，每次都是一次场景加载 = 卡顿。

**修复**：
- 重试前判活动场景：已在 `Home_PC` / `Build_PC` 就停手并清计时器（加入流程已放弃）。
- `TryConnect` 的 catch 里补 `LocalManager.AbortWorldLoading()`（以前只 `Menu.loading.Close()`，计时器留着）。
- 该 catch 补 `Debug.LogError` —— 以前异常只进一闪而过的 `MsgDrawer`，日志里查不到。
  修完立刻见效：日志出现 `[SFS-MP] 加入失败：System.InvalidOperationException: Invalid password.` ×2。
- 另加"卡顿阈值日志"：单帧 >100ms 写一行 `[SFS-MP][hitch]`（默认安静，真卡才记）。
  第一次运行就抓到 `maxFrameMs=13583.2`、`unityTimeScale=0.000` —— 那是场景加载把主线程卡死。

---

### 执行记录 V1.3.0.5 — 地形钳制 / 引擎交接 / 三条诊断探针（客户端）

**①「地图在地下，一接管就弹上天」**：位置有两条写入路径绕过了 `ClampBelowTerrain` ——
`LocalManager.cs` 的建火箭（快照）与 CreateRocket 刷新。两条都直接 `SetLocationAndState(..., physicsMode: true)`，
把"埋在地里"的位置灌进动态刚体，物理一解冻就被碰撞求解器弹出。修：两处都先钳制。
（代码里本来就有这个症状的注释，只是那两条路径没被覆盖。）

**③「无人操控的火箭像区块卸载一样冻住」**：交回引擎的分支只翻 `bodyType` 就 return，
引擎从"上一帧 transform 停在哪"起步而不是从最后权威状态起步；且 Kinematic→Dynamic 的刚体可能仍是休眠态。
修：交回前先 `ApplyStateImmediately` 写一次最新状态 + 显式 `WakeUp()`。

**诊断探针**（②③收口用，不做猜测性改动）：
- `[SFS-MP] 托管的无主火箭疑似停止推进: ... 位移 速度 body asleep planet`
- `[SFS-MP] 行星不一致: 包=? 本端=?`（位置是相对某颗行星的坐标，参照系不一致就会画进另一颗星球内部）

---

### 执行记录 V1.3.0.6 — UDP 套接字缓冲（两端同时修）

**问题**：「玩到一半突然卡顿」，服务端日志实证：`lastRecv=13.5s xmit=80 cwnd=1 ssthresh=2`
→ `heartbeat timeout` → 清空全部无主火箭。

**定位过程**：本机 ping 板子 60 包 0% 丢包、最坏 2ms；两端网卡零错误零 CRC；
但板子 `rx_dropped` 持续增长（615→618 / 15s）—— **内核在丢包**。
根因：服务端 `kcp_transport.cpp` 建完 UDP socket 没设 `SO_RCVBUF`；客户端主传输同样没设
（只有 P2P 通道设了 256KB）。双方都用内核默认值。
内核丢的每个包对上层就是丢包 → KCP 只能超时重传 → srtt 2ms 抬到 878ms、RTO 4.7s。
历史上那次"网络接收缓冲提高到 1MB"只改了内核 `rmem_max`，**程序从来没申请过**。

**修复**：服务端 bind 前 `SO_RCVBUF/SO_SNDBUF = 1MB`；客户端主传输与登录探测同样 1MB。
实测 srtt 从 878ms 降到 36ms。

> 注意：这一版把客户端与服务端版本号**对齐**了 —— 之前漂移成 client 1.3.0.5 / 源码 1.3.0.3 / 板子 1.3.0.2。

---

### 执行记录 V1.3.0.7 — 分离后权威保留 / 权威 id 不再就地删 / 收尾

**新功能：分离后 3 秒权威保留（服务端）**
服务端手里刚分离的火箭状态是**从客户端包重建**的，精度不如刚在本地完成分离运算的那台机器；
立刻按"最近玩家"改判会让它按那份不精确状态瞬移，撞掉原来那枚。
实现：`RocketEntry` 增 `authority_hold_player` / `authority_hold_until`（常量 `kAuthorityHoldSeconds = 3.0`），
在 `refresh_authorities` 的"最近玩家"分支之前兑现；三个创建点（CreateRocket、对接事务）都设保留期。

**#2 客户端不再就地删服务端下发的权威 id**
`LocalManager` 清理过期 rocketId 时顺手 `updateAuthority.Remove(id)`，等于自己删掉服务端的裁决结果：
那枚火箭随后被建出来时本端已不认为该由自己更新 → **永久不再发它的状态**
（现场："自己的火箭突然定住 / 操作没反应 / 拆件拆不掉"）。现在只清两张辅助表。
（另两处 `Remove` 在**销毁**路径上，属正常清理，未动。）

**收尾**：
- `ServerCpp/src/net/tcp_transport.cpp`（KCP 改造前、编不过）移入 `attic/`。
- 新增 `build-release.ps1`：一条命令产出全套 + `SHA256SUMS.txt`，封装两个坑 ——
  双包共用 `OutputPath` 会被 MSBuild 增量清理互删（分别构建→暂存→合并）；
  zig 不允许一次调用混 C/C++（分开编 .o 再链接）。

**部署**：客户端与板子服务端**都已升到 1.3.0.7**（版本漂移消除）。
板子上的 mefrpc 同时停用（MEFrp 失联，改由 PC 上的 ChmlFrp 映射），`start_frp` 函数保留、一行可还原。

---

### 执行记录 V1.3.0.8 — 自适应档位误判（延迟随火箭数增长的真因）

**症状**：「一开始同步各枚火箭的位置，延迟就明显变大」。

**根因**：`NetworkAdaptationPolicy.Evaluate` 用 `queueCount` 判链路质量，
而 `queueCount = TcpTransportCore.QueueCount = KCP 的 WaitSnd` —— 即**本端发送积压**，量的是我们自己的发送量。
火箭越多 → 每 tick 要发的状态包越多 → `WaitSnd` 越大 → 被判 `Poor` →
插值延迟 0.12s→0.48s、发包间隔 50ms→200ms。且是正反馈：间隔翻倍 → 发送更突发 → 积压更深。

**修复**：判档只看 RTT 与抖动（丢包会经 KCP 重传体现为 RTT 上升）；`queueCount` 保留在 F8 面板做诊断。
**每档的具体数值未动** —— 源码里有「该组数值勿再单方向收紧；如需再动，先和用户确认实机表现」。

**关于"流式传输/流式解析"的结论（重要，避免重复投入）**：两端本来就是流式的 ——
客户端收包循环每收到一个数据报就 `kcp.Input` + 处理；`ClientManager.UpdateNetwork` 全量排空；
服务端 `kcp_.poll(5)` + `poll_kcp_sessions()` 每循环处理一个数据报。
**没有"等齐了再算"的批处理可以拆**；延迟来自上面的档位误判，不是批处理。
唯一确实存在帧边界等待的是客户端包处理跑在 Unity 主线程（每帧一次），
但处理器要碰 Unity 对象，挪到后台线程不安全。

---

### 执行记录 V1.3.0.9 — C1 提频与打散 / traffic 统计 / TAB 菜单与 HUD

**C1a 提高更新频率（用户明确要求）**：只动 `Excellent` / `Good` 两档发包间隔 ——
`Excellent` 50/200/1000 → **33/100/500**，`Good` 67/250/1500 → **50/150/750**；
`Fair`/`Poor` 不动（链路真差时加频只会更堵）。插值延迟与纠偏保持原值，只动频率、不碰平滑手感。

**C1b 发送相位打散**：以前首包时间戳都取同一时刻，进场那一帧所有火箭相位永久对齐，
之后每隔 interval 全体挤在同一帧发包（接收端周期性突发，档位跟着抖）。
新增 `RocketSyncPolicy.PhaseOffsetMilliseconds(rocketId, interval)`（黄金分割散列，无需存状态），
首包基准改为"当前时刻往回退一个相位"。

**C1c 服务端 `traffic` 命令**：每连接 in/out 速率 + 累计 + RTT。
速率靠两次调用之间的差分算，所以**第一次只建立基准**，第二次起出口速率。
（以前只能靠猜"到底谁在发多少"。）

**A2 TAB 玩家列表 + A3 常驻 HUD**（`PlayerListOverlay.cs`，IMGUI）：
游戏内 TAB 开合玩家列表（名字 / 是否是你 / 操控的火箭 / 自己的 ping），
左下角一行常驻 HUD（RTT / 玩家数 / 同步火箭数 / 当前档位），F9 开关并记住。
选 IMGUI 而不是 UITools 是有意的：全是只读实时数据，IMGUI 不持有场景对象引用，
世界切换不会留悬空引用。**界面文字全 ASCII**（项目硬规则）。
别人的 ping 协议里没有，如实显示 `--` 而不是编一个数。

---
### 待讨论（未动手）：A3 HUD 的摆放位置

用户实机截图显示：HUD 在编辑器里被挤在左下角 liquid fuel 面板下方、紧贴屏幕最下沿，
并提出"放到上面会不会更好"。**那是一次提问，不是改动指令** ——
当时被误当成指令直接实现并出了 V1.3.0.10，已按要求回退。

回退情况：源码与版本号全部还原到 1.3.0.9；产物移入
`.trash/release-V1.3.0.10-never-released-hudpos`；**从未安装**，
运行中的客户端（1.3.0.9）与板子服务端（1.3.0.9）未受影响。

**在用户拍板之前不再改动。**

---
### 执行记录 V1.3.1 — IPv6 支持（新功能，第三段 +1）

**背景**：用户放宽了光猫的 IPv6 访问控制、把 9806 TCP+UDP 列入白名单、DDNS 也配了 AAAA，
但模组连不上 IPv6。

**根因在服务端，不在客户端**：
ServerCpp/src/net/kcp_transport.cpp 全程只用 AF_INET / sockaddr_in / inet_pton(AF_INET, 0.0.0.0) ——
**服务端根本没有 IPv6 socket**，IPv6 的包连收都收不到。
客户端那侧其实已有一半能力（CreateBoundUdp 按地址族绑 IPv6Any、域名走 GetHostAddressesAsync）。

**改动**：

服务端：
- 传输层改双栈。getaddrinfo 解析绑定地址，默认绑 :: 并关 IPV6_V6ONLY，一个 socket 同时收
  IPv6 与 IPv4-mapped。
- Session 的端点从 sockaddr_in 换成 sockaddr_storage + 实际长度；地址比较与打印按地址族归一
  （v4-mapped 打印成纯 IPv4）。
- bind_addr 默认值从 0.0.0.0 改成 nullptr（= 双栈），所以 kcp_.init(port_) 这个调用方一个字没改。

客户端：
- 修掉一个确定的 bug：裸 IPv6 字面量 240e:3bd:...:6173 会被「最后一个冒号切端口」切成
  主机 ...:5d0c + 端口 6173（端口恰好合法，所以不报错，只是地址被切坏）。
  现在支持 [v6]:port 与裸 IPv6 字面量两种写法。
- 域名解析优先 IPv6，但**仅当本机真有可用的 IPv6 出口**（存在已启用、非回环/非隧道网卡上的
  非 link-local、非 site-local IPv6 地址），否则退回 IPv4 —— 否则本机没 IPv6 时会白等一整个连接超时。

**一个必须补的坑**：双栈 socket 上向**纯 IPv4 目标** sendto 会被内核以 EAFNOSUPPORT 拒掉。
第一版漏了这步，实测现象是「IPv6 通了、IPv4 一个包都发不出去」（服务端连 +conn 都没有）。
补的是 to_v4_mapped()：绑成 AF_INET6 时把 IPv4 目标转成 ::ffff:a.b.c.d。

**验证 A（本地双栈服务端）**：

    UDP  0.0.0.0:19821      <- IPv4
    UDP  [::]:19821         <- IPv6（同一个 socket）
    127.0.0.1 -> PASS        ::1 -> PASS

**验证 B（真实板子，服务端 1.3.1）**：

    IPv4  192.168.1.5:9806                      -> PASS  version=1.3.1
    IPv6  240e:3bd:231:7ee0:1000:d0ff:feeb:aa50 -> PASS  version=1.3.1

板子日志里同时出现「绑定 UDP :::9806」，是双栈绑定的直接证据。
板子 /proc/sys/net/ipv6/bindv6only = 0（双栈的必要条件）。

**运维记录**：
- 板子的全局 IPv6 = 240e:3bd:231:7ee0:1000:d0ff:feeb:aa50（EUI-64，由 MAC 12:00:d0:eb:aa:50 推出）。
**域名现状（2026-10-06 复测）**：
- stch.de5.net      AAAA -> 240e:3bd:231:7ee0:1000:d0ff:feeb:aa50  = **板子**（用户手工添加，仅 DNS/灰云）
- aaa.stch.de5.net  AAAA -> 240e:3bd:231:7ee0:4f0b:8325:5d0c:6173  = **这台 PC**（由 DDNS 每 5 分钟维护）
- 实测：解析 stch.de5.net 得到板子地址，打 9806 -> PASS version=1.3.1。玩家路径（域名 -> IPv6 -> 服务端）**已通**。
  对照：aaa.stch.de5.net 指向 PC，PC 上没服务端 -> FAIL，说明该测试不是假阳性。

**一个会静默失效的隐患（重要）**：
- DDNS 脚本（D:\DDNS\ddns.conf）里 CF_RECORD_NAME=aaa.stch.de5.net，而且它取的是**本机**的 EUI-64
  地址，所以在 PC 上跑就只会发布 PC 的地址。
- 板子的地址是 RA 下发的 dynamic 地址（EUI-64，MAC 12:00:d0:eb:aa:50 -> 1000:d0ff:feeb:aa50）。
  同一 /64 前缀内稳定，但**前缀变了地址就变**，而**没有任何东西在维护 stch.de5.net** ——
  届时域名会指向一个不存在的地址，且不会有人发现。
- 千万不要为了「让 DDNS 指向板子」而把 CF_RECORD_NAME 改成 stch.de5.net：那会把**PC 的地址**写进去，
  反而覆盖掉现在正确的记录。要发布板子的地址，**取址那一步必须跑在板子上**（清单里的「下一步计划」正是这个方向）。
- 日志里出现过一次「kcp init: Address in use」：是重启期间看门狗抢跑起了第二个实例，
  一次性瞬态，当时只存活一个进程。不是回归。

**部署**：客户端与板子服务端都已升到 1.3.1（两端哈希都核对过）。

---
### 执行记录 V1.3.1.1 — 修掉「进世界超时把玩家踢下线」与「最后业务包显示 Unknown」

**毛病一（严重，是 1.3.0.4 自己引入的）：连上之后约 50 秒被踢回主菜单并断线**

实测日志（1.3.1）：

    84/90/96/102/108  [SFS-MP] 世界进入超时（第 1..5 次）：scene=Hub_PC WorldTime.main=NULL ...
    109               [SFS-MP] 世界进入连续 5 次失败，退回主菜单。scene=Hub_PC ...

时间线：WorldLoadingTimeoutSeconds = 10，WorldEntryMaxAttempts = 5 -> 加入后约 50 秒。

根因：Hub_PC 是**连上之后的第一个页面，正常现象**（WorldTime.main=NULL、worldRockets=0
在没开始玩之前本来就该如此）。而 1.3.0.4 的守卫只放过 Home_PC / Build_PC，把 Hub 当成失败：
每 10 秒重试一次，5 次之后走 GiveUpWorldEntry() -> ExitToMainMenu() + Disconnect()，
把一条完好的连接主动断掉。

顺带查清了 EnterWorld 那条路为什么必然走到 Hub（IL 证据）：

    EnterWorld(WorldReference paths, WorldSettings settings, Action callback)
    {  this.settings = settings; Menu.loading.Open();
       Base.planetLoader.LoadSolarSystem(settings, log, ok => ...);  }
    完成回调：log 非空 -> 打开 ReadMenu；否则 g__Callback()
    g__Callback(): if (!success) -> callback();     // callback 就是模组传的 LoadHubScene

模组传的是 new SolarSystemReference("") 与 paths=null，所以 success=false -> 调失败回调 -> Hub。
**但这条路本来就不该走**：真正把联机世界接上的是 DivertLoading.cs 里对
GameManager.LoadPersistentAndLaunch 的 Harmony patch —— 玩家从 Hub 按正常流程开局时才触发。
所以 EnterWorldScene() 是一次注定失败、失败了还会把玩家扔到 Hub 的多余尝试。

**本轮修的是止血（低风险那半）**：
- 停止重试的场景加入 Hub_PC（Home/Build/Hub 三者都算"这件事有结论了"）。
- GiveUpWorldEntry 不再 ExitToMainMenu + Disconnect：只关加载屏 + 记日志，**绝不惩罚玩家**。
  验证：产物里 Failed to enter the multiplayer world... 与 World entry failed 两串都已消失。

**尚未做（需要实机确认）**：是否整个去掉 EnterWorldScene()，完全交给 DivertLoading。
去掉前必须确认"连上 -> Hub -> 开始游戏"这条流程不会卡在加载屏。

**毛病二（纯显示）：F8 面板「最后业务包」显示 Unknown(83) 这类值**

TrackPacketType 在判断帧类型之前就被调用，Ping/Pong 帧也会更新它；而心跳帧的 payload 是
8 字节 .NET ticks 时间戳（服务端每 2 秒发一次），它的第 0 字节是随机数 —— 于是显示成
Unknown(83)，而且数字一直变。PacketType 枚举只定义到 31，83 从来就不是包类型。

修法：只在 Kind == Packet 时统计包类型；Ping/Pong 等直接显示帧类型名。
修完之后若**仍然**出现 Unknown(N)，那才是真信号（服务端发了客户端不认识的业务包）。

**产物**：artifacts/release-V1.3.1.1/（1.6 包 435,712 字节 sha 6A5AA41317E420E2FEAB085C…）。
服务端未改动，仍是 1.3.1。**游戏当时在运行，客户端尚未安装。**

---
### 工作约定与发现（2026-10-06）

**用户授权**：允许在安装需要时**直接关闭游戏**，不必每次询问。
（仍应优先优雅关闭 —— CloseMainWindow 走游戏自己的退出流程会存盘，只在无响应时才强杀。）

**发现：工作台飞船被清空过一次（需要用户决定是否恢复）**

`.BlueprintPersistent\Blueprint.txt` 是游戏"持久化蓝图"（工作台里那艘船）的存档，
被 DivertBuildSavingAndLoading.cs 重定向进了模组目录，所以它是**按模组文件夹隔离**的。

| 版本 | Blueprint.txt | 写入时间 |
|---|---|---|
| 1.2.5.1 ~ 1.3.0.7 | 57,725 字节（Fuel Tank 等 64+ 部件、有 stages） | 10-01 12:xx |
| 1.2.4（不是本 agent 装的） | **150 字节（parts 与 stages 都为空）** | **10-03 18:28** |
| 1.3.1 / 1.3.1.1 | 150 字节（从 1.2.4 那份复制过来） | 10-03 18:28 |

结论：10-03 装 V1.2.4 时模组目录是全新的，游戏往里写了一份**空白工作台**，把原来的船覆盖了。
**57,725 字节的原件仍然完好**，在 Rollback-Mods\SFS-Multiplayer-1.6-V1.3.0.7\.BlueprintPersistent\ 下。

**同时暴露了安装流程的一个缺陷**：我每次都是"从**当前已安装**的目录复制 .BlueprintPersistent"，
如果当前那份是空的（例如刚发生过降级），就会一路把空目录传下去。
正确做法：安装时在"当前安装 + 全部回退副本"里**取 parts 最多/文件最大的一份**，
并且当来源明显比别处小的时候打一条警告。**这条要改。**

---
### 执行记录 V1.3.1.2 — 砍掉常驻 UI，只留 Debug 弹窗（用户决策）

**用户决定**：只保留 F8 的 Debug 弹窗，TAB 玩家列表与常驻 HUD 全部砍掉。
理由（用户原话的意思）：轻量不只是延迟 —— **老电脑上 UI 一多就会卡**，功能堆多了弱机一定卡。

**为什么这个判断是对的**（把机制补全）：
- Unity 的 OnGUI **每帧至少 2 次**（Layout + Repaint），有输入事件时 3~4 次。
  而原 HUD 每次 OnGUI 都做 `string.Format`（还带 int/double 装箱）+ `new GUIContent` +
  `CalcSize`（重新算一遍文字度量）—— **每帧 2~4 次堆分配，永不停止**。
- 弱机（内存小）GC 更频繁，每次 GC 就是一次可见尖峰。
- **关键**：这个成本在 `ourMsAvg` 这种平均毫秒里几乎看不出来，但会体现在 GC 计数上。
- 另外：原计划把 TAB 菜单重写成原生 UI（UITools）。**那会比 IMGUI 更重** ——
  原生 UI 是常驻 Canvas（内存常占 + 可能触发 canvas rebuild），不看你也在付代价。
  所以"TAB 停一下"从老电脑角度本来就是对的决定。

**改动**：
- 删除 PlayerListOverlay.cs（TAB 玩家列表 + 常驻 HUD + F9 开关）。
  文件移到 `.trash/removed-PlayerListOverlay-2026-10-06.cs`，**可随时恢复**。
- 摘掉 NetworkDebugOverlay 里对它的两处接线（Update 的 HandleInput、OnGUI 的 Draw）。
- **只保留 F8 调试弹窗**（火箭数/权威数/队列/RTT/抖动/最后业务包）。

**顺手修掉一个假指标**：`[hitch]` 行里的 `ourMsAvg` 原本无论有没有打开"每秒性能日志"都打 0.00 ——
因为累加被 `if (verbosePerfLog)` 门控着。**那看起来像"模组零开销"，实际是"没测"**。
现在没测就打 `n/a`。

**给弱机的验收指标（机器无关）**：

    [SFS-MP][perf] ... gc0=+N gc1=+N gc2=+N ...

`gc0` 稳定为 0（或 ≤1/秒）= 模组不会在弱机上制造 GC 尖峰。
这个数跟你这台机器多强无关，**把分配压到 0，弱机就按比例受益**。

**产物**：artifacts/release-V1.3.1.2/（1.6 包 **432,640 字节**，比 1.3.1.1 的 435,712 少 3,072）。
已验证：Players(TAB)/multiplayersfs.hud/SFS-MP connecting 三串在产物里全部查不到；
F8 弹窗的四串全部保留。服务端未改动，仍是 1.3.1。**已安装。**

---
### 执行记录 V1.3.1.3 — 砍掉"实验性访问"，为把命令搬进原生 F1 控制台做准备

**用户决定**：砍掉实验性功能；命令入口以后用游戏原生的 F1 控制台。
权限分组**本版本不做**（用户：先做出来，权限分组以后并进密码系统）。

**先确认了那个控制台是原版的**（这次挖得很实）：
- `Assembly-CSharp.dll` 的键位表里有 **`Toggle_Console`**，与 `Throttle` / `Toggle_Map` /
  `Timewarp` / `Toggle RCS` 并列 —— 原生键位。
- 程序集里嵌着源码路径 **`Assets/Scripts/Modding/Console.cs`**，还有 `ConsoleInput`、
  `<ReadConsoleInput>` 这些成员名 —— 原生 `SFS.Modding.Console` 类。
- 界面上的 `Enter command` 只出现在游戏场景文件 `Spaceflight_Data\level4` 里（不在任何 dll）。

**砍掉的**：

| 侧 | 内容 |
|---|---|
| 客户端 | F8 弹窗里的口令输入框 + 解锁按钮；`RequestExperimentalAccess`；`ExperimentalAccessGranted`；
  25 号包的回包处理器；`Packet_ExperimentalAccess.cs`（移入 `.trash/`） |
| 服务端 | 25 号包的授权判定（改为直接忽略、不回包）；`kServerCommand` 上的
  `experimental_granted` 授权门；Connection 的 `experimental_granted` 字段 |

**保留的**：
- **消息号 25 在两端都保留占位并标注"已停用 + 原因"** —— 消息号永不改，重编号会让老客户端错位解析。
- **`stop` 仍然禁止远程执行**。这不是权限判定，是防手滑：远程把服务端关掉之后，
  板子上没有交互式终端，只能 SSH 进去重启。
- 命令输入框暂时留在 F8 弹窗里（本来就是那个"只保留 Debug 弹窗"的表面），等 F1 接通后搬走。

**新加了一个临时诊断**（拿到接口后删）：F8 弹窗里一个 `Dump console API` 按钮，
按下去用反射把本机所有含 `Console` / `Command` 的类型与公开方法打进日志。
按需触发，不点就一行都不跑。**为什么需要它**：我手上的 IL 转储是 `decompiled-v1102`（**1.1.0.2**），
1.6 新增的这个 Console 类在里面根本没有 —— 这一点之前没意识到，是个疏漏，记在这里。

**安全现状（用户已知悉并接受）**：本版本服务端**不校验身份**，任何能进服的玩家都能执行
除 `stop` 以外的服务端命令。用户判断"现在没人玩，不会有人乱执行"；权限分组后续并进密码系统。

**产物**：artifacts/release-V1.3.1.3/（客户端 1.6 包 432,640 字节；服务端 armv7 13,693,500 字节）。
**两端已部署**：客户端已安装（游戏优雅关闭后替换）；板子服务端已升级，pid 正常，password=on。

**下一步**：用户跑一次游戏点那个按钮 → 拿到 F1 Console 的接口 → 把命令搬过去 → 删掉临时诊断。

---
### 执行记录 V1.3.1.4 — 撤掉 F8 里的命令弹窗 / 回退误翻的中文

**我犯的错（两处）**：

1. **没照指令做**。用户的原话是"命令弹窗砍掉、融合到 F1 控制台"。我却把它当成"临时入口留在 F8 里"，
   还自己给自己找理由（"等 F1 接通再搬"）—— 那是我替用户做了决定，用户从没批准过。
2. **二次误读**。用户指着截图问"这个怎么还在"，我当成了"中文怎么还在"，于是去把 27 处 F8 文案
   翻译成英文。用户随即澄清：**IMGUI 显示中文完全正常**。

**中文规则的正确口径**（据用户澄清修正）：

| 界面 | 中文 |
|---|---|
| 原生 UI（UITools），如登录页 | ❌ 英文 —— 这里中文会**渲染成方框**（JoinMenu 里有这条注释） |
| IMGUI 调试窗口（F8） | ✅ 中文正常显示，不必翻译 |

**本轮改动**：
- **撤掉 F8 里的整个命令块**：输入框、"执行"、help/status/debris 三个快捷按钮、"服务端返回"显示、
  以及 `serverCommandInput` 字段。命令以后走游戏原生的 F1 控制台。
- 只留一个 `Dump console API（临时诊断，见日志）` 按钮 —— 那是拿 F1 接口用的，拿到就删。
- **回退 27 处文案翻译**（F8 窗口）与**20 处**其他文件（P2P 状态行、日志上传状态、地址校验文案、
  服务端 12 条命令回复）全部改回中文。
- 顺手修掉 F8 里一个陈旧值：那行原本硬编码 `KCP 单会话 / V1.2.2.21`，与实际版本无关，
  现在改成 `"KCP 单会话 / V" + Main.ModVersionInfo.Version`。

**我自己造成的、已修**：本轮中途为验证做了一次构建，**覆盖了 `Client/release-V1.3.1.3/`**
（1.3.1.3 已发布，按版本纪律不该被覆盖）。已从 `artifacts/release-V1.3.1.3/` 还原并核对哈希一致。

**保留未动**：`ClientManager.RequestServerCommand` 与 `LastServerCommandOutput` 仍在（目前无调用者）——
F1 接通后要用它们发命令、显示回包。

**产物**：artifacts/release-V1.3.1.4/（1.6 包 432,128 字节 / 1.5 包 432,640 字节）。
服务端本轮**净改动为 0**（翻译后又逐条回退），因此不动、不重新部署，仍是 1.3.1.3。
**客户端已安装**（游戏优雅关闭后替换，哈希一致）。

---
### 执行记录 V1.3.2 — 服务端命令接进游戏原生的 F1 控制台

**怎么找到接口的**：F8 里的临时反射转储先失败了一次（我把上限设成 60 条，被 mscorlib /
UnityEngine 的 `System.Console`、`CommandBuffer` 之类吃满，游戏自己的类一个都没轮到）。
修好过滤后再转储，拿到运行时类型名 `ModLoader.IO.Console`；随后**用游戏自带的 Mono.Cecil
离线拆 `Assembly-CSharp.dll`**，得到完整签名 —— 不再需要用户反复点按钮。

```csharp
public class ModLoader.IO.Console : SFS.Input.Screen_Menu
{
    public static Console main;
    public static List<Func<string, bool>> commands;   // 返回 true = 我已处理
    public void HandleLog(string message, string stackTrace, LogType type);
    public void WriteText(string input);
    public TMP_InputField commandInput;
    public TextMeshProUGUI text;
}
```

要点：
- **这是正式扩展点，不需要 Harmony**：往 `commands` 里加一个 `Func<string,bool>` 即可。
- 实测游戏自己的控制台**没有任何内置命令**（`help` / `stop` 都是 `Command "x" not found!`），
  所以不存在重名冲突。
- 我先前说过"实现在磁盘上不存在"，那是**错的** —— `ModLoader.IO.Console` 就在磁盘的
  `Assembly-CSharp.dll` 里，我早在字符串扫描时见过 `ModLoader.IO|Console`，却当成 IO 工具类忽略了。
  `Command "` / `not found!` 之所以搜不到，是因为那句提示走的是本地化文本，不是代码里的字面量。

**实现**：新增 `ServerConsoleBridge.cs`。
- 用**反射**接（不是直接引用类型）：1.5 的 `Assembly-CSharp` 没有这个控制台，直接引用会让 1.5 包编译不过。
- 接管 `help/status/players/network/traffic/say/time/debris/world/kick/stop`，另支持 `mp:<命令>` 强制转发；
  不认识的输入返回 `false` 交还游戏。
- 回包走 `Debug.Log` 而不是 `WriteText`：控制台的 `HandleLog` 本来就把 Unity 日志显示在屏幕上，
  这条路径是确定的；`WriteText` 的确切语义（追加日志还是填充输入框）没实机验证，不赌。
- 同时**删掉了 F8 里的临时转储按钮与那两段诊断代码**（NetworkDebugOverlay 320 → 238 行）。

**新发现（重要，推翻了一条旧约定）**：控制台的 `HandleLog` 把每条 Unity 日志塞进一个
`TextMeshProUGUI`，而 **TMP 没有中文字形** —— 中文会变成方框并刷警告。
实测日志里累计 **1845 条** `The character with Unicode value ... was not found in the
[FuturaPTBook SDF] font asset`。

所以：**凡是可能出现在 F1 控制台里的文本都必须 ASCII** —— 不只是 UI 文案，**还包括日志**。
旧约定"日志可以用中文"在 1.6 上不成立（控制台会把日志显示在屏幕上）。
影响面：我们模组有大量中文 `Debug.Log`，都会在控制台里变方框。**尚未处理**。

**待用户决定**：服务端命令回包当前是中文（上一轮按用户要求从英文回退），
而它们现在要显示在 TMP 控制台里 → **会变方框**。是否改回英文？

**产物**：artifacts/release-V1.3.2/（1.6 包 433,152 字节）。服务端未改动，仍是 1.3.1.3。**已安装**。

---
### 交接记录 2026-10-06 收工 — F1 控制台已通，语言问题为次日第一优先

**当前状态（明天从这里接）**：
- 客户端 **V1.3.2.1** 已安装（`Mods\SFS-Multiplayer-1.6-V1.3.2.1\`，sha 已核对）。
- 板子服务端 **1.3.1.3**，未改动（本轮全是客户端改动）。
- **F1 控制台接命令 = 已通**。日志实证：
  `F1 console hooked` ×1、`> help` ×3、`server: ...` ×3、**`console WriteText path OK` ×1**。
  用户实机确认"能用了"。

**故障一：用户报的"按 F1 游戏炸了"——【已查明是误报，责任在我】**

真相：那是**我自己装机时把游戏关了**。装 V1.3.2.1 的脚本里只给了 **8 秒**宽限
（`CloseMainWindow` 后 `Start-Sleep 8` 就 `Stop-Process -Force`），而用户当时正在 F1 控制台里测试 ——
画面直接消失，在用户看来就是"炸了"。

**连带的错误**：我随后把它当成真崩溃去排查（翻日志、找 dmp），还提出了"TMP 缺字爆发导致卡死"的推断。
**该推断建立在假前提上，已作废。** 日志里本来就没有崩溃痕迹 —— 那是因为根本没有崩溃。

**流程修正（立即生效）**：
- 关游戏前**必须在当轮消息里先说明**"我要关你的游戏来装新版本"，不能只在脚本输出里带过；
- 宽限期从 8 秒改回 **25 秒**，且不在宽限期内直接强杀；用户正在测试时优先下次再装。

**仍然成立的一条发现（与上面无关）**：控制台的 `HandleLog` 把每条日志塞进一个 `TextMeshProUGUI`，
而 TMP 没有中文字形 —— 中文日志会变方框并刷缺字警告（实测累计 1845 条）。
这条结论不依赖那次误报。

**次日第一优先：把我们所有"会出现在屏幕上"的日志改成 ASCII。**
理由不是"避免卡死"（那条推断已作废），而是**方框**：控制台用 TextMeshPro 渲染日志，而 TMP 没有中文字形。
旧约定"日志可以用中文"在 SFS 1.6 上不成立。
影响面：模组里有大量中文 `Debug.Log`（ClientManager / LocalManager / Interpolator / 探针等）。

**待用户决定的事（都不动手）**：
1. 服务端命令回包当前是中文（用户上轮要求从英文回退），现在显示在 TMP 控制台里会变方框；
   是否改回英文？（现有硬证据：TMP 无中文字形）
2. 工作台飞船要不要恢复：57,725 字节的原件完好保存在
   `Rollback-Mods\SFS-Multiplayer-1.6-V1.3.0.7\.BlueprintPersistent\`；当前用的是 150 字节的空白版。
3. 登录页底部那 12 像素（状态行 1 行/2 行切换导致 —— `window` 是 MiddleCenter，余量对半分，
   24px 的内容变化正好在底部表现为 12px）。修法是让"声明窗 + 状态行"总高恒定，**未实施**。
4. E3（每人独立 1~5x 时间加速）仍卡在时钟策略 ①/② 的选择。

**本轮顺手确认的另一件事**：`ModLoader.IO.Console` 是正式扩展点（`static List<Func<string,bool>> commands`），
**不需要 Harmony**；游戏自身控制台没有任何内置命令（`help`/`stop` 都 not found）。

---
### 执行记录 V1.3.2.3 — 登录页底部**补上** 12 像素

**我理解反了方向**。上一轮我把"底部差了 12 个像素"读成"多了 12"，于是做成"让底部恒定 25px、不再跳"。
用户实际要的是**补上 12**（差了 = 短了）。

改动：`windowSize.y` **742 → 766（+24）**。
理由：外层是 `childAlignment: MiddleCenter`，余量**上下对半分** —— 这点是用户早先纠正我的
（"不能直接减算"）。所以底部要多 12px 就得把窗口加高 24：底部 +12、顶部也 +12。

上一轮那处"总高恒定"的改动**保留**：它保证状态行 1 行/2 行切换时底部不再跳（与本次正交）。

**如果实机看起来不对，按这张表调（一行的事）**：
- 只加了 6px（说明不是对半分）→ 改成 **754**（+12）
- 底部对了但**顶部太空**（只想让底部长、内容整体上移）→ 不动这个数，改 `CreateLayoutGroup`
  的 `padding` **底边 +24**
- 还是不够 → 直接按 +24/档继续加

**产物**：artifacts/release-V1.3.2.3/。**已安装**（游戏当时未运行）。

---

### 执行记录 V1.3.2.2 — 三处修复 + 关闭服务端密码

**修复 1：联机发射后不能控制/切换火箭**（玩家实测报告）

机制（已从代码确认）：`ControlOwnershipCoordinator.CanApplyNativeSelection` 要求
`selectedRocketId == ConfirmedRocketId` —— **服务端确认之前，原生切换被整个挡住**。
所以"确认没到" = 玩家点不进那枚火箭，表现就是"不能切换到、发射新船也是脱离状态"。

而客户端的处理是：服务端回"拒绝"时（火箭不存在 / 已被别人控制）**当场 `ClearPending()` 放弃**。
问题在于 **"火箭不存在"是瞬态的** —— 发射时 `CreateRocket` 还在路上，服务端此时必然回
`拒绝控制: 原因=火箭不存在`。一放弃，那枚新船就再也拿不到了。

改法：
- `ControlRequestMaxTries` **5 → 12**；
- **收到拒绝不再放弃**：标记 `controlRequestRejected`，由 `RetryPendingControlRequest`
  按 **3 秒**间隔继续重试（比超时的 2 秒更慢，避免撞上"已被别人控制"时刷包），到上限才真放弃。

**修复 2：服务端关闭时客户端卡死（概率性）**

先纠正我自己的一个错误结论：我先前说"客户端没有看门狗"，**是错的** ——
`TcpTransportCore.cs:392` 有 `if (SecondsSinceReceive > 10) SignalDisconnect(...)`，
它会塞一个 Disconnect 帧，链路是闭合的。我当时 grep 结果被 `slice(0,24)` 截断就下了结论。

**真正剩下的漏洞**是 `BeginReconnect` 的冷却分支：
```csharp
if (Time.unscaledTime - lastReconnectSuccessTime < 5f) { ...; return; }   // 不重连、不退出、不提示
```
"刚重连成功 5 秒内又被踢"时**空返回**，而此刻 transport 通常已经 `connected=false` ——
客户端就永久停在死会话上：世界冻住、毫无提示、只能重启。这正是玩家说的"概率性卡死"。

改法：把这次请求**挂起**（`pendingReconnectReason`），新增 `TickPendingReconnect()` 在冷却结束后补发；
`TickPendingReconnect()` 必须放在 `UpdateNetwork` 的**早退之前** —— 断线时 `client` 可能是 null，
`CanPumpNetworkPackets()` 会直接返回，那时才最需要它。

（另注：玩家报告里"卡死"的主体可能只是——服务端没了之后，世界冻结最多约 2 分钟才认输回主页。
那段体验本身也很糟，但**不是**这条死角；这条死角是真会永久卡住的。）

**修复 3：登录页底部那 12 像素**（用户逐像素数出来）

机制：外层 `window` 的布局是 `childAlignment: MiddleCenter`，多出来的余量**上下对半分**。
状态行从 24 变 48（带拒绝原因时两行）让声明区高 24 → **底部留白正好少 12**。
这也是为什么不能靠改 `windowSize` 来修：MiddleCenter 下改它 1px 只挪 0.5px，算不准。

改法：让「声明窗 + 状态行」**总高恒定** —— 状态行长高多少，声明窗就矮多少
（声明窗可滚动，少 24px 无碍）。底部留白从此恒为 25px，不再跳。

**关闭服务端密码**（用户要求）：
- `/opt/sfs/server.env` 的 `SFS_SERVER_PASSWORD` **清空**（原带密码版本备份为 `server.env.bak-withpw`），
  文件里本来就写着"清空该变量 = 关闭密码校验"。
- 重启后实测横幅为 **`password=off`**，pid 2151 正常。
- 附带效果：客户端登录页那行 `Password: required` 提示会自动消失（它由服务端 flag 驱动）。
  密码输入框本身保留（用户此前要求常驻）。

**产物**：artifacts/release-V1.3.2.2/（1.6 包 434,176 字节）。服务端**代码**未改动（只改了板子配置）。**客户端已安装**。

---
### 执行记录 V1.3.3 — E3 每人独立时间加速（恢复老 C# 的现成设计）

**关键转折：这套东西当初就写完了，是 C++ 移植时丢的。**

用户提示"不是投票制，投票制当时被废除了，应该还有一个"后，在 `Server/`（老 C# 服务端源码）里找到：

- `SfsMultiplayer.Protocol/TimeWarpControlRules.cs`：
  `CanSet(controllingPlayers, m)` = 倍率 1 **或恰好一个人控制火箭**；
  `CanSetPersonal(onlinePlayers, m)` = 在线 > 1 时每人 1~5 倍。
- `TcpMultiplayerServer.HandleTimeWarp`：**在线 > 1 → 个人加速，只单播回请求者、服务端时钟一动不动**；
  否则走全局 `SetTimeScale`。
- `TimeWarpControlRuleTests.cs`：6 个单测覆盖上述规则。

（更早的 V1.1.4.5 是**投票制**，`TimeWarpVoteUI.ApplyTimeScale` 里两处 `worldTime` 一起重置 ——
只有"全服一起加速"时那才成立。投票在 V1.1.4.7 被废除。用户指的"另一个"就是上面这套。）

**C++ 移植时丢了什么**：`packets.cpp` 的 `kTimeWarp` 变成"钳到 20000 → 落到服务端时钟 →
广播（排除请求者）"。个人分支、规则、单播全部消失。

**顺手挖出两个真 bug（都会让时间加速在客户端完全失效）**：
1. `broadcast_packet(..., fd)` 把**请求者排除**，而客户端 `RequestTimeScale` 只发包、不本地应用 →
   按加速的人自己收不到回执，**他不变快、别人全变快**。（老 C# 版在同一类坑上留过注释。）
2. 服务端把客户端的 `operation = Request` **原样广播回去**，而客户端 `Receive` 只在
   `operation == Applied` 时才应用倍率 → **全球加速在客户端从来没生效过**。
   两处都已修（`broadcast_packet(..., -1)` + 广播前 `pkt.operation = kApplied`）。

**客户端侧：策略 ① 自然落出来了**

因为服务端时钟在个人模式下**根本不动**，所以：
- 同步时钟（`ClientManager.world`）保持 1× —— `ApplyTimeScale` 里那句 `world.SetTimeScale` 在个人模式下**不调用**；
- 本地时钟（`WorldTime.main`）个人模式下**只改速率、绝不写 `worldTime`** —— 这就是"会话内不拉回"，
  否则退出加速那一刻会把玩家快进到火星的飞行拉回起点（当初否掉 ② 的理由）；
- 心跳 `OnPacket_UpdateWorldTime` 在本地倍率 ≠ 1 时**不覆盖**本地时间（新增 `TimeWarpVoteUI.LocalTimeScale`）；
- 倍率表：多人 `{1,2,3,5}`，单人仍 `{1,2,3,5,25,100,500,2500}`（老规则单人上限 2500）。

**验证（本地测试服务端 + KCP 探针，全部实跑）**

| 场景 | 期望 | 实测 |
|---|---|---|
| 单人 + 1× | 放行 | **Applied** ✓ |
| 单人 + 3×（没人控制火箭） | 拒绝 | **Notice** ✓ |
| 双人在线 + 3× | 个人放行 | **Applied mult=3** ✓ |
| 双人在线 + 25× | 拒绝（超 1~5） | **Notice** ✓ |
| 请求者能否收到回执 | 必须收到 | **replies=1** ✓（原 bug） |
| **个人 3× 生效时服务端时钟** | **保持 1×** | 世界时间 **2.03s / 2s = 1.0×** ✓ |

最后一条是整套设计成立的前提，实测确认服务端纹丝不动。

**踩过的坑（都记下来）**：
- 探针用固定用户名 → 服务端 `block_duplicate_names=on` 直接拒绝，表现为**握手超时**；改成每轮随机名。
- 线格式里 **bool 只占 1 位**（Lidgren 风格），`approved` 之后全部位对齐，按字节解会崩；
  且位序实测为 **LSB-first**（0xA3 / 0x75 最低位都是 1）。
- `player_count_` **包含掉线宽限期内的玩家**，所以刚断开就重连时会被判成"多人"、走个人分支。
- 我第一次跑测试用的是 `bin\Debug` 的**旧二进制**，新模式静默不执行 —— 换 Release 并先核对时间戳。

**产物**：artifacts/release-V1.3.3/。客户端已安装；板子服务端已升 1.3.3（探针实测 `version='1.3.3'`）。
发布包 dist/SFS-Multiplayer-V1.3.3.zip（13,833 KB）。

---
### 执行记录 V1.3.3.1 — 解锁地图 Timewarp Here + 限幅 + 两个自查发现的 bug

**用户要求**：解开地图加速的锁，但限制最大倍率（否则倍率太大被服务端拒掉）。

**机制（反编译 `WorldTime.cs` 确认）**：
```csharp
private static double GetTimewarpSpeed_Physics(int i) => new int[]{1,2,3,5}[i];
private static double GetTimewarpSpeed_Rails(int i)   => {1,5,25}[i%3]*Pow(100, i/3);
private void ApplyState(bool showMsg) {
    bool physics = timewarpIndex <= MaxPhysicsIndex;
    main.SetState(physics ? GetTimewarpSpeed_Physics(...) : GetTimewarpSpeed_Rails(...), physics, showMsg);
}
```
- 游戏自己的**物理档就是 {1,2,3,5}** —— 与"多人上限 5"天然一致；
- 地图加速之所以倍率爆炸，是因为它走**轨道档**（25/100/2500…）；
- **所有倍率变化的最终落点是 `SetState`**（`ApplyState` 也只是转调它）→ 卡这里一处全覆盖。

**改动**：
1. 删掉 `TimewarpTo_StartTimewarp` 的拦截（并留注释：要限幅不要禁用，禁用连"到点自动停"一起没了）；
2. 新增 `[HarmonyPatch(typeof(WorldTime), "SetState")]` 的 Prefix：多人时把 `timewarpSpeed` 夹到 5、
   `realtimePhysics` 置 true（用 `__0/__1` 位置名，不依赖参数名）；
3. 心跳改读 `WorldTime.main.timewarpSpeed`（真实倍率）而不是自己缓存的 `LocalTimeScale` ——
   缓存只覆盖"经过我们这条路"的加速，地图加速会让它失真，心跳就会把玩家快进的时间拉回去；
   顺手删掉了 `LocalTimeScale` / `ResetLocalTimeScale` 这两个已无用的成员。

**通读代码时抓到的两个 bug（都是"个人加速"改造的副作用）**：
- **档位判断读错了源**：`NextMultiplier/PreviousMultiplier` 读的是 `ClientManager.world.timeScale`，
  而个人模式下**同步时钟恒为 1x**（服务端时钟没动）→ 按加速永远只到 2x、按减速直接回 1x，
  **档位完全走不动**。改成读 `WorldTime.main.timewarpSpeed`。
- `NextMultiplier` 到顶时写死返回 `2500` → 改成返回**当前模式表**的最后一项（多人就是 5）。

**今天踩的坑（值得记）**：
- `Client/release-<ver>/` 是**共享输出目录**：第二次带 `-p:GameVersion=v15` 的构建会把 1.6 的产物**清掉**。
  所以必须先构建 1.6 → 立刻复制进 artifacts → 再构建 1.5。我的复制顺序是对的，但**安装时又去读
  `Client/release-*/`**，那时 1.6 已被清掉 → 复制失败。
- **"sha 一致=True" 出现假阳性**：源和目标都不存在时，两个 `Get-FileHash` 都报错返回空串，
  `'' -eq ''` = True。结果"安装成功"是假的，Mods 里留下了**一个空目录**（旧版本已被移走）→
  游戏当时处于**无模组**状态。已改为：先 `Test-Path` 再比哈希，并从 artifacts 重装（真校验通过）。
  **教训：哈希校验必须附带存在性判断，否则缺文件时会伪装成成功。**
- 输出被 `Select-Object -First N` 截断过两次（`TimewarpTo` 的方法表、之前的类型表），
  **两次都差点据截断结果下结论**。查符号表一律不要截断。

**产物**：artifacts/release-V1.3.3.1/（客户端 1.6/1.5）。客户端已安装（437,760 字节，哈希实校）。
服务端本轮未改，仍为 1.3.3（板子未重启）。发布包 dist/SFS-Multiplayer-V1.3.3.1.zip。
产物校验：`timewarp speed clamped to` 在 DLL 里（偏移 393248），拦截提示 `Multiplayer time-warp is controlled` 已为 -1。

---
## 附录：早先的优化点普查（已完成使命，压缩保留）

| 编号 | 事项 | 结论 |
|---|---|---|
| A1 | 游戏内聊天的 UI 被注释关闭 | ❌ 不做（改用 QQ）；本轮仅补标注 |
| A2 | 没有玩家列表 | ❌ **已砍**（V1.3.1.2）：用户决定只留 Debug 弹窗，常驻 UI 在弱机上是纯负担 |
| A3 | 没有常驻状态提示（只有 F8） | ❌ **已砍**（V1.3.1.2）：同上，只保留 F8 弹窗 |
| A4 | 聊天输入框与游戏快捷键互斥的补丁被注释 | 随 A1 一并搁置 |
| B1 | 登录页无密码框 | ✅ **本轮** |
| B2 | 无历史服务器列表 | ❌ 不做 |
| B3 | 登录页全英文 | ❌ 不做（必须英文） |
| B4 | 登录错误只走一闪而过的 MsgDrawer | 随 B1 顺带归位到密码框 |
| B5 | 须滚到底才能 Join | ❌ 不做 |
| C1 | 状态更新率写死 20Hz | ✅ **V1.3.0.9**：提频 + 相位打散 + `traffic` 命令 |
| C2 | 插值/自适应参数 | 代码注释明确「勿单方向收紧，先确认实机」 |
| D1 | 管道部件崩溃 | ❌ 现版本无该部件 |
| D2 | 会话生命周期审计遗留项 | ⏳ #1 已修（V1.3.0.7）；#2 #3 待做 |
| D3 | 每秒性能日志常驻 | ✅ **本轮** |
| E1 | 远程命令默认口令 + 可远程关服 | ⚠️ **V1.3.1.3 起改为"不校验身份"**（用户决定，权限分组待并入密码系统）；`stop` 仍禁远程 |
| E2 | MOTD / 封禁 / 白名单 / 插件 | ❌ 不做 |
| E3 | 时间加速是全局的 | ⏳ 待做（B 方案见 3.1，需先定时钟策略 ①/②） |
| E4 | P2P 默认关闭 | ⏸ 搁置（此前有 bug） |
| F1 | 文档版本落后（wiki 写 V1.1.4） | ⏳ 执行记录已补至 V1.3.0.9；wiki 仍待更新 |
