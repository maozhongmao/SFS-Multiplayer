# 卖家版 SFS Pro (com.FLDPXIce.SpaceflightSimulatorPro) "按钮克隆 / UI 注入" 技术报告

> 目标：弄清卖家 mod 框架如何在安卓 SFS 原生 Unity UI 上添加/复用按钮，评估我方（SFS-Multiplayer）复刻同样 UI 注入的可行性与路线。
> 材料：`D:/SFS-Pro/jadx_out1004/sources/com/sfsmod/*.java`（1004 版 classes2）、`native1004/libpatch.so`（2,060,672 B）、`native1004/libs.so`（28,600 B）、APK assets、`build/log_now.txt`。
> 方法：jadx 源码通读 + 字符串定位 + capstone AArch64 静态反汇编（本文件中 `0x…` 均为 libpatch.so 的文件偏移，该文件 VA==文件偏移）。
> 日期：2026-10-15。约束遵守：仅新增本报告；未联网、未动设备。

---

## 0. 结论速览（TL;DR）

1. **按钮克隆/注入整套机制在 native（`libpatch.so`）里实现，不在 Java 层。** Java 层（`com.sfsmod.*`）只做两件事：(a) Android View 自绘悬浮窗/悬浮球；(b) 通过 `ModUIHelper.nativeXxx()` 向 native 要数据/回传点击。
2. 卖家用了**两套**注入技法：
   - **克隆法**（主菜单、多人联机界面、世界列表行）：`UnityEngine.Object.Instantiate` 克隆游戏**自己已有**的按钮 GameObject → 重新 SetParent/定位 → 改名/改文字 → **先 `RemoveAllListeners` 剥掉原按钮行为** → 挂自己的回调（自造 managed delegate）。
   - **工厂法**（零件面板分页器 `PMP-BTN`）：直接解析并调用游戏自己的按钮工厂方法 **`DrawFullButton(int, string, Action, bool)`**，把自己的 `System.Action` 传进去，由游戏代码完成按钮创建与 onClick 绑定。
3. 克隆源与挂载点都是**游戏原生层级路径**字符串：`Buttons/Play Button`、`Buttons/Credits Button`、`Buttons/Row 1/Play`、`Buttons/Row 2/Rename`、`Settings`、`last-button` 等，配合 `Component.get_gameObject` / `Transform.SetParent` / `Transform.SetSiblingIndex` / `RectTransform.set_anchoredPosition` / `UnityEngine.UI.LayoutRebuilder.ForceRebuildLayoutImmediate`。
4. 观察到的"添加服务器 / 直接连接 / 主菜单"按钮 = 克隆法产物，标签走**双语表**（读 `LanguageSettings_2.txt` 判断语言：`"Multiplayer Add Server"/"添加服务器"`、`"Multiplayer Direct Connect"/"直接连接"`），点击后进的是 **mod 自建的服务器列表 UI**（`native-menu-open`），不是跳游戏原生界面。
5. **对我方的关键判断**：我们的 SFS-Multiplayer 是 **C# DLL mod**（`Client/release/*/SFS-Multiplayer-*.dll` + `Client/refs/v15|v16/Assembly-CSharp.dll`）。在 PC/Mono 上，卖家做的这一切 = 几十行 C#（`Object.Instantiate` + `TMP_Text.text` + `onClick.AddListener`），**不需要 native，也不需要反射**。卖家下沉 native 的原因是其框架要在 Android **IL2CPP**（无 Mono JIT）上加载 C#，于是自嵌 HybridCLR 解释器并干脆把 UI 注入也写成 native。**纯 Java + 反射在 Android 上不可行**（Unity 对象是 il2cpp native 堆对象，Java 反射看不到），Java 只能做"覆盖层"（这正是卖家 Ball 家族/BmlUi 的做法）。

置信度（本报告结论）：克隆法/工厂法机制 = **高**（有明确字符串+调用点反汇编）；回调绑定细节 = **中**（见 §7 开放问题）；"Mod 加载器"标签来源 = **低**（未找到明文串）。

---

