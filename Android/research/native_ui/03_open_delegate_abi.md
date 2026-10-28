# 03 · `NOJZIZCFSEXSUEEX.Open` 与委托 ABI（SFS Pro 1.6.00.22 / arm64）

> 目标：用静态分析定死四件事，给 level2 注入（libsfsmp）一条**确定能跑通**的 `Open + 委托` 调用序列：
> 1. `Open(inst, NULL, NULL, false, NULL, NULL)`（零委托）是否合法；
> 2. 本 build 里委托字段的**确切偏移**；
> 3. 回调里拿到的对象在**哪个寄存器**，stub 怎么取才安全；
> 4. 最终调用序列 + 每步预期日志。
>
> 所有结论均给出**可复现的证据**（dump.cs 行号 / RVA / 反汇编片段 / 字面量地址），不做“应该”“大概”。

分析基线：
- `libil2cpp.so`：`D:/SFS-Pro/work/libil2cpp.so`（1.6.00.22，SFS Pro）
- `dump.cs`：`D:/sfs-tools/apk/dump-official/dump.cs`（597338 行）
- 反汇编脚本：`D:/SFS-Pro/work/anndis.py`（capstone + pyelftools，按 section 做 RVA→offset）
- 现状源码：`D:/sfs-tools/so/src/v43.c`（`noj_open()` / `make_del()` / `stub_noj_build()`）

---

## 0. 结论速览

| 问题 | 结论 | 关键证据 |
|---|---|---|
| 零委托 `Open(inst,NULL,NULL,false,NULL,NULL)` 合法？ | **不合法**。`arg2`（`Action<NOJ>`）为 NULL 直接走 NRE 分支，函数体一行都不执行 | `0x18DD824 cbz x24, #0x18dda68` → `0x18DDA68 bl 0x1572a9c`（= NullReferenceException raiser） |
| `arg1`(Func<bool>) / `arg4` / `arg5` 可为 NULL？ | **可以**。游戏自己就是这么调的 | 调用点 `0x18B13B4`：`w3=0`、`x4=0`、`x5=0` |
| `+0x18` | `invoke_impl`：**被 `blr` 的真实入口**，NULL → `blr 0` 立刻 SIGSEGV（调用点前不做非空判断） | `0x18DD834/0x18DD840` |
| `+0x20` | `m_target`（真正的方法接收者） | `Action::.ctor` 的 `str x1,[x0,#0x20]!` |
| `+0x28` | `method` = **MethodInfo\***（不是 target！） | `Action::.ctor` 的 `str x2,[x0,#0x28]` |
| `+0x40` | 传给 `invoke_impl` 的 **x0（“__this”上下文槽）**；真实委托里填的是“委托自身”或 `m_target`，**绝不是代码地址** | `Action::.ctor` 的 `str x19,[x19,#0x40]` |
| 回调里的 panel 在哪个寄存器？ | **x1**，等于 `Open` 的 `this`（就是我们传进去的 `inst`），不是新对象 | `0x18DD82C mov x1, x19` + `0x18DD840 blr x8` |
| `Open` 返回值 | 新建的 `FHBOAFMLNDC{bool @0x10, Action @0x18}`（关闭句柄），在函数**最后**才构造 | `0x18DDBDC bl FHBOAFMLNDC$$.ctor` |

统一调用约定（本 build，全部委托一致，已用 6 个独立调用点验证）：

```
invoke_impl( x0 = del[0x40],  x1..xN = 用户参数,  xN+1 = del[0x28](MethodInfo*) )
返回值在 w0/x0。
```

`Action<T>`（1 参）→ `(x0=+0x40, x1=参数, x2=+0x28)`
`Action` / `Func<bool>`（0 参）→ `(x0=+0x40, x1=+0x28)`  ← 注意：**0 参委托的 x1 是 MethodInfo\*，不是对象**，这就是第 5 个崩溃点。

---

## 1. 委托字段布局（证据链）

### 1.1 dump.cs 的字段表（`System.Delegate`，dump.cs:134563）

```
134566  private IntPtr method_ptr;          // 0x10
134567  private IntPtr invoke_impl;         // 0x18
134568  private object m_target;            // 0x20
134569  private IntPtr method;              // 0x28
134570  private IntPtr delegate_trampoline; // 0x30
134571  private IntPtr extra_arg;           // 0x38
134572  private IntPtr method_code;         // 0x40
134573  private IntPtr interp_method;       // 0x48
134574  private IntPtr interp_invoke_impl;  // 0x50
134575  private MethodInfo method_info;     // 0x58
134576  private MethodInfo original_method_info; // 0x60
134577  private DelegateData data;          // 0x68
134578  private bool method_is_virtual;     // 0x70
```

