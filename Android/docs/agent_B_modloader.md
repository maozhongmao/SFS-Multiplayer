# SFS 1.6.00.22 安卓版「Mod 加载器」溯源 + 官方 mod 机制评估（只读分析）

> 目标：判断主菜单「Mod 加载器」是不是官方 mod 系统入口；若是，找注册 API / 目录 / 回调，评估 native 最小接入；若不是，给出与卖家同一套机制的植入点。
> 材料：`D:/sfs-tools/apk/dump-official/dump.cs`（597,338 行）、`script.json`、`stringliteral.json`、APK `assets/bin/Data/*`（650 个 bundle）、`lib/arm64-v8a/libil2cpp.so`（56,998,304 B）。
> 方法：dump 静态检索 → capstone AArch64 反汇编（BL 交叉引用扫描器自建）+ UnityPy 解析 level0 场景（2026-10-19 现场安装 UnityPy 1.25.4）。
> 约束：未改任何工程文件、未连手机；本文件是唯一新增产物。
> 文中 `dump.cs:NNNN` 为行号，`RVA 0x…` 为该版本 libil2cpp.so 的代码偏移（偏移 = RVA − 0x4000，偏移与段内地址一致）。

---

## 0. 结论速览

1. **「Mod 加载器」是官方原生功能，不是任何 mod 加进去的。** 它就在主菜单场景 `level0` 的层级里：`--- UI ---/- Main Menu -/Buttons/Mod Loader Button`（GameObject pathID 283），按钮的 UnityEvent 直接指向 **`ModLoader.UI.ModsMenu::Open`**。卖家报告 §7-3「未找到明文，疑似 mod 注入」和附录 B 第 1 行可以据此改正。
2. **官方确实有一套 mod 系统**：命名空间 `ModLoader`（`ModLoader.UI.ModsMenu` / `ModLoader.ModsSettings` / `ModLoader.PackData`），有 `ModData` 结构、`ModType` 枚举（Mod/AssetsPack/TexturesPack/SolarSystem）、`AddMod/RemoveMod` 静态方法、`Mods` 目录扫描协程、`ModsSettings` 四张启用表。**但它没有任何「菜单项注册 / 插件回调」API**——列表里的一行只是一个「启用/停用」开关，不是可点击跳转的入口。
3. **native 接入其列表是可行的**（`AddMod(ModData)` 就是全部），代价低（构造 0x40 字节结构体 + 一个调用）；但它在语义上只能让我们在官方 Mods 菜单里多出一个**开关行**，不能承载「点了跳我们自己的页面」。若要「点开我们页面」，必须自己拿点击。
4. **推荐路线 = 克隆官方 Mod Loader Button**（与卖家同源的 Instantiate 克隆法），因为它自带官方按钮的全部外观、图标、布局与翻译槽；克隆后把继承来的 `clickEvent` 清空、改标签、自己接管点击即可。所需 RVA/偏移全部在 §5，`level0` 的层级与组件栈已在 §2 实证。
5. **v31 现在走的 `NOJZIZCFSEXSUEEX.DrawFullButton` 工厂法在主菜单上无效**：该类实例只存在于 `level1`（VAB 的 `Stats` 窗）、`level2/level3`（`Part Menu`，inactive），`level0`（主菜单）里**一个都没有**。这解释了为什么 v31 的按钮只出现在 VAB/零件统计窗。
6. 副产物：`KCQCPLWMTHTCNKCC`（dump.cs:41013，含 `moddingButton` 0x38 的那个类）**在所有 650 个 bundle 里都不存在**，是死类，不用再追；真正的主菜单没有这个组件。

---

## 1. 证据与判定方法