## 1. 体系结构：三层 + 加载链（证据）

| 层 | 载体 | 职责 | 证据 |
|---|---|---|---|
| Java | `classes2.dex` → `com.sfsmod.*`（1004 版 119 类） | Android View 自绘 UI（悬浮球/窗口/活动页）、授权；JNI 门面 `ModUIHelper.nativeXxx()` | `ModUIHelper.java:162-252` 共 ~50 个 `native` 声明（`nativeGetUiOpacity`、`nativeOnClick`、`nativeSetBlurRect`、`nativeSetHiddenParts`…） |
| Loader | `libs.so` | 验证并加载 libpatch：内含 `"SFS_LOADER"` 标记、`ED25519_verify`、`dlopen`；对外转发本应由 libpatch 提供的 13 个 `Java_com_sfsmod_*` 符号 | `libs.so` 字符串表：`SFS_LOADER`、`dlopen`、`Java_com_sfsmod_ModUIHelper_nativeSetActivity` 等 |
| Native 主框架 | `libpatch.so` | 真正的 UI 注入引擎 + 游戏逻辑 hook + **HybridCLR 解释器**（跑 C# mod DLL） | 导入表无 `il2cpp_*`（只有 `dlopen/dlsym/dl_iterate_phdr/ptrace/…`），但字符串里有 **81 个 `il2cpp_*` 名字**；含 `_ZN9hybridclr8metadata11VTableSetUp…` 符号（HybridCLR）；`store_dll refused (not licensed)`、`dll_dispatch_done`、`IC-INTERP: unsupported op …`；构建路径泄露 `D:\2SFS Project\testdll\UIToolsStub\` |
| 资源 | APK assets | `assets/bootstrap/{libpatch.so,libpatch.so.sig}`（`PeUi.ensureBootstrapPatch` 解压到 `filesDir/patches`，version≥268）；`anaisBall.png/ball.png/fnBall.png/icBall.png/infoBall.png/pdBall.png/sasBall.jpeg`；`assets/settings/*.png|jpg` | `PeUi.java:36-61`；APK 文件清单 |

- **il2cpp API 用法**：运行时 `dlopen("libil2cpp.so")`+`dlsym` 解析（"il2cpp-02"、"resolve: 命中类 kn='%s' ks='%s'"），覆盖 `class_from_name / method_from_name / runtime_invoke / object_new / string_new / field_*` 等，属标准 IL2CPP modding 面。
- **JNI 注册方式**：`libpatch.so` 只导出 13 个 `Java_com_sfsmod_*`，其余（含 `nativeGetUiOpacity`）由 `JNI_OnLoad`（0x13d448）RegisterNatives 动态注册。

---

## 2. Q1：按钮"克隆"的完整机制

### 2.1 定位：源按钮与容器（游戏原生层级路径）

按 GameObject 层级路径逐级 `GetChild` 查找（路径查找助手函数 @`0xeac6c`）。用到的路径串：

```
Buttons / Buttons/Row 1 / Buttons/Row 1/Play / Buttons/Row 1/Resume
Buttons/Row 2/Delete World / Buttons/Row 2/Rename / Buttons/Credits Button
Buttons/Play Button / Settings / last-button / Holder / Scroll Window
World List (Scroller) / Main Menu
```

另有按组件的扫描路径：`Transform.get_childCount` + `GetChild(i)` + `get_name`（`clone-child[%d]=%s` 日志 @0x158778），以及 `MP-SCAN: comp[%d]=%s want=%s`（找 `UnityEngine.UI.Button` 组件）。

### 2.2 克隆本体：`UnityEngine.Object.Instantiate`

反汇编证据（三段字符串拼出类/方法名，再 `il2cpp_class_get_method_from_name` + `il2cpp_runtime_invoke`）：

```
0xe9a4c: STR="UnityEngine"   0xe9a54: STR="Object"   0xe9a5c: STR="Instantiate"   （UITOG/主菜单切换行注入，@0xe9a00 起）
0xed0ac: STR="UnityEngine"   0xed0bc: STR="Object"   0xed0c4: STR="Instantiate"   （主菜单「不要点」按钮）
```