`MulticastDelegate` 只多一个字段（dump.cs:135226）：`Delegate[] delegates; // 0x78`。
`Action` / `Action<T>` / `Func<TResult>` 全部是 `sealed ... : MulticastDelegate` 且**没有任何自有字段**（dump.cs:2262424 / 2262731 / 2306559）—— 所以三种委托**共用同一套偏移**，`Action<NOJ>` 与 `Action` 布局完全一致，只有 `Invoke` 的签名不同。

### 1.2 铁证：`System.Action::.ctor`（0x291CDC4）自己写了哪些偏移

这是托管 `Action(object target, IntPtr method)` 的编译结果。x0=this(委托对象)、x1=target、x2=MethodInfo\*：

```
0x291CDD0  ldr  x8, [x2, #8]        ; x8 = method->methodPointer（MethodInfo+0x8）
0x291CDE0  str  x2, [x0, #0x28]     ; +0x28 = MethodInfo*        ← 不是 target
0x291CDE4  str  x8, [x0, #0x10]     ; +0x10 = method_ptr
0x291CDE8  str  x1, [x0, #0x20]!    ; +0x20 = m_target（写屏障 0x15727a0）
0x291CDF8  str  x19,[x19, #0x40]    ; +0x40 = 委托自身！        ← 不是“代码地址”
0x291CDF0  ldrb w22, [x21, #0x52]   ; MethodInfo+0x52（虚方法标记）
0x291CE04  cbnz w22, #0x291CE1C     ; 分支选择
0x291CE08  x8 = 0x153565C
0x291CE10  str  x8, [x19, #0x18]    ; 分支A: +0x18 = 通用调用器 thunk 0x153565C
0x291CE18  cbz  x20, #0x291CE48     ; 分支B: target==NULL → 抛异常
0x291CE24  str  x8, [x19, #0x18]    ; 分支B: +0x18 = method_ptr（直接打方法本体）
0x291CE28  str  x9, [x19, #0x40]    ; 分支B: +0x40 = m_target
0x291CE34  str  x8(0x153561C), [x19, #0x38]  ; extra_arg = 另一个调用器变体
```

两个分支给出**同一个可观察 ABI**：

| 分支 | `+0x18` 是什么 | `+0x40` 是什么 | 语义 |
|---|---|---|---|
| A（通用路径） | 0x153565C 通用 invoker | 委托自身 | invoker 从 `del->method_ptr`/`del->m_target` 取出真正的函数和 this |
| B（直连路径） | 方法本体地址 | `m_target` | 方法本体直接以 `x0 = this` 被调用 |

两条路径下，`invoke_impl` 收到的 **x0 都是“__this 上下文”**：要么是委托自己（A），要么是目标对象（B）。这就是为什么 `Open` 里写的是 `ldr x0,[del,#0x40]`——它实现的是标准 il2cpp 委托调用序列：

```c
r = ((InvokerMethod)del->invoke_impl)(del->[0x40] /*__this*/, arg1..argN, del->[0x28] /*method*/);
```

**这解释了 v39 的 SIGSEGV**：v39 把 `+0x40` 填成了 `v39_tramp_builder` 的代码地址，于是 stub 的 x0 是一段机器码；stub 里 `il2cpp_object_get_class(x0)` 读 `*(void**)x0`（函数头 8 字节机器码）当 klass，再 `il2cpp_class_get_name()` 解引用 → 必崩。**不只是“+0x40 不能填代码”这么表层，而是“+0x40 的语义是 __this，任何会被当对象解引用的值都不行”**。

### 1.3 交叉验证：`Open` 3 处、`Close` 1 处、`OBCGMKLLJLB` 1 处，共 5 处同构

```
[Open  builder 回调]  0x18DD834  ldr x8,[x24,#0x18] ; 0x18DD838 ldr x0,[x24,#0x40]
                      0x18DD82C  mov x1,x19(this)  ; 0x18DD83C ldr x2,[x24,#0x28] ; 0x18DD840 blr x8
[Open  list 元素]     0x18DDA4C  ldr x9,[x8,#0x18] ; 0x18DDA50 ldr x0,[x8,#0x40]
                      0x18DDA54  ldr x1,[x8,#0x28] ; 0x18DDA58 blr x9
[Open  arg4(Action)]  0x18DDB34  ldr x8,[x21,#0x18] ; 0x18DDB38 ldr x0,[x21,#0x40]
                      0x18DDB3C  ldr x1,[x21,#0x28] ; 0x18DDB40 blr x8
[Close 字段0xB0]      0x18DC890  ldr x9,[x8,#0x18] ; 0x18DC894 ldr x0,[x8,#0x40]
                      0x18DC898  ldr x1,[x8,#0x28] ; 0x18DC89C blr x9
[OBCGMKLLJLB 0xA8]    0x18DD70C  ldr x0,[x8,#0x40] ; 0x18DD710 ldr x1,[x8,#0x28]
                      0x18DD714  ldr x2,[x8,#0x18] ; 0x18DD718 br x2
```

