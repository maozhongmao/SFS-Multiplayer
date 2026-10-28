# level0 原生窗（Mods / Worlds List / World Creation）GameObject 层级与克隆复用方案

> 目标版本：SFS 安卓 **1.6.00.22**（`com.StefMorojna.SpaceflightSimulator`），native 注入（IL2CPP / arm64）。
> 场景来源：`assets/bin/Data/level0.split0` + `level0.split1`（**APK 内就是这个文件名，不是拆包错误**，见 §1）。
> 符号来源：`D:/sfs-tools/apk/dump-official/dump.cs`（21.4 MB，与 `x22/libil2cpp.so` **md5 一致**：`81c5dd930186fff83c8bd64feb95a691`）。
> 方法：UnityPy 1.25.4 解析 Transform/RectTransform/GameObject 图 + 组件类名（`m_Script.m_ClassName`）+ capstone 反汇编核对 `Awake`。

---

## 0. 结论速览（TL;DR）

1. **三个窗都是 level0 场景里"摆好的"对象，不是运行时实例化。** 根节点名带前后短横线：`- Mods Menu -`、`- Worlds Menu -`、`- World Creation -`，三者互为兄弟。它们都是 `--- UI ---`（**就是 Canvas 根**，Screen Space Overlay）的直接子节点，`activeSelf = true`。
   → 之前报告里写的 `GameObject.Find("Mods Menu")` **必然返回 null**，少了两侧的 `- ` 和 ` -`。
2. **真正的"隐藏"发生在子节点 `Holder`（`activeSelf = false`）**，三个窗全一样：`- X -/Holder`。控制器 `Open()` 把 `Holder` 打开。所以**窗根常驻、Holder 常关**。
3. **不要克隆窗根。** 根上挂着窗口控制器，其中 `NANBAJRHGSVEQBTQ.Awake()` 里有一条 `str x19,[x8]`（x19 = this，x8 = 静态字段区）→ **把 `this` 写进 `static main`**。克隆根（根是 active，Instantiate 会立刻跑 Awake）会让**克隆体顶替真实单例**，原窗的 `CloseWindow/AddMod/DrawUI` 全部改在克隆体上操作。
   → **克隆目标是 `Holder`，不是 `- X -`。** `Holder` 上最多只有 `BHNCYPQKKSTXRROJ`(SafeArea) 或 `CanvasRenderer`，**零控制器、零静态注册**，视觉却包含整扇窗（背景 + 滚动区 + 底栏按钮）。
   **三个窗根的 `childCount` 都恰好为 1，唯一孩子就是 `Holder`** —— 所以"克隆 Holder"等于"克隆全部可见内容、丢掉控制器"，没有遗漏。
4. **Mods 窗里没有标题节点。** 全树只有 `Back / Menu / Safe Area / Scroll Window / Mod List (Scroller) / Element Prefab / BottomPanel / Close / Open Mos Folder / Download Mods`，没有任何 `Title`。`Title Holder/Title Text` 只存在于 `Shop` 和 `- Android Purchase Recover Menu -` 下。**"改标题"其实是"加标题"**，模板从窗内按钮的 `Text` 子节点或 `- Main Menu -/Title Area/Title` 取。
5. **按钮的点击不能用 `RemoveAllListeners` 清。** 这游戏用的是**自定义按钮** `HKAZNXTEGNSSTQLF`，其 `onClick` 是 0x68 处的 `DNHHDNDIHAM<JBKLKAEBEEB>` —— 一个**自己写的委托聚合器**，全类里**没有** `RemoveAllListeners/Clear`。卖家那套 `UnityEvent.RemoveAllListeners` 只适用于 `UnityEngine.UI.Button`（商城/主菜单），对这里不成立。
   → 推荐：**销毁克隆体上"会点"的 `HKAZNXTEGNSSTQLF`**，点击判定走已有的 `RectTransformUtility.RectangleContainsScreenPoint` 轮询；**但滚动面的 `HKAZNXTEGNSSTQLF` 必须留着**（`NEUBHWOMTRBTWJHK` 靠它收拖拽）。
6. **克隆后要立刻补的两件事**：`RectTransform` 的 `anchorMin/Max` 已是 0..1 全屏拉伸（照抄即可），但 `Scroll Window` 的 `sizeDelta=(-50,-240)` 是"拉伸 + 负边距"，**克隆体必须重新 `SetParent(canvas, false)` 之后再改 `sizeDelta`**，否则世界坐标偏移。

---

## 1. 取证方法与一个必须知道的坑

### 1.1 split 不是拆包残留

```
$ python -c "zipfile.ZipFile('live.apk') → assets/bin/Data/level0.split0 = 1048576 B, level0.split1 = 85092 B"
level0.split0  1,048,576 B   ← 正好 1 MiB
level0.split1     85,092 B
```

`level0.split0` 恰好 1 MiB，非常像"截断"。核对 APK 原始清单后确认：**游戏自己就用 Unity 的 split serialized file 方案发的包**，`split1` 是 `split0` 的字节续块，运行时由 Unity 拼接。UnityPy 单独读 `split0` 也能拿到**完整对象表（7088 objects / 1446 GameObject）**，因为对象头在前 1 MiB 内；拼接后对象数仍是 7088（`split1` 不新增对象，只是数据尾块）。

> 实测：`cat split0 split1 > level0_full.bin` → `7088 objects`，与单读 `split0` 相同。**所以层级分析用 `split0` 单文件就够，不必拼接**（拼接也不会有新发现）。

### 1.2 真正的限制：脚本 typetree 被裁掉了

```
MonoBehaviour 总数 2756，能 read_typetree 的只有 243
```

Unity 只给内置模块保留 typetree，`Assembly-CSharp` 的脚本 MonoBehaviour **不带 typetree**。后果：

| 能读到 | 读不到 |
|---|---|
| Transform/RectTransform 全部字段（锚点、sizeDelta、父/子、anchoredPosition） | 脚本的**序列化字段值**（`menuHolder`、`elementPrefab`、`onClick` 的绑定目标） |
| GameObject 名字 / activeSelf / 组件列表 | `Text.m_Text` 的实际字符串（全空 → 文本是运行时按语言键填的） |
| 组件的**类名**（`m_Script.m_ClassName`） | UI 预制体引用指向哪个文件 |