全文件 "Instantiate" 串共 9 处引用（0xdbeec/0xdc088/0xe9a5c/0xec884/0xed0c4/0x116b8c/0x155f24/0x156fec 等），全部是同一模式：**克隆已存在的游戏按钮 GameObject**。配套日志：`created-clone`（@0xec104，含 `name='%s' parent='%s'`）、`created-no-button-component`（@0x24379）、`src has no button`（@0xe9bd4）、`clone FAILED`、`find-buttons-null`、`buttons-fail`。

### 2.3 挂载与布局

```
Component.get_gameObject → GameObject.get_transform → Transform.SetParent          （0xe9a74–0xe9afc）
                                                     → Transform.SetSiblingIndex  （0xe9b08，插入 "last-button" 之后）
```

日志：`UITOG: INJECTED after %s (src=%d insert=%d)`（@0xe9bc0）。
多人列表的按钮则用 **RectTransform 数值布局**：

```
RectTransform.get_sizeDelta / get_anchoredPosition   （0xee428–0xee4d4）
RectTransform.set_anchoredPosition                   （0xeeb74 / 0xeed34 / 0xeee68）
UnityEngine.UI.LayoutRebuilder.ForceRebuildLayoutImmediate （0xee5d0–0xee5e8）
```

配 `connect-button-split-halves`（宽度切半）、`connect-button-anchor-split`（锚点重排）、`connect-button-layout-rebuilt`（重建完成）三段日志（0xee53c–0xee5f0），即"克隆后按原生行内布局对半劈开，再强制重排"。

### 2.4 改名 / 文字 / 图标 / 本地化

- 改名：`UnityEngine.Object.set_name`（助手 @0xeb8f0），例如把克隆体命名为 `Do Not Click Button`；
- 子节点定位：`clone-child[%d]=%s` 枚举里找 `Text` / `ButtonText` / `Icon (2)`；
- 文本：`TMPro.TMP_Text.set_text`（0xed30c–0xed3d8）+ `SFS.UI.ButtonText` 字段；
- 双语标签：读 `LanguageSettings_2.txt` → `strcmp(…,"Chinese")` → 二选一：
  - `Multiplayer Add Server` / **添加服务器**（0xeead0）
  - `Multiplayer Direct Connect` / **直接连接**（0xeec90）
  - `Multiplayer Enter Server %d`（进入世界 / Enter World）、`Multiplayer Connect Server %d`（连接服务器 / Connect Server）、`Multiplayer Delete Server %d`（删除服务器）、`Multiplayer Rename Server %d`（编辑服务器）
  - 另有 `SFS.Translations` 类 + 混淆键（`DNXRIGFHFVEUMRWY`、`ITKBCXAJHTIGERVU`、`OVVLKXAISCDEMSPW`、`XXWNLMTGUDEVDYSL`——最后一个是 `SFS.UI` 的混淆静态单例/工厂）。

### 2.5 剥掉原按钮行为（关键一步）

克隆体会继承源按钮的 onClick。卖家**主动清空**：

```
0x158958–0x158a0c：
  UnityEngine.UI.Button.get_onClick          → UnityEngine.Events.UnityEvent
  UnityEvent.RemoveAllListeners              → UnityEventBase.GetPersistentEventCount
  日志："MP-HOME-STRIP: 去掉克隆 %p 的游戏原监听（剩余持久 %d ✓）"
```

### 2.6 回调绑定：自造 managed delegate（native trampoline）

明文字符串即完整证据链（引用点 @0x17ee00–0x17eeac，`PMP-BTN` 系列）：

```
"PMP-BTN: System.Action klass not found"
"PMP-BTN: il2cpp_object_new returned null"
"PMP-BTN: step=set_fptrs[%d] del=%p klass=%p"
"PMP-BTN: extra code-ptr offsets method=0x%zx method_code=0x%zx trampoline=0x%zx"
"PMP-BTN: delegates ready prev=%p next=%p"
```