5 处完全一致，可排除“某个点被编译器特化”的可能。最后一处还额外证明：`Func<bool>` 的返回值走 **w0**（`0x18DD71C mov w0,#1` 是“谓词为空”时的默认返回）。

> **对既有资料的修正**：任务书里写的“`+0x28`=target、`+0x40`=method_code”是**反的**。
> `+0x28` 是 `MethodInfo*`（每个调用点都把它当最后一个参数转发，指向元数据区，不是对象）；
> `+0x40` 是“__this 上下文槽”，运行时值是委托自身或 `m_target`。
> `method_code` 只是 Il2CppDumper 的字段名，**不代表这里放代码地址**。

---

## 2. `Open` 的逐参数 NULL 处理路径

### 2.1 函数骨架（0x18DD724–0x18DDBEC，`Offset` 0x18D9724）

参数映射（AAPCS64）：`x0=this`，`x1=Func<bool>`，`x2=Action<NOJ>`，`w3=bool`，`x4=Action`，`x5=Action`。
序言把它们落到：`x23=arg1`、`x24=arg2`、`[sp,#0xc]=arg3`、`x21=arg4`、`[sp]=arg5`（`0x18DD748–0x18DD760`）。

```
0x18DD818  mov  x0, x19
0x18DD81C  strb wzr, [x19, #0xb8]        ; this.CEKDEFIEIOJ = false   ← 第1个“实例必须有效”的点
0x18DD820  bl   #0x18dc824               ; Close(this)               ← 第2个点（破坏性副作用！）
0x18DD824  cbz  x24, #0x18dda68          ; ★ arg2==NULL → NRE，后面一行都不执行
0x18DD828  x20 = [Action 的 klass]
0x18DD82C  mov  x1, x19                  ; ★ x1 = this（= 我们传的 inst）
0x18DD834  ldr  x8, [x24, #0x18]         ; invoke_impl
0x18DD838  ldr  x0, [x24, #0x40]         ; __this
0x18DD83C  ldr  x2, [x24, #0x28]         ; MethodInfo*
0x18DD840  blr  x8                       ; ← 我们的 builder 回调在此进入
0x18DD848  x24 = [x19, #0x98]            ; 绘制队列 List<NAABCLLJEKA>
0x18DD8D8  bl   List<object>$$Sort       ; 按 entry.sortKey 排序
0x18DD908..0x18DDA64  遍历队列（见 2.4）
0x18DDA6C  内联 List.Clear               ; 队列被“消费”掉
0x18DDA94  this+0xa8 = arg1              ; 存 Func<bool>，无空判断
0x18DDAAC  [this+0x20](menuHolder).gameObject.SetActive(true)  ← 第3个点
0x18DDAD0  if (arg3 & 1) popupAnimation.SkipAnimation()        ← arg3=true 时跳过动画
0x18DDAEC  ForceRebuildLayoutImmediate(FMGCFCIBBIP$$MJFDPEGEFBD(this,0))
0x18DDB1C  [this+0x38](rootElement) 虚调用 [x8+0x188]           ← 第4个点
0x18DDB30  if (arg4) arg4()                                    ← arg4==NULL 安全
0x18DDB48  this+0xb0 = Delegate.Combine(this+0xb0, arg5) + 类型检查（0x18DDB60–0x18DDB94）
0x18DDBDC  new FHBOAFMLNDC(Action(this,<方法>), this+0xb0) → x0 返回
```

### 2.2 每个参数能不能为 NULL

| 参数 | NULL 允许？ | 依据 |
|---|---|---|
| `arg1` Func<bool> | **允许**（Open 内无空判断，直接存 0xA8） | `0x18DDA94 str x23,[x24,#0xa8]!` |
| `arg2` Action<NOJ> | **禁止** → NullReferenceException | `0x18DD824 cbz x24,#0x18dda68` |
| `arg3` bool | 无所谓（`false` 不跳动画，`true` 立刻 SkipAnimation） | `0x18DDACC ldr w8,[sp,#0xc]; tbz w8,#0` |
| `arg4` Action | **允许** | `0x18DDB30 cbz x21` 跳过整段 |
| `arg5` Action | **允许** | `0x18DDB48 ldr x1,[sp]` → `Delegate.Combine(0xB0, NULL)` 返回原值 |

### 2.3 “零委托探针”为什么是死路（而且有害）

**证据链（三层，逐级落地）**：

1. `0x18DD824 cbz x24, #0x18dda68` —— arg2 为 NULL 时跳到 `0x18DDA68`。
2. `0x18DDA68` 只有 **8 字节**（`0x18DDA6C` 已经是下一段 `List.Clear` 内联，且它要求 `x0`=队列，而此刻 `x0` 是 `Close()` 的残留返回值）：

   ```
   0x18DDA68  bl #0x1572a9c
   0x18DDA6C  ldr w8,[x0,#0x1c]     ; List.version
   ```