| 证据类型 | 做了什么 | 结果 |
|---|---|---|
| 字符串表 | `stringliteral.json` 全量过滤 `mod` | 命中 `Mod Loader`、`Mod_Loader_OpenButton`、`Mods_Button`、`Mods are still loading...`、`Download Mods`、`Open Mods Folder`、`../Mods`、`ModsSettings`、`CodeModNotAllowedOnMobile` |
| 类型表 | dump.cs 里 `ModLoader` 命名空间 | 5 个真实类（§3.1） |
| 交叉引用 | 自写 BL 扫描器（numpy 解 `imm26`，覆盖 `.text` + `il2cpp` 段，7M 条指令）＋ `script.json` RVA→方法名 | `AddMod` 的调用者只有 3 处：两个扫描协程 + `GetSolarSystemsForModsMenu`；`ModsMenu` 类型的 TypeInfo 槽（0x3508DE8）**零代码引用** |
| 反汇编 | `AddMod 0x16C15E8`、`Awake 0x16C1488`、`OnOpen 0x16C0950`、`DrawUI 0x16C0B04`、`Open 0x1859FA0`、`DNHHDNDIHAM::Invoke 0x1E7BA48` | 见 §3.2 / §5.4 |
| 资源 | UnityPy 解 `assets/bin/Data/level0.split0` | 主菜单按钮列表、按钮组件栈、UnityEvent 目标（§2） |

---

## 2. 「Mod 加载器」按钮的真实来源（场景实证）

### 2.1 层级与顺序（`level0.split0`，主菜单场景）

```
--- UI ---
├── - Main Menu -              （组件：RectTransform + BHNCYPQKKSTXRROJ 分辨率适配）
│   ├── Title Area
│   ├── Buttons                （pathID 476；组件：RectTransform, VerticalLayoutGroup,
│   │                           CUJABYOCLJHRSGWT, ContentSizeFitter, DKEKLDIKOUYOJKFB）
│   │   ├── Play Button        （289）
│   │   ├── -                  （144 分隔条）
│   │   ├── Shop Button        （286）
│   │   ├── Sale Countdown     （180，m_IsActive = False）
│   │   ├── -
│   │   ├── Mod Loader Button  （283）   ← 就是屏幕上那个
│   │   ├── -
│   │   ├── Dev Blog           （288）
│   │   ├── -
│   │   ├── Settings Button    （291）
│   │   └── Credits Button     （290）
│   ├── PC Banner / Banner Holder / Links
├── - Mods Menu -              （组件：RectTransform + NANBAJRHGSVEQBTQ + DUBBFVQJWPMXTOTT）
│   └── Holder/Menu/Safe Area/
│       ├── Scroll Window/Mod List (Scroller)
│       └── BottomPanel/Download Mods
```

顺序与现场截图（开始游戏 / 商店 / **Mod 加载器** / Dev Blog / 设置 / 制作组）完全一致，中间的 `-` 是分隔条。**这是官方主菜单，不是卖家拼的。**

### 2.2 Mod Loader Button 的组件栈（9 个组件，顺序即 Inspector 顺序）

| # | 组件（MonoScript 类名） | 说明 | 序列化里能读到的关键串 |
|---|---|---|---|
| 1 | RectTransform | | |
| 2 | CanvasRenderer | | |
| 3 | **HKAZNXTEGNSSTQLF** | 官方按钮（`holdEvent 0x40` / `clickEvent 0x48` / `onClick 0x68`） | `ModLoader.UI.ModsMenu, Assembly-CSharp`、`Open`、`UnityEngine.Object, UnityEngine` |
| 4 | DKEKLDIKOUYOJKFB | 按钮按下缩放/音效 | |
| 5 | HorizontalLayoutGroup | 图标+文字横排 | |
| 6 | IKUBYZNZZCPWONJL | 图形/描边基类 | |
| 7 | YKWRDRSYSNQEDHBG | 高亮/发光 | |
| 8 | IATNFWYCVWCHMYGA | `Image, IMeshModifier`（圆角/异形） | |
| 9 | **WRNLOFEPQTZANWYN** | 翻译文本绑定（`TranslationVariable Selector` @0x20） | `Mod_Loader_OpenButton` |

