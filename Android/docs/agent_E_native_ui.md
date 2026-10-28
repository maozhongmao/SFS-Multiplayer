# Agent E：卖家「原生游戏窗口」逆向 + 我方 .22 native 复刻方案

> 目标：搞清 SFS Pro 卖家如何在 IL2CPP/arm64 上把**游戏自己的窗口**（`Open` + `DrawTitle/DrawText/DrawFullButton` + 借来的 TMP 输入框）堆出来，并给出在 `com.StefMorojna.SpaceflightSimulator 1.6.00.22` 的 `libsfsmp.so` 里复刻登录页的可执行方案；同时定位 v39 调 `Open` 崩溃的根因。
>
> 材料：
> - `D:/SFS-Pro/build/seller_libpatch.so`（2,060,672 B，`PT_LOAD#0` 的 `p_offset==p_vaddr`，故 **文件偏移 == 虚拟地址**，下文 `0x…` 均为此）
> - `D:/sfs-tools/apk/x22/libil2cpp.so`（.22 官方，`.text` VA 0x13BD4D0 / off 0x13B94D0，VA−0x4000=off）
> - `D:/sfs-tools/apk/dump-official/dump.cs`（597,338 行）
> - `D:/sfs-tools/so/src/v39.c / v40.c / v41.c`（我方现状）
> - `D:/SFS-Pro/work/assets/`（Unity 场景拆分结果，用于判定 level0/level2 内容）
>
> 方法：capstone AArch64 全 `.text` 扫描（ADRP+ADD/LDR 交叉引用 + `.rela.plt` 符号还原）+ 目标 `libil2cpp.so` 反汇编核对。**只读分析，未改工程、未连设备。**

---

## 0. 结论速览（TL;DR）

1. **卖家从头到尾没有“创建”窗口对象。** 他 inline-hook 了游戏 `CLCPKKVBMCAKFMPN`（= 我方 `NOJZIZCFSEXSUEEX`，StatsMenu 抽屉）的 `Open`，在**游戏自己打开该抽屉时**把 `this` 抄进一张 8 槽注册表（步长 `0xDA0`，`[slot+0]=this`）；之后每次要画面板，就用 hook 留下的 **trampoline** 带着自己的委托回调 `Open(this, Func<bool>, Action<T>, true, 0, 0)`，把返回的 `FHBOAFMLNDC`（面板句柄）当“窗口”；随后在回调里用 `DrawTitle/DrawText/DrawFullButton` 填内容。**拿不到实例就什么也做不了**，他的日志里明写 `PMP: StatsMenu(...) not found yet, retrying`。
2. **委托是伪造的 `System.Action` 对象**（`il2cpp_object_new` + 把 4 个代码/参数槽全填成同一个 native stub），stub **完全忽略参数**、只往全局变量里塞一个“待处理点击”标志（`fire_prev`/`fire_next` 各 `str w9,[0x3cec30]`）。这是他能不踩 ABI 的关键。
3. **.22 的委托调用 ABI 已实证**（三处独立调用点一致）：
   ```
   x8 = *(delegate + 0x18)      ; 代码指针
   x0 = *(delegate + 0x40)      ; 转发值 A
   x1..xN = 真正的实参
   xN+1 = *(delegate + 0x28)    ; 转发值 B（0 参时就是 x1）
   blr x8
   ```
   两处转发值都**只按值传递、不解引用**。
4. **v39 崩溃根因（最可能，且与日志“无 BUILDER 行”完全吻合）**：`v39_tramp_builder` 把**第一个参数**当对象去 `il2cpp_object_get_class()`，而第一个参数正是 `*(del+0x40)`；v39 把 `+0x40` 填成了自己的**代码地址** → 拿代码当对象读 `klass` → `il2cpp_class_get_name()` 解引用乱指针 → SIGSEGV，死在 `Open` 内部第 2 步回调里，日志自然停在 `calling Open now...`。次因（同样致命，但会晚一步）：实例是**预制体/跨场景残留**，`Open` 第一件事是 `Close(this)`（0x18DC824），里面 `titleHolder.SetActive(false)` 直接打空。
5. **主菜单（level0）没有 StatsMenu**：assets 扫描 `"Stats Menu"` 只命中 `level2`；`level0.split0` 里只有 `"Mods Menu" / "World List" / "Create World"`。所以卖家那套 `Open+Draw*` **在主菜单不可用**（除非玩家已进过 level2）。主菜单要用**克隆整窗 + 克隆行**（R-A）。
6. 三条路线（详见 §3）：**R-A 克隆整窗（主菜单首选，零委托伪造、零崩溃面）** > **R-B NOJ 抽屉（进游戏后/飞行/设计器/生涯场景，卖家同款，保真度最高）** > **R-C 自建 Canvas（不推荐，字体/材质/布局全要自己搬）**。

---

## 1. 卖家 `libpatch.so` 逆向：完整调用序列

### 1.1 他先“接住”了这个类：5 个 inline hook

`PMP-MOD` 初始化函数 `0xBC3F0` 用一张 (类名, 方法名, 参数个数) 的解析器 `sub_BC8F8` 一次性解析：

| # | 调用点 | 类 | 方法 | `w3`=参数个数 | 结果存 |
|---|---|---|---|---|---|
| 1 | `0xBC47C` | `CLCPKKVBMCAKFMPN` | `Open` | **5** | `x19` |
| 2 | `0xBC498` | 同上 | `EFLOBBJGMGA` | 3 | `x20` |
| 3 | `0xBC4B4` | 同上 | `DrawTitle` | 1 | `x26` |
| 4 | `0xBC4D0` | 同上 | `Close` | 0 | `x22` |
| 5 | `0xBC4F4` | `SFS.UI` / `FKRXEFFKYQNYBSWM` | `GetPreferredSize` | 0 | `x23` |

解析器 `sub_BC8F8(class, method, paramCount)` 是“类名+方法名+参数个数”三元组定位（`0xBC8F8` 起，内部走 `il2cpp_class_from_name` / `il2cpp_method_get_name` 一类 API），**这正是我们 v39 用 `il2cpp_class_get_method_from_name(klass,"Open",5)` 做的事**。

随后五个 `install_inline_hook_to(目标, hook函数)`（**导入表实证**：`.rela.plt` → `0x1EF140 = install_inline_hook_to`、`0x1EF110 = install_inline_hook`、`0x1EF120 = interceptor_class_from_name`，来自卖家自己的 interceptor 库），返回的 **trampoline（原函数副本）** 存进全局：