3. `0x1572A9C` = `str x30,[sp,#-0x10]! ; bl 0x15b95d4`，而 `0x15b95d4 → 0x15b95e8 → 0x15b95f8`：

   ```
   0x15B95FC  mov  x19, x0
   0x15B9604  adrp x1, #0x8c3000 ; add x1,x1,#0xe7e   → 0x8C3E7E = "System"
   0x15B960C  adrp x2, #0x8c5000 ; add x2,x2,#0xf5    → 0x8C50F5 = "NullReferenceException"
   0x15B9614  mov  x3, x19
   0x15B961C  b    #0x15b9ed0                          ; raise(class, msg)
   ```
   → `0x1572a9c` 就是本 build 的**共享 NRE raiser**（被 `Close` 的 `0x18DCA00`、`DrawTitle` 的 `0x18DD614`、`DrawFullButton` 的 `0x18DDE3C` 同样使用，可交叉验证）。

**结论：`Open(inst, NULL, NULL, false, NULL, NULL)` 在 `0x18DD824` 抛 `NullReferenceException`，函数体（含返回值构造）完全不执行。**

**而且它比“无效探针”更糟**：NRE 发生在 `0x18DD820 bl Close(this)` **之后**。也就是说这个“只读探针”会先把 `menuHolder`/`titleHolder` 关掉、清空按钮列表 `0xA0`、清空 `0xB0` 回调 —— 它**有破坏性副作用**。把它当“无害诊断”是错的，`v43.c` 的 mode 8（`noj_open(0)`）必须删掉或改成只做 `FindObjectsOfType` 计数。

（补充：因为走的是 `il2cpp_runtime_invoke`，NRE 会被捕获进 `exc` 而不是 SIGSEGV，所以日志会看到 `Open returned 0 exc=0x...` —— 这个“看起来没崩”的结果极易被误读成“零委托可行”。）

### 2.4 游戏自己的两个调用点（ground truth）

`Open` 在本 build 只有两个 BL 交叉引用：`0x18B13B4`、`0x18B1574`。两处结构完全一致：

```
0x18B138C  bl System.Action<object>$$.ctor     ; x1 = x21(闭包对象), x2 = [0x3455230](MethodInfo)
0x18B1394  cbz x20, #0x18b1598                 ; ★ inst 先判空（NULL→NRE），再调 Open
0x18B1398  mov x0, x20                         ; inst
0x18B139C  mov x1, x22                         ; Func<bool>（真实委托，构造于 0x18B1360）
0x18B13A0  mov x2, x23                         ; Action<NOJ>（真实委托）
0x18B13A4  mov w3, wzr                         ; ★ bool = FALSE
0x18B13A8  mov x4, xzr                         ; ★ Action = NULL
0x18B13AC  mov x5, xzr                         ; ★ Action = NULL
0x18B13B4  bl  NOJZIZCFSEXSUEEX$$Open
```

可以直接抄的结论：
1. **`arg4`/`arg5` 传 NULL 是游戏官方用法**（两处都是 0）；
2. 游戏**从不**传 NULL 的 `arg2`（这跟 2.3 的 NRE 分支互相印证）；
3. 游戏传 `arg3 = false`（不跳动画）；卖家传 `true` 也成立，只是多调一次 `SkipAnimation`；
4. 游戏用**真实委托**（`Action<object>$$.ctor` 现造），不手搓字段 —— 我们因为要注入自己的代码只能手搓，但必须模仿 ctor 写出的**同一个形状**。

### 2.5 绘制队列 `0x98` 的语义（决定了“在回调里画”才是对的）

`0x18DD848` 起，`Open` 会把 `this.CHNLEMLKDNN`（`List<NOJZIZCFSEXSUEEX.NAABCLLJEKA>`，字段 0x98）
排序 → 逐条展开 → 最后 `List.Clear` **消费掉**。队列元素（dump.cs:20899）：

```
NAABCLLJEKA { int MAAHKIADEIE @0x10; int OAJBKNOLNNE @0x14; Action KCJKKAHFLII @0x18; bool HFKFBPKBEFE @0x20; }
```

循环体（`0x18DD908`–`0x18DDA64`）：

```
0x18DD90C  if (i >= list._size) goto 清空
0x18DD91C  x25 = list[i]
0x18DD924  if (!entry.HFKFBPKBEFE /*+0x20*/) goto 0x18DDA44
           ... enabled 分支：IKGDLCDHIOC / FMGCFCIBBIP / sizeDelta / spacing → 实例化一行
0x18DDA44  （+0x20==0 分支）x8 = entry.KCJKKAHFLII(+0x18)
0x18DDA48  cbz x8 → NRE        ← ★ +0x20==0 的条目，其 Action 不能为 NULL
0x18DDA4C  invoke_impl(x0=+0x40, x1=+0x28)
```