子节点：`Glow`、`Icon (2)`、`Text`。`Text` 子物体的组件为 `Text + UVZBQMVFQMBFTRPL + RQCKLIBDDOHDGUDN + NDJCKINKYJCMZNTI + ContentSizeFitter + Shadow`——**文字由 `NDJCKINKYJCMZNTI`（官方 ButtonText 包装，内部同时持 `Text` 0x20 与 `TextMeshProUGUI` 0x28）驱动**，不是直接写 uGUI `Text`。

### 2.3 点击去向（决定性证据）

`HKAZNXTEGNSSTQLF` 序列化的 UnityEvent 持久调用是：

```
target : ModLoader.UI.ModsMenu, Assembly-CSharp     ← 混淆后 = dump.cs:25117 的 NANBAJRHGSVEQBTQ
method : Open                                        ← 基类 NQEUSBNLKGCFHQVR.Open，RVA 0x1859FA0
```

即：**点「Mod 加载器」= 把 `- Mods Menu -` 这个官方窗口压进屏幕栈**（`VNDAREHWRIQNAAKC.OpenScreen` 体系，RVA 0x189E850）。全程没有托管代码参与，所以 `ModsMenu` 的 TypeInfo 在二进制里零引用——这也解释了为什么"在 dump.cs 里搜不到按钮创建代码"。

> 顺带：`ModsMenu` 是**混淆前**的真名。UnityEvent 里那串 `m_TargetAssemblyTypeName` 是打包时写死的原始类型名，没有被混淆器改写，所以这类串可以反查类身份（`SFS.UI.BasicMenu` 同理）。

### 2.4 结论

- 「Mod 加载器」= **官方自带 mod 管理器的入口按钮**，硬编码在 `level0` 场景预制体里（`VerticalLayoutGroup` 自动排版，不是运行时代码生成）。
- **没有任何"主菜单菜单项注册表"**：按钮列表是场景里的静态子物体，代码侧不存在 `AddMenuItem(...)` 之类 API。
- 因此"挂到同一列表"的可行做法只有两条：(a) 改场景/预制体资源（等于改 assets，重打包更重且难回滚），(b) 运行时 `Instantiate` 克隆一个已有按钮插进去（卖家路线，也是下面推荐）。

---

## 3. 官方 mod 系统全貌（`ModLoader` 命名空间）

### 3.1 类表（dump.cs 行号 / RVA）

| 角色 | 类 | dump.cs | 关键成员 & RVA |
|---|---|---|---|
| Mods 菜单窗口 | `ModLoader.UI.NANBAJRHGSVEQBTQ`（真名 **ModsMenu**） | 25117 | `static main` @static+0x0；`elementsHolder` 0x38；`elementPrefab` 0x40；`static List<ModData> CILNINLCEGC` @static+0x8；`static pool EGLAJHGHPOH` @static+0x10；`AddMod 0x16C15E8`、`RemoveMod 0x16C1080`、`DrawUI 0x16C0B04`、`OnOpen 0x16C0950`、`Awake 0x16C1488`、`Start 0x16C1444`、`OpenModDownloads 0x16C08E8`、`OpenModsFolder 0x16C17A4`、`CloseWindow 0x16C111C` |
| 列表行（预制体） | `TPCHLVMKGZLNQLMK` | 12240 | `canvasGroup 0x20`、`activeToggle 0x28`、`nameText 0x30`、`infoText 0x38`、`descriptionText 0x40`、`iconHolder 0x48`、`iconImage 0x50`、`Func<bool> 0x58`（读启用态）、`Action 0x60`（切换回调）、`DrawUI 0x188581C` |
| 数据 | `TPCHLVMKGZLNQLMK.ModData`（struct，0x40B） | 12175 | `type 0x0`(int)、`name 0x8`、`version 0x10`、`description 0x18`、`author 0x20`、`saveName 0x28`、`icon 0x30`(Texture2D)、`loadingTexture 0x38`(Task) |
| 类型枚举 | `ModType` | 12190 | `Mod=0, AssetsPack=1, TexturesPack=2, SolarSystem=3` |
| 设置 | `ModLoader.DUBBFVQJWPMXTOTT` + `.Data` | 33681 / 33666 | `Data{ modsActive 0x10, assetPacksActive 0x18, texturePacksActive 0x20, solarSystemsActive 0x28 }`；`get_main 0x1730090`、`SaveAll 0x1730214`、`Awake 0x173029C` |
| 代码 mod 扫描 | `FMDHEDMBGID`（static） | 3120 | JSON 转换器（Texture2D/Sprite/ShadowTexture）+ 异步扫描 `ANFPPHAFMOK`、`NAJFHDKDNEI`；协程 `MKAHFDJGHHI.MoveNext` 内 2 处调 `AddMod` |
| 资源包扫描 | `PNLPMFPJGGF`（static） | 9618 | `AssetBundlePack{AndroidBuild 0x10, CodeAssembly 0x18}`、`KNAONDCCGOB`、协程 `KPEOBAAMNHA.MoveNext` 内 6 处调 `AddMod` |
| 包元数据 | `ModLoader.PackData : ScriptableObject` | 9980 | `DisplayName/Version/Description/Author/ShowIcon/Icon` |
| 太阳系包 | `SFS.WorldBase.YXHFAUIATGCKNHYQ.GetSolarSystemsForModsMenu` | 8741 | 也走 `AddMod(ModData)` |

