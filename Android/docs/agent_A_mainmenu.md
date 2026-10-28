# SFS 1.6.00.22 主菜单构建方式与「多人联机」原生按钮注入方案

> 分析对象：`D:/sfs-tools/apk/dump-official/dump.cs`（597,338 行，1.6.00.22）、
> `D:/sfs-tools/apk/livesigned/live-aligned-debugSigned.apk` 内 `assets/bin/Data/level0`（主菜单场景）、`lib/arm64-v8a/libil2cpp.so`
> 方法：Il2CppDumper dump + capstone 反汇编（RVA==ELF VA）+ UnityPy 解析 level0 场景层级/序列化字段
> 只读分析，未改任何工程文件；本文件为唯一产出。

---

## 0. 结论（TL;DR）

1. **主菜单不是代码生成的，是场景里手工摆的 6 个预制按钮。**
   场景 `level0` 的层级是：`--- UI ---`（Canvas, ScreenSpaceOverlay）→ `- Main Menu -` → `Buttons`，
   `Buttons` 下依次是 `Play Button / - / Shop Button / - / Mod Loader Button / - / Dev Blog / - / Settings Button / Credits Button`。
   每个按钮的点击行为写死在 **`SFS.UI.HKAZNXTEGNSSTQLF.clickEvent`（偏移 0x48）的序列化 PersistentCall** 里
   （例如 `Mod Loader Button → ModLoader.UI.ModsMenu.Open`、`Credits Button → OpenCredits`）。
2. **主菜单管理类 = `SFS.UI.JNFOGGJGNMZRAZYH`（TypeDefIndex 1390，dump.cs:45043）**，即场景里的 `Home Manager`。
   它只负责：菜单音乐、星空材质、`settingsButton`(0x48) 和 6 个社交按钮(0x60~0x88) 的 `onClick` 接线、商店/翻译初始化。
   **它没有任何「添加菜单项」的 API，没有按钮列表，也没有预制体字段。**
3. 因此「往主菜单加第 7 个按钮」**只有克隆一条主路**（卖家版就是这么做的）：
   `Object.Instantiate` 克隆 `Buttons/Credits Button` → `SetParent(Buttons)` + `SetAsLastSibling` →
   清掉继承来的 `clickEvent` → 改文本 → 把伪造的 `System.Action` 挂到 `onClick`(0x68)。
4. `NOJZIZCFSEXSUEEX.DrawFullButton` 工厂法**在主菜单不可用**（主菜单没有 drawer 实例），只适合 VAB 零件面板/生涯界面。
5. 「Mod 加载器」按钮**也是同一套**：同一个容器下的普通预制按钮、同一组件栈、同样靠 `clickEvent` 序列化调用——它是游戏自带功能，不是 mod 注入的。

---

## 1. 版本勘误：v31.c 里的类名是 1.6.00.14 的，1.6.00.22 已变

v31.c / 旧笔记里的名字在本版 dump 中**根本不存在**（`grep` 0 命中），必须换成下表名字：

| v31 / 1.6.00.14 名 | 1.6.00.22 实名 | 证据 |
|---|---|---|
| `BKFEKAMPHBWLAMTA`（屏幕基类） | **`APRCAZUBBLBXPVZR`**（abstract, : MonoBehaviour） | dump.cs:38144；子类 `IWDQMXUUYDWJPYYP`(38226)、`VNXPGZLZJUTMTHOI`(43685)、`NQEUSBNLKGCFHQVR`(8822) |
| `GWBAFZUDKCWNMVZC`（屏幕栈管理器） | **`SFS.Input.VNDAREHWRIQNAAKC`** | dump.cs:16462；字段 `static main`@0x0、`currentScreen`@0x20、`Stack<Func<APRCAZUBBLBXPVZR>>`@0x28、`initialScreen`@0x38 |
| 同上，`OpenScreen/CloseStack/CurrentScreen` | RVA **0x189E850 / 0x189DCA4 / 0x189E718** | dump.cs:16482~16505 |
| `NOJZIZCFSEXSUEEX`（窗口工厂） | **同名未变** | dump.cs:21261，字段/方法偏移与 v30/v31 一致 |
| 主菜单类（旧版未记录） | **`SFS.UI.JNFOGGJGNMZRAZYH`** | dump.cs:45043，场景对象 `Home Manager` |

补充：`NOJZIZCFSEXSUEEX.DrawFullButton(string,Action,bool)` 在 1.6.00.22 的**唯一调用点**是
`0x181F22C`（`SFS.UI.EHHQWVCRJRNEXEFX.OLDPONPADFC$$BLMLNDKILJO`，即"零件信息面板"的闭包）；
`drawer.Open()` 的调用点只有 `0x18B13B4/0x18B1574`（生涯 `NCPYNZLPTJRKIBXS.Initialize`）和 `0x181EAC0`（`EHHQWVCRJRNEXEFX.Open`）。
→ **主菜单完全不碰这个工厂**（这也解释了为什么 v31 只能在 VAB 里找到那个未激活的 `Part Menu` 实例）。