**推论（很关键）**：
- `Draw*` 系列不是立刻建 UI，而是**入队**；真正实例化由 `Open` 的循环完成。所以**必须在回调里画**——`Open` 返回之后再画，条目只会躺在队列里等下一次 `Open`。
- 顺序天然正确：`Close()`（0x18DD820）→ 回调入队（0x18DD840）→ 排序/展开（0x18DD908）。
- 回调里**不要**再调 `Open`/`Close`（会把正在使用的队列清空 → 循环读到 size=0 或越界）。

---

## 3. 回调参数到底在哪个寄存器

`Action<NOJ>` 是 1 参委托，按 §1.3 的约定调用形式是 `invoke_impl(x0=+0x40, x1=参数1, x2=+0x28)`。

`0x18DD82C mov x1, x19` 明确把 **`this`（= 我们传入的 `inst`）放进 x1**，然后 `0x18DD840 blr x8`。
所以：

| 槽 | 运行时实际值 | 我们的 stub 该怎么对待 |
|---|---|---|
| x0 | `*(del+0x40)` = 我们填的值（NULL / 委托自身） | **绝不解引用**，直接丢弃 |
| **x1** | **`Open` 的 `this` = `inst`（NOJ 实例）** | **唯一可信的对象来源** |
| x2 | `*(del+0x28)` = MethodInfo\*（元数据区地址） | 绝不解引用 |
| x3.. | 垃圾（未定义） | 绝不使用 |

三个要点：

1. **回调参数不是“面板”，就是 `inst` 本身**。`Open` 并不把新对象交给回调 —— `Action<NOJ>` 的语义是“把你自己交回来，让我填充内容”。所以回调里做的事等价于 `this.DrawTitle(...)`，`panel == inst`。
2. **委托的 `m_target`（+0x20）被 `Open` 完全忽略**：它不用 `del->m_target` 当参数，而是固定传 `this`。所以 `m_target` 留 NULL 没有任何影响（`Delegate.Target` 返回 null 而已）。
3. **0 参委托的 x1 是 MethodInfo\***（§1.3 的 site B/C）。所以 `stub_noj_build(a,b,c,d)` 这种“把 x1 当对象”的 stub **只能**挂在 `Action<NOJ>` 这个槽上。若哪天把它挂到 `arg4`/`arg5`（`Action`），x1 就是元数据指针，`il2cpp_object_get_class` 立刻崩 —— 这是 v43 潜在的第 5 个崩溃点（目前靠“arg4/arg5 一律传 NULL”规避）。

`v43.c` 的 `noj_pick(a,b)` 之所以“能用”，是因为它优先试 `b`（=x1=inst）且带 klass 名校验；但它把 `a` 也当候选是**危险的多余动作**。正确写法是只认 x1，并且用 **klass 指针相等** 判断（比对 `il2cpp_class_from_name` 得到的 `NOJZIZCFSEXSUEEX` klass），不要用字符串 `strstr`（每次回调都 `il2cpp_class_get_name` 有分配/开销，且名字可能带命名空间前缀）。

---

## 3.5 回调里该画什么、怎么画（与 v43 现状的差异）

`v43.c` 的 `noj_draw_all()` 里三处调用参数不对，静态上可以确认：

| 调用 | 本 build 真实签名（dump.cs） | v43 现状 | 结果 |
|---|---|---|---|
| `DrawTitle` | `DrawTitle(string)` @0x18DD5D4 | `p[1]={s1}` ✔ | OK |
| `DrawText` | `DrawText(int, string)` @0x18DDC08 | `p[2]={&idx,s1}` ✔ | OK |
| `DrawFullButton` | `DrawFullButton(int, string, Action, bool)` @0x18DDCF8 | `p[4]={&idx,s1,NULL,&one}` | 参数**个数正确**（4），但第 3 个 `Action=NULL` → 按钮渲染出来但点了没反应（不崩，只“哑”） |

`DrawFullButton` 内部（0x18DDCF8）会把 `(string,bool)` 打包成闭包 `NOJ.OMFJJKFFGNI{string @0x10, bool @0x18}`，再用
`Func<object>$$.ctor` / `Func<bool>$$.ctor`（0x21241CC / 0x2123AEC）**现造真实委托**，
然后尾调用真正的实现 `0x18DDE48`。这段代码证明了两件事：
1. 本 build 的委托**必须**用「真实 ctor」或「完全同形状的手搓」两种方式之一产生；
2. `bool` 参数在闭包里是**1 字节**（`strb w23,[x24,#0x18]`）—— 我们手搓委托时不要往 +0x18 之外乱写。

**结论：`DrawFullButton` 的签名是 4 参**，`p[3]={&idx, s1, action, &one}`，其中 `action` 必须是一个
**真实可调用的 `Action`**（最省事的做法：`lambda` 不行，就传 `NULL` 并在按钮文案上写死“暂不可用”，
或者用 `DrawText` 代替按钮）。**不做**“传 NULL 假装有按钮”。

