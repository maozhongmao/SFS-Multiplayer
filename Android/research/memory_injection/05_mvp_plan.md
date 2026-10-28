# 05 · 手机端联机 MVP（最小可玩集）定义 · 里程碑 · 风险登记

> 只读引用：`Client/src/MultiplayerSFS.Client/`、`Server/cpp/src/`、`docs/mobile-feasibility.md`、`android/dump/dump.cs`、`android/src/v43.c`。
> 本文不改任何源码；所有 RVA/字段偏移来自 `android/dump/dump.cs`（官方 1.6.00.22，arm64-v8a），**换版本必须重导**。
> 状态：v1（2026-10-21）· 边做边写

---

## 0. 结论摘要

- **MVP 可行**，而且比"从零写一套联机"便宜得多：服务器侧的协议栈（KCP 帧、握手、包编解码、Lidgren 位流）是**纯 C++、无 OS 依赖**，可以直接编进我们的 `.so`，手机端只需要补 **socket + KCP 驱动 + 游戏侧读写**。
- 真正的成本不在协议，在**游戏侧两件事**：把本地火箭的结构读出来发出去（`RocketSave`/`PartSave`），以及把远端位置画到屏幕上（浮动原点坐标换算 + 对象创建）。
- **总量：30–44 人日**（单人、熟悉 C/il2cpp、不含美术与运维），加 20% 缓冲 ≈ **36–53 人日 ≈ 7.5–11 周**；M3/M4 两人并行约 6 周。
- M1–M5 五个里程碑里 **M4（远端可见）风险最高**，因为本项目历史上"直接 `Object.Instantiate`"曾死在 `libunity` 内部（`v43.c` 头部注释），必须先验证对象创建路径。
- **作废级风险 3 条**：游戏版本漂移（RVA 全失效）、协议版本不兼容（与 PC 端互相看不见）、KCP 在该机型/网络下不可用且服务端不补 TCP 入口。

### 0.1 一页速览

| 里程碑 | 一句话交付 | 人日 | 通过判据（一句话） |
|---|---|---|---|
| M0 | 一天内证明"能不能在世界里画东西" + 坐标换算对不对 | 0.5–1 | 场景里出现一个跟随任意坐标的可见对象，60 s 不崩 |
| M1 | 手机连上真服务器，拿到 playerId | 5–7 | 服务端日志有 join；设备日志有 `hello_ack`；2 s ping 稳定 |
| M2 | 中文登录页 → 加入 → 状态可见 | 2–3 | 四态截图 + 输入/连接/失败/防重入都对 |
| M3 | 手机火箭被服务端登记并按 20 Hz 上报 | 6–9 | 服务端 `rocket created parts>0`；PC 屏幕出现手机火箭 |
| M4 | 手机能看见别人（代理标记，位置+朝向+名字） | 8–12 | 双端对飞目视无跳变，坐标误差 ≤50 m，离开即消失 |
| M5 | 聊天 + 玩家表 + 重连 | 5–7 | 中文双向聊天、3 s 冷却、3 次断网自动回来 |

**最短可玩路径**：M0 → M1 → M3（别人能看到我）→ M4（我能看到别人）→ M5（能说话）。M2 可以最后补，用 `sfsmp-join.txt` 后门先跑通。

---

## 1. 起点基线（已经有什么，没有什么）

| 层 | 状态 | 证据 |
|---|---|---|
| 注入 / 反编译重打包 / 签名 | ✅ 已通 | `docs/mobile-feasibility.md` §1、§2 |
| `.so` 加载 + 延迟初始化 + `il2cpp_*` 反射 | ✅ 已通 | 同上 §8.3/§8.4（marker: `domain OK image OK Rocket OK`）|
| 主线程执行（游戏 Update 钩子） | ✅ 已通 | `v43.c:1354 hook_update`、`v43.c:1903` 替换 `WorldTime.Update` 的 `methodPointer` |
| 主菜单按钮克隆（走游戏自己的 ModGUI 面板） | ✅ 已通 | `v43.c:1-13` 注释（NOJZIZCFSEXSUEEX + `DrawFullButton`）|
| 原生输入弹窗 / 登录页（Android 视图层） | ✅ 已有骨架 | `v43.c:980 page_show_login`（Server/Username/Password 三个 EditText）|
| 协议 + 服务器 + PC 参照实现 | ✅ 完整 | `Server/cpp/src/protocol/`、`Client/src/.../MultiplayerSFS.Common/` |
| **手机端网络层（socket/KCP/握手/包）** | ❌ **0 行** | `v43.c` 全文只有 `pthread_create`，无任何 `socket/connect/sendto` |
| **手机端游戏数据读写（火箭位置/结构）** | ❌ 0 行 | 无 `Rocket` 字段读取代码 |
| **手机端远端渲染** | ❌ 0 行 | 无 |

**今天的能力边界**：能弹出一个"像游戏原生"的登录页，点按钮能回主菜单；**点"加入"之后什么都不会发生**（没有网络代码）。

---

## 2. MVP 定义

### 2.1 一句话

> **两个玩家（手机 + PC，或手机 + 手机）在同一服务器里，能在世界里互相看见对方火箭的位置与朝向、能聊天；手机端自己的飞行状态能被服务器和对方实时接收。**

### 2.2 五条必须能力（验收口径，缺一不算 MVP）

| # | 能力 | 可验收的判据 |
|---|---|---|
| A1 | 填地址/用户名/密码 → 握手成功 | 服务端日志出现该玩家 join；设备 marker 打印 `playerId / updatePeriod / worldTime`；错误密码时手机显示服务端返回的中文/英文拒绝原因 |
| A2 | 进入世界后不崩、帧率可玩 | 连续飞行 10 分钟，无 tombstone；`dumpsys gfxinfo` 或游戏内 fps ≥ 25（中端机） |
| A3 | **自己的火箭能被别人看到** | PC 端（现成模组）屏幕上出现手机玩家的火箭，位置误差 ≤ 50 m，姿态跟随；服务端日志 `rocket created: global_id=.. parts=NN` 且 `NN>0` |
| A4 | **能看到别人的火箭位置/姿态** | 手机屏幕上出现代表远端玩家的可见标记，位置误差 ≤ 50 m、朝向随对方滚转；对方销毁/离线后标记消失 |
| A5 | 聊天双向可用 | 手机发中文 → 服务端日志 `chat from N: …` 与第二客户端（第二台手机，或临时启用 `ChatWindow.CreateUI` 的 PC 调试包）显示；对方发 → 手机显示；3 s 冷却生效（第 2 条被丢弃）。PC 正式包聊天窗口当前停用（`Main.cs:100`），不作主证据 |