---

## 2. 主菜单的真实构建方式（场景证据）

### 2.1 层级（UnityPy 解析 `assets/bin/Data/level0`）

```
--- UI ---                     Canvas, RenderMode=0(ScreenSpaceOverlay)
└─ - Main Menu -               RectTransform + BHNCYPQKKSTXRROJ(自适应布局辅助)
   ├─ Title Area
   │  └─ Title … VersionText
   ├─ Buttons                  RectTransform + VerticalLayoutGroup + CUJABYOCLJHRSGWT
   │                           + ContentSizeFitter + DKEKLDIKOUYOJKFB(尺寸钳制)
   │  ├─ Play Button           (280x75)   ← 唯一带 Mask 的
   │  ├─ -                     分隔条 (0x5)
   │  ├─ Shop Button           (280x70)
   │  ├─ Sale Countdown         (inactive)
   │  ├─ -  / Mod Loader Button (280x65) / -  / Dev Blog (280x65) / -
   │  ├─ Settings Button       (280x65)
   │  └─ Credits Button        (280x65)
   ├─ PC Banner / Banner Holder / Links(Youtube,Discord,Instagram,Tiktok,SFS,VK)
```

要点：**`Buttons` 挂了 `VerticalLayoutGroup` + `ContentSizeFitter`**，所有子项 `anchoredPosition` 都是 (0,0)，
位置由布局组算出来 → **只要把新按钮 SetParent 到 `Buttons` 并 SetAsLastSibling，纵向排版自动完成**，不用手算坐标。
`Buttons` 自身锚点 (0.5,0.43)、宽 280；容器高度随内容撑开（第 7 个按钮会把整列往上/下顶，需真机确认是否出屏，见 §8）。

### 2.2 每个主菜单按钮的组件栈（完全一致）

```
Root: RectTransform, CanvasRenderer,
      MB:HKAZNXTEGNSSTQLF           ← SFS 按钮组件（点击/悬停/音效/边框）
      MB:DKEKLDIKOUYOJKFB           ← 宽高钳制（OptionalFloat min/max，0x20~0x38）
      MB:HorizontalLayoutGroup
      MB:IKUBYZNZZCPWONJL           ← 自定义布局基类 ZUMAGTXVRLFABHLW(7507)
      MB:YKWRDRSYSNQEDHBG           ← 按钮视觉/发光
      MB:WRNLOFEPQTZANWYN           ← 翻译组件（字段 Selector@0x20 = 本地化 key）
      MB:IATNFWYCVWCHMYGA           ← 自定义 Image（圆角描边，Play/Shop 没有）
      MB:Image / Mask
子物体:
      Gradient (1)  [Image+LayoutElement]
      Glow          [Image+LayoutElement] (inactive)
      Icon (2)      [Image + ASKDCNIZYXERWHDX + Shadow]  28x28
      Text          [UnityEngine.UI.Text(m_Text="Play") + UVZBQMVFQMBFTRPL + RQCKLIBDDOHDGUDN
                     + NDJCKINKYJCMZNTI + ContentSizeFitter + Shadow]
```

`WRNLOFEPQTZANWYN.Selector`（0x20，`TranslationVariable`）在 **`Start`(RVA 0x1734D50)** 里把文本刷成本地化串
（证据：各按钮该字段分别序列化为 `Play` / `Open_Shop_Button` / `Mod_Loader_OpenButton` / `Development_Blog` /
`Open_Settings_Button` / `Credits_OpenButton`）。克隆后**必须先禁用/置空这个组件再改文本**，否则 Start 会覆盖（§4 步骤 7）。

### 2.3 按钮行为来自序列化的 UnityEvent（原始类型名，非混淆名）

读 `HKAZNXTEGNSSTQLF` 组件的原始序列化字节，每个主菜单按钮的 `clickEvent` 里都能读到：

| 按钮 | clickEvent 内容 |
|---|---|
| Play Button | `Open`（目标为场景对象，实例调用） |
| Shop Button | `OpenShop` |
| **Mod Loader Button** | **`ModLoader.UI.ModsMenu, Assembly-CSharp` + `Open`**（静态/类型限定） |
| Dev Blog | `SFS.UI.BasicMenu, Assembly-CSharp` + `Open` |
| Settings Button | `OpenSettings` |
| Credits Button | `OpenCredits` |
| 事件类型名（UnityEventBase.m_TypeName） | `UnityEngine.Object, UnityEngine` |

