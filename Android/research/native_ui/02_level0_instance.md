# 02 — 主菜单（level0）能否拿到 `NOJZIZCFSEXSUEEX`（StatsMenu 抽屉）实例

> 版本：SFS Pro 1.6.00.22 / arm64 IL2CPP / native 注入（`libsfsmp.so`）
> 材料：
> - `D:/sfs-tools/apk/dump-official/dump.cs`（597,338 行）、`script.json`（61.7 MB，ScriptMethod 全表）
> - `D:/sfs-tools/apk/x22/libil2cpp.so`（.22 官方；`work/` 下同名副本，RVA 与 dump 一致）
> - `D:/SFS-Pro/work/assets/`（650 个拆分资源；`level0.split0`=主菜单场景 SerializedFile，1,048,576 B；`level1`/`level2`/`level3` 为其余场景）
> - 工具：`work/anndis.py`（capstone AArch64，按 RVA 反汇编并解析 `bl` 目标名）、`work/hier.py`（UnityPy 层级导出，本次新增）
> - 只读分析；未改工程、未连设备。

---

## 0. 结论（TL;DR）

1. **主菜单拿不到 `NOJ` 实例，且不存在任何"代码创建"路径。** `NOJZIZCFSEXSUEEX` 类**没有静态字段、没有公有静态方法、没有工厂**（dump.cs:21261–21363；22 个方法条目里唯一的 `static` 是私有格式化工具 `KGEAMKGLEPA(int,string)` @0x18DD51C，返回 `string`，不是工厂），全二进制无 `AddComponent<NOJ>` / `GetComponent<NOJ>` 的泛型实例化，`level0` 场景里 **0 个** NOJ MonoBehaviour（`level1/2/3` 各 1 个）。
2. **`new GameObject + AddComponent<NOJ>` 不可行（不是"丑"，是"没有窗"）。** 它的 15 个视觉字段全部是 Inspector 序列化引用，实测 PPtr 全部指向**同场景（level2 'Part Menu'）自己的子节点**（§2.3 表）。`Awake` 对 null 是安全的（每个字段都有 `cbz` 跳过），所以 `AddComponent` 不会立刻崩，但产出的对象**零可见像素**；`Open` 走到"画行"分支时（`statsHolderGroup==NULL` 路径 `0x1AD8184 → 0x1AD829C`）会抛 NRE。
3. **替代方案：克隆 level0 已有的整窗 GameObject。** level0 的 UI 根 `--- UI ---`（go=221）下挂着 8 个可直接用的原生窗根（其中 `Notification Menus` 内含 3 个弹窗），按需求挑：
   - **弹窗（最省事）**：`- Update Popup -`（go=467）—— 根上只有 `PKVTCUBZSQHIVNVO`，**无 Awake、`OnOpen` 只 `menuHolder.SetActive(true)`**，可整根克隆后直接 `OnOpen(clone)`；克隆体内引用会被 `Instantiate` 自动重定向（§3.2D）
   - **登录页（带输入框）**：`- World Creation -`（go=680）的 `Holder`（go=214）—— 唯一现成的 TMP 输入框，自带安卓软键盘（§3.5）
   - **房间列表**：`- Worlds Menu -`（go=138）的 `Holder`（go=210）—— 带行 prefab `World Element Prefab`（每行 4 个按钮）
   - **通用窗**：`- Mods Menu -`（go=318）的 `Holder`（go=328）—— 标题 + 列表 + 底部按钮；**切勿克隆窗根本身**，其上 `DUBBFVQJWPMXTOTT.Awake`(0x173029C) 会把克隆体写成 ModLoader 静态单例（§3.4 P1）
   - 只要一行飘字：`EJIHSVFRLSOQRBJE.main.Log(string)`（0x16D64C4）—— level0 原生 toast，零克隆（§3.6）
   克隆用 `Object.Instantiate(Object, Transform, bool)` = **RVA 0x2FC1B58**（v32 已在设备上验证可用）；**不要**用 1 参版 `0x2FC19C4`（v28/v29 实测死在 libunity 内）。
4. **要拿"活的" NOJ，唯一时机是进入 `level1/level2/level3` 之后**（`level2` 的 'Part Menu'、`level1` 的 'Stats'）；主菜单阶段 `Resources.FindObjectsOfTypeAll(NOJ)` 恒为 0 —— 与 v43 实测一致，且现在有静态证据（场景里根本没有）。

| 判定项 | 结果 | 关键证据 |
|---|---|---|
| level0 有 NOJ 代码创建路径 | **否** | dump.cs:21261–21363（无静态字段、无工厂）；script.json 74 条命中全是自身/闭包；§1.3 |
| level0 有 NOJ 序列化实例 | **否**（0 个） | `work/assets/level0.split0` 全 1446 个 GameObject / 2756 个 MonoBehaviour 扫描，`NOJZIZCFSEXSUEEX` 命中 0 |
| 有 public/static 工厂 | **否** | 无静态字段、无公有静态方法；唯一 `static` 是 private 格式化函数 0x18DD51C（dump.cs:21321） |
| `AddComponent` 可造出可用实例 | **否** | 15 个字段 PPtr 全为同场景子对象（§2.3）；`Open` 依赖 `statsHolderGroup`(0x28) |
| level0 可克隆原生窗根 | **是**：`--- UI ---` 下 8 个直属窗根 + `Notification Menus` 内 3 个弹窗 | §3.1 表、§3.2 层级 |
| 克隆 API 已验证 | **是** | `Instantiate(Object,Transform,bool)` 0x2FC1B58（v32/v35 设备日志；`native_button_clone_study.md` §4 第 5 步） |

---

## 1. 判定 1：level0 是否存在创建 NOJ 的代码路径 —— **不存在**

### 1.1 类形态：无静态字段、无工厂、无 `Start`（唯一 `static` 是私有格式化函数）

`dump.cs:21261–21363`（TypeDefIndex: 579）全文如下（省略方法体，保留签名与 RVA）：

```csharp
public class NOJZIZCFSEXSUEEX : MonoBehaviour          // TypeDefIndex: 579  (dump.cs:21261)
{
    public RectTransform menuHolder;        // 0x20
    public VerticalLayoutGroup statsHolderGroup;  // 0x28
    public BBPSLYHPNWUJECLT statsHolderLayout;    // 0x30
    public NBISDYNOVJFXBOQT rootElement;          // 0x38
    public GameObject titleHolder;          // 0x40
    public NDJCKINKYJCMZNTI titleText;      // 0x48
    public WJDAKEPBTMPDKKWP popupAnimation; // 0x50
    public RectTransform spacePrefab;       // 0x58
    public RectTransform textPrefab;        // 0x60
    public RectTransform statPrefab;        // 0x68
    public RectTransform statBackPrefab;    // 0x70
    public RectTransform togglePrefab;      // 0x78
    public RectTransform sliderPrefab;      // 0x80
    public RectTransform buttonPrefab;      // 0x88
    public RectTransform fullButtonPrefab;  // 0x90
    private List<NAABCLLJEKA> CHNLEMLKDNN;  // 0x98
    private List<GameObject>  CILNINLCEGC;  // 0xA0
    private Func<bool> JEEIJNBOEDF;         // 0xA8
    private Action IJCJCEEINNP;             // 0xB0
    private bool CEKDEFIEIOJ;               // 0xB8
```