即：`il2cpp_object_new(System.Action)` 造一个托管委托实例，把委托内部代码指针字段（method_ptr / method_code / invoke_impl，偏移运行时探测）改成 **native trampoline** 地址，于是 native 函数就能当 C# 回调使用。该 delegate 交给游戏代码（如 `DrawFullButton` 的 `Action` 参数）或注册进 mod 自己的点击分发表。

点击到达时另有分发日志：`MP-CLICK: '%s' kb=%d data=%p eventGo=%p self=%p | scene=%d menuOpen=%d addForm=%d reg=%d`（含注册表序号 `reg=%d`）、`CLICK w=%d name='%s'`、`MP-BTN: tick apply delta=%d menu=%p (frame context, safe)`、`regular-create-event name=%s event=%p cancel=%p descendant=%d`。**注意：全文未见明文 `AddListener`**（见 §7）。

### 2.7 生命周期与"复用"

- `home-button reuse: reactivated (no clone)`（@0xec598）：同一按钮需要再次出现时**不重新克隆**，直接复用/重新激活已有克隆体（配 `regular-self … activeSelf=%d`、`GameObject.get_activeSelf/get_activeInHierarchy` @0xec15c–0xec1c4）；
- `MP-HOME-REBUILD: Buttons 容器已更换 ⇒ 重建多人按钮（只丢一次缓存 ✓）`（@0xec544）：检测到容器 GameObject 被场景重建换掉时批量重建；
- 限流：`MP-HOME-REBUILD-SKIP: 「不要点」距上次重建 <10s ⇒ 本轮推迟`；
- 移除：`MP-SRC-BTN: 「不要点」已按用户要求移除（克隆体已销毁 ✓）`（@0xecfa0，配 `UnityEngine.Object.Destroy` @0xecf6c）；
- 场景恢复：`HOME-RECOVER: enter flow cleared + buttons re-enabled`、`MP-HOME-FIX: 渲染链[%d] '%s' active=%d alpha=%.2f canvasOff=%d ⇒ 修 ✓`、容器销毁时甚至 `MP-HOME-FIX: Home UI 被销毁 ⇒ 重载场景（软重启菜单）`。

### 2.8 "添加服务器/直接连接"按钮点击后去哪

不是跳游戏原生界面，而是**打开 mod 自建的服务器列表/表单 UI**：
`native-buttons addGo=%p addButton=%p directGo=%p directButton=%p`（@0xeeddc）、`native-menu-open` / `native-menu-card-fail`（@0xeef7c/0xeefb0）、`MP-NICK-TICK: menuOpen=%d scene=%d form=%p row=%p holder=%p input=%p`、`Multiplayer Cancel Add Server`、`add-rejected-server-limit`。
世界列表行内注入的 `进入世界/Enter World` 按钮 → `Multiplayer Enter Server %d` → `MP-ENTER: invoking enter callback=%p worldRef=%p settings=%p`（进 mod 的联机流程，非游戏原生"载入世界"）。

---

## 3. Q2：自绘 UI 与原生 UI 的边界

### 3.1 自绘（Android View，完全不进 Unity 树）