→ **「Mod 加载器」与其它 5 个按钮是同一种东西**：场景预制的 `HKAZNXTEGNSSTQLF` + 序列化 PersistentCall，
不是 mod 注入，也不是运行时 new 出来的。`ModLoader.UI.ModsMenu` 对应混淆类 **`NANBAJRHGSVEQBTQ`**
（dump.cs:25116，`ModLoader.UI`，`static main`，字段 `elementsHolder@0x38`/`elementPrefab@0x40`，方法 `DrawUI/AddMod/RemoveMod`），
`Open` 是基类 `PKVTCUBZSQHIVNVO.Open()`（35688）——即"把自己压进屏幕栈"。

### 2.4 主菜单管理类到底做了什么（`JNFOGGJGNMZRAZYH`，dump.cs:45043）

字段：`static main@0x0`、`menuMusic@0x38`、`starsMaterial@0x40`、`settingsButton@0x48`、`fullVersionButton@0x50`、
`fullVersionSalePage@0x58`、`youtube@0x60`、`discord@0x68`、`forums@0x70`、`tiktok@0x78`、`instagram@0x80`、`vk@0x88`。

`Start`(RVA **0x17CFDBC**) 反汇编（节选）证实：

```
0x17d02cc  ldr x20,[x19,#0x60]                 ; youtubeButton
0x17d02e8  ldr x21,[x20,#0x68]!                ; btn.onClick (HKAZNXTEGNSSTQLF+0x68)
0x17d02e4/0x17d0300  System.Action..ctor(target=this)
0x17d0310  bl 0x1E7B944  ; DNHHDNDIHAM<object>.GBOLIGPCDLI(event, Action)  ← += 回调
0x17d0318  str x0,[x20]                        ; 存回 onClick
... 0x68/0x70/0x78/0x80/0x88 同样处理 ...
0x17d050c  ldr x0,[x19,#0x48] ; settingsButton
0x17d0524  b   0x175A26C     ; AEPBOCDLJKG.NLMAFHDFCOI(btn)  ← 接 OpenSettings
```

**它没有任何创建按钮的代码**——再一次确认主菜单没有"菜单项列表"可扩展。

---

## 3. 按钮点击链路（决定回调挂哪里）

`SFS.UI.HKAZNXTEGNSSTQLF`（dump.cs:37463）关键字段：

| 偏移 | 字段 | 说明 |
|---|---|---|
| 0x40 | `HoldUnityEvent holdEvent` | 长按（`UnityEvent<OLBKCLACIJI>`，ctor RVA 0x1777D20） |
| **0x48** | **`ClickUnityEvent clickEvent`** | **序列化的菜单动作在这里**（`UnityEvent<JBKLKAEBEEB>`，ctor RVA 0x183D8A4，dump.cs:54089） |
| 0x60 | `DNHHDNDIHAM<JBKLKAEBEEB> onUp` | 抬起 |
| **0x68** | **`DNHHDNDIHAM<JBKLKAEBEEB> onClick`** | **运行时挂回调的正确落点** |
| 0x70 | `onClickDisabled` | 禁用态点击 |
| 0x78 | `onRightClick` | 右键/长按 |
| 0x98 | `noClickSound` | |
| 0xB8 | `PEMJNEHHPJO`（protected bool，点击有效性总开关） | |

点击分发入口 **Slot 8 `PDDMEFFMBGD` RVA 0x1761B88**（反汇编实测）：

```
0x1761c9c  ldr x0,[x20,#0x60] ; onUp  → DNHHDNDIHAM<object>.NBAHPFHPEED(0x1E7BA48)
0x1761cc4  ldr x8,[x20,#0x68] ; onClick
0x1761ce0  ldr x0,[x20,#0x48] ; clickEvent
0x1761ce4  bl  UnityEventBase.GetPersistentEventCount  ← 只用来决定是否放点击音
0x1761d34  ldr x0,[x20,#0x68] ; onClick → 0x1E7BA48 触发
```

→ **`clickEvent`(0x48) 承载"游戏原生动作"，`onClick`(0x68) 是给代码挂回调用的**。
克隆体两条都会继承/保留，所以：**清 0x48、挂 0x68**。

### 3.1 `DNHHDNDIHAM<T>` 的真实布局（反汇编实测，非 dump 的 0x0）

```
+0x10  Action        IOGJMPCKINN     （无参）
+0x18  Action<T>     GFAAKHMJAEK     （带参）
+0x20  int           count
```
`op_Addition`：`GBOLIGPCDLI(event, Action)` RVA **0x1E7B944**；`GBOLIGPCDLI(event, Action<T>)` RVA **0x1E7B808**。
两者都用 `Delegate.Combine`，并把结果写回 **同一个 event 实例**（`ldr x0,[x21,#0x10]!` → `System.Delegate$$Combine`），返回该实例。

### 3.2 委托调用 ABI（重要，和 v31 的假设不同）

`NBAHPFHPEED` @0x1E7BA48：