```
0xBC5B4  install_inline_hook_to(Open,            0x137ABC) → [0x3CEC48]
0xBC5D0  install_inline_hook_to(EFLOBBJGMGA,     0x137F18) → [0x3CEC50]
0xBC5EC  install_inline_hook_to(DrawTitle,       0x1380E0) → [0x3CEC58]
0xBC608  install_inline_hook_to(Close,           0x138368) → [0x3CEC60]
0xBC624  install_inline_hook_to(GetPreferredSize,0x138460) → [0x3CEC68]
```

四个都失败才报 `PMP-ERR: all inline hooks failed (prologue not relocatable?)`（`0xBC81C`）。

> **要点**：他 hook 的目的**不是**篡改行为，而是
> (a) `Open` hook 用来**捕获实例**（§1.2）；
> (b) `DrawTitle/Close/GetPreferredSize` hook 用来**接管标题/关闭/尺寸**，让面板“看起来就是游戏自己的”（`GetPreferredSize` 那条日志写着 `失效元素守卫 ✓`）。
> 他随后自己调 `Open` 走的是 **trampoline**（`[0x3CEC48]`），绕开自己的 hook —— 避免自我递归。

### 1.2 实例捕获：8 槽注册表（`Open` hook 本体，`0x137ABC`）

```
x0 = this                                  ; 被游戏调用的抽屉实例
if (this < 0x1000 || this>>48 || *(void**)this < 0x1000)  → 视为非法，跳过注册
查找 [0x3C70F20 + i*0xDA0]，i < [0x3C70F18]（上限 8，cmp w9,#7）
  命中 → x19 = 该槽
  未命中 → 取 n=[0x3C70F18]++，memset(slot,0,0xDA0)，*slot = this      （0x137C58/0x137C78）
x20 = ((OpenTrampoline)[0x3CEC48])(x0=this, x1..x5 原样转发)           （0x137B58–0x137B64）
```

槽结构（相对 `slot`，全部由后续代码访问证实）：

| 偏移 | 含义 |
|---|---|
| `+0x000` | **`this`（抽屉实例）** |
| `+0x008` | 打开次数计数 |
| `+0x010 / +0x014` | 标题长度 / 标题字符缓冲（`PMP-INNER#%d ... title="%s"`） |
| `+0x154` | 当前已画元素个数 |
| `+0x958` | 元素对象数组（`int[256]`，`0x400` 字节，`0x137140` 起按 8 个一组做非零计数） |
| `+0xD58` | 当前页 index |
| `+0xD5C` | 总页数 |
| `+0xD60` | “正在重建”标志（防重入） |
| `+0xD64` | 已构建标志 |
| `+0xD70` | 传给 `Open` 的 **`Func<bool>`** 委托 |
| `+0xD78` | 传给 `Open` 的 **`Action<T>`** 委托 |
| `+0xD80/+0xD88/+0xD90` | 仅日志用（`skip/onOpen/onClose`） |
| `+0xD98` | `Open` 返回的面板句柄 `FHBOAFMLNDC` |
| `+0xDA0` | 槽大小 |

> **这是整份逆向最关键的一条**：卖家的实例**只可能来自游戏自己调 `Open`**。他没有任何 `Instantiate`/`AddComponent`/`FindObjectsOfType` 去“造”这个抽屉。

### 1.3 他调 `Open` 的完整现场（`0x1372A4`–`0x1372D8`）

```
0x1372A4  x8 = [0x3CEC48]            ; Open 的 trampoline（原函数）
0x1372A8  x0 = [x19]                 ; this = 抽屉实例（slot+0）
0x1372B0  x1 = [x19 + 0xD70]         ; Func<bool>          （可为 NULL）
0x1372B4  x2 = [x19 + 0xD78]         ; Action<抽屉>        （可为 NULL）
0x1372B8  w3 = 1                     ; bool（是否压栈/暂停）
0x1372BC  x4 = 0                     ; Action              （NULL 合法）
0x1372C0  x5 = 0                     ; Action              （NULL 合法）
0x1372C4  [x19+0x154] = 0            ; 清元素计数
0x1372C8  [0x3CEC34]  = 1            ; “重建中”全局标志
0x1372CC  [x19+0x0C]  = 0            ; 清“行数”
0x1372D4  blr x8
0x1372D8  x24 = x0                   ; ★ 返回值 = 面板句柄
0x1372DC  log "PMP-RB: step=after_open inner=%p"
```

调用前后的铺垫：
- `0x136FF0`：`x8=[0x3CEC48]; cbz → 放弃`（trampoline 未就绪就不动手）；
- `0x1370AC`：`x5=[this+0xD78]`，`0x1370B8`：`w20=[this+0x0C]` 行数，`0x1370F4` 调 `sub_17E0BC(this, rows, page)` 建行；
- `0x1371D4` 把 `this` 写进 `[0x3CEC28]`（全局“当前面板”），之后 `0xE05C0`/`0xE06E8` 的每帧 tick 就靠它；
- `0x1372F8` 起读 `[this+0xA0]` → `List<GameObject>` 的 `_size`（`+0x18`）→ 打 `PMP-COUNT: ... layout_elements=%d menu_elements=%d`。**这证明卖家版本的字段布局与我方 .22 完全一致**（`0xA0` = 元素 GameObject 列表）。

### 1.4 委托怎么造（`0x17E8E0`–`0x17EE94`）——他连字段偏移都是运行时探测的

```
0x17E930  "il2cpp_object_new"            → dlsym 存 [sp+8]
0x17E94C  "il2cpp_class_get_fields"
0x17E960  "il2cpp_field_get_name"
0x17E96C  "il2cpp_field_get_offset"
0x17E9A4  "method"            → 命中则存偏移到 [0x19CAB70]
0x17E9B4  "method_code"       → 命中则存偏移到 [0x19CAB78]
0x17EA24  "delegate_trampoline"→ 命中则存偏移到 [0x19CAB80]
0x17EA58  log "PMP-BTN: extra code-ptr offsets method=0x%zx method_code=0x%zx trampoline=0x%zx"
```

造两个委托（`prev` / `next`，循环 2 次，`i` 从 1 开始）：