方法表（**全部是实例方法，无一 `static`**）：

| RVA | 签名 | 行号 |
|---|---|---|
| 0x18DC42C | `DrawStat_Separate(int, Func<string>×3, Action<Action>×2)` | 21291 |
| 0x18DC704 | `private void Awake()` | 21294 |
| 0x18DCA08 | `DrawToggle(...)` | 21297 |
| 0x18DCB94 | `DrawStat(int, Func<string>×2, ...)` | 21300 |
| 0x18DCD20 | `DrawSpace(int, float=5)` | 21303 |
| 0x18DCE60 / 0x18DC5C8 | `private void LNGAOLBDCPH(...)`（行插入内部实现） | 21306 / 21309 |
| 0x18DCFD8 | `DrawSlider_Separate(...)` | 21312 |
| 0x18DBD00 | `DrawStat(int, string, string)` | 21315 |
| 0x18DD1D0 | `DrawButton(int, string, string, Action, bool=true)` | 21318 |
| 0x18DD51C | `private static string KGEAMKGLEPA(int, string)` ← **唯一的 static，但只是格式化工具，不返回 NOJ** | 21321 |
| 0x18DD5D4 | `DrawTitle(string)` | 21324 |
| 0x18DD618 | `.ctor()` | 21327 |
| 0x18DD374 | `DrawButton(int, Func<string>×2, Action, Func<bool>, ...)` | 21330 |
| 0x18DD704 | `private bool OBCGMKLLJLB()` | 21333 |
| 0x18DD724 | **`FHBOAFMLNDC Open(Func<bool>, Action<NOJ>, bool, Action, Action)`** ← 唯一入口 | 21336 |
| 0x1AD8130 | `private T IKGDLCDHIOC<T>(T)`（`Instantiate(prefab, parent)` 包装，见 §2.4） | 21339–21344 |
| 0x18DDC08 | `DrawText(int, string)` | 21347 |
| 0x18DDCF8 | `DrawFullButton(int, string, Action, bool=true)` | 21350 |
| 0x18DBADC | `DrawSlider(...)` | 21353 |
| 0x18DDFD4 | `private void FGFGHJJDNJD(Action)` | 21356 |
| 0x18DC824 | `Close()` | 21359 |
| 0x18DDE48 | `DrawFullButton(int, Func<string>, Action, Func<bool>, ...)` | 21362 |

**⇒ 没有 `public static NOJ Create(...)`、没有 `GetOrCreate`、没有单例 `instance`。**（唯一的 `static` 是字符串格式化函数 `KGEAMKGLEPA`。）

### 1.2 全 dump 里谁"碰"这个类：4 类站点，全部在 level2 语境

| 行号 | 宿主类 | 形态 | 说明 |
|---|---|---|---|
| 17416 | `NCPYNZLPTJRKIBXS : CATGOLUVAEFENCMY` | `public NOJZIZCFSEXSUEEX statsMenu; // 0x70` | 部件统计窗的**序列化字段**（Inspector 拖入），不是 `new` 出来的 |
| 23433 | `Part.EEMKPEOBDIE` | `public NOJZIZCFSEXSUEEX drawer; // 0x20` | 闭包/drawer 缓存 |
| 36954 | `FCUEGTKOTIXHGPZS.OIHMNDFIBIC` | `public NOJZIZCFSEXSUEEX drawer; // 0x20` | 同上 |
| 51717 | **`EHHQWVCRJRNEXEFX : MonoBehaviour`（SFS.UI）** | `public NOJZIZCFSEXSUEEX drawer; // 0x20` | **同一个 GameObject 上的邻居组件**，`Open(...)` 0x181EA50 / `Open_DrawPart` 0x181E83C / `Close` 0x181ECDC 都只是**读** `drawer` 再转发 |
| 2544, 9162, 17391, 17407, 18106, 20857, 23520, 30272, 30875, 37028, 38012, 41435, 48637, 48919, 52891, 53370 … | `BoosterModule` / `ResourceModule` / `DetachModule` / `Part` / `SeparatorBase` 等 | 都是 `Draw(..., NOJZIZCFSEXSUEEX FPGKKNKKCCP, ...)` 的**参数** | 这些是"行渲染器"，被 NOJ 的 build 回调**调用**，不产生实例 |

**关键**：`EHHQWVCRJRNEXEFX` 与 `NOJ` 在 level2 里是**同一个 GameObject 上的两个组件**（§2.3 实证：`go=262 'Part Menu'` 上同时挂 `NOJZIZCFSEXSUEEX` 与 `EHHQWVCRJRNEXEFX`）。所以"经由 `drawer` 字段拿实例"这条路，前提依然是"level2 已加载且该对象存在"。

### 1.3 全二进制"泛型实例化"扫描：没有任何代码构造它

对 `script.json` 的 `ScriptMethod`（全部方法，含泛型实例）做 `NOJZIZCFSEXSUEEX` 子串匹配：**74 条命中**，逐条归类：

- 24 条 = 上表 NOJ 自身的方法；
- 50 条 = **嵌套闭包/委托类**：`NOJZIZCFSEXSUEEX.<>c$$*`、`.PFLGBDOGGCB$$*`、`.LBCAMPIELLF$$*`、`.HIICIPMBAJC$$*`、`.GLBCIGDPMIK$$*`、`.FGPOOKJDIJI$$*`、`.IGIMNMGCJEM$$*`、`.NEKKEGJMCPL$$*`、`.PFFMMAJPKJF$$*`、`.IECAKJAMAGO$$*`、`.OMFJJKFFGNI$$*`、`.NKCDMGAMIPI$$*`、`.BFLAFBODIDA$$*`、`.ICHHABDKLAH$$*`、`.NAABCLLJEKA$$.ctor`。

**没有一条**形如 `UnityEngine.GameObject$$AddComponent<NOJZIZCFSEXSUEEX>`、`UnityEngine.GameObject$$GetComponent<NOJZIZCFSEXSUEEX>`、`UnityEngine.Resources$$Load<NOJ...>`。IL2CPP 里泛型调用会被 AOT 展开成独立方法条目，**没有条目 = 代码里从未 `AddComponent`/`GetComponent`/`Load` 过这个类**。这是"无代码创建路径"的最硬证据。

### 1.4 资源扫描：650 个 asset 里，NOJ 只在 level1/2/3，level0 为 0