```
ldr x8,[event,#0x10]        ; Action
ldr x9,[x8,#0x18]           ; delegate.invoke_impl
ldr x0,[x8,#0x40]           ; delegate.method_code
ldr x1,[x8,#0x28]           ; delegate.method (MethodInfo*)
blr x9                       ; → invoke_impl(method_code, method)
...
ldr x8,[event,#0x18]        ; Action<T>
ldr x0,[x8,#0x40] ; x1=arg ; x2=[x8,#0x28] ; x3=[x8,#0x18]
br  x3                       ; → invoke_impl(method_code, arg, method)
```

即 **trampoline 第 0 个参数是 `method_code`（不是 this/委托对象）**，第 1 个才是业务参数。
v31 的 `action_trampoline(void*,void*,void*,void*)` 全部忽略是安全的；若要取参数（如 JBKLKAEBEEB），要按上面顺序读 x1。

委托对象字段（与 v31 一致）：`method_ptr@0x10`、`invoke_impl@0x18`、`method@0x28`、`method_code@0x40`。

---

## 4. 路径 A（推荐）：克隆 `Buttons/Credits Button`

> 与卖家版 §2 完全同源（`Object.Instantiate` → `SetParent` → `SetSiblingIndex` → 改名/改字 → 清原监听），
> 只是把"按 20 语种文本表找 Play 按钮"换成了**直接按 GameObject 名找**（场景名不本地化，更稳）。

**为什么选 Credits 而不是 Play**：Play 高 75 且有 `Mask`，其它 5 个都是 65；Credits 无 `IATNFWYCVWCHMYGA`/Mask/New Counter，克隆出的视觉与其余按钮一致。反正它的 `clickEvent` 会被清空，不损失功能。

### 4.1 前置条件

- 必须在 **Unity 主线程**执行（沿用现有 `WorldTime.Update` methodPointer 热补丁的 tick，或直接 hook `JNFOGGJGNMZRAZYH.Start` 0x17CFDBC）。
- 用 `il2cpp_runtime_invoke`（不要直接 `blr` 编译指针且传 `method=NULL`）。
- 字符串用 `il2cpp_string_new`（UTF-8 传入，v31 已验证中文可用）。

### 4.2 调用顺序（每步都给出 RVA / 偏移）

| # | 动作 | API / RVA | 说明 |
|---|---|---|---|
| 1 | 找主菜单是否在场 | `Resources.FindObjectsOfTypeAll(Type)` 传 `JNFOGGJGNMZRAZYH` 的 Il2CppType | 数组布局：`len@+0x18`，`data@+0x20`（v31 已按此读） |
| 2 | 找源按钮 | 同一 API 传 `HKAZNXTEGNSSTQLF` 类型 | 遍历取 `get_name(go)=="Play Button"`（或 Credits）且 `get_activeInHierarchy()==true`；场景名不随语言变 |
| 3 | 拿父节点 | `Component.get_transform` **0x2FB9D80** → `Transform.get_parent` **0x2FC75BC** | 父 = `Buttons` |
| 4 | 克隆 | `Object.Instantiate(Object,Transform,bool)` **0x2FC1B58**（或 1 参 0x2FC19C4） | `instantiateInWorldSpace=false`；参数：original=源 GameObject，parent=Buttons Transform |
| 5 | 排序 | `Transform.SetAsLastSibling` **0x2FC8CFC**（或 `SetSiblingIndex` 0x2FC8DA8） | 放到 Credits 之后 |
| 6 | 改名 | `Object.set_name` **0x2FC1280** | `"Multiplayer Button"`（便于重入检测） |
| 7 | 关掉翻译覆盖 | `Behaviour.set_enabled(false)` **0x2F98DD0**；或 `field_set(clone, WRNLOFEPQTZANWYN.Selector@0x20, NULL)` | 必须在写文本**之前**（Start 在同帧稍后执行） |
| 8 | 改文本 | 子物体 `Text` 上 `NDJCKINKYJCMZNTI.set_Text` **0x176EF50**（dump.cs:38908） | 找不到 NDJC 时退化用 `UnityEngine.UI.Text.set_text` **0x32669B8**（dump.cs:503413） |
| 9 | 清继承动作 | `field_set(btn, 0x48, new ClickUnityEvent)`：`il2cpp_object_new(ClickUnityEvent)` + `.ctor` **0x183D8A4**；`0x40` 可选换成新 `HoldUnityEvent`（ctor **0x1777D20**） | 不清 = 点新按钮会打开制作组页面 |
| 10 | 挂回调 | `field_set(btn, 0x68, GBOLIGPCDLI(btn.onClick, fake_action))`，`GBOLIGPCDLI` **0x1E7B944** | fake_action = `object_new(System.Action)` + `method_ptr/invoke_impl/method_code = trampoline` |
| 11 | 重排（可选） | `LayoutRebuilder.ForceRebuildLayoutImmediate(RectTransform)` **0x3255C64** | `Buttons` 有 VerticalLayoutGroup，不调也会自动排 |
| 12 | 记录 + 守护 | 保存 clone 指针；每 tick 检查 `get_activeInHierarchy(clone)`；场景重载后重跑 | 进游戏会卸载 `level0`，回主菜单是重新加载 |

