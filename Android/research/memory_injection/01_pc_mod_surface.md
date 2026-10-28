# 01 · PC 联机模组的游戏 API 依赖面（安卓移植清单）

> 只读材料：`Client/src/MultiplayerSFS.Client/`（MultiplayerSFS.Mod / .Patches / .Common）、`android/dump/dump.cs`（1.6.00.22，含 Assembly-CSharp 与全部依赖程序集）、`docs/mobile-feasibility.md`。
> 本文只做**依赖面测绘**，不含实现代码；所有"安卓是否存在"的结论都来自 dump.cs 的类/方法/字段检索，不是推测。
> 生成时间：2026-10-21。报告路径：`android/research/memory_injection/01_pc_mod_surface.md`。

---

## 0. 结论速览

1. **依赖面规模**：15 个 patch 文件、68 个 `[HarmonyPatch]` 标记、58 个 patch 方法（33 Prefix / 18 Postfix / 6 Transpiler / 1 Finalizer）；把 `TargetMethods()` 展开后覆盖 **73 个游戏方法**。`Mod/*.cs` 里**没有 Harmony patch**（全仓仅 `LocalManager.cs:174` 一处 `AccessTools.FieldRefAccess` 读 `RocketManager.rocketPrefab`），所以"patch 依赖面"= `Mod.Patches/*.cs`，"运行时依赖面"= `Mod/*.cs` 的三条路径（读状态 / 建火箭 / 应用状态）。

2. **混淆比可行性报告更重**（实测统计，见 §1.2）：Assembly-CSharp 里类名 **10.1%**、方法名 **26.2%**、字段名 **6.0%** 被替换成随机大写串。比例不高，但**模组要碰的核心类几乎全落在被混淆的 10% 里**：`Rocket / RocketManager / ScreenManager / GameManager / WorldTime / BuildManager / PlayerController / SavingCache / HubManager / SandboxSettings / StagingDrawer / LoadMenu / ResourceDrawer / StatsRecorder / MapIcon` 全部是随机名。可行性报告 §4 的"方法名保留 ⇒ 可反查"只对 **41/73** 个目标方法成立。

3. **73 个目标方法的安卓状态**：**41 个方法名保留**（可 `method_from_name` 直取）；**23 个名丢失但确认方法还在**（按 RVA hook 或按签名定位）；**9 个无法确认**（6 个目标方法的类名未命中：`Revert`×2、`GameMenus`、`GameSelector`、`Screen_Game`、`ButtonBuilder`；3 个方法名未命中且无法确认：`GameManager.LoadPersistentAndLaunch`、`WorldBaseManager.UpdateWorldPlaytime`、`Rocket.OnFixedUpdate`）。⚠️ **名未命中 ≠ 不存在**：`GameManager.IsOnLaunchpad` 一开始也查不到，最后按签名在 `FJCPPNPDPLK(string, Double2)`（RVA `0x16EC4F4`）找回来了（见 §1.4）。

4. **签名漂移是隐藏坑**：`WorldTime.SetState` 安卓是 3 参（PC 2 参）、`Part.DestroyPart` 3 参（PC 2 参）、`RocketManager.MergeRockets` 5 参（PC 2 参）、`PlayerController.SmoothChangePlayer` 2 参（PC 1 参）、`SavingCache.SaveWorldPersistent` 5 参。**照抄 PC 的 Prefix 参数表会直接崩**，反射的 argc 与 hook 的参数寄存器布局都必须按 dump 重写。

5. **好消息：同步模型的运行时原语在安卓全在，且名字保留**——`PartsLoader.CreatePart(PartSave,...)`、`Rocket.SetJointGroup(JointGroup)`、`Physics.SetLocationAndState(Location,bool)`、`Staging.InsertStage(Stage,bool,int)`、`PartJoint::.ctor(Part,Part,Vector2)`、`PartHolder.GetArray()`。也就是说 **PC 的协议与"权威模型 + 插值"架构可以整体复用**，不必重新设计；缺的是把 Harmony 逻辑换成 native 实现。

6. **最核心的一条 patch 在安卓换了形态**：`Rocket` 的 `I_Physics.OnFixedUpdate`（PC 用来让非权威火箭不跑本地物理）在安卓 dump 里 `OnFixedUpdate` 0 命中——要么被混淆，要么由 `Physics.FixedUpdate`（`UFLBGBGGAEJQCJDW`，存在）统一驱动。**不建议去猜那个钩子**：等价替代是 `rocket.physics.PhysicsMode = false`（`set_PhysicsMode` 存在）或 `rb2d.bodyType = Kinematic`——PC 的 `Interpolator` 本来就是这么干的（`Interpolator.cs:469/736`），替代路径成熟且不依赖 RVA。

7. **进世界流程必须重写**：`GameManager.LoadPersistentAndLaunch / ClearWorld` 在安卓**查不到**（`IsOnLaunchpad` 已按签名找回为 `FJCPPNPDPLK`）；安卓 `GameManager` 提供的是 `LoadSave(WorldSave,bool,NFANGDDJNLA) / OpenLoad / OpenSave / RevertToLaunch(bool) / ExitToHub / ExitToBuild`。`DivertLoading` 这个 16KB、最复杂的 patch 文件在安卓**没有一一对应**，必须按安卓的 `LoadSave` + `SceneLoader.LoadWorldScene` 重新走一遍。

8. **UI 类 patch 整类作废**：`EditUI`（6 处）、`CheatPolicy` 的按钮识别、`JoinMenu`/`ChatWindow` 依赖的 `ButtonBuilder / MenuGenerator / ElementGenerator / SizeSyncerBuilder / Loc` **不在游戏程序集里**——它们来自 **UITools 模组**（`Main.cs` 的 `Dependencies = {"UITools","1.1.5"}`）。安卓没有代码模组加载器（dump 里只有 `ModLoader.PackData` 这种资源包体系），所以这一整类只能用 native UI 重建——`v43.c` 的"克隆现成按钮"是唯一可行路线。

9. **主线程结论**：58 个 patch 方法全部在游戏方法内部执行，**天然主线程**；native hook 回调也落在主线程。但可行性报告 §11.1 已验证：worker 线程只能做静态/纯计算 API，**任何碰场景/GameObject 的调用必须投递到主线程**。因此移植后必须保留 PC 的 `mainThreadActions` 队列模型（`LocalManager.cs:96`）。

10. **分级结论（73 个目标方法，三表互斥）**：**A 类 15 项**（无它不能联机）、**B 类 44 项**（可选/体验）、**C 类 14 项**（安卓不可用，需替代或放弃）。真正决定工期的只有 A 类里的 5 条：进世界流程、帧驱动、发射请求、远端物理抑制、控制权路由。

---

## 1. 方法与判定口径

### 1.1 反查方法

dump.cs 的格式是 `// Namespace: X` + `class|struct NAME : BASE // TypeDefIndex: n` + 字段/方法（带 RVA）。做法：

1. 以 **PC 源码里的方法名**在全库检索（方法名混淆率 26%，多数仍可用）；
2. 命中后用**命名空间 + 字段指纹 + 邻近方法**确认是不是同一个类（例：`SFS.World.VCQSQMYGGZECNBPE` 同时有 `AccelerateTime/DecelerateTime/SetState/worldTime/timewarpSpeed` ⇒ 确定为 `WorldTime`）；
3. 类名本身就是随机的（如 `FULMMHJZAEKABZAY`）时，用**特征方法**反查：`SetPlayerToBestControllable` 唯一命中 ⇒ `Rocket`；
4. 方法名也丢了（如 `DockingPortModule.Dock`）时，只能靠**同类里保留的方法 + RVA 邻接**推断，或直接按 dump 的 RVA 下钩。

### 1.2 混淆统计（Assembly-CSharp，4178 个 class/struct）

| 维度 | 总数 | 随机名（`^[A-Z]{8,}$`） | 占比 |
|---|---|---|---|
| 类名 | 4178 | 421 | **10.1%** |
| 方法名 | 16698 | 4383 | **26.2%** |
| 字段名 | 14132 | 852 | **6.0%** |

> 与方法名相反，**字段名混淆率低但恰好打在我们的关键字段上**：`SavingCache` 的 `buildPersistent/worldPersistent` 在安卓是 `CGJMKEDNJED/ODJJEBOEFOP`。PC 代码大量用 `FieldRef<T>("字段名")`，这些在安卓**必须改成偏移量**（dump 已给偏移），不能按名字取。

### 1.3 可达性分级（本文统一用这 4 个标签）

| 标签 | 含义 | 安卓实现手段 |
|---|---|---|
| **L1 直调** | 类可定位（混淆名已知）+ **方法名保留**且重载唯一 | `il2cpp_class_from_name` → `il2cpp_class_get_method_from_name(klass, "Name", argc)` → `il2cpp_runtime_invoke`；hook 用 Dobby 挂 RVA |
| **L2 需 RVA** | 类在、但**方法名被混淆**或重载不唯一 | 只能按 dump 的 `RVA` 下 inline hook（版本绑定），或按签名/调用点扫描；不能 `method_from_name` |
| **L3 需替代** | 目标方法/类不存在，但游戏里有等价能力 | 换 API 重新实现同样语义（本文逐条给方案） |
| **L4 不可用** | 依赖 PC 专有框架（UITools/Harmony transpiler/Finalizer/键鼠），或语义在 IL2CPP 下不可表达 | 放弃，改 native 重建或改设计 |

三条通用前提（不再逐行重复）：
- **主线程**：全部 patch 都在游戏主线程执行（目标方法本身在主线程）。native hook 回调同样在主线程；凡是从 worker 发起的调用，必须投递回主线程。
- **实例方法**需要先拿到对象指针（`Rocket*`、`ScreenManager*` 等），不能只靠 `runtime_invoke(NULL, ...)`。
- **Transpiler 一律不能移植**（IL2CPP 没有可改写的 IL）：本文对每个 Transpiler 都给出"Postfix/Prefix 覆盖"的等价做法。

### 1.4 「方法名查不到」的证据边界（重要）

dump 里查不到方法名有两种可能，**本文不把两者混为一谈**：

| 现象 | 结论强度 | 例子 |
|---|---|---|
| 类在、同类其它方法名大量保留，但目标名 0 命中 | **大概率是混淆**（方法还在）⇒ 按 RVA 钩 | `DockingPortModule.Dock`、`StagingDrawer.AddStage`、`SandboxSettings.Toggle*` |
| 名 0 命中，但**按签名**能在同类里找到唯一匹配 | **确认存在** ⇒ 直接给出 RVA | `GameManager.IsOnLaunchpad` → `FJCPPNPDPLK(string, Double2)` RVA `0x16EC4F4`（同类唯一的 `string+Double2` 组合） |
| 类名 0 命中，且没有任何同族成员 | **不确定**（可能整类改名/移除）⇒ 标 L3/L4，需实机或 IDA 复核 | `Revert`、`GameMenus`、`GameSelector`、`Screen_Game`、`ButtonBuilder` |
| 方法名 0 命中、类里又全是 0 参重载（无法按签名区分） | **无法定位** ⇒ 换设计或按行为下钩 | `GameManager.LoadPersistentAndLaunch`、`Rocket.OnFixedUpdate` |