---

## 4. 崩溃点全表

| # | 崩溃点 | 触发条件 | 现象 | 依据 |
|---|---|---|---|---|
| 1 | `blr 0` | 交给 `Open` 的委托 `+0x18 == 0` | PC=0x0 SIGSEGV，就在 `Open` 里 | `0x18DD834 ldr x8,[x24,#0x18]` 后**无** `cbz x8`，直接 `blr x8` |
| 2 | stub 解引用 x0 | `+0x40` 被填成代码地址（v39） | SIGSEGV 在读 `*(void**)x0` | §1.2/§1.3 |
| 3 | 零委托 | `arg2 == NULL` | NRE（被 runtime_invoke 捕获成 `exc`），且 `Close()` 已执行 | `0x18DD824 cbz x24,#0x18dda68` |
| 4 | `inst` 无效（预制体/已销毁） | `Close()` 里 `[this+0x40](titleHolder)==NULL` → NRE；若为已销毁 Unity 对象 → `SetActive` 打空 | 无 BUILDER 日志，直接崩/异常 | `0x18DC870 ldr x0,[x19,#0x40]; cbz → 0x18DCA00(NRE)` |
| 5 | 0 参委托槽误用同款 stub | 把 `stub_noj_build` 挂到 `arg4/arg5`（`Action`） | x1 = MethodInfo\* → `il2cpp_object_get_class` 崩 | §1.3 site B/C |
| 6 | 队列里有 `Action==NULL` 且 `+0x20==0` 的条目 | 手工/异常构造 `NAABCLLJEKA` | Open 循环里 NRE | `0x18DDA48 cbz x8 → NRE` |
| 7 | `this+0x98`（队列 List）为 NULL | 实例未跑 `Awake` | `List.Sort` 内 NRE | `0x18DD848 ldr x24,[x19,#0x98]` 后 `0x18DD8C0 cbz x24 → NRE` |

> 1 和 2 是 v39 的两个独立死因；3 是“零委托探针”的死因；4 是“捞到预制体”的死因。
> **注意 #3 的副作用**：零委托调用会先执行 `Close()`，把面板关掉 —— 别把它当无害探针。

---

## 5. 最终推荐调用序列

### 5.1 前置校验（一次，缓存）

```c
/* (1) 定位类：NOJZIZCFSEXSUEEX（dump.cs:21261, TypeDefIndex 579） */
cls = il2cpp_class_from_name(coreImage, "", "NOJZIZCFSEXSUEEX");
if (!cls) { log("v44: NOJ class not found"); return; }

/* (2) 定位方法并校验 RVA —— 版本漂移的第一道闸 */
mOpen = il2cpp_class_get_method_from_name(cls, "Open", 5);
if (!mOpen) { log("v44: Open(5) not found"); return; }
openFn = method_get_pointer(mOpen);
if ((uintptr_t)openFn - base != 0x18DD724) {
    log("v44: *** RVA MISMATCH openFn-base=0x%llx (expect 0x18dd724) —— 放弃 ***");
    return;                      /* 绝不在未知版本上猜 ABI */
}
/* 同理校验 Close=0x18DC824 / DrawTitle=0x18DD5D4 / DrawText=0x18DDC08 / DrawFullButton=0x18DDCF8 */
```

### 5.2 造委托（**必须**严格照抄 ctor 的形状）

```c
/* 只写必要槽位；+0x40 填“委托自身”（= Action::.ctor 的 0x291CDF8 行为），永不填代码地址 */
static void* noj_make_del(void* delClass, void* stub)
{
    void* d = il2cpp_object_new(delClass);         /* 大小按 klass 分配，≥0x80（含 delegates@0x78） */
    if (!d) return NULL;
    *(void**)((char*)d + 0x10) = stub;   /* method_ptr  ┐ 两个都指向 stub：
    *(void**)((char*)d + 0x18) = stub;   /* invoke_impl ┘ 无论走“直连”还是“通用”形状都一致 */
    *(void**)((char*)d + 0x20) = NULL;   /* m_target = NULL（静态语义；Open 完全不读它） */
    *(void**)((char*)d + 0x28) = NULL;   /* method(MethodInfo*) = NULL → 任何“当 MethodInfo 解引用”的路径都不会被走到 */
    *(void**)((char*)d + 0x30) = NULL;   /* delegate_trampoline */
    *(void**)((char*)d + 0x38) = NULL;   /* extra_arg */
    *(void**)((char*)d + 0x40) = d;      /* ★ __this 槽 = 委托自身（x0 是有效对象，绝不给代码/NULL 以外的怪值） */
    *(void**)((char*)d + 0x48) = NULL;   /* interp_method */
    *(void**)((char*)d + 0x78) = NULL;   /* MulticastDelegate.delegates */
    il2cpp_gchandle_new(d, 0);           /* 保活；缓存复用，不要每次 free */
    return d;
}
```