### 2.3 明确不做（MVP 非目标，写死在范围外）

| 不做 | 为什么现在不做 |
|---|---|
| 部件级结构同步（在手机上按 `PartState` 重建火箭外形） | 需要 `PartsLoader.CreatePart` + `PartJoint` + `JointGroup` + `staging.InsertStage` 一整套（`LocalManager.cs:1248-1321`），是本项目最重的一块；MVP 用代理标记替代 |
| 手机端建世界 / 由服务端决定难度与太阳系 / 自动进场景 | `WorldBaseManager.EnterWorld` 需要 `WorldSettings`+`SolarSystemReference`+`WorldMode`+`WorldPlaytime`+`SandboxSettings.Data` 五个构造，纯 il2cpp 组装风险高、收益低（见 §3 D1）|
| 时间加速投票（Packet 24） | 与会话稳定性耦合，MVP 期间固定 1× |
| 存档同步 / 发射台共享 / 对接（Dock） | 依赖结构同步，且服务端有专门的 `dock.cpp` 语义 |
| P2P（KCP 直连、`P2PPeerOffer`/`P2PEndpoint`） | 中转已经够用；P2P 只影响延迟，不影响"能不能玩" |
| 作弊策略 UI（Packet 22）、服务端命令、日志上传 | 与玩法无关；但**必须能安全忽略**这些包（见 §4 注意）|
| 自定义外观/图标/名牌美化 | 代理标记先用纯色块 + 名字 |

### 2.4 两条范围决策（会显著改变工作量，必须先拍板）

**D1｜不做"建世界"，改为"已在飞行中再加入"。**
手游玩家先正常开局/读档进入飞行场景，再点"多人联机"→ 加入。
- 收益：省掉 `EnterWorld` 一整条构造链（约 3–5 人日）和"进场景竞态"。
- 代价：两端世界必须**同太阳系、同难度、位置可比**（靠 `planet.codeName` 对齐）；不同存档的玩家会看到对方"漂在错误的地形高度上"。
- 缓解：MVP 验收统一用同一个 sandbox 存档 + 地球轨道起始；把"服务端决定世界"放到 M6。

**D2｜远端用"代理标记"，不做真火箭外形。**
- 收益：入站只需要 `RocketId / 位置 / 朝向`，**完全不需要反序列化 `PartState`**，M4 从"三周+"降到"两周内"（8–12 人日）。
- 代价：手机看到的别人是色块/图标（带名字与朝向），不是火箭；PC 看手机仍是**真火箭**（因为出站发的是真结构，见 M3）。
- 升级路径：M6 用 `PartsLoader.CreatePart` 路线把代理换成真模型，接口不变（只换渲染层）。

> 如果 D2 不接受（必须看到真火箭外形），M4 直接变成 15–20 人日且与 M3 强耦合，MVP 总工期 +60%。**建议先按 D2 验收，再决定是否投入 M6。**

### 2.5 MVP 之后（不进本次范围，只登记）

M6 结构还原（真火箭外形）· M7 服务端建世界/自动进场景 · M8 时间加速投票与同步时钟 · M9 P2P 直连降低延迟 · M10 手机端原生 UI 完整对齐 PC（服务器列表、Ping/人数探测帧 9–12）。

---

## 3. 里程碑 M0–M5（M0 只做 spike，M1–M5 是正式里程碑）

依赖关系（可并行的已经标出）：

```
M0 spike(1d) ──> M1 网络地基 ──┬──> M2 UI/加入流程 ──┐
                               ├──> M3 出站同步 ─────┼──> M5 聊天 + 会话稳定
                               └──> M4 入站可见 ─────┘
```

### M0 · 前置 spike（0.5–1 人日，先做，别省）

**目的**：在投入任何排期承诺前，用一天回答"M4 到底能不能做"，因为它是唯一可能推翻 MVP 的一环。

**只验证三件事（每件一个最小程序，成功即停）**
1. 能否在场景里**创建**一个可见对象：优先克隆已有对象，其次 `new GameObject` + `SpriteRenderer`，最后复用地图图标。任一成功即通过。
2. `WorldView.ToLocalPosition(myRocket.location.Value.position)` 是否等于 `myRocket.rb2d.transform.position`（误差 < 1 m）。等于 → 坐标换算路线成立。
3. 从 `hook_update` 里做上述两件事，连续 60 s 不崩、fps 下降 < 3%。

**判定**：三件全过 → M4 按 8–12 人日排；只有 1、2 过 → M4 走"标记"路线但需先解决创建；只有 3 过 → M4 只能做 HUD（M4-lite），**MVP 的 A4 降级，需要重新确认范围**。

---

### M1 · 网络地基：握手成功（不发任何游戏包）

**目标**：手机上的 `.so` 能连上真服务器，完成 Hello/HelloAck，拿到 `playerId`，并且 30 分钟内不被踢。

**交付物**
1. `mp/net.c`：`socket(AF_INET, SOCK_DGRAM)` + `connect` + 非阻塞收 + 10 ms 轮询线程。
2. KCP 驱动：直接编 `Server/cpp/third_party/kcp/ikcp.c`，参数照抄服务端 `kcp_transport.hpp`（`nodelay=1, interval=10, fastresend=0, nocwnd=1, mtu=1400`）。
3. **协议层直接复用服务器源码**（关键杠杆）：`net/lidgren_buffer.cpp`、`protocol/codec.cpp`、`protocol/packets.cpp`、`protocol/handshake.cpp` —— 这四个文件只依赖 C++ 标准库（`<cstring>/<string>/<vector>/<algorithm>/<stdexcept>`，无任何 OS/平台头；已核对 include 列表），交叉编译到 arm64 无改动。
4. 2 s 一次 `Ping`（帧 4），收 `Pong`（帧 5）；10 s 无 Pong 判定断线；`Disconnect`（帧 6）负载文本直接展示给用户。
5. 设备日志：`.../files/sfsmp-mp.log`，每行带 `mono_ms`、事件、关键字段（沿用 `v43.c:25` 的 marker 协议，另开一个文件避免和 UI 探针混在一起）。