```
0x17ECA8  x0 = [0x19CAB50]            ; System.Action 的 klass
0x17ECB4  blr (il2cpp_object_new)
0x17ECDC  [del + off_method_ptr]  = stub_i        ; 固定 0x10
0x17ED04  [del + off_invoke_impl] = stub_i        ; 固定 0x18   ← ★ 调用点真正用的就是它
0x17ED10  [del + off_0x30]        = stub_i
0x17ED24  if (off_method  >= 0) [del + off_method]  = stub_i
0x17ED34  if (off_method_code >= 0) [del + off_method_code] = stub_i
0x17ED48  if (off_another >= 0) [del + off_another] = [0x19CAB60]  ; 一个“占位 MethodInfo”
0x17EDC0  il2cpp_gchandle_new(del, 0)             ; ★ 扎根，防 GC
0x17EE00  log "PMP-BTN: built[%d] del=%p target=%p mptr=%p invoke=%p minfo=%p"
```

两个 stub 就是裸函数（`0x17F370` / `0x17F39C`）：

```asm
0x17F370  stp x29,x30,[sp,#-0x10]!
          log "PMP-BTN: CLICK prev -> queued (-1)"
          w9 = -1 ; str w9,[0x3CEC30]      ; 只置全局“待翻页”标志
          x0 = 0 ; ret                     ; 返回 0（Func<bool> 场景下 = false）
```
```asm
0x17F39C  ... str w9(=+1),[0x3CEC30] ; x0 = 0 ; ret
```

> **三个可直接抄的结论**
> 1. **`+0x10` 与 `+0x18` 都填同一个 native 函数**；真正被 `blr` 的是 `+0x18`。
> 2. **`+0x28`（method 或 target，见 §2.3）和 `+0x40`（method_code）也填代码地址**——不是为了“正确”，而是为了**任何转发路径都拿到一个合法指针、且 stub 根本不解引用**。
> 3. **必须 `il2cpp_gchandle_new(del, 0)`**，否则下一次 GC 就把委托回收（这也是“偶发崩溃”的经典来源）。

### 1.5 `DrawFullButton` 的现场（`0x17F1E8`–`0x17F2D4`）——注意那个“优先级”参数

```
0x17F1E8  x1 = [x19]                  ; this
0x17F1F0  w10 = [x19+0xD58]           ; page
0x17F1F4  x8 = [0x19CAB88]            ; prev 委托
0x17F1F8  x9 = [0x19CAB90]            ; next 委托
0x17F1FC  cmp w5,#2                   ; mode==2 ?
0x17F210  csel x24, xzr, x8, eq       ; ★ mode==2 ⇒ 传 NULL Action（“NULL-action 按钮”）
0x17F240  x9 = [0x19CAB58]            ; DrawFullButton 方法指针
0x17F244  w1 = 0x7FFFFFFF             ; ★ int = layoutPriority（最大 ⇒ 排到最末）
0x17F248  x0 = [x19]                  ; this
0x17F24C  x2 = label(string)
0x17F250  x3 = 委托
0x17F254  cmp w8(page),#0 ; cset w4 = (page>0)   ; bool enabled
0x17F25C  blr x9
...
0x17F2B4  w1 = 0x7FFFFFFE             ; “下一页”优先级 = INT_MAX-1（紧跟其后）
```

> **`DrawFullButton` 第 1 个 `int` 不是“行号”，是 `layoutPriority`**（`.22` 里 `HKAZNXTEGNSSTQLF.layoutPriority` 就在 `0x3C`，dump.cs:37473）。所以想要按钮排在原生行之后，就给一个很大的数。
> 同时证明：**`Action` 参数允许 NULL**（mode 2 就是 NULL-action，按钮变灰不响应）。

### 1.6 输入框怎么“借”（`"InputField (TMP)"` @0x20C28、`borrowedForm` @0x20F83）

- `0x1539DC` 引用 `"InputField (TMP)"`（TMP 默认输入框预制体的 GameObject 名）；
- `0x153600` 起的辅助函数遍历 `GameObject` 的组件数组（`arr+0x20` 是 `Il2CppArray` 数据区），对每个组件用 **`il2cpp_class_get_name` + `il2cpp_class_get_namespace` 做字符串比对**定位类型（日志 `component[%d]=%s.%s` / `component-match=%p:%s.%s`）——**不认混淆名，只认 `TMPro` / `TMP_InputField`**；
- `0xC8DE4` 的 `MP-NICK-ROW` 日志字段 `created row=%p input=%p holder=%p borrowedForm=%d rowSize=%.0fx%.0f` 说明：他**克隆**一个现成表单（`borrowedForm=1`）拿到输入框，塞进自己那一行，而不是新造。

> 对我方极其重要：.22 里现成的输入框包装类是 **`TUXXAURTZSIFPXTZ`**（dump.cs:20505），字段 `mobileTextBox 0x20`、**`TMP_InputField TMProTextBox 0x28`**，并提供 `get_Text/set_Text`，还内置**安卓软键盘**处理（`LINRJGANWEEFRCUR mobileTextBox`）。主菜单里活着的实例就挂在 `LKSRSZGMFKUEATEP.worldNameField`（“新建世界”窗口，dump.cs:5926，偏移 `0x48`）。

---

## 2. `.22` 官方 dump / `libil2cpp.so` 对照（逐条核对，全部有据）

### 2.1 类与字段：`NOJZIZCFSEXSUEEX : MonoBehaviour`（dump.cs:21261）

```
menuHolder          0x20 RectTransform
statsHolderGroup    0x28 VerticalLayoutGroup
statsHolderLayout   0x30 BBPSLYHPNWUJECLT
rootElement         0x38 NBISDYNOVJFXBOQT
titleHolder         0x40 GameObject        ← Close/Open 第一件事就是动它
titleText           0x48 NDJCKINKYJCMZNTI
popupAnimation      0x50 WJDAKEPBTMPDKKWP
spacePrefab         0x58 / textPrefab 0x60 / statPrefab 0x68 / statBackPrefab 0x70
togglePrefab        0x78 / sliderPrefab 0x80 / buttonPrefab 0x88 / fullButtonPrefab 0x90
CHNLEMLKDNN         0x98 List<NAABCLLJEKA>      （行定义）
CILNINLCEGC         0xA0 List<GameObject>       （↑ 生成出来的元素，Close 里逐个 Destroy）
JEEIJNBOEDF         0xA8 Func<bool>             ← Open 参数 0
IJCJCEEINNP         0xB0 Action                 ← Open 参数 4
CEKDEFIEIOJ         0xB8 bool
```

### 2.2 方法表（RVA 与 dump 行号）

