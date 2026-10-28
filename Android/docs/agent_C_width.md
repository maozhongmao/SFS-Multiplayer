# SFS 1.6.00.22 安卓 · 主菜单按钮宽度同步机制 逆向与最小修复方案

目标：解释 `CUJABYOCLJHRSGWT` / `NBISDYNOVJFXBOQT` / `GJALLEMCFDN` 如何改写子按钮宽度，
并给出让 **运行时克隆的 Play Button 宽度与原按钮一致** 的具体实现（类名 / RVA / 字段偏移 / 调用序列 / 风险）。

分析对象
- dump：`D:/sfs-tools/apk/dump-official/dump.cs`（597338 行）
- 反汇编：`libil2cpp.so` 从 `D:/sfs-tools/apk/livesigned/live-aligned-debugSigned.apk` 解出（arm64-v8a，56.9 MB），capstone 5.0.7
- RVA↔文件偏移：本 so 的 `Offset = RVA - 0x4000`（第一段 `p_vaddr=0 / p_offset=0`，text 段 `vaddr=0x13BD4D0 / off=0x13B94D0`）。下文同时给出 **RVA**（dump.cs 里的写法，也是 `base+RVA` 的写法）和 **fileOff**。

---

## 0. 一句话结论

按钮宽度**不是** `sizeDelta` 也不是 anchors 在起作用，而是：

```
容器 CUJABYOCLJHRSGWT 收到 SetSize(v)
  → JIEPCEKHFLO(v) 里 JKDALCMPIDP → RectTransform.SetSizeWithCurrentAnchors(Horizontal/Vertical, …)
  → 然后 foreach (children) child.SetSize( FDPNCKOAKEP(v, -1) )
```

`children`（CUJABYOCLJHRSGWT+0x60）**只包含 Awake 时场景序列化的那 6 个按钮的 `IKUBYZNZZCPWONJL`**。
`Instantiate` 出来的克隆体不在这个列表里 → 容器推宽度时**永远跳过它** → 克隆体停在自己序列化的 280×75，
而兄弟按钮被推到运行时宽度（约 359 ≈ 280/0.78，与"视觉 0.78 倍"完全吻合）。

修复 = **把克隆体的 `IKUBYZNZZCPWONJL` 追加进容器的 `children` 列表（并补 `SetParent`），
然后在克隆体 Start 之后的帧里主动推一次 `SetSize(FDPNCKOAKEP(容器当前 rect, -1))`**。
两项都不需要 hook、不需要 MethodInfo 猜偏移，全部可用 il2cpp 公开 API 从对象上取。

---

## 1. 三个类的真实结构（反汇编确认）

### 1.1 `NBISDYNOVJFXBOQT`（dump.cs:8324，TypeDefIndex 273，abstract MonoBehaviour）

| 字段 | 偏移 | 含义（由代码行为反推） |
|---|---|---|
| `GHICNHEKHBA` | 0x20 | 缓存组件 A（padding/offset 来源，见 `FDPNCKOAKEP`）|
| `IDFKIGOGMIL` | 0x28 | 缓存组件 B（`NEKBDJHINAM` 的 offset 来源）|
| `NHLMIGHFHEL` | 0x30 | 缓存组件 C（`UpdateHierarchy` 里被 SetActive 抖一次）|
| `CJJINFONMME` | 0x38 | 缓存组件 D（`SetSize` 尾部按它的 Count 循环调 `UpdateHierarchy`）|
| `EAKPIMEIJOF` | 0x40 | 缓存组件 E（`OPPBBFEDOPL` 的第二个 clamp）|
| `DNBODGNBPDD` | **0x48** | **父 `NBISDYNOVJFXBOQT`**（`SetParent` 就写这里）|
| `AAKILGNMHLN` | 0x50 | 重入保护（SetSize 尾部）|
| `KNLIFKILDPP` | 0x51 | 重入保护（UpdateHierarchy）|

方法（RVA / fileOff）：