**验收证据（可观测）**
- 服务端日志：`[cpp-server] 会话恢复/加入` 或 join 记录 + 该玩家在线人数 +1。
- 设备日志：`hello_ack playerId=3 period=50.0 worldTime=...`；`ping rtt=..ms` 连续 ≥ 20 条。
- 反例可复现：密码错误时服务端回 `Disconnect`，手机日志出现 `deny: invalid password`（文案与服务端 `kDenyInvalidPassword` 完全一致）。
- 断网 5 s → 日志出现超时 → 恢复后自动重连成功（带 `resumeToken`）。
- **不做**：此时一个游戏包都不发，PC 端看不到任何变化。

**依赖**：无（可与 M2 并行）。

**人日**：5–7（含交叉编译脚本、日志、重连）。其中"把服务器协议层编进 `.so` 并跑通 golden 向量"约 1.5 天。

**失败回退**
- 先在 **Windows 上把同一份 `net.c` 编译成 exe**，直连真服务器跑通 → 证明协议与 KCP 参数正确，再把问题锁在"Android socket/编译"这一层（这一招能省 2–3 天）。
- 若 KCP 反复不通：用 `Server/cpp/golden/wire.txt` 的握手向量做纯编解码对拍（不连网），确认是编码错还是会话错。
- 若 UDP 被运营商/校园网拦：MVP 允许走 **IPv6 + 服务器双栈**（`kcp_transport.hpp` 注释里已说明服务器默认双栈），或临时用 EasyTier 组网（`docs/ADR-002`）。

**已知坑**
- 帧长度前缀是 **4 字节小端 + 9 字节头**（`TcpFrame.cs:57-73`）；KCP 之上还有一层 4 字节长度（`kcp_transport.hpp` 的 `in_buf` 重组），**两层都不能漏**。
- `Hello.Sequence` 不是序号，是**握手版本号 2**；`HelloAck.Sequence` 必须等于 2，否则客户端判非法（`frames.cpp:87-99`）。
- 服务器在 `HelloAck` 之后会**立刻**下发 `UpdateCheatStatus(22)`、`PlayerConnected(2)`、`UpdatePlayerControl(4)`、一串 `CreateRocket(9)`、`TimeWarp(24)`。M1 阶段必须能"按类型跳过未知包"（读 1 字节 type，丢弃剩余），否则第一包就崩。

---

### M2 · 加入流程：从登录页到"已连接"（含中文 UI）

**目标**：点主菜单"多人联机"→ 原生页填 `ip[:port]` / 用户名 / 密码 → 点"加入"→ 页面状态行变"已连接（playerId=N）"，全程有明确错误提示。

**交付物**
1. 登录页字段改造：`Server` 支持 `host:port`（缺省 9806，对齐 `JoinMenu.cs:193-210`）；标签与提示改中文（技能笔记：Android 端 CJK 正常，PC 端才需要英文）。
2. 状态机：`空闲 → 连接中 → 已连接 / 失败(原因)`，状态行与按钮文案联动（连接中禁用"加入"）。
3. 后台线程 → UI 线程的结果投递（`View.post`，不要在 worker 里碰 View）。
4. 记住上次服务器/用户名（`SharedPreferences` 或 `/sdcard/.../files/sfsmp-last.txt`，避免引入 JNI 依赖可用后者）。
5. "阅读到底才可加入"的门槛保留（沿用现状），失败时不清空输入。

**验收证据**
- 截图：连接中 / 成功 / 密码错 / 地址错 四态各一张（`android/screens/`）。
- 设备日志：`ui_join click server=192.168.x.x:9806 user=...` → `hello_ack ...` 同一条会话内成对出现。
- 服务端在线人数随手机上线下变化；关闭 App 后服务端 30 s 内回收会话（`kSessionTimeoutMs=30000`）。
- 手机端连续点 5 次"加入"不会产生 5 条会话（防重入）。

**依赖**：M1 的 `mp_connect(host, port, user, pass)` 接口。

**人日**：2–3（页面骨架已在 `v43.c:980`，主要是接线 + 状态机 + 文案）。

**失败回退**
- UI 卡住时用"后门"验证网络：读 `/sdcard/.../files/sfsmp-join.txt`（内容 `ip:port|user|pass`）在 `hook_update` 里自动发起连接 —— 把 UI 问题与网络问题彻底解耦。这条后门建议**一直保留**，后面 M3/M4 的自动化验收都靠它。

**已知坑**
- 现在的页面在 `g_login_page` 存在时会重建；重建会丢已输入内容（`v43.c:988` 的 `page_hide_login` 分支），改造时先读值再重建。
- 连接是阻塞语义，**必须**放在 worker 线程；`hook_update` 只做"轮询结果 + 改 UI 文案"，单帧耗时 < 1 ms。

---

### M3 · 出站：把"我的火箭"变成服务器认识的实体（最关键的一步）

**目标**：手机上的火箭被服务端登记（`CreateRocket` 回执拿到 `globalId`），随后以 20 Hz 上报位置/姿态，PC 端能看到它、且位置正确。

**数据流（照抄 PC 的顺序，`LocalManager.cs:1248` + `ClientManager.cs:47`）**
```
本地 Rocket ──(RocketSave ctor)──> RocketSave ──(JsonConvert)──> JSON
   └─ 解析出 PartSave[]/JointSave[]/StageSave[] ──> RocketState(线格式)
        └─ CreateRocket(localId, forLaunch=false) ──> 服务端回执 globalId
             └─ UpdatePlayerControl(playerId, globalId)  ← 没这一步就没有权威，后续包被静默丢弃
                  └─ UpdateRocketPrimary @50ms / UpdateRocketSecondary @≤10Hz
```

**交付物**
1. `game_rocket.c`：主线程遍历 `Rocket`（`FindObjectsOfType`），挑 `hasControl.Value == true` 的那一枚（玩家正在飞的那枚）。
2. `RocketSave` 构造 + JSON 序列化 + 极简 JSON 解析（≈200 行，只认对象/数组/字符串/数字/布尔/null）。
3. 线格式 `RocketState` 组装：`parts`（id→PartState）、`joints`（索引→id 映射）、`stages`。
4. 发包节奏：`Primary` 50 ms（有人在飞）/ 200 ms（移动但无人控制）/ 1000 ms（静止），对齐 `RocketSyncPolicy.cs:9-13`；`Secondary` 仅在油门/RCS/转向变化时发。
5. 结构哈希：**发 0**（"旧版本，跳过检查"，`Packet_UpdateRocketPrimary.cs:21-25`），避免 PC 端因 id 规则不同而反复请求快照。
6. `WorldTime` 一律填 0：服务端会用自己时间盖章（`packets.cpp:421`），MVP 不需要时钟同步。