> 这条边界直接决定工期：**"确认存在但被混淆"（23 个）只需写 RVA 钩子；"无法确认"（9 个）必须按 §8 清单实机验证，不能拍脑袋写代码。**

---
## 2. 逐个 patch 文件清单（表 1）

> 编号 1–73 与 §6 的 A/B/C 汇总表一一对应。`L1/L2/L3/L4` 见 §1.3。
> 每行的三个隐含答案：**能否 `method_from_name` 直调** → 看 L1/L2；**是否要主线程** → 一律「是」（这些方法本身就是游戏主线程方法，§1.3）；**是否依赖 PC 专有 API** → 逐条标在行内，汇总见 §5。

### 2.1 `DisablePausing.cs`（2 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 1 | `ScreenManager.Awake` | Transpiler + Postfix | 进世界时游戏会 `Time.timeScale = 0` 暂停；联机必须让世界继续跑。Transpiler 删掉 Awake 里的 setter，Postfix 只在"非联机且非 selfInitialize"时才置 0 | **L3 需替代**。`ScreenManager`=`SFS.Input.VNDAREHWRIQNAAKC`，`Awake()` 存在（RVA `0x189EA68`，0 参）。Transpiler 不可移植 ⇒ 用 **Postfix 强制 `Time.timeScale = 1`**（`UnityEngine.Time::set_timeScale`，在 `UnityEngine.CoreModule`，需单独 image）。主线程。 |
| 2 | `ScreenManager.OpenScreen` | Transpiler + Postfix | 打开任意界面时删掉"暂停世界"的 15 条 IL（`RemoveRange(i-14,15)`）；Postfix 用 `CurrentScreen.PauseWhileOpen` 决定倍率 | **L3 需替代**。`OpenScreen(Func<Screen_Base>)` 存在（RVA `0x189E850`，1 参）。⇒ hook 后直接写 `Time.timeScale = WorldTime.main.timewarpSpeed`（`WorldTime`=`VCQSQMYGGZECNBPE`，`timewarpSpeed` 字段 `0x30`）。主线程。 |

> 这两个 patch 是"联机时世界不能停"的唯一保障。安卓上**不要**去改 `Time.timeScale` 的写入点（写死在原生 IL 里），而是在 `Awake`/`OpenScreen` 返回后覆盖一次；`ScreenManager.Update`（`0x189E9C4`）里若游戏每帧重设，再考虑在 `WorldTime.Update` 的 Postfix 里兜底。

### 2.2 `DisableReverting.cs`（4 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 3 | `Revert.HasRevert` | Prefix（`__result=false` + skip） | 联机时禁用"回到发射台"（回退会改写世界，与服务端权威冲突） | **L4 不可用**。安卓 `Revert` **类名 0 命中**、`HasRevert`/`DeleteAll` 也 0 命中（可能整类改名，需 IDA 复核）。功能入口在 `GameManager.RevertToLaunch(bool)`（`0x16E89D4`）与 `RevertToBuild`，要封就在这两个方法上做。 |
| 4 | `Revert.DeleteAll` | Prefix（skip） | 禁止删除回退点 | **L4 不可用**，同上。 |
| 5 | `SavingCache.SaveRevertToLaunch` | Prefix（skip） | 联机时不写回退存档 | **L1 直调**：`SavingCache.SaveRevertToLaunch(WorldSave, bool)` RVA `0x186040C`，名字保留。 |
| 6 | `GameMenus.UpdateTopCenter` | Prefix（skip） | 隐藏顶部"回退"按钮 | **L4 不可用**：`GameMenus` 类名未命中（`UpdateTopCenter` 0 命中；移动端 HUD 是另一套，整类改名的可能性也低）。移动端 HUD 是另一套，需在 native UI 层自己决定显示什么。 |

### 2.3 `DisableStats.cs`（4 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 7 | `StatsRecorder.Record` | Prefix（skip） | 联机不记录飞行统计（PC 上每条记录会写盘/上报，拖主线程） | **L2 需 RVA**：类=`SFS.Stats.FGKWZQVSXYZXUGHB`，`Record` 名在安卓**已丢失**（该类 31 个方法里 21 个是随机名）。若必须拦，按同类 `OnCrash` 邻近 RVA 反推或整类禁用。 |
| 8 | `StatsRecorder.OnCrash` | Prefix（skip） | 不上报坠毁统计 | **L1 直调**：`OnCrash(1 参)` RVA `0x18D0AE4`。 |
| 9 | `StatsRecorder.OnSplit` | Prefix（skip） | 不上报分离统计 | **L1 直调**：`OnSplit(2 参)` **static** RVA `0x18D2A58`。 |
| 10 | `StatsRecorder.OnMerge` | Prefix（skip） | 不上报对接/合并统计 | **L1 直调**：`OnMerge(2 参)` **static** RVA `0x18D4598`。 |

> 该类在安卓上唯一被 `Rocket.stats` 字段（`0xA0`）持有，属于"有也不影响联机"的一类；A/B 分级里归 B。

### 2.4 `DisableWorldSaving.cs`（4 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 11 | `SavingCache.UpdateWorldPersistent` | Prefix（清空本地缓存 + skip） | 联机世界里不把"权威模型"写回本地世界缓存（否则重进单机会带出别人的船） | **L1 直调**：`UpdateWorldPersistent(Action<WorldSave>)` RVA `0x18606C8`，1 参。 |
| 12 | `SavingCache.SaveWorldPersistent` | Prefix（skip） | 禁止保存世界 | **L1 直调**：`SaveWorldPersistent(WorldSave, bool, bool, bool, bool)` RVA `0x185F864`，**5 参**（实例方法：`this` 在 x0，5 个参数随后；PC 侧 patch 不带参，但 hook 必须按真实布局读）。 |
| 13 | `GameManager.Update` | Prefix（skip） | PC 用它掐掉自动存档 tick | **L1 名在（`0x16ECF28`）但语义危险**：安卓上 `GameManager.Update` 是**世界场景主循环**，`return false` 会冻结整个世界的更新。**不可照搬**；正确做法是只拦 `SavingCache.SaveWorldPersistent`（#12）。 |
| 14 | `WorldBaseManager.UpdateWorldPlaytime` | Prefix（改写 playtime 后 skip） | 联机不累计游玩时长（PC 上会周期写 settings） | **名未命中**：`WorldBaseManager`=`SFS.WorldBase.FAELUUBBVIPEYYXS` 只剩 `ExitWorld/EnterWorld/get_IsCareer/get_AllowsCheats/Start` 5 个方法，其中 3 个是 0 参随机名（`JCEHAFNFOBC/GDICOCGBHPK/BAGNIFEBGKK`），**无法按签名区分**。但安卓 `WorldSettings` 里根本没有 playtime 相关字段（PC 的 `settings.playtime` 在 dump 里查不到）⇒ 安卓不记 playtime，**这一项可以直接跳过**。 |

### 2.5 `DisableWorldSavingUI.cs`（2 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 15 | `LoadMenu.OpenSaveMenu(CloseMode)` | Prefix（弹提示 + skip） | 联机世界里点"保存"直接拒绝，提示 `Saving is disabled in multiplayer` | **L1 直调**：`LoadMenu`=`SFS.UI.CFPEPKABIVXBYRFK`，`OpenSaveMenu` 有 **2 个重载**（0 参 `0x16CEC3C` / 1 参 `0x16CEC44`）⇒ `method_from_name` 必须按 argc 区分；两个都拦。`CloseMode` 是 `SFS.Input` 枚举。 |
| 16 | `Screen_Menu.Open` | Prefix（`__instance is LoadMenu` 时 skip） | 兜底：从任何路径打开 LoadMenu 都拦 | **L1 直调**：`Screen_Menu`=`SFS.Input.NQEUSBNLKGCFHQVR`（基类 `APRCAZUBBLBXPVZR`），`Open()` RVA `0x1859FA0`，0 参。类型判断用 `il2cpp_object_get_class` + 继承链比较（`LoadMenu` 的基类就是它）。 |

> 注意：PC 侧用 `SceneManager.GetActiveScene().name == "World_PC"` 判断是否在世界内；安卓场景名不同（需实测，`World_PC` 在移动端大概率叫 `World` 或 `World_Mobile`）。这一条是**移植时必须重新取值**的硬编码，不能沿用。

---
### 2.6 `DisableTimewarp.cs`（3 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 17 | `WorldTime.AccelerateTime` | Prefix（改发请求 + skip） | 时间加速不再本地改档，而是 `ClientManager.RequestTimeScale(下一档)` 交给服务端裁决 | **L1 直调**：`WorldTime`=`SFS.World.VCQSQMYGGZECNBPE`，`AccelerateTime()` RVA `0x18246F4`，0 参。主线程（按钮回调）。 |
| 18 | `WorldTime.DecelerateTime` | Prefix（同上） | 减速同理 | **L1 直调**：`DecelerateTime()` RVA `0x1825A90`，0 参。 |
| 19 | `WorldTime.SetState` | Prefix（`ref double __0` / `ref bool __1` 限幅） | 所有倍率变化的最终落点，把 >5x 钳到 5x 并置 `realtimePhysics=true` | **L1 名在、签名不同**：安卓 `SetState(double, bool, bool)` **3 参** RVA `0x1824C48`（PC 2 参）⇒ 第 3 个 bool 必须一起处理（大概率是 `instant`/`applyPhysics`），hook 参数布局要按 3 参写。`timewarpSpeed` 字段 `0x30`。 |

> 读当前倍率必须用 `WorldTime.main.timewarpSpeed`（字段 `0x30`），**不能读 `Time.timeScale`**（PC 注释里的血泪教训：个人加速模式下 `world.timeScale` 恒为 1）。`WorldTime.update` 会在每帧把 `timewarpIndex` 折算成 `timewarpSpeed`，字段直读最稳。

### 2.7 `DivertBuildSavingAndLoading.cs`（2 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 20 | `SavingCache.SaveBuildPersistent(Blueprint, bool)` | Prefix（改写缓存 + 异步存到模组自己的目录 + skip） | 联机时蓝图不写进原版存档目录，改写到 `Main.buildPersistentFolder`（避免污染单机蓝图库） | **L1 直调**：`SaveBuildPersistent(Blueprint, bool)` RVA `0x185FEB8`，2 参。**但**：PC 用 `FieldRef<Data<Blueprint>>("buildPersistent")`，安卓字段名是 `CGJMKEDNJED`（偏移 **0x20**，类型 `SavingCache/HMHPNINMMEL<Blueprint>`）⇒ 只能按偏移写对象指针；`Data<T>` 的 `thread/result` 字段在泛型实例上偏移不稳定，构造它风险高。**建议降级**：不接管缓存，只做"存到我们目录 + 阻止写原版目录"。 |
| 21 | `SavingCache.Preload_BlueprintPersistent()` | Prefix（自己起线程读我们的目录） | 联机进世界前预加载蓝图，跳过原版路径 | **L1 直调**：RVA `0x185FCC8`，0 参。同上：字段 `CGJMKEDNJED`@`0x20`；`Data<T>` 构造是难点（`il2cpp_object_new` + 泛型实例 + 两个字段），可作为二期。 |