`work/scan_assets.py NOJZIZCFSEXSUEEX`（遍历 `work/assets/` 全部 650 个文件，按 MonoBehaviour 的 `m_Script.m_ClassName` 匹配）：

```
level1 {'NOJZIZCFSEXSUEEX': 1}
level2 {'NOJZIZCFSEXSUEEX': 1}
level3 {'NOJZIZCFSEXSUEEX': 1}
```

单独复核 `level0.split0`（1446 GameObject / 2756 MonoBehaviour，全部可读）：**NOJ 命中 0**。

三个实例分别挂在：

| 场景文件 | MonoBehaviour path_id | 所在 GameObject |
|---|---|---|
| `assets/level1` | 703 | `Stats`（go=77） |
| `assets/level2` | 1334 | **`Part Menu`（go=262）** |
| `assets/level3` | 2823 | `Part Menu`（go=323） |

### 1.5 没有静态注册表可"捞"

`grep "static.*NOJZIZCFSEXSUEEX" dump.cs` 只命中闭包单例（`<>9`、`<>9__16_x`），**没有任何 `static NOJZIZCFSEXSUEEX` 字段**。也就是说：即便历史上某个场景创建过实例，也不存在"全局寄存器"能让主菜单取回来（对比卖家的做法：他自己 inline-hook `Open` 建了 8 槽注册表，游戏本身没有）。

### 1.6 与 v43 实测对齐

- v43：`Resources.FindObjectsOfTypeAll(NOJ) == 0`（主菜单）；
- 本文：level0 场景里**序列化层就没有** NOJ，`level2` 才有 1 个。

两者完全一致，且现在能解释**为什么**：主菜单场景根本没有这个对象，`Find` 不是"没找到"，是"不存在"。

---

## 2. 判定 2：能否 `new GameObject + AddComponent<NOJ>` —— **不能（空壳，零像素，Draw* 立刻 NRE）**

### 2.1 `.ctor`（0x18DD618）：唯一自洽的部分

反汇编（`anndis.py 0x18DD618`）显示构造只做两件事：

```
0x18DD694  bl 0x1572A8C                       ; il2cpp_object_new(List<...>)
0x18DD6A0  bl 0x2309F8C                       ; List<T>$$.ctor
0x18DD6AC  str x20, [x0, #0x98]!              ; this.CHNLEMLKDNN = new List<NAABCLLJEKA>()
0x18DD6B8  bl 0x1572A8C
0x18DD6C4  bl 0x2309F8C
0x18DD6D0  str x20, [x0, #0xA0]!              ; this.CILNINLCEGC = new List<GameObject>()
0x18DD6E4  strb w8, [x19, #0xB8]              ; CEKDEFIEIOJ = false
```

⇒ 构造**不依赖任何外部状态**，`AddComponent` 能成功建出托管对象。**问题不在构造，在字段。**

### 2.2 `Awake`（0x18DC704）：对 null 安全 —— 所以"不崩"反而是陷阱

```
0x18DC708  ldrb w8, [x0, #0xB8]; cbz  → 若 CEKDEFIEIOJ 为真才调 Close()
0x18DC718  bl   NOJ$$Close                    ; 0x18DC824
然后对 0x58,0x60,0x68,0x70,0x78,0x80,0x88,0x90 八个 prefab 字段逐个：
0x18DC71C  ldr x0,[x19,#0x58] ; 0x18DC720 cbz x0, 0x18DC820   ; ← null 直接跳到函数尾
0x18DC728  bl  Component$$get_gameObject
0x18DC738  bl  GameObject$$SetActive(false)
```

**每个字段都有 `cbz` 守卫**，所以 `AddComponent` 后 `Awake` 不会崩；它只是"什么也没干"。这就是"看起来成功、实际什么都没有"的来源。

### 2.3 【决定性】15 个字段的序列化绑定实测（PPtr → 同场景子节点）

对 `assets/level2` 里 pid 1334（`NOJZIZCFSEXSUEEX`，`len=212`）的原始字节做 PPtr 解析（Unity 6：PPtr 12 B；实测映射 `raw = 0x54 + (field-0x40)*1.5`）：

| 字段 | dump 偏移 | 实测指向 | 对象名 / 类 |
|---|---|---|---|
| `menuHolder` | 0x20 | pid 795 (RectTransform) | **`Part Menu`**（窗框自身） |
| `statsHolderGroup` | 0x28 | pid 932 (VerticalLayoutGroup) | `Content` |
| `statsHolderLayout` | 0x30 | pid 1288 (`BBPSLYHPNWUJECLT`) | `Content` |
| `rootElement` | 0x38 | pid 1313 (`ASKDCNIZYXERWHDX`) | **`Part Menu`**（显示/隐藏总控） |
| `titleHolder` | 0x40 | pid 150 (GameObject) | `Title Holder` |
| `titleText` | 0x48 | pid 1385 (`NDJCKINKYJCMZNTI`) | `Title Text` |
| `popupAnimation` | 0x50 | pid 1333 (`WJDAKEPBTMPDKKWP`) | **`Part Menu`**（弹出动画） |
| `spacePrefab` | 0x58 | pid 793 (RectTransform) | `Space Prefab` |
| `textPrefab` | 0x60 | pid 786 (RectTransform) | `Text Prefab` |
| `statPrefab` | 0x68 | pid 796 (RectTransform) | `Stat Prefab` |
| `statBackPrefab` | 0x70 | pid 755 (RectTransform) | `Stats Back + Spacing` |
| `togglePrefab` | 0x78 | pid 817 (RectTransform) | `Toggle Prefab` |
| `sliderPrefab` | 0x80 | pid 605 (RectTransform) | `Slider Prefab` |
| `buttonPrefab` | 0x88 | pid 810 (RectTransform) | `Button Prefab` |
| `fullButtonPrefab` | 0x90 | pid 669 (RectTransform) | `Full Button Prefab` |

**15/15 全部是"同场景、同父节点下的子对象引用"。** 这个类**没有自己的 Canvas / RectTransform / 背景 / 字体**——它是一层"把已有子节点当行模板来复制"的控制器。`AddComponent` 得到的对象：

- 没有 `menuHolder` → 没有窗框尺寸；
- 没有 `titleHolder` → 没有标题栏；
- 没有 `rootElement` → 没有显示/隐藏总控（`Open` 末尾 `0x18DDB1C` 靠它）；
- 8 个 prefab 全 null → `DrawSpace/DrawText/DrawStat/DrawButton` 全部无模板可复制。

### 2.4 `Open`（0x18DD724）与 `Draw*` 在空壳上的真实行为

`Open` 调用序列（`anndis.py 0x18DD724 220 0x18DDC08`）：