所以本文的「字段 → 节点」对应关系（如 `menuHolder → Holder`）是**结构推断**：按字段名 + 唯一候选 + 三个窗命名一致性推出，标注为「推断」。层级本身、组件构成、RectTransform 数值是**硬数据**。

### 1.3 复现命令

```bash
cd D:/SFS-Pro/work
python tree_level0.py "- Mods Menu -"        # 单窗全树
python tree_level0.py "- Worlds Menu -" "- World Creation -"
python probe_chain.py                        # Canvas 祖先链
```

---

## 2. 全局骨架：`--- UI ---` 就是 Canvas 根

```
--- UI ---   [act=True]  {RectTransform, Canvas, CanvasGroup, +3 个未解析 MonoBehaviour}
  ├0 - Main Menu -
  ├1 - Worlds Menu -
  ├2 - World Creation -
  ├3 - Mods Menu -
  ├4 -                （空名占位/分隔，kids=0）
  ├5 Shop              （商城，12 kids）
  ├6 -
  ├7 Notification Menus
  ├8 Development Menu
  ├9 -
  └10 MsgDrawer
```

> 那 3 个 MonoBehaviour 的 `m_Script` 指向外部程序集、本资源读不出类名（`MB?`）。按 Canvas 常规配套**推断**为 `CanvasScaler` + `GraphicRaycaster`（第三个可能是游戏自己的 UI 管理脚本）——标为推断，未验证。不影响克隆：父节点只要是 `--- UI ---`，就自动继承 Canvas 的缩放与射线设置。

- `--- UI ---` **没有父节点**（Transform 的 `m_Father == 0`）→ 它是场景根对象，`Canvas.m_RenderMode = 0`（ScreenSpaceOverlay），`m_SortingOrder = 0`。
- 克隆体的父节点 = `--- UI ---`，并 `SetSiblingIndex` 到 **`- Mods Menu -`(3) 之后**（保证盖在其他窗之上、但在 `Shop`/`MsgDrawer` 之下）。

> **注意 `CanvasGroup`**：`--- UI ---` 上挂 `CanvasGroup`。若在别的菜单打开时（例如 `- Worlds Menu -` 打开、`--- UI ---` 的 group 做过渡）克隆窗会跟着一起淡入淡出 —— 这是想要的行为；但如果你要"永远置顶不受菜单切换影响"，就得挂到独立的二级 Canvas。

---

## 3. 窗 A：`- Mods Menu -` 完整层级（主用）

### 3.1 树 + 组件（`ap`=anchoredPosition，`sd`=sizeDelta）

```
--- UI ---
└─ - Mods Menu -        [act=True]  ap(0,0) sd(0,0) a(0,0)-(1,1)
     {NANBAJRHGSVEQBTQ ← 窗口控制器, DUBBFVQJWPMXTOTT ← 设置/存档组件}
   └─ Holder            [act=False] ap(0,0) sd(0,0) a(0,0)-(1,1)   {CanvasRenderer}
      ├─ Back           [act=True]  ap(0,0) sd(0,0) a(0,0)-(1,1)
      │    {CanvasRenderer, IATNFWYCVWCHMYGA(:Image), HKAZNXTEGNSSTQLF(:自定义Button)}
      └─ Menu           [act=True]  ap(0,0) sd(0,0) a(0,0)-(1,1)   {}   ← 无任何组件，纯容器
         └─ Safe Area   [act=True]  ap(0,0) sd(0,0) a(0,0)-(1,1)   {BHNCYPQKKSTXRROJ(安全区)}
            ├─ Scroll Window  [act=True] ap(0,0) sd(-50,-240) a(0,0)-(1,1)
            │    {RectMask2D, LayoutElement, HKAZNXTEGNSSTQLF(拖拽面), UVQWEWODFFCNSEEP(Pointcast)}
            │  └─ Mod List (Scroller) [act=True] ap(0,0) sd(0,295) a(0,0.5)-(1,0.5)
            │       {VerticalLayoutGroup, ContentSizeFitter, HKAZNXTEGNSSTQLF(拖拽面), NEUBHWOMTRBTWJHK(惯性滚动)}
            │     └─ Element Prefab   [act=True] ap(396,-148) sd(793,295) a(0,1)-(0,1)
            │          {CanvasRenderer, HKAZNXTEGNSSTQLF, TPCHLVMKGZLNQLMK(行逻辑), HorizontalLayoutGroup, CanvasGroup}
            │        ├─ Background        [act] ap(0,0) sd(0,0) a(0,0)-(1,1) {CanvasRenderer,LayoutElement,Image}
            │        ├─ Icon Holder       [act] ap(20,-120) sd(200,200) a(0,1)-(0,1) {LayoutElement,CanvasRenderer}
            │        │  └─ Mask           ap(0,0) sd(200,200) a(.5,.5)-(.5,.5) {CanvasRenderer,Image,Mask}
            │        │     └─ Image       ap(0,0) sd(0,0) a(0,0)-(1,1) {CanvasRenderer,Image}
            │        ├─ Information Holder ap(773,-148) sd(523,255) a(0,1)-(0,1) {VerticalLayoutGroup}
            │        │  ├─ Title      ap(261,0) sd(523,84) a(0,1)-(0,1) {HorizontalLayoutGroup}
            │        │  │  ├─ Name    ap(0,-18) sd(846,96) {CanvasRenderer,NDJCKINKYJCMZNTI(:文本),LayoutElement,Text}
            │        │  │  └─ Active Toggle ap(0,0) sd(80,48) a(1,1)-(1,1)
            │        │  │       {CanvasRenderer,Image,LCOMDMSGLXHKMBTQ(:开关),MoveModule,LayoutElement}
            │        │  │     └─ Block ap(25,0) sd(55,48) {CanvasRenderer}
            │        │  │        └─ Icon ap(0,0) sd(-12,-12) {CanvasRenderer,Image}
            │        │  ├─ Info        ap(523,-184) sd(523,85) {...,NDJCKINKYJCMZNTI,Text}
            │        │  └─ Description ap(0,-199) sd(523,56) {...,NDJCKINKYJCMZNTI,Text}
            │        └─ (Information Holder 内还有 Title/Info/Description 三个文本)
            └─ BottomPanel   [act=True] ap(0,15) sd(-30,65) a(0,0)-(1,0)
                 {CanvasRenderer, HorizontalLayoutGroup, IKUBYZNZZCPWONJL(自定义布局)}
               ├─ Close         [act=True]  ap(268,-32) sd(261,60) a(0,1)-(0,1)
               │    {CanvasRenderer,Image,HKAZNXTEGNSSTQLF,DKEKLDIKOUYOJKFB,HorizontalLayoutGroup,IKUBYZNZZCPWONJL,WRNLOFEPQTZANWYN}
               │  ├─ Glow        [act=False] sd(0,0) a(0,0)-(1,1) {CanvasRenderer,Image,LayoutElement}
               │  ├─ Button Icon [act=False] ap(35,-30) sd(28,28) {CanvasRenderer,Image,ASKDCNIZYXERWHDX,Shadow}
               │  └─ Text        [act=True]  ap(130,-30) sd(163,66)
               │       {CanvasRenderer,Text,UVZBQMVFQMBFTRPL,RQCKLIBDDOHDGUDN,NDJCKINKYJCMZNTI,ContentSizeFitter,Shadow}
               ├─ Open Mos Folder [act=False] ap(450,-32) sd(261,60)   ← 名字里 "Mos" 是游戏自己的拼写错误
               │    {同 Close 组件集}
               │  ├─ Glow [act=False] ├─ Button Icon [act=False] └─ Text [act=True] sd(516,66)
               └─ Download Mods   [act=True] ap(544,-32) sd(261,60)
                    {同 Close 组件集}
                 ├─ Glow [act=False] ├─ Button Icon [act=False] └─ Text [act=True] sd(458,66)
```