### 2.8 `DivertLoading.cs`（4 个目标 —— 全仓最复杂、安卓缺口最大）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 22 | `SavingCache.Preload_WorldPersistent(bool)` | Prefix（造一个空 `WorldSave` 后 skip） | 联机不能加载本地单机世界：直接给一个**空世界**（`WorldSave.CreateEmptyQuicksave`），让服务端状态来填充 | **L1 直调**：`Preload_WorldPersistent(bool)` RVA `0x185F3EC`，1 参；`worldPersistent` 字段 = `ODJJEBOEFOP`@`0x30`。空世界可以用 `il2cpp_object_new(WorldSave)` + 默认字段（`rockets/branches` 置空），或调 `WorldSave` 的 8 参 ctor。 |
| 23 | `GameManager.LoadPersistentAndLaunch` | Prefix（手工走完进世界流程 + skip） | PC 的"进联机世界"总入口：清世界 → 设时间 → `OnLoadWorld` → 相机/地图归位 → 刷蓝图 → 关加载屏 | **L3 需替代（最大缺口）**：安卓 **名未命中**（`LoadPersistentAndLaunch`/`ClearWorld` 0 命中；`IsOnLaunchpad` 已按签名找回，见 #25）。安卓 `GameManager`=`SFS.World.QHQPUKFMUOSOOWES` 提供的是 `LoadSave(WorldSave, bool, NFANGDDJNLA)`（RVA `0x16E8F0C`）、`OpenLoad/OpenSave`、`RevertToLaunch(bool)`，配合 `SceneLoader.LoadWorldScene(bool)`（`SFS.USPVHOMQUKUGZPLU`，RVA `0x16EE994`）。⇒ **必须重写一条 mobile 专用的进世界序列**，PC 的 20 多行反射调用（`WorldView.SetViewLocation`、`Map.view`、`CareerState`、`AstronautState`、`GameCamerasManager.InstantlyRotateCamera`、`LogManager.ClearBranches`）在安卓要逐项验证是否同名存在。 |
| 24 | `BuildManager.<Launch>g__Launch_2\|33_5` | Prefix + **ReversePatch**（调原方法） | 发射时若发射台被"无人火箭"占用，弹确认框（Clear Launchpad），否则继续原发射流程 | **L3 需替代**：编译器局部函数**不导出到 metadata**（dump 里只有 `BuildManager.Launch`，0 参 RVA `0x1735F94`，PC 目标带 `bool forceVertical`）。⇒ 只能 hook `Launch()` 本体并按 RVA 调原函数（ReversePatch 在 native = 保存 trampoline，Dobby 的 `DobbyInstrument`/`dobby_hook` 返回值可做）。发射台占用判定直接调 #25 找回来的 `FJCPPNPDPLK(string, Double2)`（RVA `0x16EC4F4`）。 |
| 25 | `GameManager.IsOnLaunchpad(string, Double2)` | **ReversePatch** | 给上面的判定提供"是否在发射台" | **L2 已按签名找回**：`GameManager.FJCPPNPDPLK(string, Double2)`，RVA `0x16EC4F4`（`SFS.World.QHQPUKFMUOSOOWES` 里唯一的 `string + Double2` 组合，参数类型与 PC 完全一致）⇒ 直接按 RVA 直调，不必自己写距离判定。 |

> 这个文件说明一件事：**PC 的"进世界"是一条被 Harmony 拆开重拼的流程，安卓的入口完全不同**。它不是"移植 patch"，而是"在安卓上重新实现一次进世界"——这是 A 类里工作量最大、风险最高的一项，建议单独排期（见 §7）。

### 2.9 `DivertRocketSwitching.cs`（3 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 26 | `PlayerController.SmoothChangePlayer(Player)` | Prefix（改发控制权请求 + 条件 skip） | 切到别人/自己的火箭时不再本地直接切，而是向服务端申请控制权；本地未同步火箭走本地路径 | **L1 直调**：`PlayerController`=`SFS.World.FGTHEGCIRXGJNFIZ`，`SmoothChangePlayer(Player, float=0.25)` RVA `0x175D0D0`，**2 参**（PC 1 参）⇒ hook 时第二参是过渡时长，别当返回值用。 |
| 27 | `Rocket.SetPlayerToBestControllable(Rocket[])` | Postfix | 原生"切换可控火箭"后，把选中的火箭 id 报给服务端 | **L1 直调**：**static**，`Rocket[]`→`void`，RVA `0x174EEC8`，1 参。`Rocket`=`FULMMHJZAEKABZAY`。 |
| 28 | `GameSelector.SwitchTo` | Transpiler（在 `view` 字段读取后插入 `TrySwitchPlayer`） | 地图上点火箭图标切换控制权 | **L4 不可用**：`GameSelector` 类名未命中（`SwitchTo` 0 命中；地图交互移动端本就不同）。替代入口：地图点击走 `SFS.World.Maps.EVRFBUQKROSSDLYQ.TrySelect`（存在）或 `MapPlayer`/`MapIcon` 的选中回调；需要另行逆向地图点击链。属于 B 类（地图切换是体验项，飞行中仍可用"下一枚火箭"按钮）。 |

---
### 2.10 `EditUI.cs`（6 个目标 —— 全部是 UI 重构）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 29 | `HubManager.Start` | Transpiler（删 6 条 IL）+ Postfix | 联机时把"继续游戏"按钮置灰（`resumeGameButton.SetEnabled(...)`） | **L3 需替代 + 目标搬家**：`HubManager`=`SFS.UI.QQUJIOVKJFUVHVBL`，`Start()` 存在（RVA `0x18CB420`），但 **`resumeGameButton` 不在 HubManager 上**——它在世界条目类 `AXVQHBBALEUNLEXX` 的 **0x50**；HubManager 上对应的是 `worldButtons`（**0x50**，`Button[]`）。⇒ Postfix 里遍历 `worldButtons` 逐个 `SetEnabled(false)`。Transpiler 那段"删掉启用逻辑"直接用 Postfix 覆盖即可。 |
| 30 | `HubManager.OpenMenu` | Prefix（用 UITools 重建整个菜单 + skip） | 联机时主菜单只留"设置/退出"，去掉"继续/换存档" | **L4 不可用**：安卓 HubManager 只有 `OpenHub`（`OpenMenu` 名丢失/不存在），且整段构建依赖 **UITools** 的 `MenuGenerator/ButtonBuilder/ElementGenerator/SizeSyncerBuilder`——这些类**不在游戏程序集**。⇒ 必须用 native UI 重建（v43.c 的克隆路线）。 |
| 31 | `BuildManager.OpenMenu` | Prefix（同上，重建建造菜单） | 联机时蓝图编辑器菜单换 ours | **L1 钩点（`0x17387FC`）+ L4 实现**：钩子能下，但方法体全是 UITools 构建。⇒ 保留原菜单 + native 叠一层（或干脆不接管，只加"回发射中心"按钮）。 |
| 32 | `GameManager.OpenMenu` | Prefix（重建暂停菜单，并补"Open Cheats"入口） | 联机暂停菜单：新建火箭/设置/退出，按策略决定是否加作弊入口 | **L1 钩点（`0x16E9E50`）+ L4 实现**：同上。作弊入口 `SandboxSettings.main.Open` 在安卓是 `MGWYTGZHSEXYTFRZ.main`（静态字段 `0x0`）+ `OnOpen`（`0x168D4F0`），可以直调。 |
| 33 | `MapIcon.UpdateAlpha` | Postfix | 地图图标按玩家颜色着色（`SpriteRenderer.color *= player.iconColor`） | **L2 需 RVA**：`MapIcon`=`SFS.World.Maps.XVVWQJIYVAQYOSAA`（`Rocket.mapIcon` 字段类型即它，`0x70`），但 `UpdateAlpha` 名在安卓**丢失**（该类只剩 7 个方法：`Start/SetRotation/OnDestroy` + 3 个随机名 + `.ctor`）。纯表现项，B 类。 |
| 34 | `Screen_Game.ProcessInput` | Prefix（PC 上直接 `return true`，实为 no-op） | 原本想"聊天框获得焦点时不响应快捷键"，已停用 | **L4 不可用（也不需要）**：`Screen_Game` 类名未命中，且 PC 侧这个 patch 本身就是空实现 ⇒ **直接丢弃**。 |

> `EditUI` 是"PC 用 UITools 重排整个菜单"的集中体现。安卓上**不要试图逐条移植**：正确做法是"保留原生菜单 + native 叠加入口"，也就是 `v43.c` 已经在做的 UI 克隆。

### 2.11 `PartUpdateSyncing.cs`（10 个目标 —— 部件状态上行）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 35 | `DockingPortModule.Dock(DockingPortModule)` | Prefix（发 `Packet_DockTransaction` + skip） | 对接不再是本地立即完成，而是发事务给服务端裁决 | **L2 需 RVA**：`DockingPortModule`=`SFS.Parts.Modules.DockingPortModule`（类名保留），但 `Dock` 名在安卓**丢失**；同类保留 `AddPort/RemovePort/UpdateOccupied/Start/FixedUpdate/Draw`。⇒ 按 RVA 下钩，或改在 `UpdateOccupied`（对接判定处）拦截。**B 类**（对接是可选玩法）。 |
| 36 | `DockingPortModule.Draw(StatsMenu, PartDrawSettings)` | Postfix（画 "Undock" 按钮） | 给已对接的端口加"解除对接"按钮 | **L1 名在、签名不同**：dump 里 `Draw` 是 **3 参**（PC 2 参），RVA `0x1829590`。要按 3 参重写。B 类。 |
| 37 | `RocketManager.MergeRockets(Rocket, Rocket)` | Postfix（**空实现**，仅注释） | 对接合并的语义已交给服务端事务，这里什么都不做 | **L1 但无意义**：安卓 `MergeRockets` 是 **5 参**（`Rocket, Part, Rocket, Part, Vector2`，static，RVA `0x18F17A8`）⇒ 签名完全变了；PC 的 Postfix 本身是空函数，**直接丢弃**。 |
| 38 | `EngineModule.Start` | Postfix（订阅 `engineOn.OnChange`，变化时发包） | 发动机开关状态同步给其他玩家 | **L1 直调**：`EngineModule.Start()` RVA `0x16FF9A0`；`engineOn` 是 `Bool_Reference`（Obs），订阅要调 `Obs<T>::OnChange` 的 add 方法——安卓上委托构造较麻烦，**替代**：在 `EngineModule.FixedUpdate`（存在）里轮询 `engineOn.Value` 与上次值比较，变化才发包（少一次委托，逻辑等价）。 |
| 39 | `BoosterModule.FixedUpdate` | Prefix/Postfix（记录 `boosterPrimed/throttle_Out` 前后值） | 助推器点火/油门状态同步 | **L1 直调**：RVA `0x1684120`，0 参。 |
| 40 | `BoosterModule.Fire` | 同上 | 同上 | **L1 直调**：RVA `0x1682F1C`，0 参。 |
| 41 | `BoosterModule.Fire_Instantly` | 同上 | 同上 | **L1 直调**：RVA `0x1684784`，0 参。 |
| 42 | `WheelModule.ToggleEnabled` | Postfix（发 `Packet_UpdatePart_WheelModule`） | 轮子开关同步 | **L1 直调**：`SFS.Parts.WheelModule.ToggleEnabled()` RVA `0x1803CF0`。 |
| 43 | `ParachuteModule.Start` | Postfix（订阅 `targetState.OnChange`） | 降落伞开/关状态同步 | **L1 直调**：RVA `0x178D198`；同 #38，建议改轮询 `state/targetState`。 |
| 44 | `MoveModule.Toggle` | Postfix（发 `Packet_UpdatePart_MoveModule`） | 可动部件（活塞/机械臂）时间同步 | **L1 直调**：`SFS.Parts.Modules.MoveModule.Toggle()` RVA `0x1841254`。 |

