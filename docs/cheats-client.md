# 客户端侧「服务端可控的作弊开关」

> 状态：已实现并编译通过（`dotnet build Client\src\MultiplayerSFS.Client\MultiplayerSFS.Client.csproj -c Release` → 0 error）。
> 对应任务：给客户端模组实现服务端可控的作弊开关（客户端侧）。服务端实现（ServerCpp）**不在本次范围**。

## 1. 职责划分

| 侧 | 做什么 |
| --- | --- |
| 服务端 | 读自己的配置（总开关 + 每项小配置），在玩家加入/配置变更时下发 `Packet_UpdateCheatStatus`（编号 22） |
| 客户端 | 接收策略并**应用**：隐藏/禁用作弊入口、强制关闭被禁单项、拦截玩家再次打开 |

客户端**没有**自己的作弊配置文件：策略完全来自服务端。没收到策略时（单机、离线、连到不发策略的老服务端）行为与打补丁之前完全一致。

## 2. 协议：PacketType.UpdateCheatStatus（编号 22，永不改号）

编号 22 原定义是「客户端上报 8 个 bool 作弊状态」，但服务端一直把 22 当非法空位，客户端也从没有发送点 —— 该语义从未上线。
现在重定义为**服务端 → 客户端**的策略下发包，线上布局（按顺序，无 padding）：

| 偏移 | 类型 | 字段 | 含义 |
| --- | --- | --- | --- |
| 0 | `bool` | `Enabled` | 总开关。`false` = 客户端隐藏/禁用整个作弊入口 |
| 1 | `byte` | `IndividualFlags` | 6 位位图，逐项控制能否打开（见下表） |

`IndividualFlags` 位定义（`CheatFlag` 枚举，**位号是协议的一部分，不得重排**）：

| bit | 值 | 名称 | 对应游戏标识符 |
| --- | --- | --- | --- |
| 0 | 0x01 | InfiniteFuel | `SandboxSettings/Data::infiniteFuel` |
| 1 | 0x02 | InfiniteBuildArea | `SandboxSettings/Data::infiniteBuildArea` |
| 2 | 0x04 | NoGravity | `SandboxSettings/Data::noGravity` |
| 3 | 0x08 | NoHeatDamage | `SandboxSettings/Data::noHeatDamage` |
| 4 | 0x10 | UnbreakableParts | `SandboxSettings/Data::unbreakableParts` |
| 5 | 0x20 | Teleport | `SandboxSettings::teleportButton` / `TeleportMenu::OpenFromCheats()` |
| 6-7 | 0x40/0x80 | （保留） | 必须写 0，客户端忽略 |

语义：位 = 1 → 允许（**允许但不强制开启**）；位 = 0 → 禁止（强制关闭 + 拦截点击）。
其余作弊项（`noAtmosphericDrag` / `noBurnMarks` / `partClipping`）不在本次 6 项内，客户端保持原样放行。