### 3.2 组件角色速查（已对 dump.cs 逐个核实）

| 类名 | 真实身份 | 关键点 |
|---|---|---|
| `NANBAJRHGSVEQBTQ` | Mods 窗控制器 `: PKVTCUBZSQHIVNVO` | `static main`（0x0）、`elementsHolder`(0x38)、`elementPrefab`(0x40)；`Awake()` 写 `main=this` |
| `PKVTCUBZSQHIVNVO` | 通用窗基类 `: NQEUSBNLKGCFHQVR` | `menuHolder`(0x28)、`closeAnyway`(0x30)；`Open/Close/OnOpen/OnClose` |
| `NQEUSBNLKGCFHQVR` | 菜单基类 `: APRCAZUBBLBXPVZR` | `pauseWhileOpen` |
| `APRCAZUBBLBXPVZR` | 菜单根 `: MonoBehaviour` | 只有一个 `static Dictionary<key,Action>`；**没有全局菜单栈** → 克隆不会污染菜单栈 |
| `DUBBFVQJWPMXTOTT` | 设置/存档 `: NDLKOLOGGDADDQGZ<Data>` | 带 `settings` 字段、走文件读写 → **克隆它 = 多一份存档组件，必须销毁** |
| `IATNFWYCVWCHMYGA` | `: Image, IMeshModifier` | 自定义九宫格图片（背景/按钮底） |
| `HKAZNXTEGNSSTQLF` | **自定义按钮** | `onClick`(0x68) `DNHHDNDIHAM<JBKLKAEBEEB>`、`clickEvent`(0x48)、`layoutPriority`(0x3C)、`keepGlowOnInit`(0xC4) |
| `DNHHDNDIHAM<T>` | **自定义委托聚合器（非 UnityEvent）** | 只有 `Action`/`Action<T>` 槽 + `int`；**无 RemoveAllListeners** |
| `NDJCKINKYJCMZNTI` | 文本包装 | 同管 `UnityEngine.UI.Text`(0x20) 与 `TextMeshProUGUI`(0x28)；**`PEDMKFBBODP` 就是 Text 属性** |
| `TUXXAURTZSIFPXTZ` | 输入框包装 | `mobileTextBox`(0x20) + `TMP_InputField`(0x28)；自带安卓软键盘 |
| `BHNCYPQKKSTXRROJ` | SafeArea | 方向 + 刘海 insets，`Awake` 里按屏幕算 |
| `NEUBHWOMTRBTWJHK` | 惯性滚动器 | `button`(0x20) `buttons`(0x28) 列表 + `momentumDecayTime`；**靠按钮组件收拖拽** |
| `UVQWEWODFFCNSEEP` | 矩形命中辅助 | `Pointcast(Vector2)` |
| `IKUBYZNZZCPWONJL` | 自定义布局组 `: ZUMAGTXVRLFABHLW` | 底栏排布 |
| `TPCHLVMKGZLNQLMK` | Mod 行逻辑 | 持有 `canvasGroup/activeToggle/nameText/infoText/descriptionText/iconHolder/iconImage` |

---

## 4. 窗 B：`- Worlds Menu -`（World List，备选/复用价值高）