> 这一组全部是**上行**（本地权威 → 服务端）。共同模式：`LocalManager.GetSyncedRocketID(rocket)` → `GetLocalPartID` → 组包 → `SendPacket`。安卓上照抄这个模式即可，唯一要改的是"订阅观察者"改"轮询比较"（避免在 native 里构造 C# 委托）。

### 2.12 `ResourceDrawerGuard.cs`（1 个目标）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 45 | `ResourceDrawer.CheckRedraw` | **Finalizer**（吞掉 `IndexOutOfRangeException`/`ArgumentOutOfRangeException`） | 联机动态增删部件后，资源条缓存与实际部件数不一致 → 每帧抛异常 + 触发崩溃上报 + 主线程卡死。这里把越界异常吞掉，只警告一次 | **L4 语义不可移植**：① `ResourceDrawer`=`SFS.World.PEPNEZLTDOJARRIU`，`CheckRedraw` 名已丢失（只能 RVA）；② 更关键——**native hook 无法"吞掉"C# 异常**（异常在托管侧抛出，返回 `null` 的语义只存在于 Harmony 的 Finalizer）。⇒ 替代方案：a) hook `LateUpdate()`（RVA `0x17EC168`，名字保留）在调用前校验 `resources` 数组长度（同类 `DIEJNLCLOOC(2 参)` 疑似部件变更回调，是更准的失效点）；b) 从根因修：确保远端建/销毁部件时同步重建 `ResourceDrawer` 的缓存（`forceRedraw` 字段在其嵌套状态类里）；c) 接受偶发异常，但关掉崩溃上报（配合 #8–#10）。**B 类**。 |

---
### 2.13 `WorldEventSyncing.cs`（11 个目标 —— 世界事件与结构）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 46 | `RocketManager.SpawnBlueprint(Blueprint)` | Transpiler（在 `Ldloc_3` 处插入：联机则走 `SyncLaunch` 并 `ret`） | **发射**不再本地生成火箭，而是把每枚火箭打包成 `Packet_CreateRocket` 发服务端，等服务端回执 | **L3 需替代（语义等价）**：`SpawnBlueprint(Blueprint)` **static** 1 参 RVA `0x18F0310`，名字保留。Transpiler 不可移植 ⇒ 用 **Prefix 返回 false** 跳过本地生成，然后自己走 `SyncLaunch` 逻辑（组包 + `LocalManager.RegisterLaunchRequest` + 选控制权）。注意 PC 的 transpiler 是在"已经算出 rockets 数组之后"插入的，Prefix 阶段拿不到那批 `Rocket[]`——需要自己从 `Blueprint` + `PartsLoader.CreatePart` 重建一次，或改钩更靠后的 `RocketManager.LoadRocket/CreateRocket_Child`。**A 类里最需要小心的一条。** |
| 47 | `Part.DestroyPart(bool, DestructionReason)` | Prefix + Postfix | 部件被摧毁时：非权威端直接否决（不掉零件），权威端记账并上报；Postfix 清理 id 映射 | **L1 名在、签名不同**：安卓 `Part.DestroyPart(bool, bool, PNPPPNFIOPL)` **3 参** RVA `0x16A2DB0`（PC 2 参 + `ref reason`）⇒ 第二个 bool 是新增参数（可能是 `createExplosion`/`playSound` 拆分），`reason` 是第 3 参且**按值传**（要改原因必须写回参数槽）。 |
| 48 | `RocketManager.DestroyRocket(Rocket, DestructionReason)` | Prefix + Postfix | 火箭销毁：权威端记账并上报，非权威端否决；Postfix 把控制权切到别的火箭 | **L1 直调**：**static** 2 参 RVA `0x18F1C7C`，`Rocket`=`FULMMHJZAEKABZAY`、`reason`=`PNPPPNFIOPL`。 |
| 49 | `JointGroup.RecreateRockets(Rocket, List<Rocket>)` | Postfix | 分离/重建后重算部件 id 并整枚上行（结构变更） | **L2 需 RVA**：`JointGroup` 类名保留，但 **15 个方法里 14 个被混淆**（`DDDIPHKGAKL/OGPHLEDNOAK/...`，唯一例外是 `.ctor`），`RecreateRockets` 0 命中。字段可直读：`dictionary@0x10`、`joints@0x18`、`parts@0x20`。⇒ 按 RVA 下钩；或**改用 `Staging.OnSplit/OnMerge`**（见下）作为结构变更的触发点。B 类。 |
| 50 | `StagingDrawer.AddStage` | Postfix（重算 `StagingState` + 发包） | 分级（stage）编辑同步 | **L2**：`StagingDrawer`=`SFS.World.MTTJSGDCPHETUDFE`，`AddStage` 名丢失。 |
| 51 | `StagingDrawer.UseStage` | Postfix | 点火某级 | **L2**，名丢失。 |
| 52 | `StagingDrawer.OnReorder` | Postfix | 分级重排 | **L2**，名丢失。 |
| 53 | `StagingDrawer.RemoveStage` | Postfix | 删除分级 | **L2**，名丢失。 |
| 54 | `StagingDrawer.TogglePartSelected` | Postfix | 选中部件加入/移出分级 | **L1 直调**：3 参 RVA `0x177AF28`（同类里唯一保留名的目标方法）。 |
| 55 | `WorldTime.Update` | Transpiler（每帧插 `LocalManager.Update()` + 用 `ClientManager.world.WorldTime` 覆盖 `worldTime`） | **联机的帧驱动 + 服务端时钟权威** | **L3 需替代（但很干净）**：`WorldTime.Update()` RVA `0x1825624`，名字保留 ⇒ 用 **Postfix**：① 调 `LocalManager.Update()`（每帧一次）；② 写 `worldTime` 字段（**0x28**）= 服务端时间。游戏自己在 Update 里会先推进本地时间，Postfix 覆盖即可（差值 <1 帧，无感）。**A 类核心。** |
| 56 | `Rocket.SFS.World.I_Physics.OnFixedUpdate` | Prefix（非权威火箭直接 skip 物理） | **远端火箭不跑本地物理**——这是"别人飞船由网络驱动"的根本保障 | **L3 建议替代（而非找钩子）**：`OnFixedUpdate` 名未命中，`Rocket` 里 0 参方法有 30+ 个且一半是随机名，**无法可靠定位**。替代（二者都已在 PC 的 `Interpolator` 里用过）：`rocket.physics.PhysicsMode = false`（`set_PhysicsMode(bool)` 存在，`physics` 字段 `0xF8`）或 `rocket.rb2d.bodyType = RigidbodyType2D.Kinematic`（`rb2d` 字段 `0x60`）。**推荐 Kinematic + `SetLocationAndState(loc,false)`**，与 `Interpolator.cs:469/736` 完全一致。**A 类核心。** |

> **重要发现**：`StagingDrawer` 的 5 个方法里 4 个名字丢了，但**数据层的 `Staging`（`SFS.World.FIRJPZZYVBGXOYNA`）方法名全保留**：`InsertStage(Stage,bool,int)`、`RemoveStage(?,2)`、`ApplyReorder(1)`、`OnSplit/OnMerge`、`Load/3`。⇒ 分级同步应该**改钩数据层而不是 UI 层**，既避开混淆又覆盖所有调用路径（UI、快捷键、程序化）。

### 2.14 `CheatPolicyPatches.cs`（17 个目标 —— 服务端作弊策略）

| # | 目标（C# 类.方法） | 类型 | 作用 | 安卓判定 |
|---|---|---|---|---|
| 57 | `SandboxSettings.OnOpen` | Postfix | 打开作弊面板时，按服务端策略把被禁项拨回 false、隐藏解锁/购买按钮 | **L1 直调**：`SandboxSettings`=`SFS.World.MGWYTGZHSEXYTFRZ`，`OnOpen()` RVA `0x168D4F0`。字段齐全（`infiniteFuel/noHeatDamage/preventUse_Cheats/unlockCheatsButton/...`）。 |
| 58 | `SandboxSettings.UpdatePreventUse` | Postfix | 覆盖"禁止使用"遮罩的显隐 | **L2**：方法名丢失（该类 32 个方法里 **27 个是随机名**）。 |
| 59 | `SandboxSettings.ShowUnlockCheats` | Postfix（`__result=false`） | 策略禁止时连"解锁作弊"按钮都不显示 | **L2**：名字丢失（且返回值 bool，RVA hook 要读 `w0`）。 |
| 60 | `SandboxSettings.OnToggle` | Prefix ×2 + Postfix | 作弊项切换后强制回滚被禁项；并修复"联机下 `worldBase.paths==null` 导致 NRE、开关不刷新" | **L2**：名字丢失。⚠️ 这个 patch 的一半是**修 PC 特有的 NRE**（`paths==null`），安卓进世界流程不同，**大概率不需要**。 |
| 61–68 | `SandboxSettings.ToggleInfiniteFuel / ToggleInfiniteBuildArea / TogglePartClipping / ToggleNoAtmosphericDrag / ToggleNoBurnMarks / ToggleNoGravity / ToggleNoHeatDamage / ToggleUnbreakableParts` | Prefix（`CheatPolicy.AllowToggle` 决定是否放行） | 单项作弊开关的准入控制 | **L2 ×8**：8 个方法名**全部丢失**（对应字段都在，如 `infiniteFuel@?`）。⇒ 只能按 RVA 逐个 hook，或改在 `OnOpen` 一次性把被禁项拨回（#57），**放弃单项拦截**（策略效果一样，只是玩家点了没反应 vs 点了弹回去，体验差异）。 |
| 69 | `WorldBaseManager.get_AllowsCheats`（PC 用"扫全程序集所有 `AllowsCheats` getter"动态安装） | Prefix（`__result = 策略值`） | 世界总闸：决定作弊面板是否出现 | **L1 直调**：`SFS.WorldBase.FAELUUBBVIPEYYXS.get_AllowsCheats()` RVA `0x16B1BAC`，0 参。 |
| 70 | `WorldMode.get_AllowsCheats`（同上，动态安装的第 2 个声明） | Prefix | 同上（任何直接读 `WorldMode` 的 UI 会绕过第一层） | **L1 直调**：`SFS.WorldBase.WorldMode.get_AllowsCheats()`，类名与方法名**都没混淆**。⇒ 安卓上 PC 的"动态扫描"可简化为硬编码这两处。 |
| 71 | `CheatsDrawer.UpdateText` | Postfix | HUD 上"当前生效的作弊"列表按策略隐藏 | **L2**：`CheatsDrawer`=`SFS.UI.KKAVDRUGMXXRSAOD`（只有 `cheatsText` 字段 + 5 个方法），`UpdateText` 名丢失。 |
| 72 | `TeleportMenu.OpenFromCheats` | Prefix（策略禁 Teleport 时 skip） | 传送菜单准入 | **L1 直调**：`TeleportMenu`=`SFS.World.RWLYPHTVNCFXMWYC`，`OpenFromCheats()` RVA `0x178A914`。 |
| 73 | `ButtonBuilder.OnCreated(NewElement)` | Postfix（按本地化文案识别并隐藏"Open Cheats"按钮） | 兜底隐藏作弊入口按钮 | **L4 不可用**：`ButtonBuilder` 属于 **UITools**，不在游戏程序集。安卓 UI 是自己建的，不需要这个兜底。 |