伪代码（关键片段）：

```c
/* 1-2 找源 */
void *arr = ri(find_all, NULL, (void*[]){ type_hka }, &exc);      /* RVA 由 Resources.FindObjectsOfTypeAll 解析 */
int n = *(int*)((char*)arr + 0x18);
void *src = NULL;
for (int i = 0; i < n; i++) {
    void *go = ((void**)((char*)arr + 0x20))[i];
    const char *nm = get_name(go);                                 /* Object.get_name 0x2FC1138 */
    if (nm && !strcmp(nm, "Play Button") && active_in_hier(go)) { src = go; break; }
}
/* 3 父节点 */
void *tr   = get_transform(src);                                    /* 0x2FB9D80 */
void *par  = get_parent(tr);                                        /* 0x2FC75BC */
/* 4 克隆 */
int32_t worldStays = 0;
void *ip[3] = { &src, &par, &worldStays };
void *clone = ri(instantiate3, NULL, ip, &exc);                     /* 0x2FC1B58 */
/* 5-6 */
set_as_last_sibling(get_transform(clone));                          /* 0x2FC8CFC */
set_name(clone, "Multiplayer Button");                              /* 0x2FC1280 */
/* 7 关翻译 */
void *cbtn = go_get_component(clone, type_hka);                     /* GameObject.GetComponent(Type) 0x2FB9F30 */
void *trc  = go_get_component(clone, type_translation);             /* WRNLOFEPQTZANWYN */
set_enabled(trc, 0);                                                /* 0x2F98DD0 */
/* 8 文本：Transform.Find("Text") 0x2FC915C → GameObject.GetComponent(type_ndjc) → set_Text */
void *txt = find_text_component(clone);
set_text_sfs(txt, u8"多人联机");                                     /* 0x176EF50 */
/* 9 清继承 */
void *ev = object_new(class_click_event);  ctor_click(ev);           /* 0x183D8A4 */
*(void**)((char*)cbtn + 0x48) = ev;   gc_wbarrier(cbtn, 0x48, ev);
/* 10 挂回调 */
void *act = make_action();                                          /* object_new(System.Action)+3 个指针 */
void *ev2 = add_handler(*(void**)((char*)cbtn + 0x68), act);         /* 0x1E7B944 */
*(void**)((char*)cbtn + 0x68) = ev2;  gc_wbarrier(cbtn, 0x68, ev2);
```

### 4.3 关于 "Instantiate 必崩" 这个历史包袱

- v28/v29 的崩溃点不在 `Instantiate` 的托管包装层：`0x2FC1B58` 反汇编显示它先做 `CheckNullArgument`，
  然后 `Internal_CloneSingleWithParent(original, parent, bool)`（0x2FC1C80），后者在 **0x2FC1D3C 用
  `il2cpp_resolve_icall("UnityEngine.Object::Internal_CloneSingleWithParent_Injected(System.IntPtr,System.IntPtr,System.Boolean)")`
  取函数指针，`blr` 之**——若解析返回 NULL，就是 `blr NULL` 的 SIGSEGV，正是"libunity 内部查表返回 NULL"的现象。
- **反证**：v31 调 `DrawFullButton` 成功、List 从 0 涨到 1，而 `DrawFullButton` 内部必然走 `Object.Instantiate(prefab, menuHolder)`
  ——说明本进程里 Instantiate 本身是能用的，问题出在**我们怎么调**（MethodInfo 传 NULL / 直接 blr 编译指针 / 非主线程 / 参数错位）。
- 因此路径 A 的三条硬性要求：**runtime_invoke + 真实 MethodInfo* + 主线程**。卖家版在 1.6.00.14 上就是 runtime_invoke + `class_get_method_from_name`，
  与本方案一致。

---

## 5. 路径 B：工厂法 `DrawFullButton`（不推荐用于主菜单）

签名与 RVA（dump.cs:21350/21362，1.6.00.22 未变）：

| 方法 | RVA |
|---|---|
| `DrawFullButton(int,string,Action,bool)` | **0x18DDCF8** |
| `DrawFullButton(int,Func<string>,Action,Func<bool>,Action<Action>,Action<Action>)` | **0x18DDE48** |
| `DrawButton(int,string,string,Action,bool)` | 0x18DD1D0 |
| `DrawTitle(string)` / `DrawText(int,string)` | 0x18DD5D4 / 0x18DDC08 |
| `Open(Func<bool>,Action<NOJZ..>,bool,Action,Action)` | 0x18DD724 |
| `Close()` | 0x18DC824 |