> 为什么 `+0x40 = d` 而不是 `NULL`：两者对我们自己的 stub 等价（stub 丢弃 x0），但 `d` 是**有效对象**，
> 万一后续有人把 x0 传给 `il2cpp_object_get_class`，`d` 不崩、`NULL` 会崩。**默认选更安全的那个**。

### 5.3 两个 stub（签名必须与“0 参/1 参”严格对应）

```c
/* Action<NOJ>：1 参 → (__this, arg, method) */
static void noj_stub_builder(void* self_ctx, void* arg_panel, void* method_info)
{
    (void)self_ctx; (void)method_info;            /* 这两个绝不解引用 */
    if (g_noj_in_cb) return;                       /* 防重入 */
    g_noj_in_cb = 1;

    /* arg_panel == Open 的 this == 我们传进去的 inst */
    if (!arg_panel || ((uintptr_t)arg_panel & 7)) { log("v44: BUILDER bad arg"); goto out; }

    void* k = *(void**)arg_panel;                  /* 只信任 klass 指针相等 */
    if (k != g_nojCls) { log("v44: BUILDER klass mismatch %p", k); goto out; }

    g_noj_panel = arg_panel;                       /* == inst */
    noj_draw_all(arg_panel);                       /* 只入队，不递归 Open/Close */
out:
    g_noj_in_cb = 0;
}

/* Func<bool>：0 参 → (__this, method)，返回值在 w0。
   返回 1 与“JEEIJNBOEDF 为 NULL 时的默认值”完全一致（见 0x18DD71C），是最保守的选择。 */
static int32_t noj_stub_true(void* self_ctx, void* method_info) { (void)self_ctx; (void)method_info; return 1; }
```

> 若日志显示“面板刚出现就自己关了”，把 `noj_stub_true` 的返回值改成 0 再试一次——
> 这是唯一一个语义方向不由静态分析决定（`OBCGMKLLJLB` 的调用者在别处）的点，**用日志判定**。

### 5.4 调用

```c
void* fb = noj_make_del(kFuncBool, (void*)noj_stub_true);      /* arg1: Func<bool> 的真实 klass */
void* an = noj_make_del(kActionNOJ, (void*)noj_stub_builder);  /* arg2: Action<NOJ> 的真实 klass */
if (!fb || !an) { log("v44: delegate build failed"); return; }

int8_t flag = 1;                 /* arg3: true = 跳过展开动画（卖家用法）；false 也可以（游戏用法） */
void* ip[5];
ip[0] = fb;                      /* Func<bool>            */
ip[1] = an;                      /* Action<NOJ>  ← 必填 */
ip[2] = &flag;                   /* bool（1 字节，用 int8_t 最贴） */
ip[3] = NULL;                    /* Action  ← NULL 合法（游戏就这么传） */
ip[4] = NULL;                    /* Action  ← NULL 合法 */
void* exc = NULL;
log("v44: calling Open inst=%p fb=%p an=%p flag=%d", inst, fb, an, flag);
void* handle = il2cpp_runtime_invoke(mOpen, inst, ip, &exc);
if (exc) { log("v44: Open EXC klass=%s", klass_name_of(exc)); return; }
if (!handle) { log("v44: Open returned NULL (no exception)"); return; }
log("v44: Open ok handle=%p panel=%p", handle, g_noj_panel);
```

### 5.5 每步预期日志（正常路径）

| 步骤 | 期望日志 | 说明 |
|---|---|---|
| 校验 | `v44: openFn rva=0x18dd724 ok` | 不是这个值 → 立刻停 |
| 找实例 | `v44: NOJ inst n=1 inst[0]=0x... 'StatsMenu(Clone)'` | `n==0` → 该场景没有抽屉，别调 |
| 造委托 | `v44: delegates fb=0x.. an=0x..` | 任一为 0 → 停 |
| 调用前 | `v44: calling Open inst=0x.. fb=0x.. an=0x.. flag=1` | 这行之后必须出现 BUILDER |
| **回调** | `v44: BUILDER arg=0x.. klass=NOJZIZCFSEXSUEEX ok` | `arg` 必须 **== inst**；不等说明 ABI 又漂了 |
| 入队 | `v44: queue title ok / text ok / button ok` | `Draw*` 返回 void，靠返回值/`exc` 判定 |
| 返回 | `v44: Open ok handle=0x.. panel=0x..` | `handle` = `FHBOAFMLNDC*`，`panel == inst` |
| 下一帧 | `v44: panel active=1 children=N` | 用 `children` 增长确认真的实例化了行 |

失败路径的“指纹”（**这四行能一秒区分死因**）：