> 整组结论：**作弊策略在安卓可做，但不该按 PC 的方式做**。推荐只保留 #57（`OnOpen` 回滚）+ #69（`AllowsCheats` 总闸）+ #71（Teleport 准入），其余全部放弃——用 3 个钩子达成 90% 的策略效果。

### 2.15 `Mod/*.cs` 里的 patch 情况

对 `MultiplayerSFS.Mod/*.cs`（17 个文件，约 403 KB）做了全量 `HarmonyPatch|HarmonyReversePatch|TargetMethods|AccessTools` 检索，结果：

- **0 个 Harmony patch**；
- 仅 **1 处**反射访问：`LocalManager.cs:174` `AccessTools.FieldRefAccess<RocketManager, Rocket>(manager, "rocketPrefab")`；
- 其余全是"运行时逻辑"（读状态、组包、建火箭、插值、UI），依赖面见 §4。

也就是说：**PC 模组的能力 = 73 个 patch 点（拦截游戏）+ 3 条运行时路径（操作游戏）**，后者才是真正要重写的部分。

---
## 3. 目标类 → dump 混淆名映射（表 2）

> 反查口径见 §1.1。"依据"列写的是能在 dump 里唯一定位该类的成员，方便下一个人复核。

| PC 类（命名空间） | 安卓 dump 类 | 定位依据（dump 内特征） | 类名混淆 | 目标方法状态 |
|---|---|---|---|---|
| `SFS.Input.ScreenManager` | `SFS.Input.VNDAREHWRIQNAAKC` | `OpenScreen(Func<Screen_Base>)`、`GetStackCount`、`CloseStack`、`CurrentScreen` | 是 | `Awake/OpenScreen` 名保留 |
| `SFS.World.Revert` | **不存在** | `HasRevert`/`DeleteAll` 全库 0 命中 | — | 功能并入 `GameManager.RevertToLaunch(bool)` |
| `SFS.WorldBase.SavingCache` | `SFS.WorldBase.SPPLTQMHZHZKYVNY` | `Preload_WorldPersistent/SaveWorldPersistent/SaveBuildPersistent/Preload_BlueprintPersistent/SaveRevertToLaunch` | 是 | 方法名**全保留**；字段名混淆：`Data<Blueprint>@0x20`、`WorldSave@0x28`、`Data<WorldSave>@0x30`、`static main@0x0` |
| `SFS.UI.GameMenus` | **不存在** | `UpdateTopCenter` 0 命中 | — | 移动端 HUD 另一套 |
| `SFS.Stats.StatsRecorder` | `SFS.Stats.FGKWZQVSXYZXUGHB` | `OnCrash/OnSplit/OnMerge/HasFlown` + `Rocket.stats@0xA0` | 是 | `Record` 名丢失，`OnCrash/OnSplit/OnMerge` 保留 |
| `SFS.World.GameManager` | `SFS.World.QHQPUKFMUOSOOWES` | `ExitToHub/ExitToBuild/OpenMenu/LoadSave/RevertToLaunch/RevertToBuild` + `environment/rockets/fuelManager` | 是 | **`LoadPersistentAndLaunch`/`ClearWorld`/`IsOnLaunchpad` 缺失**；`Update/OpenMenu/LoadSave` 保留 |
| `SFS.WorldBase.WorldBaseManager` | `SFS.WorldBase.FAELUUBBVIPEYYXS` | `EnterWorld/ExitWorld/get_AllowsCheats` | 是 | 只剩 5 个方法；`UpdateWorldPlaytime` 不存在 |
| `SFS.UI.LoadMenu` | `SFS.UI.CFPEPKABIVXBYRFK`（基类 `Screen_Menu`） | `OpenSaveMenu/Import/Delete/Rename` | 是 | `OpenSaveMenu` 两个重载都保留 |
| `SFS.Input.Screen_Menu` | `SFS.Input.NQEUSBNLKGCFHQVR`（基类 `APRCAZUBBLBXPVZR`） | `Open/ProcessInput/PauseWhileOpen/CloseStack` | 是 | 保留 |
| `SFS.World.WorldTime` | `SFS.World.VCQSQMYGGZECNBPE` | `AccelerateTime/DecelerateTime/SetState/SetTimewarpIndex_ForLoad/StopTimewarp` + `worldTime@0x28`、`timewarpSpeed@0x30`、`timewarpIndex@0x20` | 是 | 保留；`SetState` **3 参**（PC 2 参） |
| `SFS.Builds.BuildManager` | `SFS.Builds.EYEFCFFRFATJITWQ` | `Launch/OpenSaveMenu/AskClear/UploadPC/buildGrid/selector` | 是 | `Launch` 名保留但 **0 参**（PC 局部函数带 `bool`） |
| `SFS.World.PlayerController` | `SFS.World.FGTHEGCIRXGJNFIZ` | `SmoothChangePlayer`、`player` 字段 | 是 | 保留，**2 参** |
| `SFS.World.Rocket` | `SFS.World.FULMMHJZAEKABZAY` | `SetPlayerToBestControllable/SetJointGroup` + `rb2d@0x60/partHolder@0x68/jointsGroup@0xC0/physics@0xF8` | 是 | **`OnFixedUpdate` 缺失**；`FixedUpdate` 存在（`0x1753AB4`） |
| `SFS.World.Maps.GameSelector` | **不存在** | `SwitchTo` 0 命中 | — | 地图点击另找入口（`TrySelect`） |
| `SFS.World.Maps.MapPlayer` | 未唯一确定 | `ZKACZPXBBUJXVQWC / QKTZHRPSBLLDQEVJ / NLCVKDRHBNATBHVX` 均有 `get_Player` 覆写 | 是 | 非关键（仅用于 #28 的替代路径） |
| `SFS.UI.HubManager` | `SFS.UI.QQUJIOVKJFUVHVBL` | `Start/OpenHub/OpenWorld/RenameWorld/GetOrdered` + `worldButtons@0x50`、`worldElementPrefab=AXVQHBBALEUNLEXX` | 是 | `OpenMenu` → **`OpenHub`**；`resumeGameButton` 在 `AXVQHBBALEUNLEXX@0x50` |
| `SFS.World.Maps.MapIcon` | `SFS.World.Maps.XVVWQJIYVAQYOSAA` | `Rocket.mapIcon` 字段类型（`0x70`） | 是 | `UpdateAlpha` 名丢失 |
| `SFS.World.Screen_Game` | **不存在** | `ProcessInput` 命中 4 个类但无 `Screen_Game` | — | PC 侧也是 no-op |
| `SFS.Parts.Modules.DockingPortModule` | 同名（未混淆） | `AddPort/RemovePort/UpdateOccupied/isOccupied/Draw` | 否 | **`Dock` 名丢失** |
| `SFS.World.RocketManager` | `SFS.World.WQPAXLGGCOAUZEOA` | `SpawnBlueprint/MergeRockets/DestroyRocket/LoadRocket/CreateRocket_Child` + `rocketPrefab@0x20`、`spawner@0x28` | 是 | `MergeRockets` **5 参**；其余保留 |
| `SFS.Parts.Modules.EngineModule` | 同名 | `engineOn/heatOn/throttle` | 否 | `Start` 保留 |
| `SFS.Parts.Modules.BoosterModule` | 同名 | `Fire/Fire_Instantly/boosterPrimed/throttle_Out` | 否 | 保留 |
| `SFS.Parts.WheelModule` | 同名 | `ToggleEnabled/on` | 否 | 保留 |
| `SFS.Parts.Modules.ParachuteModule` | 同名 | `targetState/state` | 否 | `Start` 保留 |
| `SFS.Parts.Modules.MoveModule` | 同名 | `Toggle/time/targetTime` | 否 | 保留 |
| `SFS.World.ResourceDrawer` | `SFS.World.PEPNEZLTDOJARRIU` | `resourcesHolder/barPrefab/temperatureHolder/LateUpdate` | 是 | `CheckRedraw` 名丢失 |
| `SFS.Parts.Part` | 同名（未混淆） | `DestroyPart/GetModules/get_Rocket/set_Rocket` | 否 | `DestroyPart` **3 参** |
| `SFS.World.JointGroup` | 同名 | `dictionary@0x10/joints@0x18/parts@0x20` | 否 | **15 个方法里 14 个名混淆**（除 `.ctor`） |
| `SFS.World.StagingDrawer` | `SFS.World.MTTJSGDCPHETUDFE` | `addStageButton_*/useStageButtons/stagesGroup/TogglePartSelected` | 是 | 4/5 目标方法名丢失 |
| `SFS.World.SandboxSettings` | `SFS.World.MGWYTGZHSEXYTFRZ` | `infiniteFuel/noHeatDamage/preventUse_Cheats/unlockCheatsButton/onToggleCheat` | 是 | 仅 `OnOpen/UpdateUI` 保留；`Toggle*` 8 个全丢 |
| `SFS.UI.CheatsDrawer` | `SFS.UI.KKAVDRUGMXXRSAOD` | `cheatsText@0x20` | 是 | `UpdateText` 名丢失 |
| `SFS.World.TeleportMenu` | `SFS.World.RWLYPHTVNCFXMWYC`（基类 `PKVTCUBZSQHIVNVO`） | `OpenFromCheats/planetSelectButton/…` | 是 | 保留 |
| `UITools.ButtonBuilder` | **不存在** | 全库无 `ButtonBuilder/MenuGenerator/ElementGenerator` | — | 外部模组依赖（L4） |