### 3.2 `AddMod` 到底做了什么（反汇编逐步，RVA 0x16C15E8）

```
0x16C1630  DUBBFVQJWPMXTOTT.get_main()          ; 取 ModsSettings 单例（0x1730090）
0x16C163C  w9 = [x19]                           ; x19 = &ModData（结构体按指针传参），w9 = data.type
0x16C1640  x8 = [settings + 0x20]               ; = settings.data
0x16C1644  switch(type): 0→[x8+0x10] modsActive
                          1→[x8+0x18] assetPacksActive
                          2→[x8+0x20] texturePacksActive
                          3→[x8+0x28] solarSystemsActive
0x16C16B8  Dictionary.set_Item(dict, data.saveName /*0x28*/, true)
0x16C16D4  ModsSettings.SaveAll()               ; 立刻落盘
0x16C16EC  NANBAJRHGSVEQBTQ.main → static_fields → List<ModData>.Add(data)   ; 0x23AC074 AddWithResize
```

要点：
- **`AddMod` 不做 UI**，只做「登记启用状态 + 落盘 + 入列表」。列表渲染在 `DrawUI` 里。
- `OnOpen`（0x16C0950）＝ `base.OnOpen` → `DrawUI`，也就是**每次打开 Mods 菜单都整表重建**（`DrawUI` 用对象池 `NKDDBIBKNON<TPCHLVMKGZLNQLMK>` 回收旧行，遍历 `List<ModData>` 逐行 `DrawUI(data)`）。
- 所以我们手工 `List.Add` 的条目**会在下次打开时照常显示**（列表不被清空），但**永远只是一行"开关"**：`activeToggle` 的 `Action 0x60` 回调只会改 `ModsSettings` 里 `saveName → bool` 并 `SaveAll()`。
- 移动端明确不支持代码 mod：字符串 `NOT SUPPORTED: Because of custom code execution restrictions on mobile, mods with custom code are not supported`、`CodeModNotAllowedOnMobile`。

### 3.3 官方 mod 接入方式（给玩家/包作者，非插件 API）

- 目录：`../Mods`（相对持久化目录，字符串 0x3543C20），配置文件名 `ModsSettings`（0x354D668）。
- 入口：游戏启动时 `FMDHEDMBGID` / `PNLPMFPJGGF` 扫目录 → 读包元数据（`PackData`）→ 组 `ModData` → `AddMod` → Mods 菜单显示行。
- 交互只有三个：行内开关、`Download Mods`（打开论坛 `https://jmnet.one/sfs/forum/...authorised-game-mods.101/`）、`Open Mods Folder`。