```
0x18DD820  bl NOJ$$Close                       ; ① 先关（titleHolder null → 走 0x18DC874 cbz 跳过）
0x18DD840  blr x8                              ; ② build(this) ← Action<NOJ> 回调，用户代码在这里
0x18DD8A8  Comparison$$.ctor / 0x18DD8D8 List$$Sort(0x98)   ; ③ 行排序（空表 → 无操作）
0x18DD900… 遍历 0x98 的行：
    0x18DD930  ldr x1,[x19,#0x70]              ;   statBackPrefab
    0x18DD93C  bl NOJ$$IKGDLCDHIOC<object>     ;   Instantiate(prefab, statsHolderGroup.transform)
    0x18DD9DC  ldr x0,[x19,#0x68]              ;   statPrefab
    0x18DDA04  BBPSLYHPNWUJECLT$$get_Spacing   ;   statsHolderLayout(0x30)
    0x18DDA2C  ldr x1,[x19,#0x58]              ;   spacePrefab
0x18DDB1C  ldr x0,[x19,#0x38]; cbz            ; ④ rootElement null → 跳过
0x18DDB30  blr x21（若 onClose 非空）
0x18DDB50  System.Delegate$$Combine → this.0xB0
```

- **空壳 + 空列表**：`Open` 会静默跑完并返回一个 `FHBOAFMLNDC` 句柄 —— **不崩，但屏幕上什么都没有**（`Close` 只把 title 关掉，没人把窗框显示出来）。
- **一旦调用 `DrawTitle`**（0x18DD5D4，最短路径）**立刻抛 NRE**：

```
0x18DD5E0  ldr x0,[x0,#0x40]        ; titleHolder
0x18DD5E4  cbz x0, 0x18DD614
0x18DD614  bl  0x1572A9C            ; ← 空引用抛出桩（noreturn）
```

- **`DrawStat/DrawText/DrawSpace`（经 `LNGAOLBDCPH` → `IKGDLCDHIOC`）**：`0x1AD8184 ldr x0,[x19,#0x28]`（`statsHolderGroup`）→ `cbz x0, 0x1AD829C` → `bl 0x1572A9C`，同样 NRE。

> 复核：`0x1572A9C` 在 `DrawTitle`、`Close`、`Open`、`IKGDLCDHIOC` 的每个 null 分支上都被调用，且调用点之后没有正常返回路径 —— 它就是 IL2CPP 的 `NullReferenceException` 抛出桩。（`DrawTitle` 里 `cbz → 0x18DD614 → bl 0x1572A9C` 是最干净的样本。）

### 2.5 结论

| 尝试 | 结果 |
|---|---|
| `new GameObject("x") + AddComponent<NOJ>()` + 手填字段 | 能建对象；`Awake` 安全；`Open` 静默返回；**`DrawTitle`/`DrawStat` 立即 NRE**；即使不崩也**零可见像素** |
| 手工把 level2 的 15 个引用搬过去 | 等于在主菜单重建整个 `Part Menu` 层级（窗口框架 + 8 个行 prefab + 布局组 + 动画器）—— 那已经不是"用 NOJ"，而是"照着 level2 的 UI 重做一遍"，工作量 > 直接克隆 level0 现成窗（§3） |
| 从 `level1/level3` 抓一个实例跨场景用 | 对象随场景卸载而销毁（`level1 'Stats'` / `level3 'Part Menu'`），主菜单阶段二者都不在；且跨场景持有会得到 destroyed 引用（`Close()` 里 `titleHolder.SetActive` 直接打空 —— 卖家日志 `StatsMenu(...) not found yet, retrying` 就是这个） |

**⇒ 主菜单阶段：`NOJ` 路线彻底关闭。**

---

## 3. 替代方案：level0 可直接克隆的原生窗根（含完整层级与步骤）

### 3.1 UI 根与全部可克隆窗根

level0 场景的 UI 根是 **`--- UI ---`（GameObject path_id = 221）**，组件：`RectTransform, Canvas, CanvasScaler, CanvasGroup, KOQGGVJZTDRTHNB`。它的直属子节点就是"所有窗口"：

| path_id | 名称 | 挂在根上的组件（含逻辑） | 结构要点 | 克隆建议 |
|---|---|---|---|---|
| 474 | `- Main Menu -` | `BHNCYPQKKSTXRROJ`(安全区) | Title Area / **Buttons**(Play, Shop, Mod Loader, Dev Blog, Settings, Credits) / PC Banner / Links | 克隆 `Buttons` 作按钮列 |
| **318** | **`- Mods Menu -`** | `NANBAJRHGSVEQBTQ`(窗) + `DUBBFVQJWPMXTOTT`(设置单例) | `Holder(act=False) → Back + Menu → Safe Area → Scroll Window → Mod List (Scroller) → Element Prefab` + `BottomPanel`(Close / Open Mos Folder / Download Mods) | **★★ 首选：克隆 `Holder`（go=328）** —— 标题+列表+底部按钮，最像登录页 |
| **138** | **`- Worlds Menu -`** | `PKVTCUBZSQHIVNVO`(窗) + `QQUJIOVKJFUVHVBL`(世界列表逻辑) + `BHNCYPQKKSTXRROJ` | `Holder(act=False) → Scroll Window → World Element Prefab(整行: Select/Name/Stat/Buttons×2行)` + `World List (Scroller) → Create World Element` + `Main Menu` 返回键 | **★ 克隆 `Holder`（go=210）** —— 唯一带"行 prefab"的列表窗 |
| **680** | **`- World Creation -`** | `LKSRSZGMFKUEATEP`(窗) | `Holder(act=False) → Rows Frame → Rows → Name/Text Box/Input Field`、Solar System、Mode、Toggles | **★ 克隆 `Holder`（go=214）** —— 唯一带 TMP 输入框 |
| 532 | `Shop` | （空根） | 12 个直属子：`Shop Managers`(ACPVHVIVRIRSCPAX) + 9 个 `- Xxx Expansion -` 内购页 + `- Android Purchase Recover Menu -` + **`- Shop Menu -`(go=1144, `PKVTCUBZSQHIVNVO`)** | 避开（计费逻辑） |
| **1507** | **`Notification Menus`** | `XQIIIGLKZOOBKBIU` | 3 个弹窗：`- Video Popup -`(583) / `- Peak Popup -`(787) / **`- Update Popup -`(467)** —— 三者根上都只有 `PKVTCUBZSQHIVNVO` | **★★ 最简窗根：`- Update Popup -/Holder + Back`（见 §3.2D）** |
| 470 | `Development Menu` | `PKVTCUBZSQHIVNVO` | 单子节点 `Holder + Safe Area`(go=707, act=False) | 调试用 |
| **195** | **`MsgDrawer`** | `EJIHSVFRLSOQRBJE`（**静态 `main`**） | 单子节点 `Text`(go=143, act=False, `NDJCKINKYJCMZNTI`) + Animator | **零克隆的即时提示通道，见 §3.6** |