字段：`menuHolder@0x20`、`spacePrefab@0x58`、`textPrefab@0x60`、`buttonPrefab@0x88`、`fullButtonPrefab@0x90`、
`List<NAABCLLJEKA>@0x98`、`List<GameObject>@0xA0`、`Func<bool>@0xA8`、`Action@0xB0`、`bool@0xB8`。

**主菜单没有这个组件实例**（全场景/全代码调用点见 §1），所以要在主菜单用它必须：
新建 GameObject → `AddComponent(NOJZIZCFSEXSUEEX)` → 从 VAB 里那个 `Part Menu` drawer 实例
`field_get/field_set` 复制 `fullButtonPrefab/buttonPrefab/spacePrefab/textPrefab/statsHolderGroup/rootElement` 等 →
把 `menuHolder` 换成正菜单的 `Buttons` RectTransform → 再 `DrawFullButton`。

- 优点：按钮的 onClick 由游戏代码绑定，零委托伪造。
- 缺点：视觉是 **ModGUI 全宽按钮**（零件面板那种），不是主菜单的 280x65 带图标样式；且依赖 6~8 个字段全部非空，任何 null 都可能 NRE。
- 结论：**只在"外观不重要/临时验证"时用**；正式方案走路径 A。

---

## 6. 路径 C（兜底，零实例化）：就地改造 `Credits Button`

如果 Instantiate 再次失败，这是唯一 100% 不崩的做法：

1. 找到 `Buttons/Credits Button`（同 §4 步骤 2）。
2. `WRNLOFEPQTZANWYN` 关掉 → `NDJCKINKYJCMZNTI.set_Text("多人联机")`。
3. `0x48` 换新 `ClickUnityEvent`，`0x68` 挂伪造 Action。
4. 代价：主菜单少一个"制作组"入口（可接受）；优点：外观/布局/动画 100% 原生，无任何新建对象。

---

## 7. 常量速查（1.6.00.22）

### 7.1 类 / 静态入口
| 项 | 值 |
|---|---|
| 主菜单管理类 | `SFS.UI.JNFOGGJGNMZRAZYH`（dump.cs:45043），`static main`（类静态区 0x0） |
| 主菜单启动 | `Start` 0x17CFDBC、`Awake` 0x17CFCE4、`OpenSettings` 0x17CFD3C、`OpenCredits` 0x17CD82C、`Close` 0x17CDE64 |
| 按钮组件 | `SFS.UI.HKAZNXTEGNSSTQLF`（dump.cs:37463） |
| 点击分发 | `PDDMEFFMBGD` Slot 8，0x1761B88 |
| 文本包装 | `SFS.UI.NDJCKINKYJCMZNTI`（dump.cs:38908），`set_Text` 0x176EF50，`get_Text` 0x17719E4 |
| 翻译组件 | `SFS.UI.WRNLOFEPQTZANWYN`（dump.cs:34187），`Selector@0x20`，`Start` 0x1734D50 |
| 事件类 | `SFS.UI.ClickUnityEvent`（dump.cs:54089）ctor 0x183D8A4；`SFS.UI.HoldUnityEvent`（dump.cs:39628）ctor 0x1777D20 |
| 事件容器 | `DNHHDNDIHAM<T>`（dump.cs:51787），+0x10 Action / +0x18 Action<T> / +0x20 count |
| 事件追加 | `GBOLIGPCDLI` 0x1E7B944（Action）/ 0x1E7B808（Action<T>）；触发 `NBAHPFHPEED` 0x1E7BA48 |
| 屏幕栈 | `SFS.Input.VNDAREHWRIQNAAKC`（dump.cs:16462）`OpenScreen` 0x189E850 |
| Mod 菜单类 | `ModLoader.UI.NANBAJRHGSVEQBTQ`（dump.cs:25116），`static main` |