| 方法 | RVA | fileOff | 语义 |
|---|---|---|---|
| `SetSize(Vector2)` | 0x184ABB0 | 0x1846BB0 | `v'=NEKBDJHINAM(v,-1)`；`OPPBBFEDOPL(v')`；虚调用 slot4 `JIEPCEKHFLO(v')`；若 0x38 非空且非重入 → 循环 `UpdateHierarchy()` |
| `GetPreferredSize()` | 0x184AB84 | 0x1846B84 | `OPPBBFEDOPL( NEKBDJHINAM( 虚 slot6 KHOKIHPNOKG(), 1 ) )` |
| `PILADBIDPKL()` | 0x1851740 | 0x184D740 | **`((RectTransform)transform).rect.size`** = 当前实际尺寸（宽,高）|
| `JKDALCMPIDP(Vector2)` | 0x185176C | 0x184D76C | **`SetSizeWithCurrentAnchors(Horizontal, v.x)` + `SetSizeWithCurrentAnchors(Vertical, v.y)`**（0x2FC3370，axis=0/1）← **真正写宽度的地方** |
| `Start()` | 0x18517BC | 0x184D7BC | `if (parent != null) 虚 slot5 ActAsRoot();` |
| `SetParent(NBIS*)` | 0x185183C | 0x184D83C | `this[0x48]=arg; wbarrier`（仅 8 字节）|
| `UpdateHierarchy()` | 0x1851578 | 0x184D578 | `parent!=null ? parent.UpdateHierarchy() : 虚 slot5 ActAsRoot()`；随后对 0x30 组件 gameObject 做 SetActive(false)/(true) 抖一次 |
| `ActAsRoot()` | 0x1851844 | 0x184D844 | 基类版：把自身挂到某个管理器；**子类覆盖版见 1.2/1.3** |
| `NEKBDJHINAM(v,int n)` | 0x18512F4 | 0x184D2F4 | `v + n * (0x28 缓存的 x,y)`（0x28 为空 → 抛 NRE）|
| `FDPNCKOAKEP(v,int n)` | 0x184B614 | 0x1847614 | `v + n * (0x20 缓存的 x,y)`（0x20 为空 → 抛 NRE）← **容器给子物体做内缩就是 `n=-1`** |
| `OPPBBFEDOPL(v)` | 0x18513CC | 0x184D3CC | 依次用 0x38/0x40 缓存组件做两个 clamp，再查 NaN/Inf |

虚表槽位（`ldp x9,x1,[vtable+off]` 确认）：`slot4 = +0x178 = JIEPCEKHFLO`、`slot5 = +0x188 = ActAsRoot`、`slot6 = +0x198 = KHOKIHPNOKG`。

> 结论：整个系统里**唯一**改宽的地方是 `JKDALCMPIDP` → `RectTransform.SetSizeWithCurrentAnchors`。
> 它按当前 anchors 反算 `sizeDelta`，所以"看 sizeDelta 相等"并不能证明宽度一致；也不是 anchors 被改。

### 1.2 `CUJABYOCLJHRSGWT`（dump.cs:30662，TypeDefIndex 896）＝ 主菜单 `Buttons` 容器

```
public class CUJABYOCLJHRSGWT : NBISDYNOVJFXBOQT
  horizontalSync  0x54   // GJALLEMCFDN
  verticalSync    0x58   // GJALLEMCFDN
  applySizeX      0x5C   // bool
  applySizeY      0x5D   // bool
  children        0x60   // List<NBISDYNOVJFXBOQT>   ← 就是这里
```

| 方法 | RVA | fileOff | 语义（逐条对照反汇编）|
|---|---|---|---|
| `Awake()` | 0x16FE9C0 | 0x16FA9C0 | `foreach (c in children) c.parent(0x48) = this; wbarrier` —— **只登记父指针，不重建列表** |
| `ActAsRoot()` | 0x16FEB84 | 0x16FAB84 | `SetSize(GetPreferredSize())` |
| `JIEPCEKHFLO(Vector2 v)` | 0x16FEBA4 | 0x16FABA4 | 见下方伪码 —— **向所有 children 推宽度** |
| `KHOKIHPNOKG()` | 0x16FED44 | 0x16FAD44 | 见下方伪码 —— **用 children 的首选尺寸算容器自己的首选尺寸** |
| `GGKKANGGAIM(c)` | 0x16FF0A4 | 0x16FB0A4 | `Refresh` 用的过滤谓词：比较 `c.transform.parent` 与 `this.transform`（直接子物体）|
| `CMBNELCONLG()` | 0x16FF13C | 0x16FB13C | **`children = GetComponentsInChildren<NBISDYNOVJFXBOQT>(false).Where(谓词).ToList()`**（RVA 0x1A411A4 → GameObject.GetComponentsInternal；0x1A8783C=Enumerable.Where；0x1A864C0=ToList）。即"从当前层级重建列表"的 Refresh |

`JIEPCEKHFLO` 反汇编还原：