| 方法 | RVA | dump 行 | 说明 |
|---|---|---|---|
| `Open(Func<bool>, Action<NOJ>, bool, Action, Action)` → `FHBOAFMLNDC` | **0x18DD724** | 21336 | 5 参，返回值是面板句柄 |
| `Close()` | 0x18DC824 | 21359 | |
| `DrawTitle(string)` | 0x18DD5D4 | 21324 | 1 参 |
| `DrawText(int, string)` | 0x18DDC08 | 21347 | |
| `DrawFullButton(int,string,Action,bool)` | **0x18DDCF8** | 21350 | 4 参（卖家的 `pc4`） |
| `DrawFullButton(int,Func<string>,Action,Func<bool>,Action<Action>,Action<Action>)` | 0x18DDE48 | 21362 | 6 参（卖家的 `pc6`） |
| `DrawButton(int,string,string,Action,bool)` | 0x18DD1D0 | 21318 | |
| `DrawButton(int,Func<string>,Func<string>,Action,Func<bool>,Action<Action>,Action<Action>)` | 0x18DD374 | 21330 | |
| `DrawSpace(int,float=5)` | 0x18DCD20 | — | |
| `DrawStat(int,string,string)` | 0x18DBD00 | — | |
| `OBCGMKLLJLB()` / `private bool` | 0x18DD704 | — | `Func<bool>` 的调用封装 |
| `FHBOAFMLNDC..ctor(Action, ref Action)` | 0x1671010 | 1063 | 面板句柄：`bool 0x10` + `Action 0x18` |

> 卖家那张 `pc4 rva != 0x1756B60 → WARN` 的检查（`0x17E640`）就是防止 `class_get_method_from_name(...,4)` 命中错重载；**我们这里两个重载参数个数 4 / 6 不同，用个数取不会混**。

### 2.3 ★ 委托调用 ABI（本 build 实证，三处一致）

**(a) `Open` 里调用参数 1（`Action<NOJ>`），`0x18DD824`–`0x18DD840`：**
```asm
0x18DD824  cbz  x24, ...            ; x24 = 参数2(Action<NOJ>)，NULL 就跳过
0x18DD82C  mov  x1, x19             ; x1 = this（真正的实参）
0x18DD834  ldr  x8, [x24, #0x18]    ; 代码指针
0x18DD838  ldr  x0, [x24, #0x40]    ; 转发值 A
0x18DD83C  ldr  x2, [x24, #0x28]    ; 转发值 B
0x18DD840  blr  x8                  ; ⇒ f(*(d+0x40), this, *(d+0x28))
```

**(b) `Close` 里调用 `Action`（0 参），`0x18DC888`–`0x18DC89C`：**
```asm
0x18DC888  ldr  x8, [x20]           ; x20 = &this->IJCJCEEINNP(0xB0)
0x18DC88C  cbz  x8, skip
0x18DC890  ldr  x9, [x8, #0x18]
0x18DC894  ldr  x0, [x8, #0x40]
0x18DC898  ldr  x1, [x8, #0x28]
0x18DC89C  blr  x9                  ; ⇒ f(*(d+0x40), *(d+0x28))
```

**(c) `OBCGMKLLJLB()`（就是 `Func<bool>` 的调用封装），`0x18DD704`：**
```asm
0x18DD704  ldr  x8, [x0, #0xA8]     ; this->JEEIJNBOEDF
0x18DD708  cbz  x8, → return true
0x18DD70C  ldr  x0, [x8, #0x40]
0x18DD710  ldr  x1, [x8, #0x28]
0x18DD714  ldr  x2, [x8, #0x18]
0x18DD718  br   x2                  ; 尾调用
```

**推论（写进实现规范）：**
- 代码指针 = `*(del+0x18)`；**`+0x10` 在这三条路径上根本没被读**（但仍要填，反射/其它路径可能用）。
- `*(del+0x40)` 与 `*(del+0x28)` 是**“隐藏的前置/后置参数”**，只按值转发、**永不在此处解引用**。
  据此可判定本 build 的 `Il2CppDelegate` 布局是 **`+0x28 = target`、`+0x40 = method_code`**，调用约定形如 `invoke_impl(method_code, args…, target)`（`target` 在**参数之后**）——这正好解释 (a) 里 `this` 在 `x1`、`target` 在 `x2`。
  （v39 注释里“`+0x28 = method(MethodInfo*)`”的假设**在本 build 是错的**；不过只要 stub 不解引用，错也不会炸。）
- **`Action` 参数可以是 NULL**：`Open` 检查 `cbz x24`，`Close` 检查 `cbz x8`。

### 2.4 `Open` 逐步语义（`0x18DD724`–`0x18DDBEC`）

```c
FHBOAFMLNDC* Open(this, Func<bool> f, Action<NOJ> build, bool pushStack, Action onCloseA, Action onCloseB)
{
    this->CEKDEFIEIOJ(0xB8) = false;
    Close(this);                                  // ← 0x18DD820，第一件事
    if (build != NULL) build(this);               // ← 0x18DD840，★ 立即回调，窗口此时还没显示
    ... 遍历 this->CHNLEMLKDNN(0x98)，对每个已标记的行:
          inst = Instantiate(prefab /*0x70 或 0x58/0x68*/, this)   // 0x1AD8130
          if (row->0x20 != 0) row->0x18->invoke(...)                // 行自己的 lambda（0x18DD948）
          // 设置 RectTransform 尺寸/位置：0x2FC6A50 / 0x2FC6B14
    this->JEEIJNBOEDF(0xA8) = f;                  // 0x18DDA98
    titleHolder(0x40).SetActive(true)? ; titleText(0x48).text = ...
    if (pushStack) popupAnimation(0x50).Play()    // 0x17C21FC
    rootElement(0x38).???(0x188)                  // 0x18DDB28
    this->IJCJCEEINNP(0xB0) = ??? (0x29AAFB0 + 0x1671010 创建 FHBOAFMLNDC 句柄)
    return new FHBOAFMLNDC(onCloseB, ref this->IJCJCEEINNP);
}
```

要点：
- **回调早于窗口显示**（`build(this)` 在压栈/动画之前）——所以**必须**在回调里画完所有行，这与卖家 `step=queue_enter/queue_done/call_open` 的顺序一致。
- `Open` **依赖一整套预制体引用**（`0x58..0x90`）与 `menuHolder/titleHolder/rootElement`。任何一个是 `null`（**包括“Unity 已销毁”的假 null**）都会走到 `0x18DDA68: bl sub_1572A9C` = **抛 NullReferenceException**；如果调用帧是 native（我们的 `.so`），托管异常无处可抛 → 进程终止（看起来就是“静默崩溃/不返回”）。
- 返回值 `FHBOAFMLNDC` 是**句柄**（`bool 0x10` + `Action 0x18`），不是窗口。