### 7.2 UnityEngine / UGUI
| API | RVA |
|---|---|
| `Object.Instantiate(Object,Transform,bool)` | **0x2FC1B58** |
| `Object.Instantiate(Object)` | 0x2FC19C4 |
| `Object.get_name` / `set_name` | 0x2FC1138 / **0x2FC1280** |
| `Object.FindObjectsOfType(Type)` | 0x2FC202C（含 inactive 版 0x2FC20A4） |
| **`Resources.FindObjectsOfTypeAll(Type)`** | **0x2FB55D0**（`ResourcesAPI` 里的同名版 0x2FB4D28，别用错） |
| `Object.Destroy(Object)` | 0x2FC1E88 |
| `Component.get_transform` / `get_gameObject` | 0x2FB9D80 / 0x2FB9E48 |
| `GameObject.GetComponent(Type)` | 0x2FB9F30 |
| `GameObject.SetActive(bool)` | 0x2FBCAF4 |
| `Behaviour.set_enabled(bool)` | 0x2F98DD0 |
| `Transform.get_parent` | 0x2FC75BC |
| `Transform.SetParent(Transform,bool)` / `(Transform)` | 0x2FC8168 / 0x2FC8124 |
| `Transform.SetAsLastSibling` / `SetSiblingIndex` | 0x2FC8CFC / 0x2FC8DA8 |
| `Transform.get_childCount` / `GetChild(int)` / `Find(string)` | 0x2FC8BA4 / 0x2FC957C / 0x2FC915C |
| `RectTransform.set_sizeDelta` | 0x2FC6B14 |
| `RectTransformUtility.RectangleContainsScreenPoint` | 0x324DBC4 |
| `LayoutRebuilder.ForceRebuildLayoutImmediate` | 0x3255C64 |
| `UnityEngine.UI.Text.set_text` | 0x32669B8 |
| `Input.GetMouseButtonDown` / `GetMouseButtonUp` | 0x300D258 / 0x300D294 |

### 7.3 场景
| 项 | 值 |
|---|---|
| 场景 | `assets/bin/Data/level0`（split0+split1 拼接；`--- UI ---` 是 Canvas） |
| 按钮容器路径 | `--- UI ---/- Main Menu -/Buttons` |
| 源按钮 | `Buttons/Play Button`（280x75, 带 Mask）/ `Buttons/Credits Button`（280x65，推荐） |
| 容器组件 | VerticalLayoutGroup + CUJABYOCLJHRSGWT + ContentSizeFitter + DKEKLDIKOUYOJKFB |

---

## 8. 风险与必须运行时验证的点

1. **Instantiate 是否真的能跑**（最高优先级）。先做最小验证：主线程 tick 里
   `Instantiate(CreditsButton, ButtonsTransform, false)`，只打印返回指针，不做后续步骤。
   成功判据：返回非 NULL 且 `get_name` 能读、`get_activeInHierarchy`=1。
   失败则退路径 C，并记录 `il2cpp_resolve_icall` 的返回值（0x2FC1C80 内 0x2FC1D3C）。
2. **`clickEvent`(0x48) 是否真的在点击时触发**。虽然 `PDDMEFFMBGD` 只拿它算"要不要放音"，
   但序列化的菜单动作显然是它触发的（否则主菜单点不动）。克隆后务必先清空；想验证"事件路线"可
   先克隆 → 只 setId 别清 → 点一下看是否打开制作组页（会跳页）→ 证明 0x48 生效。
3. **`onClick`(0x68) 挂载**：`GBOLIGPCDLI` 返回的 event 可能是同一实例（反汇编显示原地 Combine），
   仍建议把返回值写回 0x68 并判空。若点击无反应 → 说明 `PDDMEFFMBGD` 只走 `clickEvent`，
   改为"轮询命中检测"（`RectangleContainsScreenPoint` 0x324DBC4 + `Input.GetMouseButtonUp`，旧 v0.02 已验证）。
4. **文本被 Start 覆盖**：克隆当帧 `WRNLOFEPQTZANWYN.Start` 会重设 `Text`，所以顺序必须是"先禁用翻译组件，再写文本"。
   如果克隆体在 Instantiate 时已经跑过 Start，则下一帧再核对一次文本。
5. **7 个按钮出屏**：`Buttons` 锚点固定在 (0.5,0.43)，容器高度由 ContentSizeFitter 撑开；
   加第 7 个 65px 按钮后整列可能顶到 Title 或底部。真机确认；必要时把 `Buttons` 的 `localScale` 调到 ~0.92
   或把 VerticalLayoutGroup 的 spacing 调小（field 名 `spacing`，UGUI 公开字段，运行时 `field_set` 可改）。
6. **场景重载**：进入游戏会卸载 `level0`，回主菜单重新加载 → 必须重建。
   建议直接 hook `JNFOGGJGNMZRAZYH.Start`(0x17CFDBC) 的 methodPointer（Unity 消息走 methodPointer，已验证可行），
   或每 tick 用 `FindObjectsOfTypeAll(JNFOGGJGNMZRAZYH)` 判空后重建；重建前用 `Object.Destroy`(0x2FC1E88) 清旧 clone。
7. **委托合法性**：`Delegate.Combine` 会校验两个委托是同一 klass；fake 的必须是 `System.Action`（或用 `Action<T>` 的真实泛型实例）。
   若 Combine 返回 NULL（类型不匹配），`onClick` 会被写成 NULL → 点击时 `cbz x0` 走 NULL 引用异常路径（0x1761E00 = `il2cpp_codegen_raise_null_reference`）。