```csharp
protected override void JIEPCEKHFLO(Vector2 v) {
    Vector2 cur = PILADBIDPKL();                       // 容器当前 rect.size
    Vector2 v2  = ApplyXY(cur, v, applySizeX, applySizeY);   // 0x17EF888: 逐轴 select，见 §2
    JKDALCMPIDP(v2);                                   // SetSizeWithCurrentAnchors(H/V) —— 容器自身
    Vector2 childV = FDPNCKOAKEP(v2, -1);              // v2 - 0x20 缓存的 padding
    foreach (var c in children) c.SetSize(childV);     // ← 克隆体漏掉的正是这一步
}
```

`KHOKIHPNOKG` 反汇编还原：

```csharp
protected override Vector2 KHOKIHPNOKG() {
    Vector2 own = PILADBIDPKL();
    var xSeq = children.Where(activeLike).Select(c => c.GetPreferredSize());
    float  x = ApplySync(xSeq, own, horizontalSync).x;      // 0x17EF89C
    var ySeq = children.Where(activeLike).Select(c => c.GetPreferredSize());
    float  y = ApplySync(ySeq, own, verticalSync).y;
    return FDPNCKOAKEP(new Vector2(x, y), +1);              // 加回 padding
}
```

### 1.3 `IKUBYZNZZCPWONJL`（dump.cs:46706，TypeDefIndex 1430）＝ **按钮上的那个组件**

```
IKUBYZNZZCPWONJL : ZUMAGTXVRLFABHLW (dump.cs:7507, TypeDefIndex 243) : NBISDYNOVJFXBOQT
```

`ZUMAGTXVRLFABHLW` 字段：`layoutGroup 0x58`、`elements 0x60 (List<NBISDYNOVJFXBOQT>)`、`sizeMode 0x68`、
`applySelfSizeX 0x6C`、`applySelfSizeY 0x6D`、`spaceDistributionMode 0x70`、`takeAllAvailableSpace 0x74`、
`padding 0x78 (RectOffset)`、`spacing 0x80 (float)`。

| 方法 | RVA | fileOff | 语义 |
|---|---|---|---|
| `AddElement(NBIS*)` | **0x184B470** | 0x1847470 | `elements.Add(x); x.parent(0x48)=this; wbarrier` ← **官方的注册 API，可直接调用** |
| `Awake()` | 0x184B240 | 0x1847240 | 同 CUJ.Awake：只写 children 的 0x48 |
| `CMBNELCONLG()` | 0x184B37C | 0x184737C | 与 CUJ.Refresh 同构：从层级重建 `elements` |
| `ActAsRoot()` | 0x184AB6C | 0x1846B6C | `SetSize(GetPreferredSize())` |
| `IKU.KHOKIHPNOKG()` | 0x17E472C | 0x17E072C | 内容式首选宽：padding.left+right + spacing*(n-1) + Σ elements 首选宽（再按 sizeMode 处理）|
| `IKU.JIEPCEKHFLO(v)` | 0x17E499C | 0x17E099C | `v2 = ApplyXY(PILADBIDPKL(), v, applySelfSizeX(0x6C), applySelfSizeY(0x6D))` → `JKDALCMPIDP(v2)` → 按 `spaceDistributionMode(0x70)` / `takeAllAvailableSpace(0x74)` 把宽度**按比例分给内部 elements（Icon/Text/Gradient）** |

→ 也就是说：**按钮拿到父容器推下来的宽度后，才把 Icon/Text 按比例铺开**。克隆体既没拿到宽度，
内部 elements 也就按 280 铺，所以整个按钮（含内部子物体）都窄 0.78 倍。

### 1.4 `GJALLEMCFDN`（dump.cs:48694，TypeDefIndex 1503）

```csharp
public enum GJALLEMCFDN { AverageChildSize = 0, MaxChildSize = 1, NoSizeChange = 2 }
```

`ApplySync`（**RVA 0x17EF89C / fileOff 0x17EB89C**）＝ `static Vector2 ApplySync(IEnumerable<Vector2> seq, Vector2 own, GJALLEMCFDN mode)`：
- 先 `new List<Vector2>(seq)`；`seq == null || Count == 0` → 直接返回 `own`
- `mode == 0` → `Enumerable.Average`（0x1A66A7C），X/Y 各取平均
- `mode == 1` → `Enumerable.Max`（0x1A76710）
- `mode == 2` → 不动（返回 `own`）
- 结果再逐轴并回 `FDPNCKOAKEP(..., +1)`

即：容器宽度方向的首选值 = 子按钮首选宽度的平均/最大 + padding。**增删子项只会影响这个"首选值"，
不会影响"推送值"**（推送值恒为容器实际 rect − padding）。这对我们很关键：往 `children` 里加一个和源按钮
同尺寸的克隆体，`Average`/`Max` 的结果基本不变 → 容器自身尺寸不会跳。