> 注：上表前四个窗根（`- Main Menu -` / `- Mods Menu -` / `- Worlds Menu -` / `- World Creation -`）都是 **`--- UI ---` 的直属子节点**，锚点全为 `(0,0)-(1,1)` 拉伸，克隆后无需改锚点；`Notification Menus` 的 3 个弹窗是它的孙子节点，同样是全屏拉伸。

### 3.2 四个候选窗的完整层级（实测导出，`work/hier.py`）

**A. `- Mods Menu -`（go=318）** —— 组件 `{NANBAJRHGSVEQBTQ, DUBBFVQJWPMXTOTT}`，`sizeDelta=(0,0)`，anchors 全屏拉伸

```
- Mods Menu -          [act=True]  go=318
  └ Holder             [act=False] go=328   {CanvasRenderer}          ← ★ 克隆点
    ├ Back             [act=True]  go=970   {CanvasRenderer, IATNFWYCVWCHMYGA, HKAZNXTEGNSSTQLF}  (全屏压暗+点击关闭)
    └ Menu             [act=True]  go=912
      └ Safe Area      [act=True]  go=786   {BHNCYPQKKSTXRROJ}       (刘海/安全区适配)
        ├ Scroll Window[act=True]  go=487   {RectMask2D, LayoutElement, HKAZNXTEGNSSTQLF, UVQWEWODFFCNSEEP} sizeDelta=(-50,-240)
        │ └ Mod List (Scroller)   go=980   {VerticalLayoutGroup, ContentSizeFitter, ...} sizeDelta=(0,295)
        │   └ Element Prefab      go=916   {CanvasRenderer, HKAZNXTEGNSSTQLF, TPCHLVMKGZLNQLMK, HorizontalLayoutGroup, CanvasGroup} sd=(793,295)
        │     ├ Background        go=1487  {CanvasRenderer, LayoutElement, Image}
        │     ├ Icon Holder       go=1101  → Mask → Image
        │     └ Information Holder go=965  {VerticalLayoutGroup}
        │       ├ Title → Name    go=485   {NDJCKINKYJCMZNTI, LayoutElement, Text}
        │       ├ Active Toggle   go=472   {Image, LCOMDMSGLXHKMBTQ, MoveModule}
        │       ├ Info            go=1065  {NDJCKINKYJCMZNTI, Text}
        │       └ Description     go=333   {NDJCKINKYJCMZNTI, Text}
        └ BottomPanel [act=True]  go=832   {CanvasRenderer, HorizontalLayoutGroup, IKUBYZNZZCPWONJL} sd=(-30,65)
          ├ Close              go=1551  {Image, HKAZNXTEGNSSTQLF, DKEKLDIKOUYOJKFB, ...} → Glow/Button Icon/Text
          ├ Open Mos Folder    go=928   (act=False，同构)
          └ Download Mods      go=982   (同构)
```

**B. `- Worlds Menu -`（go=138）** —— 组件 `{PKVTCUBZSQHIVNVO, QQUJIOVKJFUVHVBL, BHNCYPQKKSTXRROJ}`

```
- Worlds Menu -        [act=True]  go=138
  └ Holder             [act=False] go=210   {BHNCYPQKKSTXRROJ}       ← ★ 克隆点
    ├ Scroll Window    go=171   {RectMask2D} sd=(-60,-150)
    │ └ World Element Prefab  go=216  {AXVQHBBALEUNLEXX, HKAZNXTEGNSSTQLF, KOGYGAZAWHKCWPMD, IATNFWYCVWCHMYGA, MUCHRJBWORJAKZYY, VerticalLayoutGroup} sd=(800,450)
    │   ├ Select go=132 / Name go=209 {NDJCKINKYJCMZNTI} / Stat go=215 {NDJCKINKYJCMZNTI}
    │   └ Buttons → Row1(Play, Resume) + Row2(Delete World, Rename)   ← 每行 4 个按钮，全部 HKAZNXTEGNSSTQLF
    ├ World List (Scroller)    go=178  {VerticalLayoutGroup, ContentSizeFitter, NEUBHWOMTRBTWJHK, AEAKLUUKPGFHYGXZ, CREDXDCPPFNXOHBV} sd=(0,80)
    │ ├ Create World Element   go=161  {IATNFWYCVWCHMYGA, HKAZNXTEGNSSTQLF, LayoutElement} sd=(818,80)
    │ └ Position Indicator     go=205  (act=False)
    └ Main Menu        go=141   {IATNFWYCVWCHMYGA, HKAZNXTEGNSSTQLF, WRNLOFEPQTZANWYN, ASKDCNIZYXERWHDX, WXZJEOIKEVJAJKQG} sd=(233,60)
```

**C. `- World Creation -`（go=680）** —— 组件 `{CanvasRenderer, LKSRSZGMFKUEATEP}`

```
- World Creation -     [act=True]  go=680
  └ Holder             [act=False] go=214   {BHNCYPQKKSTXRROJ}       ← ★ 克隆点（唯一带输入框）
    └ Rows Frame       go=682
      └ Rows           go=689  {VerticalLayoutGroup, ContentSizeFitter} anchors (0.10,0.5)-(0.90,0.5)
        ├ Back         go=149  (act=False)
        ├ Name         go=775
        │ ├ Label      go=774 → Text go=781
        │ └ Text Box   go=397  {IATNFWYCVWCHMYGA, ASKDCNIZYXERWHDX, HKAZNXTEGNSSTQLF}
        │   └ Input Field go=760 {CanvasRenderer, Text, Shadow, ContentSizeFitter, RQCKLIBDDOHDGUDN, LINRJGANWEEFRCUR, TUXXAURTZSIFPXTZ}  ← 真·输入框
        ├ Solar System go=780 → Button go=208 {HKAZNXTEGNSSTQLF}
        ├ Mode         go=773 → Options {NCTIIVFOTRIIHRKA} → Mode Classic(190)/Mode Challenge(177) {HKAZNXTEGNSSTQLF}
        └ Toggles      go=169 (act=False) → Cheats Toggle go=627 → Toggle Button go=988 {MoveModule}
```

**D. `- Update Popup -`（go=467，`Notification Menus` 下）** —— 组件只有 **`PKVTCUBZSQHIVNVO`**（无 CanvasRenderer、无 Awake）

```
- Update Popup -      [act=True]  go=467   {PKVTCUBZSQHIVNVO}   menuHolder=0x28
  └ Holder + Back    [act=False] go=531   {CanvasRenderer, IATNFWYCVWCHMYGA, HKAZNXTEGNSSTQLF}   ← 全屏压暗 + 点击关闭
    └ Safe Area      [act=True]  go=540   {BHNCYPQKKSTXRROJ}
      ├ Patch Notes Text  [act=True] go=701  sd=(0,1378) anchors(-0.30,0.5)-(1.30,0.5) {CanvasRenderer,Text,Shadow,ContentSizeFitter,UVZBQMVFQMBFTRPL}
      │ └ Patch Notes Title [act=True] go=1009  (标题行，同样带 Text)
      └ Buttons           [act=True] go=657  {HorizontalLayoutGroup,ContentSizeFitter}
        ├ Cancel Button   go=866  {Image, HKAZNXTEGNSSTQLF, WRNLOFEPQTZANWYN, ASKDCNIZYXERWHDX, DKEKLDIKOUYOJKFB} → Text(NDJCKINKYJCMZNTI)
        ├ Update Button   go=755  （同构）
        └ Play Button     go=466  （同构）
```