| 模块 | 形态 | 证据 |
|---|---|---|
| `BmlUi` | 全屏"正在加载 Mods 与零件包…"遮罩 = `FrameLayout+LinearLayout+ProgressBar+TextView+秒表`，`addView` 到 `activity.getWindow().getDecorView()` | `BmlUi.java:81-124`（`overlay: 已盖上 ✓`）、native 侧 `BML: 加载已完成…` |
| **Ball 家族**（`AnaisUi/FnUi/IcUi/InfoUi/PdUi/SasUi/PeUi/MpChatUi` + `UiWin`/`UiPos`/`KeypadUi`） | **悬浮球**＝`PopupWindow`+`ImageView`，图标=**assets 根目录 PNG/JPEG 解码**（`UiCore.decodeSampled`）；可拖动，位置持久化 `ballpos.json`；**窗口**＝`createModWindow`：`PopupWindow`+`LinearLayout`（GradientDrawable 圆角+半透明底）+ Android `Button` | `AnaisUi.java:432`（`anaisBall.png`）、`FnUi.java:179`（`fnBall.png`）、`ModUIHelper.java:951-1003`（`createModWindow`）、`ModUIHelper.java:1005-1082`（`installBallDrag`）、`ANAIS-BALL: assets/anaisBall.png missing -> text fallback` |
| 按钮点击回传 | Android 按钮 → `ModUIHelper.nativeOnClick(i)` → native 分发 | `UiCore.java:194/311`；`ModUIHelper.java:697`（`nativeOnText`） |
| `SettingsActivity` | 纯 Android 设置页：每行 `TextView+ToggleSwitch`，展开显示说明图 `getAssets().open("settings/"+key+".png\|.jpg\|.jpeg")`；`enable_part_menu_pager` 行特殊（`buildPmpRowsControl()`，配置 PMP 分页） | `SettingsActivity.java:52`（TOGGLES 表：`debug_hud`/`enable_legacy_burn`/`fix_mirror_rotated_parts`/…）、`:255` |
| `LicenseActivity` | 激活/授权页（与 UI 注入无关） | — |

### 3.2 克隆（Unity 原生 UI 树内，native 实现）

主菜单按钮、多人联机界面底部三键、世界列表行内按钮、零件面板分页器——全部是 §2 所述 Instantiate 克隆/工厂路线。

### 3.3 混合：IC 图标系统（native 造 Unity 物体）

`IC-*` 是"图标叠加系统"：native 用 `GameObject` + `SpriteRenderer` + `Texture2D` + `Sprite.Create` **在世界空间**生成图片对象（`IC-INIT: SpriteRenderer klass=%p`、`IC-CREATE: AddComponent(SpriteRenderer)`、`IC-CREATE: Texture2D object_new`、`IC-GO: … native(cachedPtr)=%p`、`IC-DRAG: WorldToScreenPoint`、`IC-CALL: IcCreateImage/IcCreateImageCenter`）；Java `IcUi` 只是控制器 + 窗口（`nativeIcCreate/nativeIcAction/nativeIcSetTransform`）。**IC ≠ 克隆按钮**，是"原生 mesh 图标"。

---

## 4. Q3：IC / INFO / FN-HIDE 与透明度系统

- **IC**：见 §3.3（图标系统；native 造物，Java 控窗）。
- **INFO**：`InfoUi` Java 信息浮窗；native 供数据（`infoShow/infoSetText` + `nativeMpServerInfo`）。
- **FN**：`FnUi`（InfoOverload 的功能开关条/球）；native 管状态（`nativeGetFnState/nativeFnToggle`，`s_fnOn[13]`）。`FN-HIDE: pw=` 是 **Java 弹窗隐藏日志**（`FnUi.java:128`），隐藏时还会 `UiScene.hideAllModBalls()`（`:153`）。
- **透明度系统**（native ↔ Java 同步）：
  - Java 每 **30ms** 轮询 `nativeGetUiOpacity()`，值域 clamp 到 [0,1]（`ModUIHelper.java:88-131` `s_opacityRun`）；
  - 变化>0.01 → 记录并 400ms 去抖后 `UiScene.rebuildBallsForOpacity()`（防抖窗口+1.5s 重建限流）；
  - 应用到所有 mod 窗口：`UiScene.applyOverlayOpacity / applyUiOpacityTo`、`applyPwOpacity`、`applyWinAlpha`（含 WindowManager.LayoutParams.alpha 直改 + 强制重绘）；另有 `UiWin.sweepGhostBalls` 每 2s 清"幽灵球"；
  - native 侧读出的是**游戏 UI 的显隐/淡出状态**（旁证：libpatch 有 `get_alpha`、`CanvasGroup` 字符串；推断采样游戏 CanvasGroup.alpha，未逐指令确认）→ 实现"游戏 UI 隐藏时 mod 悬浮球一起淡出"，避免 mod UI 突兀残留。