```
--- UI ---
└─ - Worlds Menu -   [act=True] ap(0,0) sd(0,0) a(0,0)-(1,1)
     {PKVTCUBZSQHIVNVO(窗基类), QQUJIOVKJFUVHVBL(世界列表控制器), BHNCYPQKKSTXRROJ(SafeArea)}
   └─ Holder          [act=False] ap(0,0) sd(0,0) a(0,0)-(1,1)  {BHNCYPQKKSTXRROJ}
      ├─ Scroll Window [act=True] ap(0,0) sd(-60,-150) a(0,0)-(1,1) {RectMask2D}
      │  ├─ World Element Prefab [act=True] ap(0,-200) sd(800,450) a(.5,.5)-(.5,.5)
      │  │    {CanvasRenderer, AXVQHBBALEUNLEXX(行逻辑), HKAZNXTEGNSSTQLF, KOGYGAZAWHKCWPMD,
      │  │     IATNFWYCVWCHMYGA, MUCHRJBWORJAKZYY(单选), VerticalLayoutGroup}
      │  │  ├─ Select  ap(0,0) sd(0,0) a(0,0)-(1,1) {CanvasRenderer,Image,LayoutElement}
      │  │  ├─ Name    ap(400,-63) sd(760,60)  {CanvasRenderer,Text,UVZBQMVFQMBFTRPL,Shadow,NDJCKINKYJCMZNTI,LayoutElement}
      │  │  ├─ Stat    ap(400,-180) sd(760,287){CanvasRenderer,Text,UVZBQMVFQMBFTRPL,Shadow,NDJCKINKYJCMZNTI}
      │  │  └─ Buttons ap(400,-347) sd(760,130){CanvasRenderer,LayoutElement}
      │  │     ├─ Row 1 ap(0,70) sd(0,60) a(0,0)-(1,0)
      │  │     │  ├─ Play   ap(-2,0) sd(-5,60) a(0,0)-(.5,0)  {…,HKAZNXTEGNSSTQLF,DKEKLDIKOUYOJKFB,WRNLOFEPQTZANWYN}
      │  │     │  │   ├─ Glow [act=False] └─ Text ap(22,0) sd(121,67) → └─ Button Icon ap(-30,0) sd(56,56)
      │  │     │  └─ Resume ap(2,0) sd(-5,60) a(.5,0)-(1,0)   {同上}
      │  │     └─ Row 2 ap(0,0) sd(0,60) a(0,0)-(1,0)
      │  │        ├─ Delete World ap(-2,0) sd(-5,0) a(0,0)-(.5,1) {同上}
      │  │        └─ Rename       ap(2,0) sd(-5,0) a(.5,0)-(1,1) {同上}
      │  └─ World List (Scroller) [act=True] ap(0,108) sd(0,80) a(0,0.5)-(1,0.5)
      │       {VerticalLayoutGroup, ContentSizeFitter, NEUBHWOMTRBTWJHK, AEAKLUUKPGFHYGXZ, CREDXDCPPFNXOHBV}
      │     ├─ Create World Element [act=True] ap(409,-40) sd(818,80) a(0,1)-(0,1)
      │     │    {CanvasRenderer, IATNFWYCVWCHMYGA, HKAZNXTEGNSSTQLF, LayoutElement}
      │     │  ├─ Glow [act=False] └─ Text ap(0,0) sd(372,0) {…,Text,…,WRNLOFEPQTZANWYN}
      │     └─ Position Indicator [act=False] ap(364,-235) sd(680,250) {LayoutElement}
      └─ Main Menu [act=True] ap(0,0) sd(233,60) a(.5,0)-(.5,0)
           {CanvasRenderer,IATNFWYCVWCHMYGA,HKAZNXTEGNSSTQLF,WRNLOFEPQTZANWYN,ASKDCNIZYXERWHDX,WXZJEOIKEVJAJKQG}
         ├─ Glow [act=False] └─ Text ap(0,0) sd(298,67)
```

要点：
- **`World Element Prefab` 是这个游戏里现成的"卡片行"模板**（图标区 + 标题 + 状态区 + 两排按钮），做服务器列表/房间列表比 Mods 的 `Element Prefab` 更合适。
- `World List (Scroller)` 里那个 `Create World Element` 是"新建"入口，**它是滚动区里的一个普通行**，可以整行复用成"新建房间"。
- `Position Indicator` 默认 `act=False`，是滚动位置指示器，克隆时可删。
- 根的 `QQUJIOVKJFUVHVBL` 持有 `selectedButton`（单选逻辑）→ **克隆根会拿到真实世界列表逻辑**，同样避开。

---

## 5. 窗 C：`- World Creation -`（Create World，输入框来源）