**这是整份报告里最干净的克隆根**，原因有三（均有反汇编证据）：

1. `PKVTCUBZSQHIVNVO` **没有 `Awake`**（dump.cs 类体只有方法，无 Awake），克隆它不会像 `DUBBFVQJWPMXTOTT` 那样自注册静态单例；
2. `OnOpen`（**0x174FDB8**）只有三条指令 —— `ldr x0,[x0,#0x28]; cbz; SetActive(true)`，**纯本地操作，不碰任何静态表/UI 栈**：
   ```
   0x174FDB8  ldr  x0, [x0, #0x28]     ; menuHolder
   0x174FDBC  cbz  x0, #0x174fdcc
   0x174FDC8  b    #0x2FBCAF4          ; GameObject.SetActive(true)
   ```
3. Unity 的 `Instantiate` 会**自动重定向克隆体内的引用**：克隆根后，克隆体的 `menuHolder(0x28)` 已指向克隆出来的 `Holder + Back`。所以直接 `OnOpen(clone)` 就能显示，且完全不用手写 `SetActive`。

⇒ **弹窗类需求（登录失败/房间已满/公告）优先用这条**：`Instantiate` 整个 `- Update Popup -` → `set_Text` 改 `Patch Notes Text` / `Patch Notes Title` → 隐藏按钮（`SetActive(false)` 掉用不上的）→ `OnOpen(clone)`。
注意 `Close`（**0x174FCAC**）里有静态类引用（`adrp x21,#0x343b000; ldr x21,[x21,#0xcd8]`），会走全局 UI 管理器 —— **隐藏克隆体请用 `SetActive(false)`，不要调 `Close()`**。

### 3.3 克隆步骤（.22 的 RVA，全部已核对 dump.cs）

| # | 动作 | API | .22 RVA | dump.cs 行 |
|---|---|---|---|---|
| 0 | 切主线程 | 已在 v27/v41 的 `WorldTime.Update` 钩子里做 | — | — |
| 1 | 取 UI 根 | `Resources.FindObjectsOfTypeAll(Type)`（Type=`RectTransform` 或 `GameObject`），按 `get_name` 过滤 | **0x2FB55D0** | 432180 |
| 1b | 取子节点 | `Transform.Find(string)`（可用 `"Holder"` / `"Holder/Menu"` 路径形式） | **0x2FC915C** | 435527 |
| 2 | 克隆 | `Object.Instantiate(Object original, Transform parent, bool worldPositionStays=false)` | **0x2FC1B58** | 434239 |
| 3 | 挂父/置顶 | `Transform.SetParent(Transform,bool)` / `SetAsLastSibling()` | 0x2FC8168 / 0x2FC8CFC | 435473 / 435515 |
| 4 | 显示 | `GameObject.SetActive(true)`（**Holder 原状态是 False**） | **0x2FCBAF4** | 433371 |
| 5 | 取组件 | `Component.GetComponent(Type)` / `Component.get_gameObject` / `get_transform` | 0x2FB9F10 / 0x2FB9E48 / 0x2FB9D80 | 432774 / 432771 / 432768 |
| 6 | 改名/改字 | `Object.set_name(string)`；`NDJCKINKYJCMZNTI.set_Text(string)` | 0x2FC1280；**0x176EF50** | 434227；38925 |
| 7 | 清点击 | `ClickUnityEvent..ctor()` → 写回 `HKAZNXTEGNSSTQLF.clickEvent(0x48)` / `holdEvent(0x40)` | **0x183D8A4** | 54089 |
| 8 | 销毁 | `Object.Destroy(Object)` | **0x2FC1E88** | 434285 |

可直接照抄的流程（与 v32 已验证的按钮克隆同构，`native_button_clone_study.md` §4 第 5 步）：

```c
/* 伪代码，参数全部为 IL2CPP 对象指针；ri = il2cpp_runtime_invoke */
src = find_by_name(RectTransform, "- Mods Menu -");        // Resources.FindObjectsOfTypeAll 0x2FB55D0
holder = ri(Transform_Find /*0x2FC915C*/, src_tr, {"Holder"});
parent = ri(Component_get_transform /*0x2FB9D80*/, uiCanvas /* go=221 */);
void* ip[3] = { holder_go, parent, &false_val };
clone = ri(Instantiate3 /*0x2FC1B58*/, NULL, ip);          // 3 参版：v32 已在设备上跑通
if (!clone) return;
ctr = ri(GameObject_get_transform /*0x2FBC8C4*/, clone);
ri(SetAsLastSibling /*0x2FC8CFC*/, ctr);
ri(SetActive /*0x2FCBAF4*/, clone, {&one});               // Holder 原本 act=False
/* 改标题 */
t = getcomp(clone, type_NDJCKINKYJCMZNTI_under("Title Text"));
ri(set_Text /*0x176EF50*/, t, {str_new_utf8_to_il2cpp("联机大厅")});
/* 弹窗变体：clone = Instantiate("- Update Popup -" 整根, parent,false);
   其 menuHolder 已重定向到克隆的 "Holder + Back"，改 "Patch Notes Text"/"Patch Notes Title" 后
   ri(OnOpen /*0x174FDB8*/, clone_ctrl, {}) 即可显示；隐藏用 SetActive(false)，别调 Close。 */
```

### 3.4 五个必须知道的坑（每条都有证据）

**P1 — 不要克隆窗根（`- Mods Menu -` 本身），要克隆它的 `Holder`。**
窗根上挂着 `DUBBFVQJWPMXTOTT`（ModLoader 设置单例），其 `Awake`（**0x173029C**）里：

```
0x17302E4  ldr x8, [x8, #0xB8]     ; klass->static_fields
0x17302E8  str x19, [x8]           ; NMNOHOLDBGO = this   ← 克隆体把自己写成了单例
```

克隆窗根 ⇒ 克隆体的 `Awake` 把静态单例劫持到克隆体上，之后 ModLoader 的存档/设置读写全落到克隆体。`NANBAJRHGSVEQBTQ` 另有 `public static main`（dump.cs:25120）与 `OnOpen/CloseWindow` 的 UI 栈逻辑，同样不该被复制。
**只克隆 `Holder`（纯视觉层，只带 `CanvasRenderer` / 动画 / 安全区）即可完全避开。**