### 2.5 `Close` 语义（`0x18DC824`，`Open` 的第一句）

```c
Close(this) {
    if (this->titleHolder(0x40) == NULL) throw NRE;   // 0x18DC870
    this->titleHolder.SetActive(false);               // 0x18DC880
    if (this->IJCJCEEINNP(0xB0) != NULL)              // 0x18DC888
        this->IJCJCEEINNP->invoke();                  // ★ 又一个委托调用点
    this->IJCJCEEINNP = NULL;
    foreach (go in this->CILNINLCEGC(0xA0)) Destroy(go);   // 0x18DC8F8
    this->CILNINLCEGC.Clear();                             // 0x18DC930
    while (this->statsHolderLayout(0x30).childCount > 2) Destroy(child);  // 0x18DC96C
    this->menuHolder(0x20)...
}
```

> **这就是“实例一旦是残留/预制体就必死”的原因**：`Close` 一上来就 `SetActive` + `Destroy`。Unity 对象被销毁后，C# 引用仍非 NULL（只是 `m_CachedPtr==0`），native 侧 `SetActive`/`Destroy` 直接打空 → SIGSEGV。

### 2.6 `DrawFullButton` 语义

- 4 参版（`0x18DDCF8`）：建一个 `NOJZIZCFSEXSUEEX+OMFJJKFFGNI{string buttonText 0x10, bool enabled 0x18}` 闭包，用 il2cpp 的委托工厂（`0x21241CC` / `0x2123AEC`）现造 `Func<string>`/`Func<bool>`，然后**尾调用** 6 参版。
- 6 参版（`0x18DDE48`）：建 `NKCDMGAMIPI{<>4__this 0x10, buttonText_UI 0x18, button 0x20, buttonAction 0x28, buttonText 0x30, enabled 0x38, <>9__2 0x40}`（与 dump.cs:21180 完全对应），最终走私有 `LNGAOLBDCPH(int, Action, Action, Action<Action>, Action<Action>, bool)`（`0x18DC5C8`）：`Instantiate(fullButtonPrefab 0x90)` → 挂到 `statsHolderGroup` → 设 `layoutPriority(0x3C)` → 把 `Action` 接进 `HKAZNXTEGNSSTQLF.onClick`。

---

## 3. 三条路线比较与选优

### 3.1 硬约束：主菜单里到底有没有“抽屉”

对 `D:/SFS-Pro/work/assets/` 做字符串扫描（Unity 场景序列化后的 GameObject 名）：

```
"Stats Menu"  → 仅 ./level2
"Mods Menu"   → ./level0.split0
"World List"  → ./level0.split0
"Create World"→ ./level0.split0（同名预制/资源另见 34e562…、level0.split0）
```

**结论：`level0`（主菜单/世界列表）里没有 StatsMenu**，`level2`（设计器/VAB 那一套）才有。
→ 卖家那套 `Open+Draw*` **在主菜单不可用**（这也是 v39 只能捞到“残留/预制体实例”的根本原因）。

### 3.2 三条路线

| | R-A 克隆整窗（**主菜单首选**） | R-B NOJ 抽屉（卖家同款） | R-C 自建 Canvas |
|---|---|---|---|
| 前提 | level0 有完整窗体 GameObject（`"Mods Menu"` 根） | 必须已有 `NOJ` **活实例**（level2/飞行/生涯） | 无 |
| 手段 | `Object.Instantiate(窗根)` → 清子节点 → 克隆行 | `Open(Func,Action,bool,0,0)` + `DrawTitle/DrawText/DrawFullButton` | `new GameObject` + `AddComponent(Canvas/Image/TMP_Text)` |
| 委托伪造 | **不需要**（点击继续用 v37 的轮询，或改接 `HKAZNXTEGNSSTQLF.onClick`） | 需要 `Func<bool>` + `Action` + `Action<NOJ>`（§4.3） | 不需要 |
| 保真度 | **像素级**（整窗就是游戏自己的预制体） | 高（原生行布局/动画/优先级） | 低（字体、材质、九宫格、SafeArea 全要自己搬） |
| 崩溃面 | 低（只碰 Unity API，参数都可校验） | 中（`Open`/`Close` 里大量解引用 + 托管异常） | 中高（TMP 资源/字体图集） |
| 工作量 | 中（一次布局调参） | 低（若实例在手） | 高 |
| 适用 | 主菜单登录页 | 进游戏后的联机大厅/房间页 | 兜底/悬浮 HUD |

### 3.3 选优

1. **主菜单登录页 → R-A**。理由：`level0` 无抽屉（硬证据）；R-A 完全避开“伪造委托 + 调 `Open`”这条唯一会崩的链路；视觉就是原版（克隆的就是原版窗根）。
2. **若登录页可以放在“进入世界后”**（联机大厅语义上也更合理）→ **R-B**，这是卖家同款、也是保真度最高的一条；实现照 §4.4 的序列。
3. **R-B 与 R-A 可共存**：进入 level2/飞行场景后检测到 `NOJ` 活实例就升级为 R-B 面板；主菜单仍用 R-A。检测方式：`Resources.FindObjectsOfTypeAll(NOJ_Type)` 数量 > 0 且候选通过活性校验（§4.2-T0）。
4. **R-C 只在**“R-A 找不到可用窗体”时兜底（例如未来版本删掉 Mods 窗）。

### 3.4 R-A 的具体落地步骤（主菜单）

```
1) cls = il2cpp_class_from_name(coreImage, "UnityEngine", "GameObject")
2) go = GameObject.Find("Mods Menu")                 // level0 里确有此名
3) root = go.transform.parent 或 go 本身（视层级）    // 找到“窗体根”（含背景/标题栏）
4) copy = Object.Instantiate(root.gameObject)        // 复制整窗（不带逻辑）
5) 遍历 copy 的子节点：保留背景/标题栏/关闭按钮，Destroy 其余（模组列表行）
6) 需要的新行：
   - 标题：克隆 titleText（NDJCKINKYJCMZNTI 实例）→ set_text
   - 说明文字：同上，字号调小
   - 输入框：克隆 TUXXAURTZSIFPXTZ（从 LKSRSZGMFKUEATEP.worldNameField 0x48 取），
             set_Text("")，读值用 get_Text()；★ 它自带安卓软键盘
   - 按钮：克隆 HKAZNXTEGNSSTQLF（从 Mods 窗的按钮取），改 label，
             ★ 不要动 onClick（保留原监听会触发原逻辑）→ 用 v37 已有的“轮询 + RectTransformUtility.RectangleContainsScreenPoint”判定点击
7) SetParent(copy, 原窗根的父)；RectTransform 锚点/尺寸照抄 root
8) 显示/隐藏：copy.SetActive(true/false)（这就是“和原版一模一样的窗口”）
```