**验收证据**
- 服务端日志：`[cpp-server] rocket created: global_id=12 owner=3 parts=57`（`parts>0` 且等于手机端日志里的部件数）。
- PC 端截图：手机玩家火箭出现在世界中，位置与手机端日志里的 `pos=x,y` 对应；滚转姿态一致。
- 位置误差：手机每 5 s 打印自己的 `pos`；PC 端把收到的 `UpdateRocketPrimary` 落盘，比对同一时间戳的坐标，误差 ≤ 50 m。
- 反向验证：**先不发 `UpdatePlayerControl`**，确认服务端丢弃位置包（日志无 broadcast）→ 证明权威链路理解正确。
- 断线重连后不重复创建（`local_id` 去重，`packets.cpp:304-325`）。

**依赖**：M1（能发包）。**不依赖 M2**（可用 `sfsmp-join.txt` 后门触发）。

**人日**：6–9（JSON 路线）。若 JSON 路线被证伪改逐字段反射：+3～4 天。

**失败回退（三级）**
1. JSON 太大/太慢 → 只序列化一次并缓存；仍不行 → 改逐字段读 `PartSave` 的 8 个字段（`name/position/orientation/temperature/NUMBER_VARIABLES/TOGGLE_VARIABLES/TEXT_VARIABLES/burns`），字典用 `Dictionary.get_Keys/get_Values` 迭代。
2. `RocketSave` 构造在手机上崩 → 手写：`PartSave(Part)` 逐件构造（`PartSave..ctor(Part)` RVA `0x17822D0`），关节/分级用 `rocket.jointsGroup.joints` 与 `rocket.staging.stages` 自行组。
3. 全部不行 → **降级占位**：发一枚单部件火箭（部件名取游戏内必有的基础件），PC 端能看到"一个小方块"在飞。功能上满足 A3，外观退化 —— 记为 M3-lite，**不算 MVP 通过**。

**已知坑**
- `Rocket` 的类名被混淆：`SFS.World.FULMMHJZAEKABZAY`（`dump.cs:35913`），字段偏移 `rb2d=0x60 / partHolder=0x68 / arrowkeys=0x78 / throttle=0x80 / staging=0x88 / rocketName=0xB8 / jointsGroup=0xC0`；`location=0x38`、`hasControl=0x50` 在基类 `DXYTHXTABGUDBUFN`。
- `partHolder.partsSet` 是 `HashSet<Part>`（`dump.cs:2881`），遍历要按 `get_Count` + 枚举器，不能当数组下标。
- 服务端会校验 `stages`（`validate_stages`）与数值有限性；`location` 里 `address` 必须是 `planet.codeName`（`LocalManager.cs:1502`），写 `"Earth"` 这类显示名会被当另一个天体。
- 服务端 `validate_rocket` **不要求** parts 非空，所以"空结构"能过校验但 PC 端 `CreatePart` 会全部失败 → 半成品火箭被销毁（`LocalManager.cs:1272-1280`），表现为"PC 看不见手机"。**必须发真部件**。
- 50 ms 发包 + 反射读字段都在主线程：单帧预算 ≤ 2 ms，超了就降频而不是硬扛（先保帧率）。

---

### M4 · 入站：把远端玩家画到屏幕上（风险最高）

**目标**：收到别人的 `CreateRocket/UpdateRocketPrimary`，在手机世界里出现一个**可见、位置正确、朝向正确、带名字**的代理标记；对方销毁/离线即消失。

**交付物**
1. 远端表：`rocketId → {位置, 速度, 朝向, 角速度, 最后包时间, 玩家名}`，按 `worldTime` 或到达时间做 100–200 ms 插值（先做线性，够用）。
2. 坐标换算：`WorldView.ToLocalPosition(Double2)` / `ToLocalVelocity(Double2)`（`SFS.World.ZIMVTWPJZBTPPXSR`，RVA `0x16CB470` / `0x16CADE0`）——**每帧重算**，因为浮动原点会随相机移动。
3. 标记对象：优先**克隆游戏里已有的轻量对象**（技能笔记：本项目直接 `Instantiate` 曾死在 `libunity`；`v43.c:1-13` 记录 v28/v29 失败）→ 退而用 `SpriteRenderer + Sprite.Create(Texture2D)` 自建；位置 `transform.position`，朝向 `eulerAngles.z`。
4. 名字牌：优先进 Android 覆盖层（屏幕坐标 = `Camera.main.WorldToScreenPoint`），避免再建 Unity UI。
5. `DestroyRocket(10)` / `PlayerDisconnected(3)` → 移除标记；超过 3 s 没收到位置包 → 标记变灰（不要立刻删，抖动会闪）。
6. 不参与物理：标记无 `Rigidbody2D`、无碰撞体、不写 `rocket` 列表（绝不能让它进游戏的火箭集合，否则会被服务器/物理当实体）。

**验收证据**
- 双端对飞：PC 起飞绕地球一圈，手机屏幕上标记跟随，**目视无跳变**；手机日志 `remote id=12 pos=(x,y) rot=..` 与 PC 日志同一时刻误差 ≤ 50 m。
- 姿态：PC 让火箭滚转 90°，手机标记同步旋转（截图 3 连拍）。
- 生命周期：PC 终止进程 → 服务端广播 `PlayerDisconnected` → 15 s 内手机标记消失。
- 稳定性：双端在线 10 min，无 tombstone；手机 fps 下降 ≤ 5%。
- 反例：把远端坐标故意加 1000 m 偏移，标记应明显偏离 —— 证明不是"碰巧画在原地"。

**依赖**：M1（收包）。与 M3 可并行（一个只管收，一个只管发）。

**人日**：8–12。**这是全项目最不可预测的一段**，因为它同时碰"对象创建"和"浮动原点坐标"两个历史雷区。