```
--- UI ---
└─ - World Creation -  [act=True] ap(0,0) sd(-30,-30) a(0,0)-(1,1)
     {CanvasRenderer, LKSRSZGMFKUEATEP(创建世界控制器)}
   └─ Holder            [act=False] {BHNCYPQKKSTXRROJ}
      ├─ Rows Frame     [act=True] ap(0,0) sd(0,-70) a(0,0)-(1,1)  {}
      │  └─ Rows        [act=True] a(0.10,0.5)-(0.9,0.5) {VerticalLayoutGroup, ContentSizeFitter}
      │     ├─ Back     [act=False] sd(40,60) {CanvasRenderer,LayoutElement,IATNFWYCVWCHMYGA}
      │     ├─ Name     [act=True] sd(0,130)
      │     │  ├─ Label    ap(0,0) sd(0,60) a(0,1)-(1,1) {CanvasRenderer,IATNFWYCVWCHMYGA,ASKDCNIZYXERWHDX,WRNLOFEPQTZANWYN}
      │     │  │  └─ Text  ap(0,0) sd(0,0) {…,UVZBQMVFQMBFTRPL,Text,Shadow,ContentSizeFitter,RQCKLIBDDOHDGUDN}
      │     │  └─ Text Box ap(0,0) sd(-60,60) a(0,0)-(1,0) {CanvasRenderer,IATNFWYCVWCHMYGA,ASKDCNIZYXERWHDX,HKAZNXTEGNSSTQLF}
      │     │     └─ Input Field ap(0,0) sd(0,0) a(.5,.5)-(.5,.5)
      │     │          {CanvasRenderer,UVZBQMVFQMBFTRPL,Text,Shadow,ContentSizeFitter,RQCKLIBDDOHDGUDN,
      │     │           LINRJGANWEEFRCUR(移动端文本框), TUXXAURTZSIFPXTZ(输入框)}
      │     ├─ Solar System [act=True] sd(0,175)
      │     │  ├─ Label  ap(0,-45) sd(0,60) {…}
      │     │  └─ Button ap(0,-115) sd(-60,60) a(0,1)-(1,1) {CanvasRenderer,HKAZNXTEGNSSTQLF,ASKDCNIZYXERWHDX,IATNFWYCVWCHMYGA}
      │     │     ├─ Glow [act=False] └─ Text ap(0,0) sd(0,0)
      │     ├─ -        [act=True] sd(0,30)   ← 空名分隔条
      │     ├─ Mode     [act=True] sd(0,130)
      │     │  ├─ Label   ap(0,0) sd(0,60) a(0,1)-(1,1)
      │     │  └─ Options ap(0,0) sd(-100,60) a(0,0)-(1,0) {NCTIIVFOTRIIHRKA(单选组)}
      │     │     ├─ Mode Classic   ap(-2,-30) sd(-5,60) a(0,.5)-(.5,.5) {…,HKAZNXTEGNSSTQLF,ASKDCNIZYXERWHDX,IATNFWYCVWCHMYGA,WRNLOFEPQTZANWYN}
      │     │     └─ Mode Challenge ap(2,-30)  sd(-5,60) a(.5,.5)-(1,.5)  {同上}
      │     │        每个子节点：Glow[act=False] / _Text[act=True] sd(795,127) / Outline[act 有时 True 有时 False]
      │     ├─ Toggles  [act=False] ap(304,-330) sd(607,130)
      │     │  ├─ Cheats Toggle     {CanvasRenderer,HorizontalLayoutGroup} → Name Back/Name(Line)/Toggle Button(Block/Icon)
      │     │  └─ Quicksaves Toggle 同上，Toggle 组件为 LCOMDMSGLXHKMBTQ
      │     ├─ -        [act=True] sd(0,30)
      │     └─ Difficulty [act=True] sd(0,130)
      │        ├─ Label   ap(0,0) sd(0,60)
      │        └─ Options ap(0,0) sd(-100,60)
      │           ├─ Normal    a(0,.5)-(.33,.5)
      │           ├─ Hard      a(.33,.5)-(.67,.5)
      │           └─ Realistic a(.67,.5)-(1,.5)
      └─ Bottom Buttons [act=True] sd(0,60) a(0.15,0)-(0.85,0) {CanvasRenderer, CUJABYOCLJHRSGWT}
         ├─ Cancel  ap(-2,0) sd(-5,60) a(0,0)-(.5,0) {CanvasRenderer,HKAZNXTEGNSSTQLF,ASKDCNIZYXERWHDX,IATNFWYCVWCHMYGA,WRNLOFEPQTZANWYN}
         └─ Confirm ap(2,0)  sd(-5,60) a(.5,0)-(1,0) {同上}
```

`LKSRSZGMFKUEATEP` 的字段（推断对应）：`worldsMenu`(→`- Worlds Menu -`)、`createWorldButton`(→`Create World Element`)、`worldNameField`(→`Text Box/Input Field`)、`solarSystemButtonText`、`difficultyGroup`(→`Difficulty/Options`)、`modeGroup`(→`Mode/Options`)、`quicksavesToggle`(→`Toggles/Quicksaves Toggle/Toggle Button`)、`togglesHolder`(→`Toggles`)、`difficultyInfo`。

> **输入框可直接借**：`Text Box/Input Field`（`TUXXAURTZSIFPXTZ` + `LINRJGANWEEFRCUR`）自带安卓软键盘，是这套 UI 里唯一能打字的控件。

---

## 6. 克隆复用方案（推荐路线 R-A′：克隆 `Holder`）

### 6.1 三条"不能克隆根"的硬证据

| # | 证据 | 后果 |
|---|---|---|
| 1 | `NANBAJRHGSVEQBTQ.Awake()` @`0x16C1488`：`str x19,[x8]`，`x8 = [0x343d000+0xC8]+0xB8`，`x19 = this` | 克隆体**顶替 `static main`**；原窗 `CloseWindow/AddMod/DrawUI` 全跑到克隆体上 |
| 2 | 根上 `DUBBFVQJWPMXTOTT : NDLKOLOGGDADDQGZ<Data>`（带 `settings` + `IFile`） | 克隆体**多一个存档/设置组件**，`Awake/Start` 里重复读写同一份文件 |
| 3 | 根 `activeSelf = true`（`Holder` 才是 false） | `Instantiate` 立即在克隆体上跑 `Awake/OnEnable`，躲不掉 |

而 `Holder`：Mods = `{CanvasRenderer}`、Worlds = `{BHNCYPQKKSTXRROJ}`、WorldCreation = `{BHNCYPQKKSTXRROJ}` —— **零业务组件**。

### 6.2 七步流程

```
1. 定位源（见 §7）：src = GameObject.Find("/--- UI ---/- Mods Menu -") → GetChild(0) = "Holder"
   校验：name=="Holder" && childCount>=1 && 第一个孩子叫 "Menu"（Mods）或含 "Scroll Window"

2. 复制：copy = Object.Instantiate(src, parent)          parent = "--- UI ---".transform
   ★ 必须传 parent，别用无参重载再 SetParent —— 传 parent 会保持 localScale/localRotation
   ★ Holder 是 inactive → 克隆体也 inactive → Awake/OnEnable 不会跑，正好

3. 生根改名：Object.set_name(copy, "MyMultiplayerWindow")
   （名字不唯一会导致后续 Find 拿到错的那个，所以必须改名）

4. 剥逻辑（**在激活之前**做，避免 OnEnable 注册）：
   Destroy 掉克隆体上所有 DUBBFVQJWPMXTOTT / *窗口控制器*（其实 Holder 下没有）
   对"会点"的节点（Back / Close / Open Mos Folder / Download Mods）：
       Destroy 其 HKAZNXTEGNSSTQLF 组件   ← 见 §6.4
   ★ 不要动 Scroll Window / Mod List (Scroller) 上的 HKAZNXTEGNSSTQLF（拖拽要用）

5. 清列表：拿 Mod List (Scroller) 的 transform，
   for i in childCount-1..0: if child.name=="Element Prefab" → 留作模板，其余 Destroy
   （VerticalLayoutGroup + ContentSizeFitter 自动重排，不需要手动改 sd）

6. 加标题 + 加按钮（§6.3 / §6.4）

7. 激活并置顶：
   SetActive(copy, true)
   SetSiblingIndex(copy, index_of("- Mods Menu -") + 1)
   首次显示后调一次 LayoutRebuilder.ForceRebuildLayoutImmediate(copy 的 RectTransform)
```