| 日志 | 死因 |
|---|---|
| `Open EXC klass=System.NullReferenceException` 且**没有** BUILDER 行 | `arg2` 传了 NULL（#3），或 `inst` 无效导致 `Close()` 崩（#4） |
| 有 `calling Open` 无任何后续（进程死/无返回） | `+0x18 == 0` → `blr 0`（#1），或 stub 解引用了 x0（#2） |
| `BUILDER bad arg` / `klass mismatch` | ABI 变了，或委托造的 klass 不对 |
| `Open ok` 但下一帧 `children==0` | `Draw*` 参数错（`DrawFullButton` 4 参），或队列在回调里被清空 |

---

## 6. 校验方法（怎么证明“真的成功”而不是“没崩”）

1. **RVA 断言**：`openFn - base == 0x18DD724`，`Close == 0x18DC824`，`DrawFullButton == 0x18DDCF8`。
   这四个值一起对上，才说明委托 ABI 也是本报告描述的这套。
2. **回参恒等断言**：回调里 `assert(arg_panel == inst)`。这是**最便宜也最强**的 ABI 自检——
   一旦某版本把参数改成从 `del->m_target` 取，这个断言立刻炸出来，而不是变成随机崩溃。
3. **委托自检**（造完立刻验，失败就别调）：
   ```c
   assert(*(void**)((char*)an+0x18) == (void*)noj_stub_builder);
   assert(*(void**)((char*)an+0x28) == NULL);
   assert(*(void**)((char*)an+0x40) == an);        /* __this 槽自指 */
   assert(il2cpp_object_get_class(an) == kActionNOJ);
   ```
4. **effect 校验**：`Open` 之后 `menuHolder`（inst+0x20）`.activeInHierarchy == true`，
   且 `menuHolder.childCount` 比调用前增加（每行一个子物体）。**只看“没崩”不算成功**。
5. **可逆性校验**：再调一次 `Open`（内部会先 `Close`），若第二次仍然 `Open ok` 且 `children` 不单调增长，
   说明队列被正确消费、没有泄漏残留。

---

## 7. 与 v43 现状的 diff 清单

| 项 | v43 现状 | 改成 |
|---|---|---|
| mode 8（零委托探针） | `noj_open(0)` → 传 NULL 的 `arg2` | **删除**。它必抛 NRE，且已先执行 `Close()` 破坏状态 |
| `make_del` 的 `+0x40` | `NULL` | `d`（自指，更安全；NULL 也行，stub 不看） |
| `make_del` 的 `+0x10` | `fn` ✔ | 保持 |
| `make_del` 的 `+0x28` | `NULL` ✔ | 保持（务必不要填 `MethodInfo` 或任何会被解引用的东西） |
| `stub_noj_build(a,b,c,d)` | 候选 `b,a` 都试 + `strstr` 名字 | 只认第 2 个参数（x1），用 **klass 指针相等** 判断；x0/x2/x3 一律丢弃 |
| `noj_pick` | 会去解引用 `a` 的 klass | 删掉对 `a` 的处理（`a` 是 `+0x40`，语义是 __this，不是 panel） |
| `DrawFullButton` 调用 | `p[4]={&idx,s1,NULL,&one}`（个数对） | 参数数不变，但第 3 个 `Action` 要传**真实可调用委托**（否则按钮点击无响应）；实在造不出就改用 `DrawText` |
| `arg3` | `1`（true，跳动画） | 保留 true；若怀疑动画路径，切 false 对照 |
| `arg4/arg5` | `NULL` ✔ | 保持 |
| 缓存 | 有 `g_delFuncBool` 缓存 ✔ | 保持，并在造完做 §6.3 自检 |

---

## 附录 A：本报告用到的地址表

| RVA | 符号 | 用途 |
|---|---|---|
| 0x18DD724 | `NOJZIZCFSEXSUEEX.Open(Func<bool>,Action<NOJ>,bool,Action,Action)` | 主目标 |
| 0x18DC824 | `NOJZIZCFSEXSUEEX.Close()` | Open 第一步调用 |
| 0x18DD704 | `NOJZIZCFSEXSUEEX.OBCGMKLLJLB()`（读 0xA8 的谓词） | 证明 0 参委托的 (x0,x1) 约定 |
| 0x18DD5D4 | `DrawTitle(string)` | 回调里画标题 |
| 0x18DDC08 | `DrawText(int,string)` | 回调里画文本 |
| 0x18DDCF8 | `DrawFullButton(int,string,Action,bool)` | 4 参，注意 |
| 0x291CDC4 | `System.Action::.ctor(object,IntPtr)` | **委托字段布局的铁证** |
| 0x1671010 | `FHBOAFMLNDC::.ctor(Action, ref Action)` | Open 的返回值类型 |
| 0x1572A9C | 共享 NRE raiser（→0x15b95d4→0x15b95f8→"NullReferenceException"） | 所有 `cbz → 0x18dda68` 的真实含义 |
| 0x15727A0 | 写屏障（GC write barrier） | 识别“字段赋值” |
| 0x1572A8C | `il2cpp_object_new` 形态的分配器 | 识别对象分配 |