**补充：移植时真正会直接调用的"支撑类"（不在 patch 里，但 A 类离不开）**

| PC 类 | 安卓类 | 备注 |
|---|---|---|
| `SFS.World.Player`（Rocket 基类） | `SFS.World.DXYTHXTABGUDBUFN` | `location@0x38`、`mapPlayer@0x40`、`isPlayer@0x48`、`hasControl@0x50` |
| `SFS.World.Physics` | `SFS.World.UFLBGBGGAEJQCJDW` | `SetLocationAndState(Location,bool)`、`set_PhysicsMode(bool)`、`get_PhysicsMode` |
| `SFS.World.Staging` | `SFS.World.FIRJPZZYVBGXOYNA` | `stages@0x48`、`InsertStage/3`、`RemoveStage/2`、`OnSplit/OnMerge`（**推荐的结构变更钩点**） |
| `SFS.Parts.PartsLoader` | 同名 | `CreatePart(PartSave,Transform,string,OnPartNotOwned,out OwnershipState)`、`static CreatePart/5` |
| `SFS.World.PartJoint` | 同名 | `.ctor(Part,Part,Vector2)`、`a@0x10/b@0x18/anchor@0x20` |
| `SFS.World.SpaceCenterData` | 同名（`SFS.World`） | `address@0x10`、`position_LaunchPad@0x20`、`get_LaunchPadLocation` |
| `SFS.SceneLoader` | `SFS.USPVHOMQUKUGZPLU` | `LoadWorldScene(bool)`、`LoadHubScene(4×bool)`、`LoadBuildScene(bool)` |
| `SFS.Base`（静态门面） | **`.LKOPCEJHPBJ`**（全局命名空间，类名混淆） | 9 个静态字段全在但**名字全混淆**：`language@0x0`、`sceneLoader@0x8`、`worldBase@0x10`、`sound@0x18`、`planetLoader@0x20`、`partsLoader@0x28`、`inputManager@0x30`、`screenTracker@0x38`、`saver@0x40`。（另有 `SFS.RQOFTSKFXWRDBQCA` 是场景里持有同一组引用的 MonoBehaviour，疑似 Base 的宿主，仅作交叉验证） |
| `SFS.Parts.PartSave` | 同名 | `name@0x10/position@0x18/orientation@0x20/temperature@0x28/NUMBER_VARIABLES@0x30/TOGGLE_VARIABLES@0x38/TEXT_VARIABLES@0x40/burns@0x48`；`.ctor(Part)` 存在 |
| `SFS.World.Location` | 同名 | `time@0x10/planet@0x18/position@0x20/velocity@0x30`；`.ctor/4`、`.ctor/3` |

---
## 4. 运行时依赖面（`Mod/*.cs`，非 patch）

PC 模组的逻辑可以拆成三条路径。**这三条才是安卓真正要重写的部分**，patch 只是用来"挂"它们的钩子。

### 4.1 上行：把本地状态读出来（`LocalRocket.ToState` / `LocalManager.SendUpdatePackets`）

| 读取项 | PC 写法 | 安卓对应 | 判定 |
|---|---|---|---|
| 火箭名 | `rocket.rocketName` | `FULMMHJZAEKABZAY.rocketName@0xB8`（string） | L1 字段直读 |
| 位置/速度/星球 | `rocket.location.Value` → `ToNetLocation()` | `Player.location@0x38` → `Location{time@0x10,planet@0x18,position@0x20,velocity@0x30}` | L1（`Location` 类名未混淆） |
| 旋转 | `rocket.rb2d.transform.eulerAngles.z` | `rb2d@0x60` → Unity `Transform.eulerAngles`（CoreModule） | L1（Unity API） |
| 角速度 | `rocket.rb2d.angularVelocity` | 同上 | L1 |
| 油门 | `rocket.throttle.throttleOn/throttlePercent` | `throttle@0x80` → `AKQXNLQKSRJXPQGE{throttleOn@0x38, throttlePercent@0x40}` | L1，**注意是 `Obs<T>` 包装**，要调 `get_Value` |
| 姿态输入 | `rocket.arrowkeys.{rcs,turnAxis,rawArrowkeysAxis,horizontalAxis,verticalAxis}` | `arrowkeys@0x78` → `WMCZPVIROBWPMJFG{rcs@0x38, hasTurn@0x40, turnAxis@0x48, rawArrowkeysAxis@0x50, horizontalAxis@0x58, verticalAxis@0x60}` | L1，同样是 `Obs<T>` |
| 零件 | `rocket.partHolder.partsSet` + `new PartSave(part)` | `partHolder@0x68` → `partsSet@0x30`；`PartSave..ctor(Part)` 存在 | L1 |
| 关节 | `rocket.jointsGroup.joints`（`pj.a/pj.b`） | `jointsGroup@0xC0` → `joints@0x18`；`PartJoint{a@0x10,b@0x18,anchor@0x20}` | L1 字段直读（**不要调方法**，JointGroup 方法名全混淆） |
| 分级 | `rocket.staging.stages`（`stage.stageId/parts`） | `staging@0x88` → `FIRJPZZYVBGXOYNA.stages@0x48` | L1 字段直读 |

> `Obs<T>.Value` 在 IL2CPP 里是属性 getter（`get_Value`），不是字段——`Bool_Local/Float_Local/Vector2_Local` 都继承同一套。native 侧统一用 `il2cpp_runtime_invoke(get_Value)`，不要试图按偏移读泛型内部字段。

### 4.2 下行：把远端火箭造出来（`LocalManager.SpawnLocalRocket`）

| 步骤 | PC 写法 | 安卓对应 | 判定 |
|---|---|---|---|
| 取 prefab | `RocketManager.rocketPrefab`（实例字段）或静态 `prefab` | `WQPAXLGGCOAUZEOA.rocketPrefab@0x20`（实例）+ **`private static Rocket DEODPIKGPKM@0x0`**（等价 PC 的静态 `prefab`，名字被混淆）；`spawner@0x28` | L1 字段直读；静态那一路**按名字取不到**（`prefab` → `DEODPIKGPKM`），要用偏移 |
| 实例化 | `UnityEngine.Object.Instantiate(prefab)` | 泛型 `Instantiate<T>`；**建议改调 `RocketManager.LoadRocket/CreateRocket_Child`**（名字保留）避开泛型实例化 | L1（`LoadRocket/2`、`CreateRocket_Child/3` 存在） |
| 建零件 | `PartsLoader.CreatePart(partSave, null, null, OnPartNotOwned.Allow, out ownership)` | `CreatePart(PartSave, Transform, string, KDLKDGOJHFD, out GDPLLALKNJC)` RVA `0x186A3F4`（5 参，static） | L1；枚举/结构体**被混淆但都是 int32/值类型**，按值传即可 |
| 建关节 | `new PartJoint(a, b, b.Position - a.Position)` | `PartJoint..ctor(Part,Part,Vector2)`（`argc=3`） | L1 |
| 组装 | `new JointGroup(list, parts)` + `rocket.SetJointGroup(jg)` | `JointGroup..ctor(2 参)`、`Rocket.SetJointGroup(JointGroup)` RVA `0x175380C` | L1 |
| 定位 | `rocket.physics.SetLocationAndState(loc, physicsMode: true)` | `Physics.SetLocationAndState(Location,bool)` RVA `0x17B3824` | L1 |
| 姿态 | `rocket.rb2d.transform.eulerAngles` / `angularVelocity` | Unity | L1 |
| 分级 | `rocket.staging.InsertStage(new Stage(id, parts), record:false)` | `Stage..ctor(int, List<Part>)`、`Staging.InsertStage(Stage,bool,int)` RVA `0x184F70C` | L1（**3 参**，第 3 参默认 `-1`） |
| 销毁 | `RocketManager.DestroyRocket(rocket, reason)` | static 2 参 `0x18F1C7C` | L1 |

### 4.3 应用：插值（`Interpolator.cs`，67KB）

写操作只有 4 类，全部有安卓对应：

| 写操作 | PC 位置 | 安卓 |
|---|---|---|
| `rb2d.bodyType = Kinematic/Dynamic` | `Interpolator.cs:432-473, 736-772` | `rb2d@0x60`（Unity API） |
| `rb2d.interpolation = Extrapolate/None` | 同上 | Unity API |
| `physics.SetLocationAndState(loc, physicsMode:false)` | `:621, :1241` | `Physics.SetLocationAndState` RVA `0x17B3824` |
| `arrowkeys.*.Value = packet.*` / `throttle` | `:1305-1309` | `Obs<T>.set_Value` |

> 结论：**"权威端跑物理 + 观测端 Kinematic 插值"这套模型在安卓 1:1 可用**。唯一要补的是"非权威端禁止本地物理"的兜底（#56）——`Interpolator` 已经在做 `bodyType=Kinematic`，所以 #56 的替代方案是**把 PC 的 OnFixedUpdate patch 合并进 Interpolator 的每帧设置**，不需要单独钩子。

### 4.4 线程模型

PC 的做法（`LocalManager.cs:96-98, 215-236`）：

```
System.Threading.Timer(10ms) → 加锁 → mainThreadActions.Enqueue(SendUpdatePackets)
主线程（WorldTime.Update 的 transpiler 里调 LocalManager.Update）→ 出队执行
```

安卓对应：`std::thread` + `std::mutex` + `std::queue<std::function<void()>>`，在 `WorldTime.Update` 的 Postfix（#55）里 drain。**不要**在 worker 里直接碰 `Rocket/GameObject`（可行性报告 §11.1 已实测崩溃）。

### 4.5 网络栈

- PC 端：`MultiplayerSFS.Common` 用 `System.Net.Sockets.UdpClient`（8 处）+ `TcpTransportCore` + 自带 KCP（`MultiplayerSFS.Common.Kcp`，14 个文件）。
- 安卓端：**不要试图在 IL2CPP 里跑这套 C#**（需要 Mono 运行时/HybridCLR，属另一条研究线 02）。
- **现成可复用**：`Server/cpp/src/protocol/`（`codec.cpp/hpp`、`packets.cpp/hpp` + `models/`，约 2.4k 行）与 `Server/cpp/src/net/kcp_transport.cpp` 已经是同一套协议的 C++ 实现 ⇒ 客户端直接链接这份代码，**字节格式零风险**。

---
## 5. PC 专有 API / 框架清单（安卓侧必须替换的部分）