**结论：有官方 mod 系统，但没有"可追加菜单项/可注册 UI 页面"的 API。** 对我们的「多人联机」页面零帮助（除非我们甘愿做成一行开关）。

---

## 4. native 能怎么用官方系统（三个方案，按性价比排）

| 方案 | 做法 | 代价 | 结论 |
|---|---|---|---|
| **M1 塞一行** | 构造 `ModData{type=0, name="多人联机", saveName="sfsmp", icon=null}` → `AddMod` | 低（0x40B 结构 + 1 次调用） | 可行，但只多一个开关行，**不能当入口**；且会往 `ModsSettings` 写脏键 |
| **M2 借它的窗口** | `ModsMenu.main->Open()`（RVA 0x1859FA0）打开官方 Mods 菜单 | 极低 | 可行，用于"我们能进官方 mod 页"的验证；页面内容不受我们控制 |
| **M3 克隆官方按钮**（推荐） | 见 §5 | 中（克隆 + 清 clickEvent + 点击接管） | 唯一能实现"主菜单原生外观按钮 → 我们自己的页面"的路线 |

M1 的参考代码（仅用于验证官方列表可写）：

```c
void* cls = il2cpp_class_from_name(cs_image, "", "NANBAJRHGSVEQBTQ");
void* fld = il2cpp_class_get_field_from_name(cls, "main");
void* main_menu = NULL; il2cpp_field_static_get_value(fld, &main_menu);
if (!main_menu) { /* 用 FindObjectsOfTypeAll 兜底（v31 已有 g_mFindAll） */ }
typedef void (*fn_addmod)(void*);
((fn_addmod)(g_base + 0x16C15E8))(moddata_ptr);   /* 结构体按指针传（已由反汇编确认） */
```

---

## 5. 推荐实现：克隆「Mod Loader Button」并接管点击

> 与 `seller_ui_clone_report.md` §2 同一套机制（Instantiate 克隆 + 清继承事件 + 自造回调），但替换为 1.6.00.22 的真实类/偏移。用户名下的 `native_button_clone_study.md` 已在 1.6.00.14 用这套跑通过（probe 版有实机日志）。

### 5.1 需要的 RVA（1.6.00.22，全部已在本机 dump 校验）

| 用途 | 方法 | RVA |
|---|---|---|
| 实例化（带父节点） | `UnityEngine.Object.Instantiate(Object, Transform, bool)` | **0x2FC1B58** |
| 取 GameObject | `Component.get_gameObject` | 0x2FB9E48 |
| 取 Transform | `Component.get_transform` / `GameObject.get_transform` | 0x2FB9D80 / 0x2FBC8C4 |
| 激活 | `GameObject.SetActive(bool)` | 0x2FBCAF4 |
| 改名 | `Object.set_name` / `get_name` | 0x2FC1280 / 0x2FC1138 |
| 兄弟序 | `Transform.SetSiblingIndex(int)` | **0x2FC8DA8** |
| 父节点 | `Transform.get_parent` | 0x2FC75BC |
| 强制重排 | `LayoutRebuilder.ForceRebuildLayoutImmediate(RectTransform)` | **0x3255C64** |
| 按钮文本 | `NDJCKINKYJCMZNTI.set_Text(string)` | **0x176EF50**（`get_Text` 0x17719E4） |
| 空事件构造 | `ClickUnityEvent..ctor` | **0x183D8A4**（`HoldUnityEvent..ctor` 见 dump.cs:39628 段） |
| 屏幕栈 | `VNDAREHWRIQNAAKC.OpenScreen` / `CloseStack` | 0x189E850 / 0x189DCA4 |
| 官方 mod 页 | `NQEUSBNLKGCFHQVR.Open`（ModsMenu 未重写） | 0x1859FA0 |