- **模糊底**：Java 上报 mod 窗口屏幕矩形 `nativeSetBlurRect(x,y,w,h)`（`ModUIHelper.java:503-539`），native 侧 GL/IMGUI 模糊（`IMGUI-BLUR: shader/FBO/targets ready`、`IMGUI-BLUR: shader compile FAILED`）。
- **`nativeSetHiddenParts(boolean)` / `HIDE-PARTS(v91/v92/v93)`**：零件选择器**过滤掩码**（crew/fuel/engine/legacy/career），含指纹校验（`HIDE-PARTS(v91): fingerprint … NOT hooked`）、hook `CareerState.HasPart`（与 §UI 无关，属零件系统）。
- **关系总结**：透明度/模糊/位置持久化是**共存基建**（让克隆按钮与自绘悬浮窗不与游戏视觉打架）；IC/INFO/FN 是功能 UI；克隆/工厂是"进入原生 UI 树"的两种手段。彼此并列、共享同一套 JNI 基建。

---

## 5. Q5：`DrawFullButton` 的签名与调用方式

- **签名（游戏自身方法）**：`DrawFullButton(int, string, Action, bool)` —— 字符串 `"PMP-BTN: DrawFullButton(int,string,Action,bool) not found"`（引用点 @0x17e8b8）。
- **解析方式**：`il2cpp_class_get_method_from_name` 取候选后，**用代码指针校验重载**：
  `PMP-BTN: DrawFullButton resolve pc4=%p(rva=0x%llx) pc6=%p(rva=0x%llx) %s`、`PMP-BTN: refusing to use the wrong overload`、`!!! SAME = WRONG OVERLOAD !!!`（防止选错重载）；解析失败则退化 `PMP-BTN: placeholder MethodInfo=%p (klass=%p)`。
- **调用方式**：构造 §2.6 的 `System.Action`（native trampoline）→ `il2cpp_runtime_invoke` 调用 `DrawFullButton(slot, label, action, enabled)`；标签就是 `"<   %s"`/`"%s   >"` + `Previous`/`Next`（0x17eef8–0x17ef30）→ 零件面板出现"< Previous / Next >"分页键。日志：`PMP-BTN: built`、`PMP-BTN: tick ignored`、`PMP-BTN: change_page out of range %d (page=%d pages=%d)`、`PMP-BTN: verifying…`。
- **意义**：这是"**让游戏自己的 UI 工厂替你建按钮**"的路线（onClick 由游戏绑定，无需自己 AddListener）；克隆路线用于游戏没有现成工厂的场景（菜单/列表）。

---

## 6. Q4：复刻可行性与我方路线建议

### 6.1 若走 native 复刻（Android 通用），需具备：

1. **注入载体**：自研加载器（卖家为 `libs.so` "SFS_LOADER" → 验签 → `dlopen libpatch.so`；你可用自己的壳 + `System.loadLibrary` 或直接改 `libmain` 挂载）。
2. **il2cpp API 解析**：`dlopen("libil2cpp.so")`+`dlsym` 拿 `il2cpp_*` C API（或指针直调，更快）；`il2cpp_thread_attach`；类/方法/字段查询 `class_from_name / method_from_name / field_*`。
3. **场景定位**：`GameObject.Find`/层级路径遍历。注意 SFS 的 UI 成员名是**混淆名**（`XXWNLMTGUDEVDYSL` 等），需按游戏版本用 Il2CppDumper 生成 offset 表或在运行时按特征扫描。
4. **建按钮两条路**：`Object.Instantiate`（克隆）+ `TMP_Text.set_text` + `RectTransform` 布局 + `LayoutRebuilder.ForceRebuildLayoutImmediate`；或 `DrawFullButton` 工厂（若目标菜单有）。
5. **回调**：`il2cpp_object_new(System.Action)` + 改 `method_ptr/invoke_impl` 指向 native trampoline；或直接 `UnityEvent.AddListener`（若有 MethodInfo）。
6. **生命周期**：场景/容器重建检测、去重、限流、容器销毁兜底（卖家用了一整套 `MP-HOME-*` 状态机，这部分实际占代码量最大）。
7. **（可选）共存基建**：透明度同步、模糊底、位置持久化。