### 6.3 "改标题" → 其实是"加标题"（窗里没有标题节点）

Mods 窗全树**没有任何 Title 节点**（`Title Holder/Title Text` 只在 `Shop` 与 `- Android Purchase Recover Menu -` 下）。三种取模板方式，按保真度排序：

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A（推荐）** | 克隆 `BottomPanel/Download Mods/Text` → 挂到 `Menu/Safe Area` 下，`anchoredPosition` 设 (0,-40)，`anchorMin/Max=(0,1)-(1,1)`，`sizeDelta=(0,70)` | 同窗同风格（同一套 Shadow/Outline/ContentSizeFitter） | 字号偏大，需调 |
| B | 克隆 `- Main Menu -` 的 `Title Area/Title` | 有正式标题样式 | 跨窗引用，父级缩放体系不同，要重设 anchor |
| C | 直接改 `NDJCKINKYJCMZNTI.set_Text` 到已有文本节点（如 `Element Prefab/Information Holder/Title/Name`） | 零新增对象 | 位置不对，且那是行内文本 |

**改文字的统一入口**：`NDJCKINKYJCMZNTI.set_Text(string)`（属性 `PEDMKFBBODP`），它同时支持 `Text` 与 `TextMeshProUGUI`。
> 注意：场景里的 `Text.m_Text` **全为空**，UI 文案是运行时按语言键填的 —— 克隆体不会继承任何旧文字，**必须自己 set_Text**，否则是空标题（这反而是好事：不会有残留串）。

### 6.4 按钮：**不要用 RemoveAllListeners**（该类没这方法）

`HKAZNXTEGNSSTQLF.onClick` 是 0x68 处的 `DNHHDNDIHAM<JBKLKAEBEEB>` —— 自定义委托聚合器：
```
public class DNHHDNDIHAM<T> {
    private Action IOGJMPCKINN;        // 无参槽
    private Action<T> GFAAKHMJAEK;     // 带参槽
    private int <…>k__BackingField;    // 计数
    public static DNHHDNDIHAM<T> NLEMEHGPBIB(DNHHDNDIHAM<T>, Action)  // "合并/添加"，返回新实例
}
```
**没有 `RemoveAllListeners` / `Clear`**。卖家那套 `Button.get_onClick → UnityEvent.RemoveAllListeners` 针对的是 `UnityEngine.UI.Button`（商城/主菜单的那些），**在这里无效**。

可选破法：

| 方案 | 做法 | 评价 |
|---|---|---|
| **A（推荐）** | **`Destroy` 克隆体上会点节点的 `HKAZNXTEGNSSTQLF` 组件**，点击用已有的 `RectTransformUtility.RectangleContainsScreenPoint` 轮询（v37 现成） | 零 managed 调用、零委托伪造、零崩溃面。**代价：失去 hover 发光**（`Glow` 本来就 `act=False`，需要时自己 SetActive 模拟） |
| B | 保留组件，改写 `DNHHDNDIHAM` 的两个 `Action` 槽指向自己的 stub | 能保留发光，但泛型字段偏移在 dump 里是 0x0（未解析），**偏移要运行期探测**，风险高 |
| C | 保留组件但把 `skipRaycast`(0x38) 置 true / `layoutPriority`(0x3C) 调低 | 只影响排序/射线，**onClick 仍会触发原逻辑**，不可靠 |

> **必读**：克隆体的按钮上，`onClick` 绑定的目标是**原窗控制器实例**（在 `Holder` 之外，`Instantiate` 不会重映射到克隆体）。所以若不清，点克隆体的 `Back` = **关掉真实的 Mods 窗**，克隆体自己还留着 —— 这是最容易被忽略的"假克隆"事故。

**滚动面的例外**：`Scroll Window` 与 `Mod List (Scroller)` 上的 `HKAZNXTEGNSSTQLF` 是 `NEUBHWOMTRBTWJHK` 的**拖拽接收面**（它有 `public HKAZNXTEGNSSTQLF button` + `List<...> buttons`）。**销毁它们 = 列表不能滚**。

### 6.5 伪代码（贴合你现有 il2cpp 解析风格）