> 注意第 6 步“按钮不要动 onClick”：克隆按钮会带着原 `onClick`（`DNHHDNDIHAM<JBKLKAEBEEB>`，`HKAZNXTEGNSSTQLF+0x68`）。要么 `onClick.RemoveAllListeners()`（需要 managed 调用，成本高），要么**保留但轮询**（v37 已在做，零风险）。卖家在主菜单按钮上走的是 `RemoveAllListeners` 路线（见 `seller_ui_clone_report.md` §2），因为他要的是“点了进自己的界面”。

---

## 4. v39 为什么崩在 `Open` 里 + 修正方案

### 4.1 现象对齐

v39 日志停在：
```
v39: calling Open now...
```
之后**没有** `v39: BUILDER panel=`（`v39_build()` 的第一行）、也没有 `Open returned exc=`。

`Open` 的执行顺序是：`this->0xB8=0` → **`Close(this)`**（`0x18DD820`）→ **`if (build) build(this)`**（`0x18DD840`）→ 建行 → 返回。
日志里没有 BUILDER ⇒ 崩在 **`Close()` 或 `build()` 的入口处**，二者都在 `0x18DD820`–`0x18DD840` 这 **32 字节**内。

### 4.2 三个候选，按可能性排序

**C1（最可能，且能完整解释“没有 BUILDER 行”）——委托第 1 个参数被当成对象去解引用。**

`v39_tramp_builder(a,b,c,d)` 里：
```c
cand[0] = a; cand[1] = b;
for (i=0;i<2;i++){ k = g_mObjGetClass(cand[i]); kn = g_mClassGetName(k); if (strstr(kn,"NOJ...")) ... }
```
而按 §2.3 的 ABI，`a = *(del+0x40)`。v39 的 `v39_make_delegate()` 写的是：
```c
*(void**)(d+0x10) = fn;
*(void**)(d+0x18) = fn;
if (g_doffMethodCode >= 0) *(void**)(d+0x40) = fn;   // ← g_doffMethodCode 运行时探测，几乎必然 >= 0（=0x40）
```
⇒ `a = &v39_tramp_builder`（一段**代码地址**）。`il2cpp_object_get_class(a)` 读 `*(void**)a` = 该函数前 8 字节机器码当 klass 指针 → `il2cpp_class_get_name(k)` 再解引用 → **必然 SIGSEGV**，且发生在 `v39_build()` 之前（所以没有 BUILDER 日志）。

> 判别实验（10 行代码即可确诊）：把 `v39_tramp_builder` 改成**只打日志、不碰参数**，若日志出现 `a=<代码地址> b=<对象>` 且不再崩，C1 成立。

**C2（同样致命，且与“场景残留实例”的描述吻合）——实例是预制体/已销毁对象，`Close()` 打空。**

`Resources.FindObjectsOfTypeAll` 会返回**预制体资产对象**和**上一条场景卸载后仅剩 C# 外壳**的对象。这类对象的 `titleHolder(0x40)` 虽然 `!= NULL`（C# 引用还在），但 `m_CachedPtr==0`：
- `Close` 第一句 `titleHolder.SetActive(false)` → native 空指针 → SIGSEGV；
- 若侥幸过了，`menuHolder/statsHolderLayout`（`0x20/0x30`）的 `childCount` 一样炸。
- 就算实例是活的但 `Awake` 没跑完（预制体），`0x58..0x90` 预制体全 null → `Instantiate(null)` → 托管 NRE → 从 native 帧返回失败 → 进程终止。

**C3（不致命但会埋雷）——`+0x28` 没赋值。**
v39 只写了 `0x10/0x18/0x40`，`0x28` 留 0。按本 build 的布局 `0x28` 是 `target`，**为 0 恰好等于“静态方法”语义，安全**；但如果哪天走到 `Delegate.Target`/反射路径就会 NRE。另外 `Func<bool>` 的 stub `v39_tramp_bool` 返回 `int32 1` 是**对的**（`w0`）。

### 4.3 修正后的委托构造（可直接替换 `v39_make_delegate`）

```c
/* 一次搞定：klass 直接取自 Open 的形参类型，版本无关、不写死混淆名 */
static void* make_delegate_from_param(void* methodInfo, int idx, void* stub)
{
    void* type  = g_method_get_param(methodInfo, (uint32_t)idx);   // Il2CppType*
    void* klass = type ? g_class_from_type(type) : NULL;           // Func<bool> / Action<NOJ> / Action
    if (!klass) return NULL;

    void* d = g_object_new(klass);                                 // 注意：object_new 不清零 header? 实测为 0 域
    if (!d) return NULL;

    *(void**)((char*)d + 0x10) = stub;   // method_ptr
    *(void**)((char*)d + 0x18) = stub;   // ★ 调用点真正 blr 的槽
    *(void**)((char*)d + 0x28) = NULL;   // target / method 槽 → 作为“最后一个实参”被转发；NULL 最安全
    *(void**)((char*)d + 0x40) = NULL;   // ★ method_code → 作为“第一个实参”被转发；★ 必须是 0 或安全指针
    /* 0x30 delegate_trampoline / 0x38 extra_arg 保持 0 */

    g_gchandle_new(d, 0);                // ★ 必须扎根
    return d;
}
```

规则（写给后续所有版本）：
1. **`+0x40` 只允许 `0` 或一个“绝不会被当对象解引用”的哨兵值**；绝不要填自己的代码地址（v39 的错就错在这）。
2. **`+0x18` 必填**（唯一的执行入口）。
3. **`+0x10` 也填**（反射/其它调用点可能读它）。
4. **`+0x28` 填 `NULL`**（静态语义；若需要 target 语义，填一个**真实活对象**，绝不填代码地址）。
5. **务必 `il2cpp_gchandle_new`**。
6. **stub 永不信任参数**：见下。