### 5.2 步骤

1. **等主菜单就绪**：`JNFOGGJGNMZRAZYH.main`（主菜单控制器，挂在场景根物体 **`Home Manager`** 上；UnityEvent 里它的真名是 `SFS.UI.BasicMenu`）非空，或 `Buttons` 容器能按路径找到。
2. **定位容器**：`--- UI ---` → `- Main Menu -` → `Buttons`（Transform.Find 逐级 GetChild，卖家同款 `0xeac6c` 逻辑）。容器组件：`VerticalLayoutGroup + CUJABYOCLJHRSGWT + ContentSizeFitter`，**自动排版，插入即重排**。
3. **定位源按钮**：`Buttons/Mod Loader Button`。也可以按 `HKAZNXTEGNSSTQLF` 实例扫描 + 读子 `NDJCKINKYJCMZNTI.get_Text()` 匹配"Mod 加载器 / Mod Loader"（抗改名）。
4. **克隆**：`Instantiate(src_go, buttons_transform, false)`。
5. **插序**：`SetSiblingIndex(6)`（紧跟 Mod Loader Button，位于它与后面那个 `-` 之间）→ `LayoutRebuilder.ForceRebuildLayoutImmediate(buttons_rect)`。
6. **断继承（关键）**：克隆会带过来 `clickEvent 0x48` 的持久调用（点一下就打开官方 Mods 菜单）。照 1.6.00.14 的做法造空事件覆盖：
   ```c
   void* empty = il2cpp_object_new(click_event_class);   /* ClickUnityEvent */
   il2cpp_object_init(empty);                            /* 也可直接调用 0x183D8A4 的 methodPointer */
   *(void**)((char*)clone_btn + 0x48) = empty;           /* clickEvent @0x48 */
   ```
   同时建议把 `holdEvent 0x40` 也置空事件（否则长按会开 Mods 菜单），并把源按钮的按压动画数组 `KJFNLJCJFHI 0xC8` 共享给克隆（否则按下去没反馈——1.6.00.14 的坑）。
7. **改标签**：先把 `WRNLOFEPQTZANWYN`（翻译组件，在克隆体上）`set_enabled(false)`，否则 `Start()` 会把文字刷回"Mod 加载器"；再 `NDJCKINKYJCMZNTI.set_Text(clone_text, "多人联机")`。图标 `Icon (2)` 想换就 `Image.set_sprite`，不想换就保留（cloned icon = mod 图标，观感统一）。
8. **点击接管**（二选一）：
   - **8A（已验证、推荐）轮询**：`Input.GetMouseButtonDown(0)` + `RectTransformUtility.RectangleContainsScreenPoint(clone_rect, pos, NULL, NULL)`（Android 触摸默认映射鼠标键 0），命中即 `SetActive(true)` 打开我们自己的页面（v31 已有 `page_show()`，Java 覆盖层）。`.14` 的 probe 版就是这条路，日志可查。
   - **8B（保留原生按压手感）改委托**：`onClick`（`DNHHDNDIHAM<JBKLKAEBEEB>`，@0x68）内部布局已确认：`Action @0x10`、`Action<T> @0x18`、`int @0x20`；其 `Invoke`（RVA 0x1E7BA48）实际执行
     `ldr x9,[del+0x18] ; ldr x0,[del+0x40] ; ldr x1,[del+0x28] ; blr x9`
     → 造一个 `il2cpp_object_new(System.Action)`，把 **+0x18 写成 trampoline**、+0x40 写自身、+0x28 抄一份合法 MethodInfo，塞到 `onClick+0x10` 即可（v31 的 `make_action()` 已有 90% 代码，注意它按 v31 注释写的是 +0x10/+0x18 两个都写，这里 **+0x18 才是被调用的那个**）。
     注意：这一步要放在清 `clickEvent` 之后、克隆体 `OnEnable` 之后再写，否则会被游戏重建覆盖。

### 5.3 若要"多人联机"页也做成原生 uGUI