代码位置：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Common/Packet_UpdateCheatStatus.cs`
（`CheatFlag` 枚举与该包同文件，保证客户端/服务端字段定义只有一个来源）。
保留了一个带参构造 `Packet_UpdateCheatStatus(bool enabled, CheatFlag individualFlags)`，供将来客户端上报使用；本次客户端**不发送**。

## 3. 客户端应用逻辑

策略持有类：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/CheatPolicy.cs`
Harmony 补丁：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod.Patches/CheatPolicyPatches.cs`

### 3.1 总开关 `Enabled = false`：入口不可见/不可用

| 目标 | 补丁点（类 + 方法） | 动作 |
| --- | --- | --- |
| 暂停菜单里的 "Open Cheats" 按钮 | `SFS.UI.ButtonBuilder.OnCreated`（Postfix） | 按本地化文案 `Loc.main.Open_Cheats_Button` 识别并 `SetActive(false)` |
| 屏幕上的作弊抽屉 | `SFS.UI.CheatsDrawer.UpdateText`（Postfix） | 强制 `gameObject.SetActive(false)` |
| 解锁/购买按钮 | `SFS.World.SandboxSettings.OnOpen` + `ShowUnlockCheats` | `OnOpen` 后隐藏 `unlockCheatsButton` / `buyCheatsButton`；`ShowUnlockCheats` 结果强制为 `false` |
| 作弊菜单本体 | `SFS.World.SandboxSettings.UpdatePreventUse` / `OnOpen`（Postfix） | 强制显示游戏自带的 `preventUse_Cheats` 遮罩（世界 `AllowsCheats=false` 时游戏本就会显示它，不会冲突） |

### 3.2 单项 `IndividualFlags` 位 = 0：对应项必须关掉且不能打开

| 单项 | 补丁点（类 + 方法） | 动作 |
| --- | --- | --- |
| InfiniteFuel | `SandboxSettings.ToggleInfiniteFuel`（Prefix） | 返回 `false` → 原方法不执行，点击无效 |
| InfiniteBuildArea | `SandboxSettings.ToggleInfiniteBuildArea`（Prefix） | 同上 |
| NoGravity | `SandboxSettings.ToggleNoGravity`（Prefix） | 同上 |
| NoHeatDamage | `SandboxSettings.ToggleNoHeatDamage`（Prefix） | 同上 |
| UnbreakableParts | `SandboxSettings.ToggleUnbreakableParts`（Prefix） | 同上 |
| Teleport | `SFS.World.TeleportMenu.OpenFromCheats`（Prefix） | 返回 `false` → 传送菜单打不开；同时 `OnOpen` 里 `teleportButton.SetEnabled(false)` |

兜底：`SandboxSettings.OnToggle`（Postfix，所有作弊切换的汇聚点，会触发 `onToggleCheat`）与 `SandboxSettings.OnOpen`（Postfix）都会调用
`CheatPolicy.EnforceSettings`，把被禁项在 `SandboxSettings/Data` 里**强制拨回 `false`** 并刷新 UI。
所以即使策略到达时该项已经是开的，也会立即被关掉，而不是等到玩家再点一次。

> 说明：任务里点名的 `OnCheatsChange` 是 `SFS.Builds.BuildCamera` 的私有方法，它只是重算相机距离（订阅 `SandboxSettings.onToggleCheat`），
> 并不是一个能拦住作弊状态的落点；真正的汇聚点是 `SandboxSettings.OnToggle`/`onToggleCheat`，已在上面的 Postfix 里处理。

### 3.3 策略到达 / 断线

* `ClientManager.HandlePacket` 新增 `case PacketType.UpdateCheatStatus` → `CheatPolicy.Receive(...)`（主线程 `UpdateNetwork` 调用，可安全操作 Unity 对象）。
* `Receive` 打印一行 ASCII 策略日志，并立即对当前世界应用（强制关被禁项 + 隐藏抽屉）。
* `ClientManager.Disconnect(string)` 开头调用 `CheatPolicy.OnDisconnected()`：**保留**最后一次策略，只打一行日志说明（需求允许保留）。

## 4. 离线 / 未连服务端

`CheatPolicy.HasPolicy` 默认为 `false`，此时：

* `BlocksEntry == false` → 不隐藏任何入口；
* `AllowToggle(...) == true` → 不拦截任何点击；
* `ForcesOff(...) == false` → 不改世界设置。

即行为与今天完全一致。只有真正收到过策略才生效。

## 5. 诊断日志

统一前缀 `[SFS-MP][cheat]`，内容为纯 ASCII，可直接在游戏日志/服务端日志里 grep：

* `policy <- server: enabled=true individualFlags=0x0F [InfiniteFuel,InfiniteBuildArea,NoGravity,NoHeatDamage]` —— 策略变化
* `BLOCK ToggleNoGravity: item 'NoGravity' is disabled by server policy (flags=0x0F)` —— 拦截点击
* `BLOCK ToggleInfiniteFuel: total cheat switch is OFF (server policy); ignoring click` —— 总开关拦截
* `FORCE OFF 'NoGravity': disabled by server policy (flags=0x0F) at menu open` —— 强制拨回
* `hide 'Open Cheats' menu entry button: entry disabled by server policy`
* `hide CheatsDrawer: entry disabled by server policy`
* `disconnected from server; RETAINING last cheat policy (...)` —— 断线保留

## 6. 自测方式

无法在本次任务里启动游戏，按下面顺序人工验证：

1. **编译**：`dotnet build Client\src\MultiplayerSFS.Client\MultiplayerSFS.Client.csproj -c Release`，要求 0 error。
2. **离线基线**：不连服务端进世界，作弊菜单、抽屉、各单项应与改动前完全一致（日志里不应出现 `[SFS-MP][cheat]`）。
3. **总开关关闭**：服务端下发 `Enabled=false`（`IndividualFlags=0x00`）。预期：暂停菜单不再出现 Open Cheats 按钮；已打开的作弊菜单显示游戏的 "禁止使用作弊" 遮罩；`CheatsDrawer` 消失；日志出现 `policy <- server: enabled=false` 与 `hide ...` 行。
4. **单项关闭**：下发 `Enabled=true, IndividualFlags=0x1F`（仅 Teleport 关）。预期：点 NoGravity 等能正常切换；点 Teleport 无效且日志有 `BLOCK ... Teleport`；若 NoGravity 下发前已是开的，`OnToggle`/`OnOpen` 会看到 `FORCE OFF 'NoGravity'`。
5. **单项打开**：`IndividualFlags` 补上对应位后确认可以正常打开（客户端不主动替玩家打开）。
6. **兼容 `AllowsCheats=false` 的世界**：在职业/不允许作弊的存档里重复 3、4，确认不抛异常（所有字段访问均有 null 保护）。
7. **断线**：连上拿到策略后断开，确认日志有 `RETAINING last cheat policy`，且本地仍按最后策略执行。

## 7. F8 本地测试（不依赖服务端）

服务端尚未接入 `Packet_UpdateCheatStatus` 时，用 F8 面板在不联机、不改任何配置的情况下验证客户端补丁。

打开方式：进游戏后按 **F8**（窗口标题 `... - Network Debug`），在「超级调试」等原有调试项下方的 **「作弊策略（本地测试）」** 分组里操作：

| UI 元素 | 作用 |
| --- | --- |
| 总开关「允许作弊入口」 | 对应 `Enabled`：取消勾选 = 隐藏/禁用作弊入口 |
| 无限燃料 / 无限建造区 / 无重力 / 无热损伤 / 零件不损坏 / 传送 | 六个单项勾选框，分别对应 bit0..bit5（`CheatFlag`） |
| 「清除本地覆盖（回到服务端策略）」 | 调 `CheatPolicy.ClearLocalOverride()` |

* **勾选即刻生效**：任一开关变化都会立刻调用 `CheatPolicy.SetLocalOverride(enabled, flags)`，无需重启游戏或重进世界；总开关与六个单项写入同一份覆盖，改哪一项都会把当前生效的整份策略提交为覆盖。
* **默认不动任何开关时**：`HasLocalOverride == false`，行为与今天完全一致（离线全放行、日志里不出现 `[SFS-MP][cheat]`）。
* **状态行**：显示当前是否本地覆盖、生效的 `Enabled`、生效 `flags`（十六进制 `0xNN`），以及「服务端策略：有/无」（有则附带服务端的 `enabled` / `flags`）。

优先级：**本地覆盖 > 服务端策略 > 默认全放行**。

* 没有覆盖时：服务端策略到达就按服务端策略执行（与未加本地测试前一致）。
* 有覆盖时：所有判定（`AllowToggle` / `BlocksEntry` / `IsAllowed` / `ForcesOff`）一律以覆盖为准；服务端策略仍会被接收并保存，但暂时不生效，状态行标明「本地覆盖生效中，服务端策略被暂时忽略」。
* 点「清除本地覆盖」后：立即回到服务端策略；若服务端从未下发，则回到全放行。

日志（纯 ASCII，前缀 `[SFS-MP][cheat]`）：

* `LOCAL OVERRIDE enabled=true flags=0x3F [InfiniteFuel,InfiniteBuildArea,NoGravity,NoHeatDamage,UnbreakableParts,Teleport]`
* `LOCAL OVERRIDE cleared`
## 8. 风险与做不到的事（重要）

* **这是客户端侧策略，不等于反作弊。** 改过 DLL、或不上这个 mod 的客户端可以绕过所有拦截；
  联机世界想要**硬约束**，必须由服务端在收到相关动作/状态后主动拒绝或回滚（本次不做）。
* 客户端拦截只作用于**本机 UI 与本地世界设置**；它不会、也无法阻止别的玩家（或被改过的客户端）使用作弊效果。
* 断线后**保留**最后一次策略：如果之后连到一个不发策略的老服务端，会继续沿用旧策略（有日志可查）。要改成"每次连接先清空"需要额外的连接开始钩子，本次未做。
* "Open Cheats" 按钮是 `ButtonBuilder` 运行时按本地化文案动态生成的，客户端靠**文案匹配**识别；
  换语言/换游戏版本文案变化时可能识别失败 —— 失败时按钮仍可见，但单项拦截与抽屉隐藏仍生效（降级为"不可用"而非"不可见"）。
* 补丁依赖 v16（`Client/refs/v16`）里 `SandboxSettings` / `CheatsDrawer` / `TeleportMenu` / `ButtonBuilder` 的成员名（Beebyte 未混淆这些成员）。
  本次只按需求验证了默认 v16 构建；`-p:GameVersion=v15` 的 1.5 包未验证（若 1.5 缺少同名成员，需要按 `Port15/PORT-NOTES.md` 的约定加 `#if SFS15` 分支或改用反射）。
* `PacketType.cs` 里关于 22 的旧注释（"服务端视为非法空位"）按写作用域要求**未改动**，以本文件与 `Packet_UpdateCheatStatus.cs` 为准。

## 9. 涉及文件

* 新增：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/CheatPolicy.cs`
* 新增：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod.Patches/CheatPolicyPatches.cs`
* 改写：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Common/Packet_UpdateCheatStatus.cs`
* 改动：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/ClientManager.cs`（`HandlePacket` 加 case + `Disconnect` 调 `OnDisconnected`）
* 改动：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/NetworkDebugOverlay.cs`（F8 面板新增「作弊策略（本地测试）」分组）
* 新增：`docs/cheats-client.md`（本文件）