**失败回退**
1. 对象创建路径全挂 → **地图图标路线**：复用游戏自己的 `Rocket.mapIcon`（`Rocket` 字段 `0x70`）那套图标系统，只在地图视图里显示远端玩家。
2. 地图也不行 → **HUD 方位表**：Android 覆盖层列出"玩家名 / 距离 / 高度 / 方位角"，外加一个指向最近玩家的箭头。功能上"能看到位置"，但**不算 A4 通过**（记为 M4-lite）。
3. 坐标换算错乱（标记乱飞）→ 先用"自己跟自己"验证：把本机火箭的 `location` 走一遍 `ToLocalPosition` 再和 `transform.position` 比，误差应为 0；不为 0 就是换算用错（`ToLocalPosition` 是相机相对量，不是世界坐标）。

**已知坑**
- `WorldView.ToLocalPosition` 返回的是**相对当前视图**的 Unity 坐标；相机一动就必须重算，缓存一帧就会看到标记"漂移"。
- 跨天体（`address` 不同）不能直接相减；MVP 只在同一 `codeName` 内比较，跨天体一律"不显示"。
- 标记若被 `DontDestroyOnLoad` 之外的方式挂到场景根，切场景会残留 → 场景名变化时统一清表（沿用 `hook_update` 里的 scene 检测）。
- 别在 `Update` 里对每个远端做 `FindObjectsOfType`（O(n²)），远端表要自己维护。

---

### M5 · 聊天 + 会话稳定（把"能连"变成"能待住"）

**目标**：双向聊天可用（含中文），玩家列表正确，掉线能自己回来；连续 30 min 不出现"卡死/黑屏/必须重启"。

**交付物**
1. 聊天发送：`SendChatMessage(8)` = `int32 SenderId` + 压缩字符串（`Packet_SendChatMessage.cs:20-24`）。服务端会把 `SenderId` 覆盖成认证身份（`packets.cpp:763`），本地填 0 即可。
2. 聊天接收与显示：Android 覆盖层里的消息列表（最多 200 条，滚动），格式 `[名字] 内容`；名字来自 `PlayerConnected(2)`。
3. 快速入口：飞行中一个可拖动的悬浮按钮 → 展开输入条（避免每次回主菜单）；发送后自动收起。
4. 冷却与限制：客户端本地也做 3 s 冷却（服务端硬限制 `kChatCooldownSeconds=3.0`，`packets.cpp:754`），超长截断到 512 字节。
5. 玩家表：`PlayerConnected(2)` / `PlayerDisconnected(3)` 维护 `playerId → 名字/颜色`，供标记名字与聊天前缀使用。
6. 重连：断线后 1/2/3/5 s 退避重试，带 `resumePlayerId + resumeToken`（服务端 `can_resume` 会复用手身份，`frames.cpp:116-119`）；重连成功刷新 `worldTime` 基准，不清空远端表（等新一轮快照覆盖）。
7. 前后台：App 切后台 → Unity 主线程停 → 位置停发；回前台若已超 30 s 会话超时，直接走重连分支。

**验收证据**
- 手机发"你好，我是手机端" → 服务端日志 `chat from N: 你好，我是手机端`；第二台手机（或临时打开 `ChatWindow.CreateUI` 的 PC 调试包）显示该消息；PC 回"收到" → 手机列表显示。**注意：PC 正式包的聊天窗口当前被停用**（`Main.cs:100` `// [已停用] ChatWindow.CreateUI`），不能拿它当聊天验收的主证据。
- 连发两条（间隔 < 3 s）→ 第二条被服务端丢弃（手机端本地也应拦住，日志记 `chat throttled`）。
- 512 字节以上消息被截断而不是崩溃。
- 在线 30 min、期间手动切飞行模式 5 s ×3 → 3 次都自动回到会话（日志有 `reconnect ok`），远端标记不重复、不残影。
- 服务端在线人数在手机退出后 30 s 内归零。

**依赖**：M1 + M2（要有地方显示与输入）。M3/M4 不是硬依赖，但同时跑才叫"可玩"。

**人日**：5–7。

**失败回退**
- 覆盖层聊天做不出来 → 用游戏自带的 `MsgDrawer.Log`（PC 端就是用它弹提示）显示最近一条消息，输入仍用原生页；记为 M5-lite。
- 重连反复失败 → 保守策略：直接 `SceneLoader.ExitToMainMenu()` 回主菜单（PC 端的旧行为，`ClientManager.cs:89-92`），提示"连接已断开，请重新加入"。

**已知坑**
- 服务端拒收含控制字符的消息（`ch < 0x20 || 0x7F`），但中文 UTF-8 字节都 ≥ 0x80，**可以正常发**；不要按"ASCII only"自我审查。
- 聊天广播是 `broadcast ... -1`（含发送者自己），所以本地不要重复插入一条，否则自己看到两遍。
- `PlayerConnected` 在初启状态里会把自己也发一遍（`initial_state.cpp:46-58`），去重按 `playerId`。
- 重连时服务端会重发整份初启状态（含 `CreateRocket` 全量），远端表必须"按 id 覆盖"而不是"追加"，否则会看到重影。

---

## 4. 每个里程碑的最小 API / 字段 / 消息集

> 类名混淆的用**混淆名**给出，方法名未混淆的直接写方法名。RVA 只对 1.6.00.22 有效。
> 偏移是 `dump.cs` 里的字段偏移（托管对象内偏移），**不要**和 RVA 混用。

### 4.1 游戏 API / 字段