---

## 2. `ApplyXY`（逐轴 select）—— 0x17EF888 / fileOff 0x17EB888

```asm
tst  w0,#1 ; fcsel s0, s2, s0, ne     ; x = applyX ? new.x : cur.x
tst  w1,#1 ; fcsel s1, s3, s1, ne     ; y = applyY ? new.y : cur.y
ret
```
签名：`static Vector2 ApplyXY(Vector2 cur, Vector2 newV, bool applyX, bool applyY)`（cur 在 s0/s1，newV 在 s2/s3）。

---

## 3. 9 个组件逐个查继承链（任务 2）

Play Button 上的组件（`agent_A_mainmenu.md` §2.2 场景解析 + 本次 dump 复核）：

| # | 组件 | 继承链 | 是否 `NBISDYNOVJFXBOQT` 子类 |
|---|---|---|---|
| 1 | `RectTransform` | UnityEngine.Transform | ✗ |
| 2 | `CanvasRenderer` | UnityEngine | ✗ |
| 3 | `HKAZNXTEGNSSTQLF`（dump.cs:37463）| `: MonoBehaviour, GHAAENMILKO, LBMGEJGPNBG, GPAGBFPEJOP` | ✗ |
| 4 | `DKEKLDIKOUYOJKFB`（dump.cs:31216）| `: MonoBehaviour`（宽高 clamp，OptionalFloat 0x20~0x38）| ✗ |
| 5 | `HorizontalLayoutGroup` | UnityEngine.UI | ✗ |
| 6 | **`IKUBYZNZZCPWONJL`**（dump.cs:46706）| **`: ZUMAGTXVRLFABHLW` → `: NBISDYNOVJFXBOQT`** | **✔ 唯一** |
| 7 | `YKWRDRSYSNQEDHBG`（dump.cs:52726）| `: MonoBehaviour`（视觉/发光）| ✗ |
| 8 | `IATNFWYCVWCHMYGA`（dump.cs:10381）| `: Image, IMeshModifier` | ✗ |
| 9 | `WRNLOFEPQTZANWYN`（dump.cs:34187）| `: MonoBehaviour`（SFS.Translations）| ✗ |

`NBISDYNOVJFXBOQT` 的全部子类（dump.cs grep）只有 7 个：
`ZUMAGTXVRLFABHLW`(7507)、`ASKDCNIZYXERWHDX`(10793)、`CUJABYOCLJHRSGWT`(30662)、`RQCKLIBDDOHDGUDN`(32116)、
`NBQUPLAPLSWBTLMX`(41261)、`FLWTHTQPXKKFHKFC`(43169)、`MHCNBJBYEPHCLHON`(49758)。
主菜单按钮链路上只会出现 `IKUBYZNZZCPWONJL`（按钮）和 `CUJABYOCLJHRSGWT`（`Buttons` 容器）。
注意：`IATNFWYCVWCHMYGA`/`ASKDCNIZYXERWHDX`/`RQCKLIBDDOHDGUDN` 都在**内部子物体**（Icon/Text）上，不是按钮根。

**→ 克隆体上需要注册进 `children` 的组件 = 克隆体 GameObject 上的 `IKUBYZNZZCPWONJL`。**

---

## 4. 为什么"复制 sizeDelta / localScale / rect 对比"必然无效

- `JKDALCMPIDP` 用的是 `SetSizeWithCurrentAnchors`，它**按 axes 反算 sizeDelta**。
  在容器推宽度之前 `rect == sizeDelta == 280` 是自洽的，推之后兄弟变 359 而克隆体仍是 280；
  你克隆时读到的是推之前的 280（日志 `src rect=280.0 clone=280.0` 完全对得上）。
- v36 现有那段 `sizeDelta *= srcW/clW`（v36.c:556-575）在此刻 `srcW==clW` → 系数 1.0 → 等于没做。
- `localScale` 与宽度无关（两者本来就相同）。
- 唯一有效的是让克隆体**进入容器的 `children`**，或**在布局完成后再复制一次几何**。

---

## 5. 最小修复方案

### 5.1 主方案 A（推荐）：注册进 `children` + 延后一帧推宽度

**为什么必须"延后"**：`NBISDYNOVJFXBOQT.Start()`（0x18517BC）会在克隆体首次激活的下一帧执行
`if (parent != null) ActAsRoot()` → `SetSize(GetPreferredSize())`，把宽度改回"内容首选值"（280）。
在 Start 之前推的宽度会被它覆盖。v36 已经有每帧主线程钩子 `hook_update`（`WorldTime/ScreenManager.Update`，
`g_frame` 自增），直接在 `g_frame + 2` 那一拍做最终推送即可。