### 6.2 能否绕开？（**纯 Java + 反射**）

- **Android/IL2CPP 上不行**：Unity 的 UI 对象是 il2cpp 托管对象（native 堆），Java 反射只能看到 Java 类；`UnityPlayer` 只提供 Java→Unity 的 `UnitySendMessage` 发送口，SFS 未暴露接收器。Java 能做的只有**覆盖层**：把 Android `Button` 叠在 Unity 画面之上（卖家 Ball 家族/`BmlUi` 就是这么做的）——能看能点，但**无法复用**原生按钮的布局/动画/语言切换，且只能靠轮询（如 `nativeGetUiOpacity`）勉强同步显隐。
- **你们真正的捷径**：SFS-Multiplayer 是 **C# DLL mod**（`Client/release/…/SFS-Multiplayer-*.dll`，引用 `Assembly-CSharp.dll` v15/v16）。**在 PC/Mono 上**，卖家 native 里干的事在 C# 里是直接可用的 Unity API：
  ```csharp
  var src = GameObject.Find("Buttons/Play Button");           // 或任意已有按钮
  var clone = Object.Instantiate(src, parent, false);
  clone.name = "MP_AddServer";
  clone.GetComponentInChildren<TMP_Text>().text = "添加服务器";
  var btn = clone.GetComponent<Button>(); btn.onClick.RemoveAllListeners(); // 卖家也这么做
  btn.onClick.AddListener(() => OpenMyServerList());
  ```
  这等价于卖家 §2 全部工作，且无版本偏移风险。
- **Android 版**：IL2CPP 无法直接加载 DLL——**这正是卖家内嵌 HybridCLR 解释器的原因**。三条路：(a) 自建 native 注入（§6.1，成本最高但最通用）；(b) 集成 HybridCLR 式解释器（需与游戏 `global-metadata.dat` 严格配版）；(c) 覆盖层过渡（Android 上先用 Android View 方案，功能可用、观感不融合）。
- **建议顺序**：PC C# 先做 + 把"找按钮/克隆/挂回调"抽成 UI 模块（屏蔽平台差异）→ Android 需求明确后再评估 native 或解释器路线。

---

## 7. 开放问题与不确定项

1. **克隆按钮自身回调的最终绑定 API 未见明文 `AddListener`**；二进制中存在 XOR 0x5A 加密串（已验证 `"Action"` 的 0x5A 变体存在，服务器地址也是 0x5A 编码）——不排除 `AddListener` 被加密或改写 `UnityEvent.m_Calls` 内部结构/走 `MP-CLICK` 注册表分发。**结论按证据保守表述。**
2. `nativeGetUiOpacity()` 的采样对象（CanvasGroup.alpha / 游戏自管 fade 字段）未逐指令确认，仅 `get_alpha`/`CanvasGroup` 字符串旁证。
3. 现场看到的"**Mod 加载器**"主菜单按钮标签：native 与 classes2.dex 均**未找到明文**；`UITOG`（0xe9a00 区）证实"克隆 `Settings` 页里一行并插到 `last-button` 之后"的机制（日志 `UITOG: INJECTED after %s…`、`ModUI Toggle Row`），标签可能运行时拼接或来自被解释的 mod DLL。
4. `MP-CLICK` 注册表（`reg=%d`）与 `regular-create-*`（疑似 hook 游戏常规按钮创建流程）的确切挂点未定位。
5. libpatch 内 HybridCLR 解释器与 C# DLL 的加载/授权门（`store_dll refused / method_interp blocked`）细节未展开（另一主题）。
6. `build/log_now.txt` 是 logcat 转储，不含 mod 自身 tag，未用于运行期交叉验证（机制结论全部基于静态证据 + 用户现场观察）。

---

## 附录 A：关键偏移速查（libpatch.so，VA==文件偏移）