```c
// ---- 一次性解析（复用你 method_checked 的 name+argc+RVA 三重校验）----
g_goFind        = "UnityEngine","GameObject","Find",1
g_goFindWithTag = ... (不用)
g_instantiate   = "UnityEngine","Object","Instantiate",2      // (Object, Transform) -> Object
g_destroy       = "UnityEngine","Object","Destroy",1
g_getTransform  = "UnityEngine","GameObject","get_transform",0
g_setParent     = "UnityEngine","Transform","SetParent",2     // (Transform, bool)
g_setSibling    = "UnityEngine","Transform","SetSiblingIndex",1
g_getChild      = "UnityEngine","Transform","GetChild",1
g_getChildCount = "UnityEngine","Transform","get_childCount",0
g_setName       = "UnityEngine","Object","set_name",1
g_setActive     = "UnityEngine","GameObject","SetActive",1
g_getComp       = "UnityEngine","GameObject","GetComponent",1  // 泛型：需带 klass 的 MethodInfo
g_getCompT      = "UnityEngine","GameObject","GetComponentInChildren",2
g_rtRectContains= "UnityEngine","RectTransformUtility","RectangleContainsScreenPoint",3
// 文本：Assembly-CSharp
g_setText       = "","NDJCKINKYJCMZNTI","set_Text",1
g_forceRebuild  = "UnityEngine.UI","LayoutRebuilder","ForceRebuildLayoutImmediate",1

// ---- 建窗 ----
void build_my_window(void) {
    void* ui   = g_goFind(str("/--- UI ---"));            if (!ui) return retry_later();
    void* src  = g_goFind(str("/--- UI ---/- Mods Menu -")); if (!src) return retry_later();

    // Holder = 唯一子节点
    void* srcT = call(g_getTransform, src);
    void* holder = call(g_getChild, srcT, (void*)0);
    if (!holder) return;
    // 校验：名字 == "Holder"
    if (strcmp(name_of(holder), "Holder")) return;

    // 2) 克隆（父节点一起给）
    void* copy = call(g_instantiate, holder_go(holder), call(g_getTransform, ui));
    call(g_setName, copy, str("MyMPWindow"));

    void* copyT = call(g_getTransform, copy);

    // 3) 剥按钮：遍历，按名字挑
    strip_buttons(copyT);       // 只对 Back/Close/Download Mods/Open Mos Folder 调 Destroy(HKAZNXTEGNSSTQLF)

    // 4) 清列表
    void* scroller = find_by_path(copyT, "Menu/Safe Area/Scroll Window/Mod List (Scroller)");
    int n = call(g_getChildCount, scroller);
    for (int i = n - 1; i >= 0; i--) {
        void* ch = call(g_getChild, scroller, (void*)(intptr_t)i);
        if (!name_is(ch, "Element Prefab")) call(g_destroy, go_of(ch));
    }

    // 5) 加标题：克隆 Download Mods/Text
    void* tpl = find_by_path(copyT, "Menu/Safe Area/BottomPanel/Download Mods/Text");
    void* title = call(g_instantiate, go_of(tpl), find_by_path(copyT, "Menu/Safe Area"));
    set_rect(title, /*amin*/0,1, /*amax*/1,1, /*pivot*/0.5,1, /*ap*/0,-40, /*sd*/0,70);
    call(g_setText, comp_of(title, "NDJCKINKYJCMZNTI"), str("多人联机"));

    // 6) 激活 + 置顶
    call(g_setActive, copy, (void*)1);
    call(g_setSibling, copyT, (void*)(intptr_t)(idx_of_mods_menu + 1));
    call(g_forceRebuild, copyT);
}
```

---

## 7. 不能删 / 可以删（Mods 窗，逐节点）

| 节点 | 必须保留？ | 原因 |
|---|---|---|
| `- Mods Menu -`（根） | **不克隆它** | 静态单例 + 存档组件（§6.1） |
| `Holder` | ✅ 克隆起点 | 背景+内容的总容器；控制器 `menuHolder` 指向它 |
| `Back` | ✅ 留（剥按钮） | 全屏背景板 + 点空白关闭。**删了窗底透明、无遮罩** |
| `Menu` | ✅ 留 | 纯容器（无组件），但 `Safe Area` 的父必须存在 |
| `Safe Area` | ✅ 留 | `BHNCYPQKKSTXRROJ` 负责刘海/挖孔避让；删了在全面屏上顶到状态栏 |
| `Scroll Window` | ✅ 留 | `RectMask2D` = 裁剪；`UVQWEWODFFCNSEEP` = 命中；**它的负 sizeDelta 决定可视区** |
| `Mod List (Scroller)` | ✅ 留 | `VerticalLayoutGroup` + `ContentSizeFitter` + `NEUBHWOMTRBTWJHK`；删了不能滚、行不排列 |
| `Element Prefab` | ✅ **留作行模板** | 唯一现成的行视觉（图标/标题/开关/描述）。但**必须销毁其 `TPCHLVMKGZLNQLMK`**，否则它按 `ModData` 自己的逻辑刷新 |
| `BottomPanel` | ✅ 留 | `HorizontalLayoutGroup` 自动排布，加了按钮会自动挤 |
| `Close` | 可留可删 | 我们要自己的按钮；若留，`Text` 是现成标题模板（§6.3-A） |
| `Open Mos Folder` | 可删 | 默认 `act=False` |
| `Download Mods` | **留**（作按钮模板） | 组件集最全（Image+Button+DKEKLDIKOUYOJKFB+两个布局+WRNLOFEPQTZANWYN） |
| `Glow` | 可删 | 默认 `act=False`，只是 hover 发光 |
| `Button Icon` | 可删 | 默认 `act=False` |

**一条通用红线**：删除**任何**带 `ContentSizeFitter` / `VerticalLayoutGroup` / `NEUBHWOMTRBTWJHK` 的节点前，先确认它的父链布局还能算出来；这三类组件是"布局水龙头"，误删会导致整片区域塌成 0 尺寸（表现为"窗是空的但点得到"）。

---

## 8. 运行时查找方法

### 8.1 现状：窗根是**场景内建**，不是运行时实例化

硬证据：`- Mods Menu -` 在 `level0` 场景资源里就以 `activeSelf=true`、`parent=--- UI ---`、位于 `--- UI ---` 的 `m_Children` 第 4 位（0-based 3，紧接 `- World Creation -` 之后）存在，`m_Father` 链完整。运行时唯一变化的是 `Holder` 的 `activeSelf`。

> 补充硬数据：三个窗根的 `childCount` **都恰好是 1，且唯一孩子都叫 `Holder`**（实测：`- Mods Menu -` / `- Worlds Menu -` / `- World Creation -` → `['Holder']`，无例外）。这正是"克隆 `Holder` 而不是根"最直接的依据 —— 根的**全部可见内容**都在那唯一子节点下，根上只剩控制器与存档组件。
> （注：Transform 的 `m_SiblingIndex` 不在该资源的 typetree 里，上面的次序是按 `m_Children` 数组顺序得出；Unity 序列化时 `m_Children` 即同级顺序。）

**所以：level0 加载完即可 `Find`，不需要等实例化。** 但 `Find` 只在对象 active 时命中 —— 根是 active，满足。

> 例外：**列表行**是运行时由 `DrawUI()` 按 `elementPrefab` 实例化的；`Element Prefab` 本身在场景里是常驻模板。

### 8.2 四种查法（按稳健度递增）