不建议自己 `AddComponent` 搭（字体/底色/圆角/安全区都要复刻）。两条更省的路：
- 复用我们已有的 Java 覆盖层页面（v31 `page_show/page_hide`，已验证）；
- 或者克隆官方 `- Mods Menu -` 的 `Holder/Menu/Safe Area` 容器当"空壳页面"（自带标题栏、关闭按钮、滚动区），只改标题 + 清空 `Mod List (Scroller)` 子项，再把我们的内容渲进去。这是最像官方的一版。

### 5.4 为什么不能只 hook 点击方法

1.6.00.14 的复盘（`native_button_clone_study.md` §12）已证明：`HKAZNXTEGNSSTQLF` 的点击分发走**接口/虚调用**（`slot 8 = PDDMEFFMBGD @0x1761B88` 只是多路分发口），改 `MethodInfo->methodPointer` 拦不住（同程序集直接调用/虚表调用都不经过它）。要么清事件后自己轮询，要么改事件对象内部委托（8B）。别在这上面再花时间。

---

## 6. 和卖家版对照：卖家的按钮是怎么上去的

| 项 | 卖家（SFS Pro，1.6.00.14 时代） | 我们（1.6.00.22） |
|---|---|---|
| 机制 | `Object.Instantiate` 克隆 `Buttons/Play Button`（或 `Credits Button`）→ SetParent/SetSiblingIndex → set_name → 改文本 → `RemoveAllListeners` 剥原行为 → 自造 `System.Action`（`object_new` + 改 `method_ptr/invoke_impl`）挂回调 | 同一套；但 **1.6.00.22 的按钮不是 `UnityEngine.UI.Button`**，是 `HKAZNXTEGNSSTQLF`，`onClick` 不是 `UnityEvent` 而是 `DNHHDNDIHAM<JBKLKAEBEEB>` → 没有 `RemoveAllListeners`，改为「覆盖 `clickEvent 0x48` + 改 `onClick` 内部委托」或「轮询」 |
| 标签 | 双语硬编码表（读 `LanguageSettings_2.txt`） | 直接写中文，或读 `SFS_Translation` 的 `fields` 字典（`SFS_Translation` dump.cs:12297，`Mods_Button` 等键在 13827） |
| 「Mod 加载器」 | 卖家**没有**加它（其报告 §7-3 存疑） | **官方自带**（§2 实证），我们不需要动它，只需在它下面插一个自己的 |
| 工厂法 | 零件面板 `< Previous/Next >` 用 `DrawFullButton` | 同方法在 .22 存在于 `NOJZIZCFSEXSUEEX`（0x18DDCF8），但**实例只在 VAB `Stats` / Career `Part Menu`**，主菜单没有 → v31 的按钮因此不会出现在主菜单 |

所以答案是：**卖家的「添加服务器 / 直接连接」是克隆法；「Mod 加载器」不是他们的，是官方的。** 我们可以照抄克隆法，但目标源按钮换成官方 `Mod Loader Button`（比 Play Button 更合适：它的图标/文案槽位就是为"功能入口"准备的）。

---

## 7. 坑与验证清单