| 里程碑 | 类（命名空间） | 字段 / 方法 | 用途 | 证据 |
|---|---|---|---|---|
| M1 | —（仅 libc） | `socket/bind/sendto/recvfrom/poll`、`pthread_create`、`clock_gettime` | UDP + KCP 驱动线程 | — |
| M1 | 复用服务器源码 | `protocol/codec.cpp`、`protocol/handshake.cpp`、`protocol/packets.cpp`、`net/lidgren_buffer.cpp`、`third_party/kcp/ikcp.c` | 帧/握手/包编解码 + KCP | `#include` 只有 C++ 标准库，无 OS/平台头 |
| M2 | `SFS.World.VCQSQMYGGZECNBPE`（WorldTime） | `Update()`（主线程钩子，已替换 `methodPointer`） | 所有游戏 API 的唯一执行点 | `v43.c:1354/1903` |
| M2 | `UnityEngine.SceneManagement.SceneManager` | `GetActiveScene().name` | 判断是否在飞行场景（`Home_PC/Build_PC/Hub_PC`） | `ClientManager.cs:232` |
| M3 | `SFS.World.FULMMHJZAEKABZAY`（Rocket） | `rocketName`(0xB8)、`rb2d`(0x60)、`partHolder`(0x68)、`arrowkeys`(0x78)、`throttle`(0x80)、`staging`(0x88)、`jointsGroup`(0xC0)；基类 `location`(0x38)、`hasControl`(0x50) | 找本地火箭、读姿态/输入/结构 | `dump.cs:35913`、`dump.cs:21648` |
| M3 | `SFS.World.FNVQPOCIQHTEDVSZ`（Location 容器） | `get_Value()` RVA `0x1701F70` → `SFS.World.Location`：`position`(Double2)、`velocity`(Double2)、`planet`(0x18) | 位置/速度/天体 | `dump.cs:30939`、`dump.cs:7398` |
| M3 | `SFS.World.MUENIWBWFGLMAZNZ`（Planet） | `codeName` | 线格式里的 `address` | `LocalManager.cs:1502` |
| M3 | `SFS.World.RocketSave` | `.ctor(Rocket)` RVA `0x1852640`；字段 `rocketName/location/rotation/angularVelocity/throttleOn/throttlePercent/RCS/parts/joints/stages` | 一次性拿到整枚结构 | `dump.cs:8472` |
| M3 | `SFS.Parts.PartSave` | `.ctor(Part)` RVA `0x17822D0`；字段 `name/position/orientation/temperature/NUMBER_VARIABLES/TOGGLE_VARIABLES/TEXT_VARIABLES/burns` | 单件序列化（JSON 路线时由 Newtonsoft 代劳） | `dump.cs:40433` |
| M3 | `SFS.World.JointSave` / `StageSave` | `partIndex_A/B`；`stageId` + `partIndexes[]` | 索引→id 映射 | `dump.cs:8499`、`dump.cs:50926` |
| M3 | `Newtonsoft.Json.JsonConvert` | `SerializeObject(object, Formatting, JsonSerializerSettings)` RVA `0x29EDE84`（`Formatting.None=0`，settings 传 NULL） | 把 `RocketSave` 变成可解析 JSON，省掉逐字段/字典迭代 | `dump.cs:479076` |
| M3 | `SFS.Variables.Bool_Local` / `Float_Local` / `Vector2_Local` | `.Value` 字段 | 油门/RCS/转向输入 | `dump.cs:17954` |
| M3 | `SFS.World.WMCZPVIROBWPMJFG`（Arrowkeys） | `rcs`(0x38)、`turnAxis`(0x48)、`rawArrowkeysAxis`(0x50)、`horizontalAxis`(0x58)、`verticalAxis`(0x60) | Secondary 包 | `dump.cs:52613` |
| M3 | `SFS.World.AKQXNLQKSRJXPQGE`（Throttle） | `throttleOn`(0x38)、`throttlePercent`(0x40) | Secondary 包 | `dump.cs:44665` |
| M3 | `SFS.World.WQPAXLGGCOAUZEOA`（RocketManager） | 仅用于确认"我没把代理标记塞进火箭集合" | 反向校验 | `dump.cs:22853` |
| M4 | `SFS.World.ZIMVTWPJZBTPPXSR`（WorldView，`dump.cs:26023`） | 静态 `ToLocalPosition(Double2)` RVA `0x16CB470`、`ToLocalVelocity(Double2)` RVA `0x16CADE0` | 双精度世界坐标 → Unity 视图坐标 | `dump.cs:26086/26061` |
| M4 | `UnityEngine.GameObject` / `Transform` / `SpriteRenderer` / `Sprite` / `Texture2D` | 构造、`set_position`、`set_eulerAngles`、`Sprite.Create` | 代理标记 | —（UnityEngine.CoreModule）|
| M4 | `UnityEngine.Camera` | `main`、`WorldToScreenPoint` | 名字牌贴屏 | — |
| M5 | `SFS.UI.MsgDrawer`（可选） | `main.Log(string)` | M5-lite 的兜底显示 | `ClientManager.cs:66` |

**不需要（MVP 明确不碰）**：`PartsLoader.CreatePart`、`PartJoint`、`JointGroup`、`Stage`、`Stage.insert`、`WorldBaseManager.EnterWorld`、`Interpolator` 全套、`Rocket.physics.SetLocationAndState`。这些是 M6/M7 的账。

### 4.2 最小网络消息集

帧层（`TcpFrameKind`，1 字节 kind + 4 字节 seq + 4 字节 bits）：

| 里程碑 | 帧 | 方向 | 频率 | 备注 |
|---|---|---|---|---|
| M1 | `Hello=1` | C→S | 首次 + 200 ms→2 s 退避重发 | `Sequence=2`（握手版本）|
| M1 | `HelloAck=2` | S→C | 一次 | 解析出 `playerId/updatePeriod/chatCooldown/worldTime/difficulty/udpToken/resumeToken` |
| M1 | `Ping=4` / `Pong=5` | 双向 | 2 s | 10 s 无 Pong 判死 |
| M1 | `Disconnect=6` | S→C | 事件 | 负载是拒绝原因文本，直接显示 |
| M2 | `ServerInfoRequest=9/Response=10`、`ServerVersionRequest=11/Response=12` | 双向 | 可选 | 登录页"Ping/人数/版本"那行，**MVP 可不做** |

包层（1 字节 `PacketType` + Lidgren 位流）：

| 里程碑 | 包 | 方向 | 频率 | 说明 |
|---|---|---|---|---|
| M3 | `CreateRocket=9` | C→S | 每枚一次（可重发） | `localId` 去重，`forLaunch=false`，`Rocket` 为全量状态（含真部件表）|
| M3 | `UpdatePlayerControl=4` | C→S | 切换控制时 | 不发就没有权威，后续包被丢弃（`packets.cpp:411-417`）|
| M3 | `UpdateRocketPrimary=11` | C→S | 50/200/1000 ms | 位置、速度、朝向、角速度；`StructureHash=0` |
| M3 | `UpdateRocketSecondary=12` | C→S | ≤10 Hz，变化时 | 油门/RCS/四路输入 |
| M4 | `CreateRocket=9` | S→C | 初启全量 + 增量 | MVP 只取 `globalId/location/rotation/rocketName`，**忽略 parts** |
| M4 | `UpdateRocketPrimary=11` | S→C | 同上 | 远端插值源 |
| M4 | `DestroyRocket=10` | S→C | 事件 | 删标记 |
| M4 | `PlayerConnected=2` / `PlayerDisconnected=3` | S→C | 事件 | 名字表 |
| M4 | `UpdateWorldTime=6` / `TimeWarp=24` | S→C | 周期/事件 | MVP 只记录不使用（插值用到达时间）|
| M5 | `SendChatMessage=8` | 双向 | 用户触发（3 s 冷却）| 中文 UTF-8 可行 |
| M5 | `ShowToastMessage=21` | S→C | 事件 | 可选：服务端提示（如"玩家 X 加入"）|