```c
// ① 按绝对路径（最简单，名字含空格和短横线，必须原样）
GameObject.Find("/--- UI ---/- Mods Menu -")

// ② 按父链 + 子节点遍历（推荐，避开全局 Find 的开销和重名）
ui = GameObject.Find("/--- UI ---")
for (i=0; i<Transform.get_childCount(ui.t); i++)          // 11 个孩子
    if (strcmp(Transform.GetChild(ui.t,i).name, "- Mods Menu -") == 0) hit;

// ③ 按组件类型（最稳健，抗改名；也能覆盖"将来改成运行时实例化"的情况）
klass = il2cpp_class_from_name(image_AssemblyCSharp, "", "NANBAJRHGSVEQBTQ");
arr   = Object.FindObjectsOfType(klass);                   // 只返回 active
// 候选校验：go.name 含 "Mods" && childCount==1 && child.name=="Holder"
//          && GetComponent(menuHolder) 非空

// ④ 万一变成预制体/未实例化（本版本不会）：
Resources.FindObjectsOfTypeAll(klass)                      // 含 inactive
```

**活性校验（避免拿到半初始化实例）**：
```
go && go.activeInHierarchy
  && Transform.get_childCount(go.transform) == 1
  && strcmp(Transform.GetChild(go.transform,0).name, "Holder") == 0
```

**重试策略**：`0.25s × N` 轮询，别在 `il2cpp_thread_attach` 后立刻取。场景切换会**销毁并重建** `--- UI ---` 下的孩子，所以：

> **持有 GameObject 指针跨场景 = 悬空指针。** 每次显示前重新 `Find`，或用 `Object.op_Equality(ptr, NULL)` 判活后重建。PC/Android 上 `Object` 被销毁后托管指针非 null 但 `== null` 为 true —— 直接读 native 字段会崩。

### 8.3 若确实要等（未来版本/子场景）

- 检测场景切换：轮询 `SceneManager.GetActiveScene().name == "level0"`（或 `buildIndex`）后再 Find；
- 不要用 `StartCoroutine` 依赖主线程 MonoBehaviour —— 你在 native worker 线程里，所有 Unity API 必须在该线程 `il2cpp_thread_attach` 后调用，且 **Unity API 非线程安全**：要么在 hook 的 `Update` 里做（推荐，v37 已是这条路），要么自己 `il2cpp_runtime_thread_attach` + 单线程队列。

---

## 9. 风险清单

| # | 风险 | 触发条件 | 规避 |
|---|---|---|---|
| 1 | **克隆根 → 顶替 `static main`** | 克隆 `- Mods Menu -` | 克隆 `Holder`（§6.1） |
| 2 | **克隆按钮 → 触发原窗逻辑** | 未剥 `HKAZNXTEGNSSTQLF` | 只对"会点"节点 Destroy 组件（§6.4） |
| 3 | **销毁滚动面按钮 → 列表不能滚** | 一刀切销毁所有 Button | 白名单式剥离 |
| 4 | **世界坐标偏移** | `Instantiate` 不带 parent 再 `SetParent(t)` | 用 `Instantiate(obj, parent)` 重载，或 `SetParent(t, false)` |
| 5 | **SafeArea 重复计算** | 克隆体激活时 `BHNCYPQKKSTXRROJ.Awake` 重跑 | 正常行为，无需处理；但**别删**它 |
| 6 | **布局塌陷** | 删了带 LayoutGroup/ContentSizeFitter 的节点 | 见 §7 红线；改完调 `ForceRebuildLayoutImmediate` |
| 7 | **跨场景悬空指针** | 缓存了 clone/源指针 | 每次显示前重 Find（§8.2） |
| 8 | **`Element Prefab` 自己刷新** | 行模板的 `TPCHLVMKGZLNQLMK` 还在 | 克隆行后 Destroy 该组件 |
| 9 | **文本为空** | 以为克隆会带文字 | 场景里 `m_Text` 全空，必须 `set_Text` |
| 10 | **`--- UI ---` CanvasGroup 联动** | 别的菜单做淡入淡出 | 需要常显就挂独立二级 Canvas |
| 11 | **名字重名** | 多个克隆体都叫 `Holder` | 克隆后立刻 `set_name` 唯一名 |

**验证方法（不靠肉眼）**：注入后打印
```
[MP] src=%p holder=%p copy=%p kids=%d idx=%d active=%d
[MP] title=%p btn=%p text="%s"
```
再 `Transform.get_childCount` 与源比对；点一次克隆窗的关闭，确认**真实 Mods 窗没被关掉**（这是第 2 条风险的唯一可靠判据）。

---

## 10. 备选复用建议

| 需求 | 最佳源 | 理由 |
|---|---|---|
| 全屏面板 + 列表 + 底栏（登录/房间列表） | `- Mods Menu -/Holder` | 结构最通用：Back + Scroll + BottomPanel 三段式 |
| 卡片式列表行 | `- Worlds Menu -/Holder/Scroll Window/World Element Prefab` | 自带图标位 + 标题 + 状态 + 两排按钮 |
| 滚动列表容器 | `Worlds Menu/.../World List (Scroller)` | 带 `NEUBHWOMTRBTWJHK` 惯性滚动 + 位置指示器 |
| 文本输入（房间名/密码） | `- World Creation -/.../Name/Text Box/Input Field` | 唯一带安卓软键盘的输入控件 |
| 单选组（难度/模式风格） | `- World Creation -/.../Mode/Options` | `NCTIIVFOTRIIHRKA` 已封装互斥选中 |
| 开关 | `- World Creation -/.../Toggles/Quicksaves Toggle/Toggle Button` | `LCOMDMSGLXHKMBTQ` |

---

## 11. 附：本次用到的脚本

| 文件 | 作用 |
|---|---|
| `D:/SFS-Pro/work/dump_level0.py` | 扫描 level0，导出 `level0_tree_raw.json`（名字/active/父子） |
| `D:/SFS-Pro/work/tree_level0.py` | 按名字打印全树 + 组件 + RectTransform 数值 + 祖先链 |
| `D:/SFS-Pro/work/probe_chain.py` | Canvas 祖先链 / Canvas 参数 |
| `D:/SFS-Pro/work/probe_titles.py` | `--- UI ---` 子节点清单 / Title* 节点归属 |

> 备注：`level0_full.bin`（split0+split1 拼接）经验证与单读 `split0` 对象数一致（7088），**无需拼接**。