| 风险 | 现象 | 对策 |
|---|---|---|
| 继承 `clickEvent` | 点我们的按钮打开官方 Mods 菜单 | 覆盖 `0x48` 为空 `ClickUnityEvent`；同时清 `holdEvent 0x40` |
| 翻译组件回写 | 文字被刷回"Mod 加载器" | 关闭 `WRNLOFEPQTZANWYN`（`Behaviour.set_enabled(false)`）后再 `set_Text` |
| 按压无反馈 | 点下去没动画/没声音 | 共享源按钮的 `KJFNLJCJFHI 0xC8` 按压动画数组 |
| 布局错位 | 新按钮压在分隔条上/不居中 | `SetSiblingIndex` 后调 `LayoutRebuilder.ForceRebuildLayoutImmediate 0x3255C64`；`CUJABYOCLJHRSGWT` 的 `children`(0x60) 是 Awake 时快照，可能不认新子物体——若尺寸异常，直接克隆 `LayoutElement` 的 `preferredHeight` 覆盖 |
| 场景重建 | 回主菜单后按钮消失/重复 | 每帧检查 `clone.activeInHierarchy` 与父节点；失效即销毁重建（卖家 `MP-HOME-REBUILD` 同款），加 10s 限流 |
| 中文字体 | 缺字形/方块 | 用克隆体自带字体（`Text` 组件的 font 引用），中文由语言包同款字体渲染；先小范围验证 |
| `main` 为空 | 调 `ModsMenu.main->Open()` 崩 | `- Mods Menu -` 在场景里 `m_IsActive=true`，Awake 会跑；但仍要判空，兜底 `FindObjectsOfTypeAll` |

**真机验证顺序（建议）**
1. 只做定位 + 打印：`Buttons` 子物体名/序号、`Mod Loader Button` 的 `NDJCKINKYJCMZNTI.get_Text()` → 确认路径与中文标签；
2. 克隆但不改事件：点一下应打开官方 Mods 菜单（证明克隆成功、继承存在）；
3. 覆盖 `clickEvent` 后再点：应无反应（证明切断成功）；
4. `set_Text("多人联机")` + `page_show()`：闭环；
5. 反复进出 VAB/主菜单 10 次，确认无重复按钮/无崩溃。

---

## 8. 一页速查

```
# 场景（level0）
--- UI ---/- Main Menu -/Buttons                      <- VerticalLayoutGroup 容器（pathID 476）
--- UI ---/- Main Menu -/Buttons/Mod Loader Button    <- 克隆源（pathID 283）
--- UI ---/- Mods Menu -                              <- 官方 mod 窗口（NANBAJRHGSVEQBTQ + DUBBFVQJWPMXTOTT）
Home Manager（场景根）                                 <- JNFOGGJGNMZRAZYH.main + NDETYFDOWUMGDBJI（主菜单控制器）

# 类型
HKAZNXTEGNSSTQLF      官方按钮   dump.cs:37463  holdEvent 0x40 clickEvent 0x48 onClick 0x68 KJFNLJCJFHI 0xC8
NDJCKINKYJCMZNTI      按钮文本   dump.cs:38908  set_Text 0x176EF50
ClickUnityEvent       点击事件   dump.cs:54089  ctor 0x183D8A4
DNHHDNDIHAM<T>        事件容器   dump.cs:51787  Action 0x10 / Action<T> 0x18 / int 0x20；Invoke 0x1E7BA48
NANBAJRHGSVEQBTQ      ModsMenu   dump.cs:25117  main@static+0x0, List@+0x8, AddMod 0x16C15E8, DrawUI 0x16C0B04
TPCHLVMKGZLNQLMK      Mod 行     dump.cs:12240  DrawUI 0x188581C
ModData               数据       dump.cs:12175  0x40B: type0x0 name0x8 ver0x10 desc0x18 author0x20 saveName0x28 icon0x30
DUBBFVQJWPMXTOTT      ModsSettings dump.cs:33681 get_main 0x1730090 SaveAll 0x1730214

# RVA
Instantiate(Object,Transform,bool) 0x2FC1B58 | SetSiblingIndex 0x2FC8DA8 | SetActive 0x2FBCAF4
set_name 0x2FC1280 | get_name 0x2FC1138 | ForceRebuildLayoutImmediate 0x3255C64
NQEUSBNLKGCFHQVR.Open 0x1859FA0 | VNDAREHWRIQNAAKC.OpenScreen 0x189E850
NOJZIZCFSEXSUEEX.DrawFullButton 0x18DDCF8（仅 VAB/Career 有实例）
```

*报告完。所有结论均有 dump 行号 / RVA / 资源解析结果对应；未做真机验证的部分已在 §7 标注为待现场确认。*