| 依赖 | 用在哪 | 安卓状态 | 替代方案 |
|---|---|---|---|
| **Harmony**（Prefix/Postfix/Transpiler/Finalizer/ReversePatch） | 全部 73 个 patch | **不可用**：IL2CPP 是 AOT，没有可改写的 IL，也没有 `System.Reflection.Emit` | `Dobby`/`ShadowHook` inline hook；ReversePatch 用 hook 返回的 trampoline；Transpiler 用 Prefix/Postfix 覆盖语义（本文逐条给法） |
| **UITools 模组**：`MenuGenerator` / `ButtonBuilder` / `ElementGenerator` / `SizeSyncerBuilder` | `EditUI` 全部、`JoinMenu`、`CheatPolicy` 的按钮识别、`ChatWindow` | **不在游戏程序集**（全库 0 命中）——它是 `Main.cs` 的 `Dependencies = {"UITools","1.1.5"}` | native UI：克隆现成按钮（`v43.c` 已验证），或直接用 `UnityEngine.UI` 建 GameObject |
| `ModLoader.Mod` / `SceneHelper`（模组加载器） | `Main.cs` 的场景回调、`LocalManager.Initialize` | 不可用（dump 里只有 `ModLoader.PackData` 这种**资源包**体系，没有代码模组加载器） | hook `UnityEngine.SceneManagement.SceneManager` 的场景加载回调，或轮询 `GetActiveScene().name` |
| `SFS.Translations.Loc` | 所有 UI 文案 | **类名在 dump 里不可见**（已混淆） | 文案硬编码中文/英文，或按 dump 的方法名反查本地化表；不要依赖 `Loc.main.XXX` |
| `UnityEngine.Input`（`GetKeyDown` 等键鼠） | `ChatWindow`、`NetworkDebugOverlay`、`Screen_Game.ProcessInput` | `UnityEngine.InputLegacyModule` 存在，但**没有键盘** | 触屏/屏上按钮；PC 的快捷键逻辑整体丢弃 |
| `Application.runInBackground` / `Application.quitting` | `Main.cs` | 存在但语义不同（Android 后台由系统调度） | 用 `OnApplicationPause/OnApplicationFocus` 或 native 生命周期回调 |
| `System.Threading.Timer` + C# 委托 | `LocalManager.Initialize` | 不可用（不跑 .NET） | `std::thread` + `std::mutex` + 主线程队列（§4.4） |
| `AccessTools.FieldRef`（按**字段名**取） | `SavingCache`/`HubManager`/`ResourceDrawer`/`StagingDrawer`/`SandboxSettings` | **字段名混淆**（`buildPersistent`→`CGJMKEDNJED` 等） | 按 dump 的**偏移**读写（§3/§4 已给） |
| `Physics2D.IgnoreCollision` | 发射台碰撞隔离 | Unity 可用（`UnityEngine.Physics2DModule` 存在） | 直接用 |
| `Resources.FindObjectsOfTypeAll` / `FindObjectOfType` | prefab 兜底查找 | Unity 可用但慢 | 直接用 `RocketManager` 实例的 `rocketPrefab@0x20`，不要全场景扫描 |
| `Harmony Finalizer`（吞异常） | `ResourceDrawerGuard` | **语义不可表达**：托管异常在 C# 侧抛出，native 无法"返回 null 表示已处理" | 改根因 / 前置校验（§2.12） |
| `Harmony Transpiler`（改 IL 常量、插调用） | 6 处（`ScreenManager`×2、`HubManager.Start`、`GameSelector.SwitchTo`、`RocketManager.SpawnBlueprint`、`WorldTime.Update`） | **不可用** | 全部用 Prefix/Postfix 重写语义；`WorldTime.Update` 这条尤其干净（Postfix 直接覆盖字段） |

---

## 6. 三张汇总表

### 表 A · 必须移植的核心 patch（15 项，无它不能联机）

| # | Patch | 为什么是核心 | 安卓实现要点 |
|---|---|---|---|
| 22 | `SavingCache.Preload_WorldPersistent` | 不拦就会把**本地单机世界**加载进联机会话，服务端状态和本地存档互相打架 | L1；`ODJJEBOEFOP@0x30` 写空 `WorldSave`（`rockets/branches=null`） |
| 23 | `GameManager.LoadPersistentAndLaunch` | **进联机世界的唯一入口**：不重写就没有"进世界"这一步 | **L3 重写（名未命中，无法按签名定位）**：`LoadSave(WorldSave,bool,msg)` + `SceneLoader.LoadWorldScene(bool)` + 手工补齐时间/相机/地图/`OnLoadWorld`；PC 的 20 余行反射调用要逐项在安卓找对应 |
| 11 | `SavingCache.UpdateWorldPersistent` | 联机世界不能回写本地缓存，否则重进单机带出别人的船 | L1 `0x18606C8`，Prefix 清 `syncedRockets/unsyncedRockets` 后 skip |
| 12 | `SavingCache.SaveWorldPersistent` | 同上（真正落盘那一步） | L1 `0x185F864`（**5 参**） |
| 1 | `ScreenManager.Awake` | 游戏默认进世界就 `timeScale=0` 暂停；不解除则**整个世界不动**，同步全废 | L3：Postfix 写 `Time.timeScale=1` |
| 2 | `ScreenManager.OpenScreen` | 打开任何菜单都会暂停世界（联机不能暂停） | L3：Postfix 写 `Time.timeScale=WorldTime.main.timewarpSpeed` |
| 55 | `WorldTime.Update` | **每帧驱动**（`LocalManager.Update`）+ **服务端时钟权威**（覆盖 `worldTime`） | L3：Postfix ①调 `LocalManager.Update` ②写 `worldTime@0x28` |
| 17 | `WorldTime.AccelerateTime` | 本地直接加速会让客户端时间与服务端分叉 | L1 `0x18246F4`；Prefix 发 `RequestTimeScale` 后 skip |
| 18 | `WorldTime.DecelerateTime` | 同上 | L1 `0x1825A90` |
| 46 | `RocketManager.SpawnBlueprint` | **发射**必须走服务端；否则本地凭空多一枚只有自己看得见的火箭 | L3：Prefix skip + 自己组 `Packet_CreateRocket`（注意拿不到 transpiler 里那批 `Rocket[]`，需从 Blueprint 重建） |
| 47 | `Part.DestroyPart` | 不掉别人打掉的零件 = 结构永久分叉（PC 注释里的"撞一下才同步"就是这个） | L1 `0x16A2DB0`（**3 参**，reason 按值传） |
| 48 | `RocketManager.DestroyRocket` | 火箭销毁不同步 = 幽灵船 | L1 static `0x18F1C7C` |
| 56 | `Rocket.I_Physics.OnFixedUpdate` | **远端火箭不能跑本地物理**，否则被本地重力/碰撞拽走 | **L3 替代**：`rb2d.bodyType=Kinematic` + `physics.SetLocationAndState(loc,false)`（`Interpolator` 已在做，合并即可） |
| 26 | `PlayerController.SmoothChangePlayer` | 控制权必须服务端裁决，否则两人同时开同一枚船 | L1 `0x175D0D0`（**2 参**） |
| 27 | `Rocket.SetPlayerToBestControllable` | 原生"切下一枚"也要走请求 | L1 static `0x174EEC8` |

### 表 B · 可选 / 体验类（44 项）