> **唯一例外**：`PKVTCUBZSQHIVNVO` 系窗口（`- Update Popup -` / `- Peak Popup -` / `- Video Popup -` / `Development Menu`）**没有 Awake**，`OnOpen` 只做 `menuHolder.SetActive(true)`（§3.2D），可以整根克隆，且克隆体内 `menuHolder(0x28)` 会被 `Instantiate` 自动重定向到克隆子树。

**P2 — 三个 `Holder` 全是 `act=False`。** 它们是"窗关着"的状态；`Instantiate` 会连 inactive 一起复制，克隆后必须 `SetActive(true)`，否则"克隆成功但看不见"（与 §2 的空壳症状相同，容易误判）。

**P3 — `GameObject.Find` 在本包里被裁剪。** `script.json` 的 `ScriptMethod` 里**没有** `UnityEngine.GameObject$$Find`（只有 `Transform$$Find`、`Resources$$FindObjectsOfTypeAll`）。想按名字找窗只能：`FindObjectsOfTypeAll(type)` 遍历 + `get_name` 比对，或先用 `FindObjectsOfTypeAll(Canvas)` 拿 `--- UI ---` 再 `Transform.Find`。

**P4 — 只用 3 参 `Instantiate`。** 1 参版 `0x2FC19C4` 在 v28/v29 实测"死在 libunity 内部（某次内部查找返回 NULL）"（`v41.c` 头注释原文）；3 参版 `0x2FC1B58` 在 v32/v35 上真机跑通。

**P5 — 克隆体不在游戏的 UI 栈里。** 游戏用 `NQEUSBNLKGCFHQVR`（dump.cs:8822）的 `Open/Close/CloseStack` 维护窗口栈与返回键。克隆体绕过它，后果：① Android 返回键不会关它；② 之后打开的任何原生窗会盖在它上面（除非每次原生窗关闭后重新 `SetAsLastSibling`）。对策：自己接管返回键 + 在 `Update` 里周期性 `SetAsLastSibling`（或在原生窗 `OnOpen` 时 `SetActive(false)`）。

### 3.5 输入框（登录必需）：level0 唯一现成的 TMP 输入框

`- World Creation -/Holder/Rows/Name/Text Box/Input Field`（**go=760**）：

- 组件：`TUXXAURTZSIFPXTZ`（dump.cs:20505，TypeDefIndex 551）+ `LINRJGANWEEFRCUR`（移动端文本框）+ `UnityEngine.UI.Text`。
- `TUXXAURTZSIFPXTZ` 字段：`mobileTextBox 0x20`、`TMProTextBox 0x28`、`KMMKEFMINBH 0x30`；属性 `PEDMKFBBODP { get; set; }`（= `get_Text/set_Text`，即游戏统一文本接口）。
- **它自带安卓软键盘**（`LINRJGANWEEFRCUR` 就是移动端输入弹窗），这是自己 new `TMP_InputField` 拿不到的东西。
- 用法：克隆 `- World Creation -/Holder`（或只克隆 `Text Box` 节点），`SetActive(true)`，`set_Text("")`，读值用 `get_Text()`。**不要**用 `UnityEngine.UI.Text.text` 直接赋值（中文缺字形，`NDJCKINKYJCMZNTI` 会择优写 `TextMeshProUGUI`，见 dump.cs:38908 的 0x20/0x28 双字段）。

### 3.6 零克隆的即时反馈：`MsgDrawer`（level0 原生 toast）

`MsgDrawer`（go=195，在 `--- UI ---` 下）挂着 `EJIHSVFRLSOQRBJE`（dump.cs:27598）：

```csharp
public static EJIHSVFRLSOQRBJE main;   // 0x0
public NDJCKINKYJCMZNTI text;          // 0x20
public void Log(string GAENKPPFLDA);              // RVA 0x16D64C4
public void Log(string GAENKPPFLDA, bool GMHBCKPGKBP); // RVA 0x16D64CC
private void Awake();                              // RVA 0x16D646C  ← main = this（0x16D64A8 ldr x8,[klass+0xB8]）
```

**主菜单阶段 `EJIHSVFRLSOQRBJE.main` 非空**（场景里就有，`Awake` 自注册静态单例）。所以任何"连接中/房间已满/登录失败"提示，一行就够：

```c
void* msg = EJIHSVFRLSOQRBJE__main;                 // il2cpp_class_get_static_field_data + 0x0
ri(Log /*0x16D64C4*/, msg, { utf8_to_il2cpp_string("正在连接服务器…") });
```

这是**level0 里唯一一个"和 StatsMenu 同性质、但真的存在"的静态入口**；缺点是只能显示一行飘字，不能承载列表/输入。

### 3.7 选型建议（主菜单阶段）

| 需求 | 方案 | 成本 |
|---|---|---|
| 只要一句提示 | `EJIHSVFRLSOQRBJE.main.Log(...)`（§3.6） | 1 次调用，0 克隆 |
| 弹窗（公告 / 登录失败 / 确认框） | **克隆整根 `- Update Popup -`（go=467）+ `OnOpen(clone)`**（§3.2D） | 1 次 Instantiate + 改字 + 隐藏多余按钮 |
| 登录页（标题 + 输入框 + 2~3 个按钮） | 克隆 `- World Creation -/Holder`（无列表，自带输入框） | 1 次 Instantiate + 改字 + 清点击 |
| 联机大厅（房间列表 + 每行按钮） | 克隆 `- Worlds Menu -/Holder`（行 prefab = `World Element Prefab`，每行 4 按钮） | 1 次 Instantiate + 克隆行 + 改字 |
| 通用窗（标题 + 说明 + 底部按钮） | 克隆 `- Mods Menu -/Holder`（`Menu/Safe Area/Scroll Window` + `BottomPanel`） | 1 次 Instantiate + 改字 |
| 进游戏后的房间面板 | 那时已有活 `NOJ`，切 R-B（卖家同款 `Open+Draw*`） | 见 agent_E §4 |

---

## 4. 证据索引（行号 / RVA / 资源路径）

<details>
<summary>展开：全部关键引用</summary>