**必须"安全忽略"的包**（收到就当没看见，不能崩、不能错位）：
`UpdateCheatStatus=22`（握手后第一包就会来）、`UpdatePlayerColor=7`、`RenameRocket=27`、`UpdatePartTemperature=28`、`UpdateDestructionReason=29`、`ServerCommand=30`、`UploadLog=31`、`P2PPeerOffer=26`、`P2PEndpoint=32`、以及所有 15–20 的部件模块包。实现方式：读到类型字节后，若不在白名单，**直接丢弃整个 payload**（不要尝试跳过字段）。

---

## 5. 工作量汇总

| 里程碑 | 内容 | 人日（低–高） | 关键路径 |
|---|---|---|---|
| M0 | spike：对象创建 / 坐标换算 / 稳定性 | 0.5–1 | ✅ 先做（决定 M4 形态）|
| M1 | 网络地基 + 握手 + KCP + 重连 | 5–7 | ✅ 关键路径起点 |
| M2 | 加入 UI（中文）+ 状态机 | 2–3 | 可并行 |
| M3 | 出站结构 + 位置同步 | 6–9 | ✅ 关键路径 |
| M4 | 入站代理标记 + 坐标换算 | 8–12 | ✅ 关键路径（最长）|
| M5 | 聊天 + 玩家表 + 稳定性 | 5–7 | 收尾 |
| — | 集成联调 / 双端回归 / 文档 | 3–5 | — |
| **合计** | | **30–44** | |

按 20% 缓冲：**36–53 人日 ≈ 7.5–11 周**（单人）。
并行度：M3/M4 由两人同时做可压到 **约 6 周**（M4 仍是瓶颈）。
不含：M6 结构还原（15–20 人日）、M7 建世界（5–8 人日）、美术/图标、服务器运维。

---

## 6. 风险登记表

标记说明：⛔ = **会导致整条路线作废**（或该里程碑的核心验收无法达成）；⚠️ = 影响工期/体验但可绕。

| ID | 类别 | 风险 | 概率 | 影响 | 早期信号 | 缓解 / 回退 | 作废级 |
|---|---|---|---|---|---|---|---|
| R1 | 技术 | 游戏内**无法创建自有对象**（`Object.Instantiate` 曾在 `libunity` 内崩；`new GameObject` 未验证） | 中高 | M4 无法在世界里画标记 → A4 不成立 | 第一次尝试创建标记就 tombstone，`Cause: null pointer dereference` 落在 `libunity` | ①先克隆场景里已有对象；②退地图图标；③退 HUD 方位表。**先花 1 人日做 spike 验证三条路** | ⛔（若三条全不可用）|
| R2 | 技术 | 浮动原点坐标换算错（`WorldView.ToLocalPosition` 用法/时机不对） | 高 | 标记乱飞或永远在原点 | 自机位置换算后与 `transform.position` 不等 | 用"自己跟自己"校准（§3 M4 回退 3）；每帧重算，不缓存 | ⚠️ |
| R3 | 技术 | 在 worker 线程碰 GameObject/场景 API → SIGSEGV | 中 | 随机崩溃，难定位 | tombstone 的 `lib<ours>.so` 帧出现在 socket 线程 | 纪律：worker 只做 socket/KCP/编解码；所有游戏 API 只在 `hook_update` 内；代码评审强制 | ⚠️（已有明确证据与规则）|
| R4 | 技术 | `RocketSave`/`PartSave`/Newtonsoft 序列化在手机上崩或 >50 ms | 中 | 出站结构发不出 → PC 看不见手机 | 第一次 `CreateRocket` 前崩溃，或单帧 >100 ms | 三级回退（JSON → 逐字段 → 单部件占位）；序列化只做一次并缓存 | ⚠️ |
| R5 | 技术 | KCP/UDP 在移动网络、校园网、双栈环境下连不通或抖动严重 | 中 | 连不上 = 全线不可用；抖动 = 体验差 | `hello` 重发 10 s 无 ack；`ping rtt` 波动 >500 ms | 服务器已双栈；可走 EasyTier；抖动靠 200 m 硬同步 + 插值；**兜底需服务端补 TCP 入口**（`net/tcp_transport.hpp` 还在，约 1–2 人日）| ⛔（仅当 UDP 完全不可用且不改服务端）|
| R6 | 兼容 | 游戏版本升级 → RVA / 混淆类名全失效 | 高（时间问题，必然发生） | 所有反射调用失效，App 起不来 | 启动自检：关键类解析返回 NULL | ①所有 RVA 集中到一张表 + 启动逐项校验；②失败时降级为"仅 UI/聊天"而不是崩溃；③锁版本（1.6.00.22）+ 升级清单 | ⛔（对未适配的新版本）|
| R7 | 兼容 | 协议版本漂移（`ProtocolVersion=32`、`SessionHandshakeCodec.Version=2`）与 PC 端/服务端不一致 | 中 | 握手被拒或包解析错位 → 互相看不见 | 服务端日志 `Handshake mismatch`；或收到包就错乱 | 协议常量集中在 `mp_proto.h`，与 PC 端同一提交升级；登录页显示双方版本号 | ⛔（不跟随就整条断）|
| R8 | 兼容 | 非 arm64 设备 / Android < 8 | 低 | 直接不支持 | 安装即失败 | 声明最低要求：arm64-v8a + Android 8+；安装脚本检查 `ro.product.cpu.abilist` | ⚠️ |
| R9 | 兼容 | 反射 + 物理同帧争 CPU → fps < 20 | 中 | "能玩"变"不能玩" | 连续 3 帧 >50 ms | 50 ms 发包降频；每帧只处理 1 枚火箭的结构；标记更新分帧 | ⚠️ |
| R10 | 时间 | M4 估算偏差（历史雷区叠加） | 高 | 工期 +2 周 | spike 超过 2 天还没画出第一个标记 | **先 spike 再承诺**；M4 与 M3 拆给两人并行；M4-lite（HUD）先交付 | ⚠️ |
| R11 | 数据 | 两端世界不一致（不同存档/难度/太阳系）→ 位置"看起来错" | 中 | 验收误判，白查一周 | 同一位置在两端高度差 >1 km | D1 限定：统一 sandbox 存档 + 校验 `planet.codeName` 与难度；不匹配就在 UI 明示 | ⚠️ |
| R12 | 服务端 | 我们发的 `CreateRocket` 被 `validate_rocket` 拒绝 | 中 | 火箭不出现，且客户端无感 | 服务端日志 `拒绝 CreateRocket ... 原因=` | 用 PC 端把同一 `RocketState` 发一遍做对照；把拒绝原因回显到手机日志 | ⚠️ |
| R13 | 会话 | 切后台/锁屏 → 30 s 会话超时 | 高（必发生） | 回来就掉线 | 回前台第一次发包被丢 | 回前台先探测（`Ping`），失败直接重连；重连不清远端表 | ⚠️ |
| R14 | 时间 | 上下文/API 中断导致研究或实现半途而废 | 中 | 返工 | — | 本文档分节落盘；代码按里程碑独立可编译（每个 `.c` 单独能编） | ⚠️ |