| # | Patch | 作用 | 说明 |
|---|---|---|---|
| 5 | `SavingCache.SaveRevertToLaunch` | 不写回退存档 | L1，纯保护，可不做 |
| 7–10 | `StatsRecorder.Record/OnCrash/OnSplit/OnMerge` | 不记录/不上报统计 | 减少主线程 I/O；`Record` 需 RVA，其余 L1 |
| 15–16 | `LoadMenu.OpenSaveMenu` / `Screen_Menu.Open` | 联机世界内禁止手动保存 | 纯 UI 拦截（#12 已兜底） |
| 19 | `WorldTime.SetState` | 把倍率钳到 ≤5x | L1；**3 参**要处理第三个 bool |
| 20–21 | `SavingCache.SaveBuildPersistent/Preload_BlueprintPersistent` | 蓝图存到模组目录 | 字段名混淆，`Data<T>` 构造麻烦，建议二期 |
| 24–25 | `BuildManager.Launch` / `GameManager.IsOnLaunchpad` | 发射台被无人火箭占用时弹"清空发射台" | 不做也能发射，只是可能留幽灵船 |
| 33 | `MapIcon.UpdateAlpha` | 地图图标按玩家着色 | 纯表现 |
| 35–36 | `DockingPortModule.Dock/Draw` | 对接事务 + "解除对接"按钮 | 对接是可选玩法；`Dock` 需 RVA |
| 38–44 | `EngineModule.Start`、`BoosterModule.{FixedUpdate,Fire,Fire_Instantly}`、`WheelModule.ToggleEnabled`、`ParachuteModule.Start`、`MoveModule.Toggle` | 部件状态上行（引擎/助推/轮子/伞/活塞） | 全部 L1；建议把"订阅观察者"改成"轮询比较" |
| 49 | `JointGroup.RecreateRockets` | 分离后重算结构并上行 | 可用 `Staging.OnSplit/OnMerge`（名字保留）替代钩点 |
| 50–54 | `StagingDrawer.{AddStage,UseStage,OnReorder,RemoveStage,TogglePartSelected}` | 分级编辑同步 | **改钩数据层 `Staging`**（`InsertStage/RemoveStage/ApplyReorder` 名字全保留），覆盖所有调用路径 |
| 57–72 | `CheatPolicyPatches` 全部（16 项） | 服务端作弊策略 | 安卓可做但别照搬：只留 `OnOpen`(#57) + `AllowsCheats`(#69/#70) + `TeleportMenu`(#72)，其余 8 个 `Toggle*` 名字全丢，建议放弃单项拦截 |

### 表 C · 安卓不可用 / 需替代（14 项）

| # | Patch | 为什么不可用 | 替代方案 |
|---|---|---|---|
| 3 | `Revert.HasRevert` | `Revert` 类不存在 | 封 `GameManager.RevertToLaunch(bool)` / `RevertToBuild` |
| 4 | `Revert.DeleteAll` | 同上 | 同上 |
| 6 | `GameMenus.UpdateTopCenter` | `GameMenus` 类不存在 | native HUD 自己决定显示什么 |
| 13 | `GameManager.Update` | 安卓上它是**世界主循环**，`return false` 会冻结世界 | **不要移植**；只拦 `SaveWorldPersistent`(#12) |
| 14 | `WorldBaseManager.UpdateWorldPlaytime` | 名未命中且无法定位；安卓 `WorldSettings` 无 playtime 字段 | 无需替代（直接跳过） |
| 28 | `GameSelector.SwitchTo` | `GameSelector` 类不存在 | 从地图点击链（`EVRFBUQKROSSDLYQ.TrySelect` / `MapPlayer` 选中回调）重新找入口；B 级可先不做 |
| 29 | `HubManager.Start` | 目标字段搬家（`resumeGameButton` 在 `AXVQHBBALEUNLEXX@0x50`）+ Transpiler | Postfix 遍历 `HubManager.worldButtons@0x50` 置灰 |
| 30 | `HubManager.OpenMenu` | 方法改名 `OpenHub` + 依赖 UITools | 保留原生 Hub + native 叠加按钮 |
| 31 | `BuildManager.OpenMenu` | 方法体全是 UITools 构建 | 保留原菜单，native 叠一层 |
| 32 | `GameManager.OpenMenu` | 同上 | 同上（作弊入口可直调 `SandboxSettings.OnOpen`） |
| 34 | `Screen_Game.ProcessInput` | 类不存在，且 PC 侧本来就是 no-op | 直接丢弃 |
| 37 | `RocketManager.MergeRockets` | 签名变 5 参且 PC Postfix 是空实现 | 直接丢弃 |
| 45 | `ResourceDrawer.CheckRedraw` | Finalizer 语义在 native 不可表达（吞不了托管异常） | 前置校验/根因修复/关崩溃上报 |
| 73 | `ButtonBuilder.OnCreated` | UITools 类，不存在 | native UI 无需此兜底 |

---
## 7. 移植顺序建议与风险

### 7.1 顺序（按"能否联机"倒推，而不是按 patch 文件顺序）

| 阶段 | 内容 | 涉及 # | 里程碑（可验收） |
|---|---|---|---|
| 0 | 框架：inline hook（Dobby）+ 主线程队列 + `il2cpp_*` 反射封装 + §3 类/字段表落地 | — | 能在主线程 hook 一个空方法并回调 |
| 1 | **进世界**：#22 空世界 → #11/#12 不落盘 → #1/#2 不暂停 → #55 帧驱动+时钟 → #23 进世界序列 | 22,11,12,1,2,55,23 | 进联机世界、时间在跑、退出后单机存档干净 |
| 2 | **看别人**：#56 物理抑制 + 收包建火箭（§4.2）+ 插值（§4.3） | 56 | 能看到别人的船在动、不被本地物理拽走 |
| 3 | **自己被看到**：#17/#18 时间请求、#26/#27 控制权、#46 发射、#47/#48 销毁 + 上行组包（§4.1） | 17,18,26,27,46,47,48 | 双端互见、能发射、能炸、能换船 |
| 4 | 体验：部件状态（#38–44）、分级（改钩 `Staging`）、对接（#35/36）、地图着色（#33）、发射台（#24/25） | B 组 | 别人的船"看起来对" |
| 5 | 策略/UI：作弊 3 钩子（#57/#69/#70/#72）、native 入口按钮 | B/C 组 | 服务端策略生效、UI 可用 |

> 阶段 1 的 #23 是**唯一无法预估的项**：安卓的 `LoadSave`/`LoadWorldScene` 组合是否能达到 PC `LoadPersistentAndLaunch` 的效果，必须实机验证（见 §8）。建议阶段 1 先用"从主菜单正常进一个空世界，再在 `OnLoadWorld` 处接上"的土办法跑通，再回头补 #23。

### 7.2 风险

| 风险 | 等级 | 说明 / 缓解 |
|---|---|---|
| #23 进世界流程无对应 | **高** | PC 是 20+ 行反射编排；安卓入口不同。缓解：先用"正常加载空世界 + 钩 `OnLoadWorld`"，把 `DivertLoading` 的其余步骤按需补 |
| #46 发射拿不到 `Rocket[]` | **高** | transpiler 在"已生成 rockets"处插入；native Prefix 阶段还没有。缓解：改钩 `RocketManager.LoadRocket/CreateRocket_Child` 或从 `Blueprint` 重建 |
| 23 个方法名丢失（RVA 绑定） | 中 | 游戏一更新（1.6.00.23…）RVA 全变。缓解：把 RVA 做成**版本表** + 启动时用"相邻方法签名"自校验；或改用特征码扫描 |
| 字段名混淆（`CGJMKEDNJED` 等） | 中 | 偏移同样版本绑定；且泛型类 `Data<T>` 偏移不稳定。缓解：偏移表 + 版本校验；`Data<T>` 相关逻辑降级 |
| Transpiler/Finalizer 语义 | 中 | 6 个 Transpiler + 1 个 Finalizer 无法 1:1 移植，本文已逐条给等价做法；`ResourceDrawer` 那条只能改根因 |
| 移动端性能 | 中 | 手机 CPU 弱 + 插值每帧写 Transform；PC 的 10ms 发包定时器在手机上要放宽（建议 20–33ms），爆炸特效预算（`RemoteExplosionPerFrame=2`）要保留 |
| 场景名/输入/UI 差异 | 低-中 | `World_PC` 场景名、键鼠快捷键、UITools 菜单——都要按移动端重做；`v43.c` 已证明 UI 克隆可行 |

### 7.3 一条重要的架构判断

PC 模组的 73 个 patch 里，**真正承载联机语义的只有 15 个（表 A）**，其余 58 个是"让单机行为在联机下不变形"的补丁（禁暂停/禁存档/禁回退/禁统计/UI 重排/作弊策略）。

安卓移植应该**先做 A 的 15 个 + 三条运行时路径**，得到"能联机但体验粗糙"的 MVP；B 类按需补；C 类不要碰（除了 #23 必须重写）。

这也意味着：**不需要一个通用的 Harmony 兼容层**，只需要"15 个精确的 inline hook + 一套反射封装"——工作量从"移植一个模组框架"降到"移植 15 个函数"。

---

## 8. 待实机验证清单（本文没能从 dump 确认的）

| # | 问题 | 为什么重要 | 验证方法 |
|---|---|---|---|
| 1 | 安卓世界场景名到底是什么（PC 是 `World_PC`） | #15/#16 用场景名判断"是否在世界内" | `SceneManager.GetActiveScene().name` 打到 marker 文件 |
| 2 | `ScreenManager` 是否每帧重设 `timeScale` | 决定 #1/#2 的 Postfix 够不够，还是要在 `WorldTime.Update` 兜底 | 进世界后读 `Time.timeScale` 两次（隔 1s） |
| 3 | `Part.DestroyPart` 第 2 个 bool 的语义（PC 只有一个 `createExplosion`） | #47 要正确传参，否则爆炸/音效错 | 反编译 `libil2cpp.so` 该 RVA 或对比行为 |
| 4 | `WorldTime.SetState` 第 3 个 bool 是什么 | #19 限幅要一起处理 | 同上 |
| 5 | `GameManager.LoadSave` + `SceneLoader.LoadWorldScene` 能否替代 #23；若不能，`LoadPersistentAndLaunch` 对应哪个 0 参随机方法 | 决定阶段 1 的工期 | 实机调 `LoadSave(空WorldSave)`；不行就用 IDA 在 `0x16E86D0`~`0x16EC654` 这批 0 参方法里按"是否创建 `WorldTime`/调 `OnLoadWorld`"定位 |
| 6 | `BuildManager.Launch`（0 参）内部是否还走 `SpawnBlueprint` | 决定 #46 的钩点选哪里 | hook 两个点打日志看调用顺序 |
| 7 | 8 个 `SandboxSettings.Toggle*` 的 RVA 对应关系 | 只有做单项作弊拦截才需要（建议放弃） | 逐个 RVA 下钩 + 点按钮验证 |
| 8 | `MapIcon.UpdateAlpha` 对应的混淆方法 | #33 是表现项，可延后 | 在 `MapIcon` 的 6 个方法上逐个试 |
| 9 | 字段偏移是否随 1.6.00.22 之后的小版本变化 | 所有偏移读写都依赖它 | 版本升级时重跑 dump + diff 偏移表 |
| 10 | 安卓静态 prefab 字段的真实名字 | `LocalManager` 的 prefab 兜底路径按名反射 `"prefab"` 会失败 | dump 已确认：实例字段 `rocketPrefab@0x20` 在，静态字段是 `DEODPIKGPKM@0x0`（混淆名）⇒ 反射要改成按偏移/按混淆名取 |

---

## 9. 附：一眼能用的结论

按"名字能不能用"分三桶（41 / 23 / 9，合计 73）：

**① 名字能用，直接 `method_from_name`（41 个，重点列）**
`ScreenManager.Awake/OpenScreen`、`SavingCache` 全部 6 个方法、`WorldTime.{Update,AccelerateTime,DecelerateTime,SetState}`（SetState 注意 3 参）、`Part.DestroyPart`（3 参）、`RocketManager.{SpawnBlueprint,DestroyRocket,MergeRockets}`（后两个签名变了）、`PlayerController.SmoothChangePlayer`（2 参）、`Rocket.SetPlayerToBestControllable`、`GameManager.{Update,OpenMenu}`、`BuildManager.{Launch,OpenMenu}`、`HubManager.Start`、`DockingPortModule.Draw`（3 参）、`EngineModule.Start`、`BoosterModule.{FixedUpdate,Fire,Fire_Instantly}`、`WheelModule.ToggleEnabled`、`ParachuteModule.Start`、`MoveModule.Toggle`、`StagingDrawer.TogglePartSelected`、`SandboxSettings.OnOpen`、`WorldBaseManager.get_AllowsCheats`、`WorldMode.get_AllowsCheats`、`TeleportMenu.OpenFromCheats`，以及 8 个 `StatsRecorder`/`LoadMenu`/`Screen_Menu` 的入口。

**② 名字没了但方法确认还在，按 RVA 钩（23 个）**
`StatsRecorder.Record`、`DockingPortModule.Dock`、`JointGroup.RecreateRockets`、`StagingDrawer.{AddStage,UseStage,OnReorder,RemoveStage}`、`SandboxSettings.{UpdatePreventUse,ShowUnlockCheats,OnToggle}` + 8 个 `Toggle*`、`CheatsDrawer.UpdateText`、`ResourceDrawer.CheckRedraw`、`MapIcon.UpdateAlpha`、`GameManager.IsOnLaunchpad`（=`FJCPPNPDPLK(string,Double2)`，`0x16EC4F4`）、`HubManager.OpenMenu`（实际方法名是 **`OpenHub`**）。

**③ 无法确认，先别写代码（9 个）**
`Revert.HasRevert`、`Revert.DeleteAll`、`GameMenus.UpdateTopCenter`、`GameSelector.SwitchTo`、`Screen_Game.ProcessInput`（PC 侧也是 no-op）、`ButtonBuilder.OnCreated`、`GameManager.LoadPersistentAndLaunch`、`WorldBaseManager.UpdateWorldPlaytime`（功能在安卓本就没有）、`Rocket.OnFixedUpdate`（用 Kinematic 替代）。

> 换句话说：**A 类 15 项里，11 项有明确落点（名字或 RVA 已知），4 项需要设计/验证（#23 进世界、#46 发射、#56 物理、#22 空世界构造）。** 这是排期的真实分母。

---

*本文所有 dump 结论可用 §1.1 的方法复现；RVA/偏移均取自 `android/dump/dump.cs`（1.6.00.22）。*