**注入点**：`mainmenu_button()`（v36.c:461）内，紧跟 `Instantiate` 之后（v36.c:507）加"阶段1"；
在 `hook_update`（v36.c:611）里加"阶段2"。

阶段1（克隆当帧，只登记，不改尺寸）：

```c
/* ---- v37 阶段1：注册到容器的 children ---- */
static void* g_typeIku = NULL;   /* IKUBYZNZZCPWONJL 的 System.Type */
static void* g_typeCuj = NULL;   /* CUJABYOCLJHRSGWT 的 System.Type */
static void* g_cloneSync = NULL; /* 克隆体的 IKU 组件 */
static void* g_contSync  = NULL; /* 容器 CUJ */
static void* g_srcTr     = NULL; /* 源按钮 Transform（阶段2 兜底要用，必须存成 static） */
static int   g_fixFrame  = -1;

/* worker 初始化里： */
g_ikuCls  = cf(img,"SFS.UI","IKUBYZNZZCPWONJL");       if(g_ikuCls)  g_typeIku = tgo(cgt(g_ikuCls));
g_cujCls  = cf(img,"SFS.UI","CUJABYOCLJHRSGWT");       if(g_cujCls)  g_typeCuj = tgo(cgt(g_cujCls));

/* mainmenu_button() 里 Instantiate 之后： */
{ void* tp[1]; void* e3=NULL;
  tp[0] = g_typeIku;
  g_cloneSync = (g_typeIku && g_mGetComponent) ? g_ri(g_mGetComponent, clone, tp, &e3) : NULL;
  if (!g_cloneSync) { log_app("v37: clone has no IKUBYZNZZCPWONJL\n"); }
}

/* 找容器：优先用克隆体 IKU 的 0x48（Instantiate 若未复制则为空）→ 退回沿父 Transform 往上找 */
{ void* e3=NULL;
  g_contSync = g_cloneSync ? *(void**)((char*)g_cloneSync + 0x48) : NULL;
  if (!g_contSync && ctr && g_mGetParent && g_mGetGO && g_mGetComponent) {
      void* t = g_ri(g_mGetParent, ctr, NULL, &e3);
      for (int d=0; d<6 && t && !g_contSync; ++d) {
          void* go = g_ri(g_mGetGO, t, NULL, &e3);      /* Component.get_gameObject，Transform 也可用 */
          void* tp2[1]; void* e4=NULL; tp2[0]=g_typeCuj;
          g_contSync = go ? g_ri(g_mGetComponent, go, tp2, &e4) : NULL;
          t = g_ri(g_mGetParent, t, NULL, &e3);
      }
  }
}

/* 追加到 children（List<NBISDYNOVJFXBOQT>.Add）+ 补 SetParent */
if (g_contSync && g_cloneSync) {
    void* e5=NULL;
    void* list = *(void**)((char*)g_contSync + 0x60);     /* CUJ.children / ZUM.elements */
    if (!list) {
        /* 保险：走容器自己的 Refresh 从层级重建（CUJ 0x16FF13C / ZUM 0x184B37C） */
        void* refMi = gm(*(void**)g_contSync, "CMBNELCONLG", 0);
        if (refMi) g_ri(refMi, g_contSync, NULL, &e5);
        list = *(void**)((char*)g_contSync + 0x60);
    }
    if (list) {
        void* listKlass = *(void**)list;                  /* Il2CppObject.klass 在 +0 */
        void* addMi = gm(listKlass, "Add", 1);
        if (addMi) { void* ap[1]; ap[0]=g_cloneSync; g_ri(addMi, list, ap, &e5); }
        snprintf(b,sizeof(b),"v37: children.Add clone=%p list=%p exc=%p\n", g_cloneSync, list, e5); log_app(b);
    }
    /* 父指针（base.SetParent 0x185183C：写 0x48 + wbarrier） */
    void* spMi = gm(*(void**)g_cloneSync, "SetParent", 1);
    if (spMi) { void* pp[1]; pp[0]=g_contSync; g_ri(spMi, g_cloneSync, pp, &e5); }

    g_fixFrame = g_frame + 2;      /* Start 之后 */
    g_srcTr    = srcTr;            /* 阶段2 兜底用 */
}
```

阶段2（`hook_update` 内，`g_fixFrame` 到点）：