8. **trampoline 参数顺序**：x0=method_code、x1=arg/x1=method、x2=method（§3.2）；写成 `(void* self, void* arg)` 也能跑（都忽略），但别把 x0 当 self。
9. **图标**：克隆 Credits 会带"制作组"图标（`Icon (2)` 的 Image.sprite）。要换图需 `Image.set_sprite`，或干脆 `SetActive(false)` 隐藏 Icon 子物体（`GameObject.SetActive` 0x2FBCAF4）。
10. **每次改包都要重取 RVA**：本表全部对应 1.6.00.22，`Instantiate/get_name/set_name` 这类 UnityEngine 常量在 1.6.00.14→.22 恰好没变，
    但 SFS 自己的类名/方法每版都变（见 §1）。

---

## 9. 与卖家版 / 旧成果的复用关系

| 卖家机制（1.6.00.x） | 本版可用性 | 说明 |
|---|---|---|
| `Object.Instantiate` 克隆 `Buttons/Play Button` | ✅ 直接复用 | 但按**名称**找比按 20 语种文本表找稳（v0.02 的文本表可删） |
| `Transform.SetParent` + `SetSiblingIndex` 插到 `last-button` 之后 | ✅ | 本版没有 `last-button`，用 `SetAsLastSibling` 或插在 Credits 之后 |
| `Object.set_name` + `TMP_Text.set_text` | ⚠ 改 API | 本版按钮文本是 **UGUI Text**（不是 TMP），走 `NDJCKINKYJCMZNTI.set_Text`(0x176EF50) 最稳 |
| `UnityEvent.RemoveAllListeners` → 清克隆监听 | ❌ 本版**找不到**该方法 | 改为 `field 0x48 = new ClickUnityEvent`（0x183D8A4），等效且更彻底 |
| `il2cpp_object_new(System.Action)` + 改 `method_ptr/invoke_impl/method_code` | ✅ | 偏移 0x10/0x18/0x40 不变；本版额外需要 0x28(method) 可为 NULL |
| `DrawFullButton` 工厂法（零件面板分页） | ⚠ 仅 VAB | 主菜单无 drawer 实例，且外观不同（§5） |
| 容器重建检测 / 复用 / 限流 | ✅ 需要 | 场景重载会销毁 clone，至少要"每帧判空 + 重建" |
| `Last-button`/`Settings` 行克隆（UITOG） | — | 与本任务无关 |

旧 v31 的两个结论需要修正：
- "主菜单按钮由 DrawFullButton/工厂创建"——**错**，主菜单是场景静态按钮；
- "`BKFEKAMPHBWLAMTA`/`GWBAFZUDKCWNMVZC`"——**本版已改名为 `APRCAZUBBLBXPVZR`/`VNDAREHWRIQNAAKC`**。

---

## 10. dump.cs 行号索引

| 内容 | 行号 |
|---|---|
| `NOJZIZCFSEXSUEEX`（窗口工厂） | 21261（DrawFullButton 21350/21362） |
| `NQEUSBNLKGCFHQVR`（菜单基类 : APRCA…） | 8822 |
| `PKVTCUBZSQHIVNVO`（简易菜单屏，Open/Close） | 35688 |
| `APRCAZUBBLBXPVZR`（屏幕基类） | 38144 |
| `IWDQMXUUYDWJPYYP` / `VNXPGZLZJUTMTHOI` | 38226 / 43685 |
| `VNDAREHWRIQNAAKC`（屏幕栈）+ OpenScreen | 16462 / 16505 |
| `HKAZNXTEGNSSTQLF`（按钮） | 37463 |
| `ClickUnityEvent` / `HoldUnityEvent` | 54089 / 39628 |
| `NDJCKINKYJCMZNTI`（文本包装） | 38908 |
| `WRNLOFEPQTZANWYN`（翻译组件） | 34187 |
| `DNHHDNDIHAM<T>` | 51787 |
| `JNFOGGJGNMZRAZYH`（主菜单 Home Manager） | 45043 |
| `NANBAJRHGSVEQBTQ`（ModLoader.UI 菜单） | 25116 |
| `UnityEngine.Object` / `Instantiate` | 434187 / 434239 |
| `UnityEngine.Transform` | 435384 |
| `UnityEngine.UI.Text` | 503413 |
| `LayoutRebuilder` | 501516 |
| `RectTransformUtility` | 581916 |

---

## 附：一句话给实现的建议顺序

1. 只做 §8-1（主线程 Instantiate 一个 Credits 克隆、打印指针）——**先证伪/证实这条**；
2. 加上 SetParent + SetAsLastSibling + 文本 + 清 0x48 —— 看按钮是否出现且不跳页；
3. 最后挂 0x68 的伪造 Action —— 看 trampoline 是否被点中（打日志 `ACTION HIT n=`）；
4. 再加场景重载重建 + 出屏微调。