```c
/* 安全取“哪个参数才是抽屉对象” */
static void* pick_panel(void* a, void* b)
{
    void* cand[2] = { a, b };
    for (int i = 0; i < 2; i++) {
        void* p = cand[i];
        if (!p) continue;
        if ((uintptr_t)p < 0x1000) continue;
        if ((uintptr_t)p >> 48)    continue;          // 不像用户态堆/映射地址
        if ((uintptr_t)p & 7)      continue;          // il2cpp 对象 8 字节对齐
        void* k = *(void**)p;
        if ((uintptr_t)k < 0x1000 || ((uintptr_t)k >> 48)) continue;
        if (g_panelCls && k != g_panelCls) {           // 类型对得上才算
            const char* kn = g_class_get_name(k);
            if (!kn || !strstr(kn, "NOJZIZCFSEXSUEEX")) continue;
        }
        return p;
    }
    return NULL;
}
/* 回调 stub（Action<NOJ>） */
static void stub_build(void* a, void* b, void* c, void* d)
{
    void* panel = pick_panel(a, b);        // a 可能是 0 或哨兵；b 才是 this
    if (!panel) { log("build: no panel"); return; }
    build_ui(panel);
}
/* Func<bool> stub：返回 1（允许打开/允许关闭） */
static int32_t stub_bool(void* a, void* b, void* c, void* d){ return 1; }
/* Action stub（按钮点击）：只置标志，绝不碰参数 */
static void stub_click(void* a, void* b, void* c, void* d){ g_pending = 1; }
```

### 4.4 正确的调用序列（"无任何实例" → 出窗）

```c
/* ===== 阶段 0：符号与类 ===== */
g_base   = dlopen("libil2cpp.so", RTLD_NOW);
g_domain = il2cpp_domain_get();
il2cpp_thread_attach(g_domain);                       // ★ 必须：主线程
coreImg  = il2cpp_assembly_get_image(
             il2cpp_domain_assembly_open(g_domain, "mscorlib"));   // 或 "__Internal"/CoreModule，按版本
clsNOJ   = il2cpp_class_from_name(coreImg_of_Assembly-CSharp, "", "NOJZIZCFSEXSUEEX");
             // 更稳：g_panelCls 反向查找（v30 已在用：拿 DrawFullButton/4 的 declaring type）

/* ===== 阶段 1：方法 ===== */
mOpen      = method_from_name(clsNOJ, "Open", 5);        // 0x18DD724
mDrawTitle = method_from_name(clsNOJ, "DrawTitle", 1);
mDrawText  = method_from_name(clsNOJ, "DrawText", 2);
mDrawFB    = method_from_name(clsNOJ, "DrawFullButton", 4);
mClose     = method_from_name(clsNOJ, "Close", 0);
openFn     = *(void**)mOpen;                             // MethodInfo->methodPointer（offset 0）
/* ★ 校验：把 openFn - g_base 与 0x18DD724 比对，版本不符立刻放弃（打印 RVA） */

/* ===== 阶段 2：实例（三条来源，优先级递减） ===== */
T0  活性校验(p): p && *(void**)p==g_panelCls && p->titleHolder(0x40) 的 Unity 对象有效
      （读 `GameObject` 的 m_CachedPtr：il2cpp 对象 +0x10 是 native 指针；为 0 即已销毁）
T1  Resources.FindObjectsOfTypeAll(typeof(NOJ)) → 逐个 T0 校验（遍历全部，别只取 [0]）
T2  若当前场景有 EHHQWVCRJRNEXEFX（dump.cs:51711，字段 drawer 0x20）→ 直接读 drawer
T3  若在 level2/设计器：hook Open 捕获 this（卖家做法）或监听游戏自己的 Stats 按钮

/* ===== 阶段 3：先做“零回调”探针（★ 关键诊断） ===== */
h = openFn(inst, NULL, NULL, 0 /*pushStack*/, NULL, NULL);
   // 若这里崩 → 实例是死的（C2），换实例，别浪费时间在委托上
   // 若返回非 NULL → 实例健康，再往下

/* ===== 阶段 4：委托 ===== */
dFunc = make_delegate_from_param(mOpen, 0, (void*)stub_bool);     // Func<bool>
dBuild= make_delegate_from_param(mOpen, 1, (void*)stub_build);    // Action<NOJ>
dAct  = make_delegate_from_param(mDrawFB, 2, (void*)stub_click);  // System.Action（按钮）

/* ===== 阶段 5：开窗（build 回调里画内容） ===== */
h = openFn(inst, dFunc, dBuild, 1, NULL, NULL);
   // pushStack=1 才会走 popupAnimation.Play() 的原生弹出动画

/* ===== 阶段 6：在 stub_build(panel) 里填内容（顺序照卖家） ===== */
settle();                                   // 不要在 build 里再开窗（会重入 Close）
DrawTitle(panel, "Multiplayer SFS");
DrawText (panel, 0, "Server: ...");
DrawText (panel, 1, "Status: ...");
DrawFullButton(panel, INT_MAX,   "Join Server", dJoin, 1);
DrawFullButton(panel, INT_MAX-1, "Back",        dBack, 1);
/* 输入框（原生观感）：克隆 TUXXAURTZSIFPXTZ，SetParent 到 panel->statsHolderGroup(0x30)，
   设 layoutPriority，读 get_Text() */

/* ===== 阶段 7：每帧 ===== */
if (g_pending) { g_pending = 0; refresh_panel(); }   // 卖家就是 fire_stub 置标志 → tick 里重建
```

**为什么阶段 3 的“零回调探针”是本报告最值钱的一条**：它把“实例问题”和“委托问题”彻底分开。v39 把两件事混在一次调用里，崩了以后无法判断是 `Close` 还是 `build`。

---

## 5. 风险清单

| 风险 | 触发条件 | 规避 |
|---|---|---|
| `Close()` 打空 | 实例是预制体/跨场景残留（`m_CachedPtr==0`） | T0 活性校验 + 阶段 3 探针 |
| 委托 ABI 误解 | 把 `*(del+0x40)` 当对象 | stub 只用 `pick_panel()`，永不裸解引用 |
| 委托被 GC | 未 gchandle | `il2cpp_gchandle_new(d,0)` |
| 托管异常穿透 native 帧 | 任一字段 null / 空列表 | 所有 Draw 前判空 + 托管异常只能“不发生”，不能 catch |
| 线程 | 非主线程调 Unity/il2cpp | 只在 `Update` hook（v39 已如此）里做 |
| 版本漂移 | 方法 RVA 变 | 运行期比对 `openFn - g_base == 0x18DD724`，不符则降级 R-A |
| 重入 | `build` 里又 `Open` | `[slot+0xD60]` 式“重建中”标志 |
| 主菜单无抽屉 | level0 无 `Stats Menu` | 走 R-A；R-B 仅在 level2 启用 |
| 克隆按钮带原监听 | `HKAZNXTEGNSSTQLF.onClick(0x68)` | 保留 + 轮询（v37 方案），或 `RemoveAllListeners` |
| `Resources.FindObjectsOfTypeAll` 返回预制体 | 只判 `!= NULL` | 遍历全部 + `scene.IsValid()` + `m_CachedPtr!=0` |