```c
if (g_fixFrame > 0 && g_frame >= g_fixFrame && g_contSync && g_cloneSync) {
    g_fixFrame = -1;
    void* e6 = NULL;
    void* pilMi = gm(*(void**)g_contSync,  "PILADBIDPKL", 0);   /* 0x1851740 容器 rect.size */
    void* fdpMi = gm(*(void**)g_contSync,  "FDPNCKOAKEP", 2);   /* 0x184B614 (Vector2,int) */
    void* setMi = gm(*(void**)g_cloneSync, "SetSize",     1);   /* 0x184ABB0 */
    if (pilMi && fdpMi && setMi) {
        void* r1 = g_ri(pilMi, g_contSync, NULL, &e6);
        if (r1 && !e6) {
            float* cur = (float*)g_unbox(r1);
            float vec[2] = { cur[0], cur[1] };   int neg = -1;
            void* p2[2]; p2[0]=vec; p2[1]=&neg;
            void* r2 = g_ri(fdpMi, g_contSync, p2, &e6);      /* = 容器rect - padding */
            if (r2 && !e6) {
                float* inner = (float*)g_unbox(r2);
                float sv[2] = { inner[0], inner[1] };
                void* p3[1]; p3[0]=sv;
                g_ri(setMi, g_cloneSync, p3, &e6);            /* 容器推给兄弟的同一个值 */
                snprintf(b,sizeof(b),"v37: pushed %.1fx%.1f to clone (exc=%p)\n", sv[0], sv[1], e6); log_app(b);
            }
        }
        if (e6) {   /* 兜底：直接抄源按钮的 rect 尺寸 */
            void* rs = g_ri(g_mGetRect, g_srcTr, NULL, &e6);   /* g_srcTr 已在阶段1 存好 */
            if (rs && !e6) { float* a=(float*)g_unbox(rs); float sd[2]={a[2],a[3]};
                             void* p4[1]; p4[0]=sd; g_ri(g_mSetSizeDelta, g_cloneRect, p4, &e6);   /* g_cloneRect 是 v36 已有的全局 */
                             log_app("v37: fallback set_sizeDelta(src.rect)\n"); }
        }
    }
    if (g_mForceRebuild && g_contSync) { /* 可选：LayoutRebuilder 已在 v36 里对 parent 调过 */ }
}
```

**调用序列（最终形态）**

```
[帧 N ] Instantiate(srcGo, parent, false)
        → GetComponent<IKUBYZNZZCPWONJL>(clone)                 // 1 次
        → cont = cloneSync[0x48]  或  祖先 Transform 上的 CUJABYOCLJHRSGWT
        → List<NBISDYNOVJFXBOQT>.Add(cont[0x60], cloneSync)     // 关键
        → SetParent(cloneSync, cont)                            // 写 0x48 + wbarrier
        → 清 clickEvent/holdEvent、改文本、复制 localScale（原有逻辑，保留）
[帧 N+1] 克隆体 Start → （parent 已非空）ActAsRoot → SetSize(GetPreferredSize())  // 会把宽度改回 280，故意让它跑
[帧 N+2] hook_update:
        cur   = cont.PILADBIDPKL()                // 容器 rect.size
        target= cont.FDPNCKOAKEP(cur, -1)         // 容器rect − padding = 兄弟拿到的同一个值
        cloneSync.SetSize(target)                 // → JKDALCMPIDP → SetSizeWithCurrentAnchors
                                                  // → IKU.JIEPCEKHFLO 里再按比例铺 Icon/Text
[帧 >N] 以后任何一次容器 SetSize 都会顺带覆盖克隆体（因为已在 children 里）→ 自适应（转屏/换分辨率）
```

**关键点**
- 推送值不是"抄源按钮的 sizeDelta"，而是**容器算给兄弟的同一个数**，所以天然与源按钮逐像素一致。
- 注册后克隆体也进入 `KHOKIHPNOKG` 的 Average/Max 统计；因为克隆体首选宽 = 源按钮首选宽，
  `AverageChildSize` 结果不变、`MaxChildSize` 也不变 → 容器自身尺寸不会跳。
- 全部通过 `il2cpp_runtime_invoke`（v36 里的 `g_ri`）调用 ⇒ 有异常能拿到 `exc`，不会因为 NRE 直接把进程带走。
- MethodInfo 取法：**`*(void**)mi` = methodPointer（偏移 0）**（v36 里 hook `Update` 用的就是这个，
  `void** mi=(void**)upd; g_orig_update=mi[0];`）。方法全部用 `gm(klass, 名字, 参数个数)` 从**对象自身的 klass**
  （`*(void**)obj`）取，不写死 RVA，跨版本更稳；RVA 只作交叉校验。

### 5.2 备选 B：只调容器自己的 `Refresh`（一行流）