**"整条路线作废"的判定线（三条，任一成立就要重新评估方案）**
1. **R1 的第三条也不成立**：既不能创建对象、也不能复用地图图标、连 HUD 都做不了 —— 实际上 HUD 只是 Android 视图，几乎不可能失败，所以这条概率极低，但它决定 A4 是"世界内可见"还是"仅数值"。
2. **R6**：目标机型上的游戏版本升级且我们跟不上 —— 届时不是"重做映射"而是"重做整份 dump 适配"，属版本级作废。
3. **R7**：PC 端/服务端协议升级而手机端不跟随 —— 直接互相看不见。

---

## 7. 验收脚本（一次跑完 M1–M5）

**环境**：服务器（本机或局域网，默认 9806）· PC 端装现成模组 · 手机装我们的包 · 两端同一 sandbox 存档/同一太阳系。

| 步 | 操作 | 期望证据 |
|---|---|---|
| 1 | 手机开 App → 主菜单 → "多人联机" | 出现原生登录页（中文），无黑屏 |
| 2 | 填 `IP:9806` / 用户名 / 密码 → 加入 | 状态行"已连接 playerId=N"；服务端日志 join；设备日志 `hello_ack` |
| 3 | 手机进飞行（先单机起飞或读档） | 设备日志 `local rocket found id=.. parts=NN` |
| 4 | 观察 PC 端 | PC 屏幕出现手机火箭；服务端日志 `rocket created ... parts=NN` |
| 5 | PC 起飞，绕行 60 s | 手机出现标记并跟随；两端坐标误差 ≤50 m；手机 fps ≥25 |
| 6 | PC 滚转 90° | 手机标记同步旋转 |
| 7 | 手机发"你好" | 服务端日志 `chat from N: 你好`；第二台手机/PC 调试包显示；3 s 内再发被服务端丢弃 |
| 8 | PC 发"收到"（或第二台手机发） | 手机列表显示，前缀为发送者名字 |
| 9 | PC 强杀进程 | 15 s 内手机标记消失，玩家表移除 |
| 10 | 手机切飞行模式 5 s ×3 | 每次自动重连成功，标记不重影；日志 `reconnect ok` ×3 |
| 11 | 连续在线 30 min | 无 tombstone；`sfsmp-mp.log` 无 `E/` 级错误 |

**证据归档**：`android/screens/mvp_*.png` + `android/research/memory_injection/evidence/mvp_run_<date>/`（两端日志、服务端日志切片、fps 采样）。

---

## 8. 需要拍板的三件事（阻塞排期，不阻塞开工）

1. **D2 是否接受**"远端是代理标记而不是真火箭"？—— 决定 M4 是 8–12 人日还是 20+ 人日。
2. **D1 是否接受**"先自己进飞行再加入"？—— 决定要不要现在做 `EnterWorld` 那条链。
3. **是否允许服务端补一个 TCP 入口**作为 UDP 兜底？—— 1–2 人日，能把 R5 从"作废级"降为"⚠️"。

---

## 9. 证据索引（本文所有断言的可查来源）

| 断言 | 来源 |
|---|---|
| 传输是 **KCP over UDP**，帧是 `TcpFrame` 套在 KCP 消息里 | `TcpTransportCore.cs:157-266`（`SendKcpMessage(helloData)`）、`kcp_transport.hpp`（conv 复用、4 字节长度重组）|
| 帧格式 4 字节长度 + kind/seq/bits | `TcpFrame.cs:57-73` |
| 握手魔数 `SSF1`、版本 2、字段顺序 | `SessionHandshakeCodec.cs:14-57`、`handshake.hpp:20-42` |
| 握手错误文案与拒绝顺序 | `frames.cpp:87-149` |
| 初启状态顺序（cheat → players → snapshot → timewarp）| `initial_state.cpp:20-80` |
| 权威门槛：无 `UpdatePlayerControl` 则位置包被丢 | `packets.cpp:251-256, 411-417` |
| 服务端给世界时间盖章 | `packets.cpp:418-429` |
| 聊天 3 s 冷却 / 512 字节 / 控制字符过滤 | `packets.cpp:741-767` |
| `RocketSyncPolicy` 50/200/1000 ms | `RocketSyncPolicy.cs:9-13` |
| PC 端建火箭的完整 API 链 | `LocalManager.cs:1248-1321` |
| `address = planet.codeName` | `LocalManager.cs:1495-1502`（`ToNetLocation` 第 1502 行）|
| Rocket/PartSave/RocketSave 字段与 RVA | `dump.cs:35913 / 40433 / 8472` |
| WorldView 静态换算 | `dump.cs:26061, 26086` |
| 主线程约束与钩子实现 | `docs/mobile-feasibility.md:271-290`、`v43.c:1354, 1903` |
| `Instantiate` 历史失败 | `v43.c:1-13` |