| 偏移 | 内容 |
|---|---|
| 0x2b2a2 / 0x2d856 | `clone-child[%d]=%s` / `created-clone` |
| 0x1b72c | `home-button reuse: reactivated (no clone)` |
| 0x2b1eb / 0x2b2b5 | `MP-BTN-CLASS: go=%p comp=%p cls=%s` / `get_onClick` |
| 0xe9a4c–0xe9a68 | 三个字符串解析 + `UnityEngine/Object/Instantiate` 克隆调用（UITOG 区） |
| 0xe9a74–0xe9b2c | SetParent / SetSiblingIndex / 插到 `last-button` 之后 |
| 0xe9bd4 / 0xe9be8 | `src has no button` / `clone FAILED` |
| 0xeac6c | 层级路径查找助手（GetChild 链） |
| 0xeb8f0 | `Object.set_name` 助手 |
| 0xec104 | `created-clone` 日志（含 `name='%s' parent='%s'`） |
| 0xec544 / 0xec598 | `MP-HOME-REBUILD: Buttons 容器已更换…` / `home-button reuse…` |
| 0xecf6c–0xecfa0 | `Object.Destroy` + `MP-SRC-BTN: 「不要点」已移除` |
| 0xed0ac–0xed3d8 | 主菜单按钮：Instantiate → SetParent → set_name → `TMP_Text.set_text` |
| 0xee0a0–0xee128 | 世界列表行注入（`Buttons/Row 1/Play`… / `Multiplayer Enter Server %d`） |
| 0xeead0 / 0xeec90 | **添加服务器** / **直接连接** 标签选择 |
| 0xee53c–0xee604 | `connect-button-split-halves / anchor-split / layout-rebuilt / created` |
| 0xeeddc | `native-buttons addGo=… addButton=… directGo=… directButton=…` |
| 0x158704–0x158790 | `get_childCount`/`GetChild`/`get_name` → `clone-child[%d]=%s` |
| 0x158958–0x158a0c | `get_onClick` → `RemoveAllListeners` → `GetPersistentEventCount` → `MP-HOME-STRIP` |
| 0x17e8b8 | `PMP-BTN: DrawFullButton(int,string,Action,bool) not found` |
| 0x17eef8–0x17ef30 | `"<   %s"` + `Previous` / `"%s   >"` + `Next`（工厂法分页键） |
| 0x17ee00–0x17eeac | `System.Action` delegate 自造（object_new + set_fptrs + delegates ready） |
| 0x136c18 | `PMP-BTN: built` |
| 0x13d448 | `JNI_OnLoad`（动态注册 JNI 方法） |
| 0x243cd / 0x1e0c5 | `RemoveAllListeners` / `GetPersistentEventCount` 字符串 |

## 附录 B：中文标签 → 机制速查

| 现场标签 | 机制 | 点击去向 |
|---|---|---|
| 主菜单"Mod 加载器" | 未定（UITOG 克隆 Settings 行机制已证；标签来源待查，§7-3） | 打开 mod UI |
| 多人界面"添加服务器" | 克隆法（§2）+ 双语标签 `Multiplayer Add Server` | mod 自建服务器表单（`native-menu-open`） |
| 多人界面"直接连接" | 同上（`Multiplayer Direct Connect`） | mod 直连表单 |
| 多人界面"主菜单" | 复用/重建（`home-button reuse` / `MP-HOME-REBUILD`） | 回游戏主菜单并恢复按钮流 |
| 世界列表行"进入世界/连接服务器" | 克隆进 `Buttons/Row 1|2` | `Multiplayer Enter/Connect Server %d` → mod 联机流程 |
| 零件面板"< Previous / Next >" | **工厂法** `DrawFullButton(int,string,Action,bool)` | 零件面板翻页（PMP 状态机） |
| 悬浮球（anaisBall/icBall/fnBall/infoBall/pdBall/sasBall/ball） | **自绘**：Java PopupWindow+ImageView，图标=assets PNG | 展开对应 mod 窗口（Android 层） |

---
*报告完（时间盒 30min 目标，实际因补充线索追加 Ball 家族排查）；所有结论基于静态证据，运行期行为以用户真机日志为准。*