`CUJ.CMBNELCONLG`（RVA 0x16FF13C）+ 5.1 阶段2：

```c
void* refMi = gm(*(void**)g_contSync, "CMBNELCONLG", 0);
g_ri(refMi, g_contSync, NULL, &exc);     /* children = GetComponentsInChildren<NBISDYNOVJFXBOQT>()
                                            .Where(c => c.transform.parent == transform).ToList() */
```
优点：不用碰 `List<T>`，一行重建，克隆体自动进列（它是容器的直接子物体且带 IKU）。
风险：①谓词极性只能从 `GGKKANGGAIM`（0x16FF0A4，比较 `c.transform.parent` 与 `this.transform`）反推，
`WHERE` 的过滤方向需真机验证——建议先加日志打印 `children.Count` 前后值；②`Refresh` 会**替换**整个 List
（丢掉任何非直接子物体的登记项，正常场景下没有）；③若容器尺寸依赖顺序，重建后顺序 = 层级顺序（与原来一致）。
**结论：作为 Add 失败时的兜底，而不是首选。**

### 5.3 备选 C：纯几何（不注册，只延后抄几何）

若不想动 il2cpp 容器状态：把 v36 现有的"复制 sizeDelta"整段（v36.c:539-575）**移到 `g_frame+2`** 再执行一次，
并在复制前把源按钮的 `anchorMin/anchorMax/pivot/anchoredPosition` 一并写到克隆体的 RectTransform 上，
再 `set_sizeDelta(srcSizeDelta)`。
优点：改动最小、零 il2cpp 内部耦合；缺点：**不具自适应性**（转屏/改分辨率后源按钮变宽、克隆体不动），
而且把 `JKDALCMPIDP` 用 `SetSizeWithCurrentAnchors` 建立的 anchors 语义强行改写，属于治标。
建议只作为第 3 层兜底。

### 5.4 `ZUM.AddElement` 说明（其它菜单可直接用）

如果哪天要往 **`ZUMAGTXVRLFABHLW` 派生**的容器（不是本例的 `Buttons`）里加项，
官方 API 就在那里：`AddElement(NBIS*)` **RVA 0x184B470 / fileOff 0x1847470**，
内部 = `elements.Add(x)` + `x[0x48]=this` + wbarrier（`Add` 走的是手动实现的 `List` 内联调用，
慢了才 `0x23067F4` = `List<T>._AddWithResize`）。`RemoveElement(int)` = RVA 0x184B104。
调用约定：`x0=this, x1=元素, x2=MethodInfo`。
本例 `Buttons` 的容器是 `CUJABYOCLJHRSGWT`，**没有** `AddElement`，所以必须用 5.1 的 List.Add。

---

## 6. 风险与对策

| 风险 | 说明 | 对策 |
|---|---|---|
| 克隆体 `SetSize` 抛 NRE | `SetSize→NEKBDJHINAM` 读 `this[0x28]`，`FDPNCKOAKEP` 读 `this[0x20]`；这两个是**非序列化的懒缓存**，`Instantiate` 出来的克隆体很可能为 NULL（源按钮是 `Awake/Start` 时才填）| ①一律走 `g_ri` 抓 `exc`；②进 `SetSize` 前把源按钮 `srcSync` 的 `0x20/0x28/0x30/0x38/0x40` 五个缓存指针直接拷到 `cloneSync`（它们指向同 prefab 的等价组件，尺寸数值一致，只读不写）——**这是最省事且语义安全的一刀**；③仍失败则走 5.3 兜底 |
| Start 覆盖 | 克隆体 `Start`（0x18517BC）在 `parent!=null` 时 `ActAsRoot→SetSize(首选)`，把宽度打回 280 | 推送放在 `g_frame+2`；不要为了躲它去清 `0x48`（清空会让 `UpdateHierarchy` 走 `ActAsRoot` 分支，反而在 `SetSize` 尾部递归把宽度改回去） |
| 容器尺寸被 `Average/Max` 带偏 | 容器首选宽 = children 首选宽的均值/最大值 + padding | 克隆体首选宽 = 源按钮首选宽（同 prefab）→ 均值/最大值不变；但仍需真机对比容器高度（`verticalSync` 若是 Average，高度均值会因新增同高按钮而不变，若为 Max 也不变） |
| `children` 里出现重复项 | 同一克隆体被 Add 两次（例如面板重复打开） | Add 前用 `List.Contains`（`gm(listKlass,"Contains",1)`）判一次；或每次重建克隆体时重置 `g_cloneSync/g_contSync` |
| `LayoutRebuilder.ForceRebuildLayoutImmediate`（0x3255C64，v36 已用）与 `SetSizeWithCurrentAnchors` 互相打架 | 布局组 `childControlWidth=false` 时不会覆盖宽度；若为 true，布局组会在下一帧把宽度再按容器拉伸一次，与我们的值一致，无害 | 观察 `v37: pushed` 与下一帧 `rect` 是否回退；若回退，把推送再延到 `g_frame+3` 并在推送后立刻 `ForceRebuild` 容器 |
| 转屏/分辨率变化 | 注册进 `children` 后自动跟随，但只在容器下一次 `SetSize` 时生效 | 已在 `children` 里 ⇒ 与兄弟按钮同生命周期，无需额外处理 |
| 偏移/RVA 硬编码 | 本报告给的 RVA 只对 1.6.00.22 有效 | 实现里全部用 `gm(klass,"方法名",n)` + `cgfn(klass,"字段名")` 解析；RVA 仅写进日志做交叉校验 |