---

## 附录 A：关键地址速查

**卖家 `seller_libpatch.so`（off == VA）**

| 地址 | 内容 |
|---|---|
| `0xBC3F0` | PMP-MOD 初始化（解析 5 方法 + 装 5 hook） |
| `0xBC8F8` | 方法解析器 `(类名, 方法名, 参数个数) → methodPointer` |
| `0x137ABC` | **Open hook**（捕获 `this` 入 8 槽表） |
| `0x137F18` / `0x1380E0` / `0x138368` / `0x138460` | EFLOBBJGMGA / DrawTitle / Close / GetPreferredSize hooks |
| `0x137070` | 重建面板（`Close → 建行 → Open → 计数`) |
| `0x1372A4`–`0x1372D8` | **调 Open 的现场** |
| `0x17E0BC` | 建行（`DrawText/DrawButton` 序列） |
| `0x17E3DC` | 委托工厂（`0x17E8E0`–`0x17EE94`：object_new + 字段扫描 + gchandle） |
| `0x17F240` / `0x17F2B8` | `DrawFullButton` 调用（prio `INT_MAX` / `INT_MAX-1`） |
| `0x17F370` / `0x17F39C` | `fire_prev` / `fire_next` stub（置 `[0x3CEC30]=∓1`，返回 0） |
| `0x3C70F20` / `0x3C70F18` | 槽表基址 / 槽数（步长 `0xDA0`，上限 8） |
| `0x3CEC28` | 全局“当前面板实例” |
| `0x3CEC30` | 全局“待处理翻页 delta” |
| `0x3CEC48/50/58/60/68` | Open / EFLOBBJGMGA / DrawTitle / Close / GetPreferredSize 的 **trampoline** |

**目标 `.22` `libil2cpp.so`**

| RVA | 内容 |
|---|---|
| `0x18DD724` | `NOJ.Open(Func<bool>,Action<NOJ>,bool,Action,Action)` |
| `0x18DD820` | `Close(this)`（Open 第一句） |
| `0x18DD834`–`0x18DD840` | **委托调用：`blr [d+0x18]`，`x0=d[0x40]`, `x1=this`, `x2=d[0x28]`** |
| `0x18DD704` | `OBCGMKLLJLB()` = `Func<bool>` 尾调用 |
| `0x18DC888`–`0x18DC89C` | `Close` 里 `Action(0xB0)` 的调用点（同为该 ABI） |
| `0x18DC824` | `Close()` |
| `0x18DD5D4` / `0x18DDC08` | `DrawTitle` / `DrawText` |
| `0x18DDCF8` / `0x18DDE48` | `DrawFullButton` 4 参 / 6 参 |
| `0x18DD1D0` / `0x18DD374` | `DrawButton` 5 参 / 7 参 |
| `0x1859FA0` | `NQEUSBNLKGCFHQVR.Open()`（SFS.UI 通用窗口，0 参，压 UI 栈 + 动画） |
| `0x1671010` | `FHBOAFMLNDC..ctor(Action, ref Action)`（面板句柄） |

**`.22` 类/字段（dump.cs 行号）**

| 行 | 内容 |
|---|---|
| 21261 | `public class NOJZIZCFSEXSUEEX : MonoBehaviour`（字段 0x20–0xB8） |
| 21336 | `Open(...)` 5 参 |
| 21350 / 21362 | `DrawFullButton` 4 / 6 参 |
| 1063 | `FHBOAFMLNDC`（`bool 0x10`, `Action 0x18`） |
| 8822 / 8845 | `NQEUSBNLKGCFHQVR` / `Open()` `0x1859FA0` |
| 25117 | `NANBAJRHGSVEQBTQ : PKVTCUBZSQHIVNVO`，**`static main`（0x0）**、`elementsHolder 0x38`、`elementPrefab 0x40`、`DrawUI()` |
| 5922 | `LKSRSZGMFKUEATEP : PKVTCUBZSQHIVNVO`（新建世界窗），**`worldNameField 0x48`** |
| 20505 | `TUXXAURTZSIFPXTZ`（文本输入包装），`TMProTextBox 0x28`，`get_Text/set_Text`，自带安卓软键盘 |
| 37463 | `HKAZNXTEGNSSTQLF`（原生按钮），**`layoutPriority 0x3C`**、`onClick 0x68` |
| 51711 | `EHHQWVCRJRNEXEFX`（`SFS.UI`），**`drawer 0x20`** ← 拿 `NOJ` 实例的捷径 |

**场景内容证据（`D:/SFS-Pro/work/assets/`）**

```
"Stats Menu"   → level2 only          ← 主菜单没有抽屉
"Mods Menu"    → level0.split0        ← 主菜单有完整窗体可克隆（R-A）
"World List"   → level0.split0
"Create World" → level0.split0 / 34e562…(prefab)
```

---

## 附录 B：给下一版的“先做这个”清单

1. `native_window_test()` 开头加：`log("openFn rva=0x%llx", openFn-g_base)` —— 必须是 `0x18DD724`。
2. 先跑 **零回调探针** `openFn(inst,NULL,NULL,0,NULL,NULL)`，结果写日志（`NULL` = 实例死，别继续）。
3. `v39_tramp_builder` 第一行改成 `log("builder a=%p b=%p c=%p d=%p")`，**然后再决定**取哪个参数。
4. `v39_make_delegate` 的 `+0x40` **改填 0**（不是 `fn`），`+0x28` 填 0，加 `il2cpp_gchandle_new`。
5. 主菜单（`read_mode()!=5/6` 时）默认走 R-A（克隆 `"Mods Menu"` 窗根），不要去 `FindObjectsOfTypeAll(NOJ)`。
6. 进 level2/飞行后再切 R-B；切换点用 `FindObjectsOfTypeAll(NOJ)` 计数 > 0 且通过 `m_CachedPtr` 校验。