| 内容 | 位置 |
|---|---|
| ⚠️ 行号约定：表中行号为**方法签名行**，`// RVA:` 注释在**上一行** | 例：`Instantiate` 0x2FC1B58 的 RVA 注释在 434238、签名在 434239 |
| `NOJZIZCFSEXSUEEX` 类定义（15 字段） | `dump.cs:21261–21283` |
| `.ctor` RVA 0x18DD618 | `dump.cs:21327` |
| `Awake` RVA 0x18DC704（null 安全） | `dump.cs:21294` |
| `Close` RVA 0x18DC824 | `dump.cs:21359` |
| **`Open(Func<bool>,Action<NOJ>,bool,Action,Action)` RVA 0x18DD724** | `dump.cs:21336` |
| `DrawTitle` RVA 0x18DD5D4（titleHolder null → 0x18DD614 `bl 0x1572A9C`） | `dump.cs:21324` |
| `IKGDLCDHIOC<T>` RVA 0x1AD8130（Instantiate+parent，读 0x28） | `dump.cs:21339–21344` |
| `EHHQWVCRJRNEXEFX.drawer 0x20` / `Open` 0x181EA50 | `dump.cs:51714–51731` |
| `NCPYNZLPTJRKIBXS.statsMenu 0x70` | `dump.cs:17411–17416` |
| NOJ 在场：`level1` pid 703 (go=77 `Stats`)、`level2` pid 1334 (go=262 `Part Menu`)、`level3` pid 2823 (go=323) | `work/assets/level1,level2,level3` |
| level0 无 NOJ（1446 GO / 2756 MB 全扫） | `work/assets/level0.split0` |
| NOJ 15 字段 PPtr 绑定（实测表） | §2.3，脚本见 `work/hier.py` 同目录一次性 PPtr 解析 |
| `--- UI ---` go=221 与 8 个窗根 | §3.1；`work/assets/level0.split0` |
| `- Mods Menu -` 全层级 | §3.2A；`work/hier.py assets/level0.split0 "- Mods Menu -"` |
| `- Worlds Menu -` 全层级 | §3.2B；`work/level0_worlds.txt` |
| `- World Creation -` 全层级 | §3.2C；`work/hier.py assets/level0.split0 "- World Creation -"` |
| `DUBBFVQJWPMXTOTT.Awake` RVA 0x173029C（`str x19,[klass+0xB8]` 自注册单例） | `dump.cs:33709` |
| `NANBAJRHGSVEQBTQ.main` / `OnOpen` / `CloseWindow` | `dump.cs:25117–25145`（`main` 在 25120） |
| `PKVTCUBZSQHIVNVO`（menuHolder 0x28 / Close 0x174FCAC / OnOpen 0x174FDB8 —— **无 Awake，OnOpen 只 SetActive**） | `dump.cs:35688–35720`；Close 35706 / OnOpen 35709 |
| `- Update Popup -` go=467 + `Holder + Back` go=531 + `Patch Notes Text` go=701 + 3 按钮(866/755/466) | `work/assets/level0.split0`；`hier.py "- Update Popup -"` |
| `- Peak Popup -` go=787 / `- Video Popup -` go=583（同 `PKVTCUBZSQHIVNVO`） | 同上 |
| `Shop` go=532 的 12 个直属子（含 `- Shop Menu -` go=1144） | 同上 |
| `Development Menu/Holder + Safe Area` go=707 | 同上 |
| `MsgDrawer` go=195 → `Text` go=143（act=False） | 同上 |
| `NQEUSBNLKGCFHQVR`（UI 栈基类：Open 0x1859FA0 / Close 0x1859DD8 / CloseStack 0x1859EF0） | `dump.cs:8822–8852` |
| `EJIHSVFRLSOQRBJE`（MsgDrawer，static main，Log 0x16D64C4） | `dump.cs:27598–27624` |
| `TUXXAURTZSIFPXTZ`（输入框，20505） | `dump.cs:20505–20525` |
| `NDJCKINKYJCMZNTI`（Text/TMP 双字段，set_Text 0x176EF50） | `dump.cs:38908–38925` |
| `HKAZNXTEGNSSTQLF`（按钮：clickEvent 0x48 / holdEvent 0x40 / onDown 0x50） | `dump.cs:37463–37490` |
| `ClickUnityEvent..ctor` RVA 0x183D8A4 | `dump.cs:54094` |
| `Object.Instantiate(Object,Transform,bool)` 0x2FC1B58 / 1 参 0x2FC19C4 | `dump.cs:434239` / `434236` |
| `Resources.FindObjectsOfTypeAll(Type)` 0x2FB55D0 | `dump.cs:432180` |
| `Object.FindObjectsOfType(Type)` 0x2FC202C | `dump.cs:434295` |
| `Transform.Find(string)` 0x2FC915C / `SetParent` 0x2FC8168 / `SetAsLastSibling` 0x2FC8CFC | `dump.cs:435527` / `435473` / `435515` |
| `GameObject.SetActive` 0x2FCBAF4 / `AddComponent(Type)` 0x2FBC8C0 | `dump.cs:433371` / `433351` |
| `Object.Destroy` 0x2FC1E88 | `dump.cs:434285` |
| `GameObject.Find` **不存在**（被裁剪，`script.json` 无该条） | `script.json` ScriptMethod 全表 |
| 克隆按钮的既有实证 | `build/native_button_clone_study.md` §4、`D:/sfs-tools/so/src/v32.c`（`g_mInst3`） |

</details>

---

## 5. 一句话交付

**主菜单拿不到 `NOJ`，也不该去造它**：该类是"零自有 UI、15 个字段全是同场景子节点引用"的控制器（level0 场景里根本没有它）。

主菜单阶段要原生窗，按需求挑 level0 现成的根：

1. **弹窗** → 整根克隆 `- Update Popup -`（go=467，`PKVTCUBZSQHIVNVO`，无 Awake，`OnOpen` 只 SetActive）——最省事；
2. **登录页**（带输入框）→ 克隆 `- World Creation -/Holder`（go=214）；
3. **房间列表** → 克隆 `- Worlds Menu -/Holder`（go=210，行模板 `World Element Prefab`）；
4. **通用窗** → 克隆 `- Mods Menu -/Holder`（go=328），**不要**克隆窗根本身（会劫持 ModLoader 单例）。

统一用 `Instantiate(Object,Transform,bool)` **0x2FC1B58**（3 参，1 参版会崩）；一句提示则 `EJIHSVFRLSOQRBJE.main.Log()` **0x16D64C4**。等进了 level1/level2/level3 再切回 `NOJ` 的 `Open+Draw*`。

---

## 6. 复现命令（全部只读）

```bash
cd D:/SFS-Pro/work

# 1) 场景里有没有 NOJ（应为 level1/2/3 各 1，level0 无）
python scan_assets.py NOJZIZCFSEXSUEEX

# 2) 导窗层级（go id / active / sizeDelta / 组件类名）
python hier.py assets/level0.split0 "- Mods Menu -" "- Worlds Menu -" "- World Creation -"
python hier.py assets/level2 "Part Menu"          # NOJ 的真身所在

# 3) 反汇编 NOJ 关键方法（Awake/Close/Open/DrawTitle/ctor）
python anndis.py 0x18DC704 60 0x18DCA08
python anndis.py 0x18DD724 220 0x18DDC08
python anndis.py 0x18DD5D4 18 0x18DD618

# 4) 确认 GameObject.Find 被裁剪（无输出 = 不存在）
python - <<'PY'
import json; d=json.load(open('D:/sfs-tools/apk/dump-official/script.json',encoding='utf-8'))
print([m['Name'] for m in d['ScriptMethod'] if m['Name'].startswith('UnityEngine.GameObject$$Find')])
PY
```