---

## 7. 必加的验证日志（一次真机就能定论）

```
v37: ikuCls=%p contCls=%p cloneSync=%p contSync=%p
v37: cloneSync fields 20=%p 28=%p 30=%p 38=%p 40=%p 48=%p
v37: children before=%d after=%d exc=%p            // 用 get_Count 或者 List 的 0x18 字段(size)
v37: cont rect=%.1fx%.1f  src rect=%.1fx%.1f  clone rect=%.1fx%.1f
v37: pushed %.1fx%.1f  (frame %d, target frame %d)
v37: +1frame clone rect=%.1fx%.1f   // 确认没有被 Start / 布局组打回
```
判读：`pushed` 之后 `clone rect == src rect`（±0.5）且下一帧不回退 → 修好。
若 `pushed` 时 `cont rect` 本身还是 280（容器尚未被撑开），说明容器推宽发生在更晚的时机，
把 `g_fixFrame` 改成"检测到 `cont rect.x` 变化后的下一帧"再推一次（把目标值换成"源按钮 rect.size"更直接）。

---

## 8. 附：本次用到的关键 RVA 一览（1.6.00.22，arm64，fileOff = RVA − 0x4000）

```
NBISDYNOVJFXBOQT            273   SetSize 0x184ABB0  GetPreferredSize 0x184AB84  PILADBIDPKL 0x1851740
                                  JKDALCMPIDP 0x185176C  Start 0x18517BC  SetParent 0x185183C
                                  UpdateHierarchy 0x1851578  ActAsRoot 0x1851844
                                  NEKBDJHINAM 0x18512F4  FDPNCKOAKEP 0x184B614  OPPBBFEDOPL 0x18513CC
CUJABYOCLJHRSGWT            896   Awake 0x16FE9C0  ActAsRoot 0x16FEB84  JIEPCEKHFLO 0x16FEBA4
                                  KHOKIHPNOKG 0x16FED44  Refresh(CMBNELCONLG) 0x16FF13C  pred 0x16FF0A4
ZUMAGTXVRLFABHLW            243   AddElement 0x184B470  RemoveElement 0x184B104  get_Spacing 0x184B15C
                                  Awake 0x184B240  Refresh 0x184B37C  ActAsRoot 0x184AB6C
IKUBYZNZZCPWONJL           1430   KHOKIHPNOKG 0x17E472C  JIEPCEKHFLO 0x17E499C
ApplyXY(4 args)             —     0x17EF888
ApplySync(seq,vec,mode)     —     0x17EF89C   (Average 0x1A66A7C / Max 0x1A76710)
List<T>.Add → _AddWithResize —    0x23067F4
Enumerable.Where/ToList     —     0x1A8783C / 0x1A864C0
GameObject.GetComponentsInternal — 0x1A97454（经 Component.GetComponentsInChildren 0x1A411A4）
SetSizeWithCurrentAnchors   —     0x2FC3370     （axis 0=Horizontal, 1=Vertical）
Transform.get_rect          —     0x2FC24F4     （PILADBIDPKL 用它取 rect.size）
```

字段偏移速查：`parent 0x48`（所有 `NBISDYNOVJFXBOQT`）、`children/elements 0x60`、CUJ 的 `horizontalSync 0x54 /
verticalSync 0x58 / applySizeX 0x5C / applySizeY 0x5D`、ZUM 的 `layoutGroup 0x58 / sizeMode 0x68 /
applySelfSizeX 0x6C / applySelfSizeY 0x6D / spaceDistributionMode 0x70 / takeAllAvailableSpace 0x74 /
padding 0x78 / spacing 0x80`。
