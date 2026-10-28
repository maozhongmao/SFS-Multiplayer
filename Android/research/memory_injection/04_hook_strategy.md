# 04 — Android IL2CPP Hook 选型 + 主线程任务队列 + 候选 Hook 点

> 目标包：`com.StefMorojna.SpaceflightSimulator` 1.6.00.22（arm64-v8a，Android 13 实测机 vivo V2046A）
> 注入体：`libsfsmp.so`（JNI_OnLoad → pthread → 延迟 8 s → `dlopen("libil2cpp.so")` → `dlsym` il2cpp C API）
> 现有成果：`src/v43.c`，mode 11（原生输入弹窗）稳定；hook 点 `SFS.Input.VNDAREHWRIQNAAKC.Update`
> 本文只做**资料调研 + 静态分析 + 现有代码审阅**，不做设备实验；所有 RVA/长度/指令均来自本仓库 `dump/` 与 APK 内的真实 `libil2cpp.so`。
> 约束：不修改 `D:/SFS-Multiplayer/1`，不改 `src/`，只写本文件。

**覆盖**：① 三种 hook 对比 → §2；② 主线程任务队列（含可编译代码 + 自测）→ §3；③ 候选 hook 点 + RVA → §4；④ Play Integrity / 签名 / Unity 干扰 / SIGSEGV 排查 → §5；落地顺序 → §6。

---

## 0. 结论（TL;DR）

| 问题 | 结论 |
|---|---|
| 主 hook 机制 | **继续用 ① methodPointer 换首指针**，作为"每帧 tick + void 无参 Unity 消息"的唯一默认手段 |
| 什么时候才上 inline hook | 只有需要**在调用点拦截参数化/静态/直接调用**的方法（`RocketManager.SpawnBlueprint`、`WorldTime.SetState`、`Part.DestroyPart`…）且**无法用帧内状态 diff 替代**时，才用 **ShadowHook**（比 Dobby 更合适），且只对"长函数"用 |
| 能不能用 NativeDetour | Unity **没有**给 player 暴露这个 API；社区所说的 NativeDetour 在 arm64-Android 上就是 "inline patch + methodPointer 记账" 的组合，不存在第三条独立路线 |
| bhook 能不能 hook 游戏方法 | **不能**。bhook 是 PLT/GOT hook，只能拦跨 so 的导入符号；libil2cpp 内部方法不走 PLT |
| 主线程队列 | 必须。MPSC（mutex + 环形缓冲）+ 每帧 swap 批量 + 预算上限 + 重入保护；worker 只投递，所有 il2cpp/Unity 对象操作在 drain 里做 |
| 最省事的同步架构 | **一个 tick + 每帧状态快照 diff**（rockets 列表、worldTime、timewarpSpeed、isPlayer/hasControl）覆盖 ~90% 同步需求；hook 只用于"必须改参数/阻止执行/必须在事件前拿到对象"的少数点 |

三条硬证据（本仓库实测数据，详见 §2.2）：

1. 本 build 13.6 万个方法里 **20.5% ≤ 16 字节**、15.2% ≤ 8 字节、**3.4% 只有一条 `ret`**；inline hook 的 16 字节补丁在五分之一的方法上没有落脚点。
2. 方法入口只有 **24.6% 是 16 字节对齐**（`RVA % 16` 在 0/4/8/12 上各约 25%），16 字节补丁会跨缓存行，且无法用单条指令原子写入。
3. 我们已 hook 的 `VNDAREHWRIQNAAKC.Update` **本体只有 20 字节**（`ldr/cbz/ldr/ldp/br`），16 字节补丁零余量；而 methodPointer 是"每方法一份"的 8 字节指针，写一次 = 单条 `str`，原子、可无损还原、v43 已稳定运行。

---

## 1. 现状：v43 到底做了什么

### 1.1 安装链路（`src/v43.c`）

```
JNI_OnLoad
 └─ pthread_create(worker) / detach
     └─ sleep(8)                        // 等 Unity/il2cpp 初始化完
     └─ dlopen("libil2cpp.so", RTLD_NOW)
     └─ dlsym: il2cpp_domain_get / il2cpp_thread_attach / il2cpp_domain_assembly_open /
               il2cpp_assembly_get_image / il2cpp_class_from_name / il2cpp_class_get_method_from_name / ...
     └─ domain = il2cpp_domain_get(); il2cpp_thread_attach(domain)
     └─ img = image(assembly_open("Assembly-CSharp"))
     └─ smCls = class_from_name(img, "SFS.Input", "VNDAREHWRIQNAAKC")
     └─ upd   = class_get_method_from_name(smCls, "Update", 0)
     └─ mprotect(page_of(upd) 起 2 页, RWX)
     └─ g_orig_update = ((void**)upd)[0];   // MethodInfo.methodPointer
        ((void**)upd)[0] = hook_update;
```

`hook_update(self)` 里依次做：帧计数 → 检查 `g_do_clone`（JNI 点击置位）→ 每 90 帧自动弹窗 → 每 120 帧重开抽屉 → 心跳日志 → `poll_click()` → 每 30 帧轮询输入框 → **调用原函数** `g_orig_update(self)`。

### 1.2 这个做法的三个结构性问题

| 问题 | 表现 | 后果 |
|---|---|---|
| 只有一个 `volatile int g_do_clone` 标志 | 只能表达"下一次 tick 做某件事"，不能带参数、不能排队、不能延迟 N 帧 | 多任务/多来源（网络包、UI、定时器）会互相覆盖 |
| hook 里直接跑重活 | `FindObjectsOfType`、`Instantiate`、伪造 `Action` 委托、`Open()` 全在 tick 内 | 单帧几十 ms → 掉帧；一旦抛异常/崩溃没有隔离点 |
| 没有 RVA 校验（除 Instantiate 选择） | `class_get_method_from_name` 返回什么都信 | 换版本后静默 hook 错方法、或 `methodPointer` 为 NULL 直接跳飞 |

### 1.3 为什么"非主线程调 `FindObjectsOfType` 会崩"，而 `Time.get_time` 不会

这是两件不同的事，必须分开理解，否则会误判 hook 的边界：

| 机制 | 含义 | 谁需要 |
|---|---|---|
| **IL2CPP 线程 attach** | 让当前 pthread 进入 IL2CPP 域：有托管 `Thread` 对象、能分配托管内存、GC 能看见它 | 任何线程调用**任何**托管方法前（包括 `Time.get_time`） |
| **Unity 主线程亲和性** | `GameObject`/`Transform`/`Object.FindObjectsOfType`/`Instantiate`/UI 等 native 对象 API 内部依赖 Unity 主线程状态（场景图、批处理、渲染线程同步），非主线程访问 = 数据竞争，通常直接 SIGSEGV | 任何**碰 native 对象**的 API |

所以：

- `Time.get_time` 是纯静态、只读引擎全局变量，attach 后在任意线程都能用（已实测）。
- `FindObjectsOfType` 虽然是 `UnityEngine.Object` 的静态方法，但它要遍历 native 场景对象表，**必须在 UnityMain**。`il2cpp_thread_attach` 救不了它。
- 结论：**不是"非主线程不能调 il2cpp"，而是"非主线程不能碰 Unity 对象"**。队列方案的本质就是把"碰对象"的部分搬回主线程。

> 补充：v43 的 worker 自己 `il2cpp_thread_attach` 了，但 JNI 线程（`Java_com_sfsmp_bridge_Bridge_nativeOnClick`）没有 attach 就直接置标志——目前只是置标志所以没事；一旦有人在 JNI 回调里直接调 il2cpp，就会踩到"未 attach + 非主线程"的双重坑。

---

## 2. 三种 hook 方案对比

### 2.1 方案 ①：改 `MethodInfo->methodPointer`（现用）

#### 机制

本 build 的 `MethodInfo` 布局（`dump/il2cpp.h:117`，由 dump 工具按该 il2cpp 版本生成）：

```c
struct MethodInfo {
    Il2CppMethodPointer methodPointer;        // +0x00  ← 我们改的就是这个
    Il2CppMethodPointer virtualMethodPointer; // +0x08  ← 本版本新增，vtable 用的是它
    InvokerMethod       invoker_method;       // +0x10
    const char*         name;                 // +0x18
    Il2CppClass*        klass;                // +0x20
    const Il2CppType*   return_type;          // +0x28
    const Il2CppType**  parameters;           // +0x30
    ...
    uint16_t slot; uint8_t parameters_count; uint8_t bitflags;
};
```

两个关键事实：

1. `methodPointer` 仍在 **+0x00**，所以 `((void**)mi)[0]` 是对的；但结构体已经比老版本多了 `virtualMethodPointer`，**不能假设其它字段偏移**（比如别硬编码 `invoker_method` 在 +0x8）。
2. `VirtualInvokeData` 是**另一份**函数指针（`dump/il2cpp.h:5`）：

```c
struct VirtualInvokeData { Il2CppMethodPointer methodPtr; const MethodInfo* method; };
```

→ 虚方法/接口方法的分发走 class vtable 里的 `methodPtr`，**改 `MethodInfo->methodPointer` 完全不影响它**。这是"hook 装了但从来不触发"的头号原因（skill 里也记了这条坑）。

#### 覆盖矩阵

| 调用形态 | methodPointer 换首指针是否生效 | 说明 |
|---|---|---|
| Unity 消息（`Update`/`LateUpdate`/`FixedUpdate`/`OnGUI`…） | ✅ | 引擎按 MethodInfo 分发，v43 已实证 |
| `il2cpp_runtime_invoke` / 反射 | ✅ | `Runtime::Invoke` 每次读 `method->methodPointer` 再传给 invoker |
| 委托 `Action`/`Func` 的 `Invoke` | ⚠️ 有条件 | delegate 在**创建时**把目标函数的 methodPointer 拷进自己的 `method_ptr`；hook 之前创建的委托仍指向原函数，之后创建的才指向代理 |
| 同类/同程序集内的直接调用 | ❌ | IL2CPP 生成的是直接 `bl` 到函数体，编译期就定了 |
| 虚方法 / 接口方法 | ❌ | 走 `VirtualInvokeData.methodPtr` |
| 泛型实例（full generic sharing） | ⚠️ | 多个实例共享同一段代码/桩，改一个可能影响一片，或改不到 |
| icall（`internal call`，如 `Object.FindObjectsOfType` 的 native 侧） | ❌ | 不是托管方法 |

**为什么 Unity 消息能命中**（源码链，2022.3 的 libil2cpp）：

```
Unity 引擎 ScriptingInvocationNoArgs::Invoke
  → il2cpp_runtime_invoke(method, obj, params, exc)
    → Runtime::Invoke → InvokeWithThrow
      → method->invoker_method(method->methodPointer, method, obj, params, ...)
```

`methodPointer` 是**每次 invoke 现读**的（不是缓存在 invoker 里），所以改它立刻生效。这也解释了 v43 的 hook 为什么能稳定每秒命中 60 次。

#### 优点

- **零指令改写**：不碰代码段，不需要重定位、不需要 icache flush、不需要暂停线程。
- **精确到方法**：同一个函数被多个类共享（ICF 折叠）也不受影响（本 build 136,034 个方法对应 136,034 个唯一地址，见 §2.2）。
- **可无损还原**：保存旧指针，`mi[0] = old` 即还原；没有"补丁写坏原指令"的风险。
- **写入原子**：8 字节对齐指针的单条 `str` 在 ARM64 上是原子的；最坏情况是"本次调用仍走旧函数"，下一帧生效。
- **改的是数据不是代码**：对"代码段完整性校验"更友好（但改的是 `il2cpp` 段，仍属于 AX 段，见 §5.1 / §5.2）。

#### 缺点 / 注意

- 只能覆盖"运行时分发"的调用（上表）。要拦 `SpawnBlueprint` 这种被直接调用的静态方法，必须换方案。
- 需要拿到 `MethodInfo*`：要么用 il2cpp API 按 `命名空间+类名+方法名+参数个数` 查，要么从 `Il2CppClass->methods` 遍历。混淆后的名字是稳定的（同一版本内），跨版本会变。
- `MethodInfo` 落在 **`il2cpp` 段（AX，只读可执行）**，必须 `mprotect` 才能写。建议 **RW → 写 → 还原 R-X**，把可写窗口缩到几条指令。
- 若目标线程已经把旧指针读进寄存器正在执行旧函数体，本次调用不会进 hook——对每帧 tick 无所谓（下一帧就进来了），对"只发生一次"的事件（如 `SpawnBlueprint`）就是漏事件。
- 版本升级会改布局（本 build 已多出 `virtualMethodPointer`）→ **必须做 RVA 校验**（见本节 `hook_method`）。

#### 代码（生产可用，带 RVA 校验）

```c
/* 用 dladdr 拿 load bias，再用"已知方法 RVA"反查校验；两个方法都对上才认。 */
static uintptr_t il2cpp_base;

static int calibrate_base(void* mi_get_name, void* mi_get_transform,
                          uintptr_t rva_get_name, uintptr_t rva_get_transform) {
    Dl_info di;
    if (!dladdr((void*)mi_get_name, &di) || !di.dli_fbase) return 0;
    uintptr_t base = (uintptr_t)di.dli_fbase;
    if ((uintptr_t)(*(void**)mi_get_name) - base != rva_get_name) return 0;
    if ((uintptr_t)(*(void**)mi_get_transform) - base != rva_get_transform) return 0;
    il2cpp_base = base;
    return 1;
}

/* 解析 + 校验 + 换指针。返回旧指针（失败 NULL）。g_proxy = 你的代理函数 */
static void* hook_method(void* img, const char* ns, const char* cls,
                         const char* meth, int argc,
                         uintptr_t expect_rva, const char* tag) {
    void* k = g_cf(img, ns, cls);
    if (!k) { LOGE("%s: class %s.%s not found", tag, ns, cls); return NULL; }
    void* mi = g_gm(k, meth, argc);
    if (!mi) { LOGE("%s: method %s/%d not found", tag, cls, argc); return NULL; }

    void* old = *(void**)mi;                       /* +0x00 methodPointer */
    uintptr_t rva = (uintptr_t)old - il2cpp_base;
    if (expect_rva && rva != expect_rva) {          /* 版本不匹配，绝不硬上 */
        LOGE("%s: RVA mismatch got 0x%lx want 0x%lx", tag, (unsigned long)rva, (unsigned long)expect_rva);
        return NULL;
    }
    uintptr_t page = (uintptr_t)mi & ~(uintptr_t)(sysconf(_SC_PAGESIZE) - 1);
    if (mprotect((void*)page, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        LOGE("%s: mprotect failed", tag); return NULL;
    }
    *(void**)mi = (void*)g_proxy;                  /* 8 字节对齐 = 原子 */
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    mprotect((void*)page, sysconf(_SC_PAGESIZE), PROT_READ | PROT_EXEC);   /* 收窄可写窗口 */
    LOGI("%s: hooked %s::%s rva=0x%lx mi=%p old=%p", tag, cls, meth, (unsigned long)rva, mi, old);
    return old;
}
```

> 说明：`mprotect` 恢复成 `R-X` 而不是 `R-W-X`，是因为 `il2cpp` 段本来就没有写权限；有些机型/内核在 `PROT_EXEC` 页上加 `PROT_WRITE` 会被 SELinux 拦（本机 OK，v43 已跑通），保持最小权限能减少差异。

### 2.2 方案 ②：inline hook（Dobby / ShadowHook / bhook）

#### 三个库到底是什么

| 库 | 类型 | 许可 / 活跃度 | 能不能 hook libil2cpp 内部方法 |
|---|---|---|---|
| **Dobby**（jmpews） | inline hook 框架，多平台多架构 | Apache-2.0，4.8k star，最后 push 2025-01 | 能，但需要指令重定位；有 PC-relative 重定位缺陷报告 |
| **ShadowHook**（bytedance/android-inline-hook） | Android inline hook | MIT，2.4k star，push 2026-08（活跃），支持 Android 4.1–17 / armv7+arm64 | 能；支持"地址"或"库名+符号名"，内置 xDL 符号查找，自动防递归 |
| **bhook / ByteHook**（bytedance） | **PLT/GOT hook** | MIT，2.6k star，push 2026-06 | **不能**（只能拦导入符号） |

`bhook` 被归到 inline hook 是常见误解：它的原理是改 ELF 的 `.plt`/`.got`，让"调用外部 so 的符号"跳到代理。libil2cpp 内部的方法调用是同一个 so 内的直接 `bl`/`b`，根本不经过 PLT。bhook 的真正用途是：

- 拦 libc（`open`/`read`/`write`/`fopen`/`stat`）——做文件重定向、抓存档路径；
- 拦 `libunity.so` 导入的符号；
- 拦 JNI 函数（`libart` 的 `GetMethodID` 等）——做 Java 层观测。

对我们的主线程 hook 需求，bhook 不解决问题，但可以作为**辅助**（比如拦 `android_log_print` 看游戏日志、拦 `open` 找存档目录）。

#### ARM64 指令修复风险（用本 build 的真实数据量化）

统计口径：`dump/script.json` 的 136,034 个 `ScriptMethod` 地址，映射到 APK 内 `libil2cpp.so`（`lib/arm64-v8a/libil2cpp.so`，56,998,304 B，与 dump 完全对应），读入口前 16 字节。

| 指标 | 数值 | 对 inline hook 的含义 |
|---|---|---|
| 方法总数 | 136,034 | — |
| 入口 `RVA % 16` = 0 / 4 / 8 / 12 | 33,511 / 34,291 / 33,628 / 34,604（各 ~25%） | 只有 24.6% 天然 16 字节对齐；16 B 补丁跨缓存行 |
| 入口首指令 = `ret` | 3,548（2.6%） | 空方法/桩，**没有任何字节可改写**，写 16 B 会砸掉下一个函数 |
| 估计长度 ≤ 4 B | 4,673（3.4%） | 同上 |
| 估计长度 ≤ 8 B | 20,612（15.2%） | 连最短的绝对跳转都放不下（`ldr x16,#8; br x16; .quad addr` 共 16 B） |
| 估计长度 ≤ 16 B | 27,928（20.5%） | 16 B 补丁零余量，重定位必须成功 |
| 长度中位数 / 均值 | 108 B / 216 B | 长函数占多数，inline hook 对"业务方法"通常可行 |
| 入口首指令 = `bti c` | **0** | 本 build 未启用 BTI，少一层风险（但仍要按有 BTI 写 trampoline） |
| 入口首指令 = `pacibsp` | **0** | 未启用 PAC 返回地址签名 |
| 入口首指令 = `b`（尾调用/桩） | 1,128（0.8%） | 可重定位，但要注意"跳过去之后原函数体不在这里" |
| 入口首指令 = `adrp` | 15（0.01%） | 入口就要重定位，最麻烦的一类（少见） |
| `ScriptMethod` 地址重复 | **0**（136,034 方法 / 136,034 唯一地址） | 本 build 没有把多个方法折叠到同一入口；methodPointer 精确到方法，inline hook 也不会"一勾勾一串" |

> 长度 = "下一个 `ScriptMethod` 地址 − 本方法地址"，是**上界**（中间可能夹着未列出的编译器辅助函数/对齐填充），所以真实的短函数比例只会更高，不会更低。

再看几个真实候选（本报告 §4 会用到）：

| 方法 | RVA | 估计长度 | 入口前 4 条 | 首 16 B 内的 PC 相对指令 |
|---|---|---|---|---|
| `VNDAREHWRIQNAAKC.Update` | 0x189E9C4 | 28 B（**本体 20 B**） | `ldr` / `cbz` / `ldr` / `ldp` | `cbz`（+0x10） |
| `WorldTime.Update` | 0x1825624 | 252 B | `stp d9,d8` / `stp` / `stp` / `adrp` | `adrp` |
| `GameManager.Update` | 0x16ECF28 | 104 B | `str d8` / `stp` / `mov` / `mov` | 无 |
| `Rocket.FixedUpdate` | 0x1753AB4 | 280 B | `stp` / `stp` / `adrp` / `mov` | `adrp` |
| `WorldTime.SetState` | 0x1824C48 | 424 B | `sub sp` / `str` / `str` / `stp` | 无 |
| `RocketManager.SpawnBlueprint` | 0x18F0310 | 1,528 B | `sub sp` / `str d10` / `stp` / `stp` | 无 |
| `MoveModule.Toggle` | 0x1841254 | 60 B | `stp` / `str` / `cbz` / `mov` | `cbz` |

`VNDAREHWRIQNAAKC.Update` 的真实反汇编（这就是我们 v43 hook 的那个函数）：

```
189e9c4:  ldr  x0, [x0, #0x20]        ; this.CurrentScreen
189e9c8:  cbz  x0, 0x189e9d8          ; 为空就跳走（跳到下一个函数的开头！）
189e9cc:  ldr  x8, [x0]               ; vtable
189e9d0:  ldp  x2, x1, [x8, #0x178]   ; 槽位 0x178
189e9d4:  br   x2                     ; 尾调用到 CurrentScreen 的虚方法
```

也就是说：**它自己只是个 20 字节的转发桩**，真正的每帧逻辑在当前 screen 对象的 vtable 槽 0x178 里。这解释了两件事：

1. 为什么 methodPointer hook 它有效——引擎调的就是这个入口；
2. 为什么**不能**对它做 16 字节 inline hook——补丁区 16 B 之后只剩 4 B，还要重定位一个 `cbz`，任何重定位器都必须把 `cbz` 的目标（`0x189e9d8`，已经属于下一个函数）映射到 trampoline 里的对应位置，一旦算错就是跳进隔壁函数。

#### inline hook 的通用风险清单

| 风险 | 说明 | 缓解 |
|---|---|---|
| 短函数 | 见上表，20% 的方法 ≤16 B，2.6% 只有 `ret` | 安装前检查函数长度/首指令，短函数拒绝安装 |
| PC 相对指令 | `adrp/adr/b.cond/cbz/tbz/ldr literal` 必须重定位；`adrp` 还要保证 trampoline 在 ±4 GB 内 | 用成熟库（ShadowHook/Dobby）而不是自己写；安装前记录 trampoline 地址，验证 `adrp` 目标落在合法映射 |
| 并发打补丁 | UnityMain 可能正在执行该函数，16 B 非原子写入会取到半条指令 | 加载期安装（目标场景还没开始跑）；或"先写 4 B `b` 到岛，再填岛"的两段式；ShadowHook 内部有线程安全处理但非绝对 |
| icache | 改代码后必须 `__builtin___clear_cache` | 库会做；自己写一定别忘 |
| 指针认证 / BTI | 本 build 没有；但 trampoline 要用 `br x16`，别用 `blr`（会污染 LR/返回地址签名） | 跟库的模板走 |
| 代码段完整性 | 有些加固会 hash `.text` | 本包无 PairIP（见 §5.1），但别假设永远没有 |
| 卸载/还原 | 还原时要保证没有线程停在 trampoline 里 | 只装不卸；退出即杀进程 |

#### Dobby vs ShadowHook（本场景的取舍）

| 维度 | Dobby | ShadowHook | 结论 |
|---|---|---|---|
| 维护 | 2025-01 后无 push | 2026-08 仍在更新 | ShadowHook |
| Android 版本覆盖 | 官方未声明上限 | 明确 4.1–17 | ShadowHook |
| 按符号名 hook | 需要自己 `dlsym` | 内置 `shadowhook_dlsym` + 跨 linker namespace 查 `.dynsym/.symtab` | ShadowHook |
| 递归保护 | 无 | 有（同一代理重入自动放行） | ShadowHook |
| 体积 | 小（单文件级） | 稍大（含 xDL） | Dobby |
| 已知问题 | 有 PC-relative 重定位缺陷报告（第三方项目） | 文档要求代理函数用 `SHADOWHOOK_POP_STACK`，漏了会导致"代理只执行一次" | 都要按文档写 |

**结论：如果非要 inline hook，用 ShadowHook。** Dobby 只作为"ShadowHook 集成失败"的备选。

### 2.3 方案 ③：Unity 的 "NativeDetour" —— 先澄清它是什么

先给结论：**Unity 没有向 player 暴露 `NativeDetour` API**。

- `dump/dump.cs`（21 MB，10,684 个类）全文搜索 `NativeDetour` = **0 命中**；`il2cpp.h` 也没有。
- 社区里叫 "NativeDetour" 的是两样东西：
  1. **MonoMod.RuntimeDetour.NativeDetour**：托管侧的 detour 框架，官方文档明确它是"raw native detour"；对 ARM64 的支持远不如 x86/x86_64 成熟（社区 issue 长期在讨论 aarch64）。
  2. **BepInEx / Il2CppInterop 的 `IL2CPPDetourMethodPatcher` + `FastNativeDetour`**：它的做法是"**inline patch 原生函数体**（`FastNativeDetour(original, detour)`）+ 复制一份 MethodInfo 并把 `MethodPointer` 指向 trampoline"。`FastNativeDetour` 是 Windows/x64 实现（5 字节 `jmp` / `mov rax, imm64`），**不能直接拿到 arm64 Android 用**。

所以在本平台上，"NativeDetour 方案"落地后就是：

```
inline hook（改函数体）  ←→  methodPointer 记账（让 runtime 分发走 trampoline）
```

**关键陷阱：这两个不能同时对同一个方法做。** 如果已经 inline hook 了函数体，所有调用者（直接调用、methodPointer 分发）都会落到代理里；此时再把 `mi[0]` 指向 trampoline，runtime 分发的调用就会**绕过代理直接执行原函数**——表现为"反射/消息调用不进 hook，直接调用进 hook"，极难排查。

正确姿势（二选一，按"这个方法是怎么被调的"决定）：

| 方法被谁调 | 选哪个 | 理由 |
|---|---|---|
| Unity 消息、反射、委托 | methodPointer | 零风险、可还原 |
| 直接调用 / 静态方法 / 需要改参数 | inline hook（ShadowHook） | methodPointer 拦不到 |
| 虚方法 / 接口方法 | 改 vtable 槽（高级，见 §4.6）或 inline hook 具体实现 | 两份指针，改 MethodInfo 无效 |

### 2.4 三方案总表

| 维度 | ① methodPointer | ② inline（ShadowHook/Dobby） | ③ NativeDetour（社区组合） |
|---|---|---|---|
| 改什么 | MethodInfo+0 的 8 字节指针 | 函数入口 16 字节指令 | 函数体 + MethodInfo |
| 覆盖直接调用 | ❌ | ✅ | ✅ |
| 覆盖虚/接口调用 | ❌ | ✅（hook 实现体） | ✅ |
| 覆盖 Unity 消息 | ✅ | ✅ | ✅ |
| 短函数风险 | 无 | 高（20% ≤16 B） | 高 |
| 指令重定位风险 | 无 | 中（adrp/cbz/ldr literal） | 中 |
| 并发补丁风险 | 无（原子指针写） | 中 | 中 |
| 可还原 | ✅ 一条赋值 | 需保存原字节 + 确保无线程在 trampoline | 需两处都还原 |
| 跨版本稳定性 | 中（布局/名字会变，需 RVA 校验） | 低（RVA 变、函数变短都可能） | 低 |
| 实现成本 | 极低（v43 已在用） | 中（集成 so + 编译） | 高（还要处理 ABI/平台） |
| 本 build 适用性 | ★★★★★ | ★★★（只对长函数） | ★（无现成 arm64 实现） |

---

## 3. 主线程任务队列（标准写法）

### 3.1 为什么必须是队列，而不是 v43 那种标志位

v43 的 `g_do_clone` 是"单槽邮箱"：只能表达一种动作、不能带参数、不能延迟、两个来源同时置位会互相覆盖。真实联机场景里同时会有：

- 网络线程收到"创建火箭/更新部件/时间加速"包；
- JNI 线程收到按钮点击；
- 定时器要 30 帧后重试。

这些都必须变成"在主线程按顺序执行的一次函数调用"。队列是唯一能同时满足**顺序、参数、延迟、可观测**的结构。

### 3.2 设计取舍

| 决策 | 选择 | 理由 |
|---|---|---|
| 并发模型 | MPSC（多生产者/单消费者）+ `pthread_mutex` | 投递频率是"每包一次"（百/秒级），无竞争时 mutex ~20 ns；lock-free 环要处理 ABA/内存序，收益为零、出错代价高 |
| 队列结构 | ready 环形缓冲（512）+ delay 数组（128） | 延迟任务用 swap-remove，O(1)；ready 保持 FIFO |
| drain 语义 | 锁内取一批（≤64）→ **锁外**执行 | 执行期间不持锁；执行中 post 的任务自然落到下一帧（防重入） |
| 满了怎么办 | 丢弃 + 计数，**绝不阻塞** | 网络线程不能被主线程卡住；丢一帧状态，下一次同步会覆盖 |
| 预算 | 每帧最多 64 个任务 | 任务约定 <5 ms；超了分帧，避免掉帧/ANR |
| 时间基准 | `frames` 而不是 ms | 不受 `Time.timeScale` 影响；Unity 的 Update 帧就是单调的 |

### 3.3 完整实现（`sfsmp_queue.c`，已编译 + 自测）

```c
/*
 * sfsmp_queue.c — 主线程任务队列（MPSC，投递任意线程，执行只在 UnityMain）
 *
 * 用法：
 *   worker/JNI 线程:  mpq_post(fn, arg);  mpq_post_delayed(fn, arg, 30);
 *   UnityMain (hook): mpq_tick();        // 每帧一次
 *
 * 约定：
 *   - 任务体必须短（< 5 ms），只能碰"主线程允许"的 API。
 *   - 任务里再 post 的任务进入下一帧（不会递归 drain）。
 *   - 不抛异常（C，-fno-exceptions）；失败靠返回值和日志。
 */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MPQ_READY_CAP 512
#define MPQ_DELAY_CAP 128
#define MPQ_BATCH     64          /* 每帧最多执行多少个任务 */

typedef void (*mpq_fn)(void *arg);
typedef struct { mpq_fn fn; void *arg; int due; } mpq_task;

static mpq_task g_ready[MPQ_READY_CAP];
static int      g_rhead, g_rtail, g_rcount;
static mpq_task g_delay[MPQ_DELAY_CAP];
static int      g_dcount;

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int      g_frame;
static int      g_draining;
static int      g_stop;

static struct {
    unsigned long posted, ran, dropped, delayed, tick, max_batch, reentry;
} g_stat;

static void mpq_log(const char *fmt, ...);

int mpq_init(void) {
    pthread_mutex_lock(&g_lock);
    g_rhead = g_rtail = g_rcount = 0;
    g_dcount = 0;
    g_frame = 0;
    g_draining = 0;
    g_stop = 0;
    memset(&g_stat, 0, sizeof(g_stat));
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void mpq_shutdown(void) { __atomic_store_n(&g_stop, 1, __ATOMIC_RELEASE); }

/* 任意线程可调；队列满则丢弃并计数（绝不阻塞调用者） */
int mpq_post(mpq_fn fn, void *arg) {
    int ok = 0;
    unsigned long dropped = 0;
    pthread_mutex_lock(&g_lock);
    if (g_rcount < MPQ_READY_CAP) {
        g_ready[g_rtail].fn = fn;
        g_ready[g_rtail].arg = arg;
        g_ready[g_rtail].due = 0;
        g_rtail = (g_rtail + 1) % MPQ_READY_CAP;
        g_rcount++;
        g_stat.posted++;
        ok = 1;
    } else {
        g_stat.dropped++;
    }
    dropped = g_stat.dropped;          /* 锁内取值，避免与 tick 竞争 */
    pthread_mutex_unlock(&g_lock);
    if (!ok) mpq_log("queue full, dropped=%lu", dropped);
    return ok;
}

/* frames 帧之后执行（frames=1 表示下一次 tick） */
int mpq_post_delayed(mpq_fn fn, void *arg, int frames) {
    int ok = 0;
    pthread_mutex_lock(&g_lock);
    if (g_dcount < MPQ_DELAY_CAP) {
        g_delay[g_dcount].fn = fn;
        g_delay[g_dcount].arg = arg;
        g_delay[g_dcount].due = g_frame + (frames > 0 ? frames : 1);
        g_dcount++;
        g_stat.delayed++;
        ok = 1;
    } else {
        g_stat.dropped++;
    }
    pthread_mutex_unlock(&g_lock);
    return ok;
}

/* 只在 UnityMain 调；返回本帧执行的任务数 */
int mpq_tick(void) {
    if (__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE)) return 0;
    if (__atomic_add_fetch(&g_draining, 1, __ATOMIC_SEQ_CST) > 1) {   /* 重入：放弃本次 */
        __atomic_sub_fetch(&g_draining, 1, __ATOMIC_SEQ_CST);
        g_stat.reentry++;                                             /* 主线程独占计数 */
        return 0;
    }

    mpq_task batch[MPQ_BATCH];
    int n = 0;

    pthread_mutex_lock(&g_lock);
    int frame = ++g_frame;          /* 与本队列同一把锁，避免 post_delayed 读到撕裂值 */
    /* 1) 到期任务从 delay 搬到 ready（swap-remove，不保证顺序） */
    for (int i = 0; i < g_dcount; ) {
        if (g_delay[i].due <= frame) {
            if (g_rcount >= MPQ_READY_CAP) break;   /* ready 满：留在 delay，下一帧再搬，不丢 */
            g_ready[g_rtail] = g_delay[i];
            g_rtail = (g_rtail + 1) % MPQ_READY_CAP;
            g_rcount++;
            g_stat.posted++;
            g_delay[i] = g_delay[--g_dcount];
        } else {
            i++;
        }
    }
    /* 2) 取一批出来，锁外执行 */
    while (n < MPQ_BATCH && g_rcount > 0) {
        batch[n++] = g_ready[g_rhead];
        g_rhead = (g_rhead + 1) % MPQ_READY_CAP;
        g_rcount--;
    }
    pthread_mutex_unlock(&g_lock);

    for (int i = 0; i < n && !__atomic_load_n(&g_stop, __ATOMIC_ACQUIRE); i++) {
        batch[i].fn(batch[i].arg);       /* 任务自己负责检查 il2cpp 异常 */
        g_stat.ran++;
    }
    g_stat.tick++;
    if ((unsigned long)n > g_stat.max_batch) g_stat.max_batch = (unsigned long)n;

    __atomic_sub_fetch(&g_draining, 1, __ATOMIC_SEQ_CST);
    return n;
}

void mpq_dump_stats(const char *tag) {
    mpq_log("%s: tick=%lu posted=%lu ran=%lu dropped=%lu delayed=%lu reentry=%lu max_batch=%lu pending=%d",
            tag ? tag : "queue", g_stat.tick, g_stat.posted, g_stat.ran,
            g_stat.dropped, g_stat.delayed, g_stat.reentry, g_stat.max_batch, g_rcount);
}
```

**验证记录（真实执行，不是推演）**

| 项 | 结果 |
|---|---|
| `aarch64-linux-android24-clang -O2 -Wall -Wextra -Werror -c` | 通过（库 + 自测 main 两份都编译） |
| WSL gcc 13.3 自测 | `order: r1=2 r2=1 r3=0 seq=1 2 3` / `stress: ran=638 spins=10` / `PASS` |
| ThreadSanitizer（`-fsanitize=thread`，双生产者 638 任务） | **无 data race 报告**（第一轮曾报 `g_stat.dropped` 锁外读，已修） |

### 3.4 在 hook 里怎么接

```c
static pid_t g_unity_tid;
static int   g_in_hook;
static unsigned g_log_frame;          /* 只用于日志节流，别和队列内部的 g_frame 混用 */
static struct timespec g_last_tick;

static void hook_update(void* self) {
    if (!g_unity_tid) {
        g_unity_tid = gettid();
        LOGI("unity tid = %d", (int)g_unity_tid);
    }
    /* 重入保护：drain 里的任务若间接触发了 Update，直接走原函数 */
    if (g_in_hook) { ((void(*)(void*))g_orig_update)(self); return; }
    g_in_hook = 1;

    /* 双 ticker 限流：8 ms 内只 drain 一次（120 Hz 也不会跑两遍） */
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    long ms = (now.tv_sec - g_last_tick.tv_sec) * 1000L
            + (now.tv_nsec - g_last_tick.tv_nsec) / 1000000L;
    if (ms >= 8) {
        g_last_tick = now;
        int n = mpq_tick();
        if (n > 0 && (g_log_frame++ % 600) == 0) mpq_dump_stats("tick");
    }

    g_in_hook = 0;
    ((void(*)(void*))g_orig_update)(self);   /* 最后调原函数 */
}
```

> 顺序问题：**先 drain 再调原函数**，我们的修改会在游戏本帧物理/UI 之前生效（适合"应用远端状态"）。
> 若某个任务需要读"本帧游戏已经算完的结果"，用 `mpq_post_delayed(fn, arg, 1)` 推到下一帧开头。

### 3.5 任务函数的标准写法

```c
typedef struct { int warp_index; } apply_warp_arg;

/* 调 WorldTime.SetTimewarpIndex_ForLoad(int)（RVA 0x1824648） */
static void task_apply_warp(void* p) {
    apply_warp_arg* a = (apply_warp_arg*)p;
    void* exc = NULL;
    void* inst = NULL;
    g_staticGet(g_timeMainField, &inst);           /* WorldTime.main（静态字段） */
    if (!inst) { LOGE("apply_warp: no WorldTime"); free(a); return; }

    int idx = a->warp_index;
    void* args[1] = { &idx };                      /* 值类型参数传地址 */
    g_ri(g_mSetWarpIndex, inst, args, &exc);       /* il2cpp_runtime_invoke */
    if (exc) {
        LOGE("apply_warp: managed exception");
        exc = NULL;                                /* 不要继续用返回值 */
    }
    free(a);                                       /* 参数由任务自己回收 */
}

/* 网络线程 */
void on_warp_packet(int idx) {
    apply_warp_arg* a = (apply_warp_arg*)malloc(sizeof *a);
    if (!a) return;
    a->warp_index = idx;
    if (!mpq_post(task_apply_warp, a)) free(a);    /* post 失败：自己回收，别泄漏 */
}
```

规则（每一条都对应一类真实崩溃）：

| 规则 | 不遵守会怎样 |
|---|---|
| 参数**堆分配**，不能指向栈 | 函数返回后指针悬空，drain 时读到垃圾 → SIGSEGV |
| `mpq_post` 失败要自己 `free` | 内存泄漏；高频丢包时几千个包/分钟 |
| 每次 `il2cpp_runtime_invoke` 都检查 `exc` | 托管异常被忽略，后续用 `NULL` 返回值 → 崩在 libil2cpp 里，看起来像引擎 bug |
| 跨帧持有托管对象用 `il2cpp_gchandle_new` | 对象被 GC 回收/场景销毁 → 野指针 |
| 不在任务里调 `mpq_tick()` | 递归 drain，栈溢出/顺序错乱（已有 reentry 保护，但别依赖它） |
| 不在任务里 `sleep`/等锁 | 卡住 UnityMain → ANR（5 s） |

### 3.6 线程 attach 的三条规则

| 线程 | 要不要 attach | 说明 |
|---|---|---|
| worker（v43 的 `worker()`） | **要**，一次 | `il2cpp_thread_attach(il2cpp_domain_get())`；attach 后该线程会被 GC 看见。长期空闲的线程建议 `il2cpp_thread_detach` 再 sleep |
| JNI 回调线程（`nativeOnClick` 等） | 调 il2cpp 才要 | 先 `(*vm)->GetEnv`，失败再 `AttachCurrentThread`；**即使 attach 了也不能碰 Unity 对象** |
| drain（UnityMain） | **不要** | 已经是托管线程；重复 attach 是浪费，detach 写错会把主线程踢出域 |

> 结论再强调一次：attach 解决的是"能不能调托管代码"，不解决"能不能碰 Unity 对象"。后者只能靠"回主线程"。

### 3.7 观测指标

| 指标 | 健康值 | 异常含义 |
|---|---|---|
| `dropped` | 0 | 网络洪峰 / 主线程卡住 / 队列太小 |
| `max_batch` | < 64 | 长期打满说明任务产生速度 > 消费速度 |
| `ran / tick` | 0.1 ~ 5 | 突增说明有循环投递 |
| `reentry` | 0 | 有任务在 drain 里触发了 Update（逻辑错误） |
| `pending` | < 50 | 积压 |

---

## 4. 候选 Hook 点清单（RVA 来自本仓库 dump）

### 名称映射（混淆名 ↔ PC 模组里的真名）

本 build 的类名全部混淆，但**命名空间和成员名保留**。映射方法是：拿 PC 模组里用到的成员名去 `dump.cs` 反查唯一匹配的类。

| PC 模组里的名字 | 本 build 的类 | 判定依据（成员名） |
|---|---|---|
| `WorldTime` | `SFS.World.VCQSQMYGGZECNBPE` | `main`、`worldTime`、`timewarpSpeed`、`timewarpIndex`、`AccelerateTime`、`DecelerateTime`、`SetState` |
| `GameManager` | `SFS.World.QHQPUKFMUOSOOWES` | `main`、`rockets`、`OpenMenu`、`LoadSave`、`ExitToHub`、`ExitToMainMenu` |
| `RocketManager` | `SFS.World.WQPAXLGGCOAUZEOA` | `rocketPrefab`、`SpawnBlueprint`、`DestroyRocket`、`MergeRockets`、`LoadRocket` |
| `Rocket` | `SFS.World.FULMMHJZAEKABZAY` | `rocketName`、`partHolder`、`jointsGroup`、`physics`、`GetRotation`、`SetParts` |
| `Location` | `SFS.World.FNVQPOCIQHTEDVSZ` | `position`、`velocity`、`get_Value`/`set_Value` |
| `Part` | `SFS.Parts.Part` | `DestroyPart`（未混淆） |

> **`Rocket.Update` 在本 build 不存在**：Rocket 只有 `FixedUpdate`（`0x1753AB4`）。PC 版里的 `SFS.World.I_Physics.OnFixedUpdate` 在移动版也没有同名符号（接口 `CDOFHFCKKCF` 存在，但实现方法名不同）。所以"Rocket.Update"这个 hook 点要按 `Rocket.FixedUpdate` 或"主 tick 里遍历 rockets"来实现。

### 4.0 选点原则

1. **靠近"状态改变的唯一出口"**：时间加速有 `AccelerateTime`/`DecelerateTime`/`SetState` 三个入口，只 hook `SetState` 就够（前两个最终都会走到它）。
2. **优先 `void` 无参 Unity 消息**：这类方法 methodPointer 直接可用，零指令风险。
3. **不要按实体 hook**：100 个火箭各 hook 一次 `FixedUpdate` = 每帧 100 次代理调用，不如在一个 tick 里遍历 `GameManager.rockets`。
4. **能用帧内 diff 就不用 hook**：位置/速度/时间这些连续量，每帧读一次比拦截写入更简单、更不容易漏。
5. **短函数不碰**（< 32 B 或首 16 B 含 `cbz`/`adrp` 且无余量）。

### 4.1 主 tick（必装，二选一或都装）

| 用途 | 类 | 方法 | RVA | 长度 | 入口特征 | 推荐 | 备注 |
|---|---|---|---|---|---|---|---|
| **全局每帧 tick** | `SFS.Input.VNDAREHWRIQNAAKC` | `Update` | `0x189E9C4` | 20 B | `ldr/cbz/ldr/ldp/br` | **methodPointer** | v43 已验证；本体是转发桩，跳到 `CurrentScreen` 的 vtable 槽 0x178；所有场景都在 |
| 世界内每帧 tick | `SFS.World.QHQPUKFMUOSOOWES`（GameManager） | `Update` | `0x16ECF28` | 104 B | `str d8/stp/mov/mov` | methodPointer | 只在世界场景跑；可作第二个 ticker（带 8 ms 限流） |
| 世界时间 tick | `SFS.World.VCQSQMYGGZECNBPE`（WorldTime） | `Update` | `0x1825624` | 252 B | `stp/adrp` | methodPointer | 世界场景；时间同步的天然节拍 |

> `VNDAREHWRIQNAAKC.Update` 反汇编见 §2.2。它只做 `if (CurrentScreen != null) CurrentScreen.<slot 0x178>()`，所以**不要**在这里假设"当前 screen 一定是游戏内"——它只是转发。

### 4.2 世界时间 / 时间加速

| 用途 | 方法 | RVA | 参数 | 推荐 | 说明 |
|---|---|---|---|---|---|
| 时间状态唯一出口 | `WorldTime.SetState` | `0x1824C48` | `(double, bool, bool)` | **inline**（或改为轮询） | 所有倍率变化的最终落点；hook 它能拿到"谁在什么时候改了倍率" |
| 加速（UI 入口） | `WorldTime.AccelerateTime` | `0x18246F4` | `()` | methodPointer | 只是 UI 入口，不必单独 hook |
| 减速（UI 入口） | `WorldTime.DecelerateTime` | `0x1825A90` | `()` | methodPointer | 同上 |
| 停止 | `WorldTime.StopTimewarp` | `0x18253C0` | `(bool)` | 不 hook | 最终仍走 SetState |
| 读当前倍率 | `WorldTime.get_TimeScale` | `0x1824DF0` | `()` | 每帧读 | 纯 getter，别 hook |
| 读当前时间 | 字段 `worldTime` @ `+0x28` | — | double | 每帧读 | 直读字段最快，无调用开销 |
| 读倍率 | 字段 `timewarpSpeed` @ `+0x30` | — | double | 每帧读 | 同上；`timewarpIndex` @ `+0x20` |

**MVP 建议**：不 hook 时间，直接每帧读 `worldTime`/`timewarpSpeed`，发现与服务端偏差 > 阈值就投递一个 `task_apply_warp`（走 `SetTimewarpIndex_ForLoad`/`SetState`）。这样零 inline hook。

### 4.3 火箭 / 物理

| 用途 | 类 | 方法/字段 | RVA | 长度 | 推荐 | 说明 |
|---|---|---|---|---|---|---|
| 火箭物理步进 | `SFS.World.FULMMHJZAEKABZAY` | `FixedUpdate` | `0x1753AB4` | 280 B | methodPointer（**每实例**调用一次，慎用） | 每帧每火箭一次；不如在主 tick 里遍历 |
| 物理积分 | `SFS.World.UFLBGBGGAEJQCJDW` | `Update` | `0x17B2450` | 1,592 B | 不 hook | 内部实现，改它有连锁反应 |
| 位置/速度 | `DXYTHXTABGUDBUFN` 基类 | 字段 `location` @ `+0x38` | — | — | 每帧读 | `location` 是 **`Location*`**；`Location.position` @ `+0x28`、`velocity` @ `+0x30` 又是 **`Double2_Local*`**（`Obs<Double2>` 包装）——**不能**把 `+0x28` 当 double 读，必须再解一层或调 `get_Value` |
| 是否玩家控制 | 同上 | `isPlayer` @ `+0x48`、`hasControl` @ `+0x50` | — | — | 每帧读 | 也是 `Bool_Local*`（`Obs<bool>` 包装），读 `.Value` 或调 `get_Value` |
| 部件集合变化 | `Rocket` | `SetParts` | `0x1753078` | 1,260 B | inline 或 diff | 建造/分离/对接时调用；diff `partHolder` 更省事 |
| 火箭销毁 | `Rocket` | `OnDestroy` | `0x1753680` | 28 B | **不要 inline**（太短）；用列表 diff | 从 `GameManager.rockets` 消失即视为销毁 |

### 4.4 实体增删（世界同步的核心）

| 用途 | 类 | 方法 | RVA | 长度 | 推荐 | 说明 |
|---|---|---|---|---|---|---|
| 创建火箭（蓝图） | `SFS.World.WQPAXLGGCOAUZEOA` | `SpawnBlueprint` | `0x18F0310` | 1,528 B | **inline（首选 ShadowHook）** | 静态方法、被直接调用，methodPointer 无效；长度足够，入口无 PC 相对 |
| 销毁火箭 | 同上 | `DestroyRocket` | `0x18F1C7C` | 208 B | inline | 入口第 3 条是 `adrp`（可重定位） |
| 合并火箭 | 同上 | `MergeRockets` | `0x18F17A8` | 1,236 B | inline（可选） | 对接/分离；MVP 可先不做 |
| 读火箭列表 | `SFS.World.QHQPUKFMUOSOOWES` | 字段 `rockets` @ `+0x40` | — | — | **每帧 diff** | `List<Rocket>`；`_size` @ `+0x18`，`_items` @ `+0x10` |
| 炸药/部件销毁 | `SFS.Parts.Part` | `DestroyPart` | `0x16A2DB0` | 376 B | diff 或 inline | 参数 `(bool,bool,DestructionReason)`；爆炸/分离都会走 |

> **MVP 强烈建议**：先只做 `rockets` 列表 diff（指针集合比较），拿到"新增/消失"事件；`SpawnBlueprint`/`DestroyRocket` 的 inline hook 留到需要"在创建前分配网络 ID"时再加。这样整个 MVP **一个 inline hook 都不需要**。

### 4.5 其它可选点

| 用途 | 类/方法 | RVA | 推荐 | 说明 |
|---|---|---|---|---|
| 原生 toast（调试反馈） | `SFS.UI.EJIHSVFRLSOQRBJE.Log(string)` | `0x16D64C4`（见 `00_ROUTE.md`） | 直接调用，不 hook | 零克隆的日志出口 |
| 屏幕栈切换 | `VNDAREHWRIQNAAKC.OpenScreen/CloseCurrent` | `0x189E850` / `0x189EB2C` | 不 hook | 需要时读 `main` 静态字段即可 |
| 轮子开关 | `WheelModule.ToggleEnabled` | `0x1803CF0` | inline（可选） | 部件状态同步 |
| 移动模块开关 | `MoveModule.Toggle` | `0x1841254` | inline（可选，60 B 偏短） | 同上 |
| 输入 | `SFS.Input.VZHBSJIZARUKVJWO.Update` | `0x1705264` | 不 hook | 与联机无关 |

### 4.6 虚方法 / 接口方法怎么办

`MethodInfo` 里有两份函数指针（§2.1），vtable 里还有第三份（`VirtualInvokeData.methodPtr`）。想拦虚调用只有三条路：

1. **inline hook 具体实现体**（推荐）：找到实际被调用的那个类的方法，hook 它的函数体。缺点是要对每个实现类各 hook 一次。
2. **改 vtable 槽**：`Il2CppClass` 的 vtable 是内联在类对象尾部的（`dump/il2cpp.h` 里每个 `_c` 结构都以 `VirtualInvokeData vtable[N]` 结尾），偏移随 il2cpp 版本变，**不建议手写**。
3. **改 `virtualMethodPointer`（+0x08）**：只在该类**尚未初始化**时有效。vtable 是类初始化时一次性拷贝的——Unity 2022.3 的 `Class::Init` 里是 `klass->vtable[i].methodPtr = method->virtualMethodPointer`（2021.3 用的还是 `methodPointer`，**版本相关**）。我们的 hook 在游戏跑起来之后才装，基本无效。

> 结论：虚方法用 ①，别碰 ②③。
>
> 顺带一个副作用：因为 vtable 拷的是 `methodPointer`/`virtualMethodPointer` 的**快照**，**装 hook 越早，越可能把 hook 写进"之后才初始化"的类的 vtable**（表现为虚调用也进 hook）。这不算 bug，但会让"为什么这个虚方法也被拦了"变得难解释——所以 hook 尽量晚装（v43 的 `sleep(8)` 正好），并且只 hook 非虚的私有/公有方法。

### 4.7 如何在本 build 里可靠地拿到 MethodInfo（带 RVA 校验）

```c
/* 1) 解析：命名空间 + 类名 + 方法名 + 参数个数 */
void* cls = g_cf(img, "SFS.World", "WQPAXLGGCOAUZEOA");
void* mi  = g_gm(cls, "SpawnBlueprint", 1);      /* 1 个参数 */

/* 2) 校验：methodPointer - base 必须等于 dump 里的 RVA */
uintptr_t rva = (uintptr_t)(*(void**)mi) - il2cpp_base;
if (rva != 0x18F0310) { LOGE("version mismatch: 0x%lx", rva); return; }

/* 3) 同名同参数个数的重载：遍历 + 比参数类型（v43 选 Instantiate 就是这么做的） */
void* it = NULL, *m;
while ((m = g_cms(cls, &it))) {
    if (strcmp(g_mn(m), "SpawnBlueprint") != 0) continue;
    if (g_mpc(m) != 1) continue;
    void* pt = g_mgp(m, 0);
    const char* tn = g_tn(pt);
    if (tn && strstr(tn, "Blueprint")) { mi = m; break; }
}
```

三条纪律：

- **先校验再 hook**：`rva` 对不上就放弃并记日志，绝不"试试看"。
- **base 用两个已知方法交叉校准**（v43 用 `Object.get_name` 的 RVA 反推 base，再校验 `set_name`）。
- **换版本时第一件事是重新导出这张 RVA 表**（`dump.cs` 里每个方法上方的 `// RVA: 0x...`），而不是改代码逻辑。

### 4.8 读 `Obs<T>` 包装值（位置/速度最容易踩的坑）

`Location.position` / `velocity` 不是 `double`，而是 `Double2_Local*`；`Double2_Local : Obs<Double2>`，真正的值在 `Obs<T>.value` 里。`Obs<T>` 是泛型，dump 里字段偏移全是 `0x0`（泛型未展开），**不能硬编码**。

三种正确读法：

| 方式 | 做法 | 代价 | 建议 |
|---|---|---|---|
| 调 getter | `parent = il2cpp_class_get_parent(Double2_Local_cls)` → `class_get_method_from_name(parent, "get_Value", 0)` → `il2cpp_runtime_invoke` | struct 返回值会**装箱**，每帧每火箭一次分配 → GC 压力 | 低频（1~4 帧一次）用 |
| 探字段偏移 | 遍历 `il2cpp_class_get_fields(parent, &iter)` 找 `value`，`il2cpp_field_get_offset` 缓存 | 零分配 | **高频读用这个** |
| 直接猜 | 按 `Obs<T>` 的字段顺序硬编码 | 版本一变就错 | ❌ 禁止 |

本 build 的泛型实例 getter RVA（供校验用）：

| 实例 | `get_Value` RVA |
|---|---|
| `Obs<bool>` | `0x249B2C0` |
| `Obs<double>` | `0x249BA0C` |
| `Obs<Double2>` | `0x249C13C` |

> 同样的坑还有 `Float_Local`、`Int32Enum` 包装、`Bool_Local`（`isPlayer`/`hasControl`）。凡是 dump 里类型名以 `_Local` 结尾的，基本都是 `Obs<T>` 包装，**先解引用再取值**。

---

## 5. 风险清单

### 5.1 Play Integrity / SafetyNet（**本包确实集成了**）

对本 APK 的实际检查结果：

| 检查项 | 结果 | 证据 |
|---|---|---|
| Play Integrity SDK | **存在且完整** | `classes2.dex` 里有 **94 个** `com/google/android/play/core/integrity/*` 类（含 `IntegrityManager`、`StandardIntegrityManager`、`IntegrityTokenRequest/Response`）；`classes.dex` 里有字符串 `playIntegrityToken`、`Integrity check failed.`、`This request is missing a valid app identifier...` |
| SafetyNet | 无 | dex 里 0 命中 |
| PairIP 加固 | **无** | APK 内无 `libpairipcore.so`，无 `com.pairip.*` |
| Firebase | 有 | `libFirebaseCpp*.so`、`libdatastore_shared_counter.so` |

> 注意：SDK 存在 ≠ 游戏在调用。它可能是 Firebase/Play Core 的传递依赖。要确认"是否真的校验"，得在运行时看 `IntegrityManagerFactory` 有没有被调用（或抓包看 `/decodeIntegrityToken` 请求）。但**只要后端校验，我们的包就过不了**，所以按"会校验"来设计更安全。

Play Integrity 的判定（Google 官方文档）：

| 字段 | 取值 | 我们的包会得到 |
|---|---|---|
| `appIntegrity.appRecognitionVerdict` | `PLAY_RECOGNIZED` / `UNRECOGNIZED_VERSION` / `UNEVALUATED` | **`UNRECOGNIZED_VERSION`**（证书与 Play 记录不符） |
| `accountDetails.appLicensingVerdict` | `LICENSED` / `UNLICENSED` / `UNEVALUATED` | **`UNLICENSED`**（不是从 Play 安装/购买） |
| `deviceIntegrity.deviceRecognitionVerdict` | `MEETS_DEVICE_INTEGRITY` / `MEETS_BASIC_INTEGRITY` / `MEETS_STRONG_INTEGRITY` / 空 | 通常仍通过（我们没 root、没装 hook 框架；自己的 so 属于应用自身代码）；文档说检测到 "API hooking" 时会为空 |

结论：

- **重打包 + 重签名必然拿不到 `PLAY_RECOGNIZED`**，这是设计使然，不是能"绕过"的技术问题（Google 校验的是签名证书 + 包名 + 版本）。
- 只有当**游戏后端**（或其 Firebase App Check）校验 token 时才影响使用。SFS 是单机游戏，联机靠玩家自建服务器，目前没有证据表明官方校验 PI。
- 因此：**不要把"过 Play Integrity"当成目标**；补丁包严格限制在自用/自建服务器，不对外分发（skill 里也是这条红线）。
- 如果将来开发商用 PI 做排行榜/反作弊，我们的包会被识别 → 只能玩单机/私服，或者改用 root + LSPosed 路线（不重签名，但设备完整性会掉）。

### 5.2 签名校验

- 本包**没有** PairIP（`com.pairip` 0 命中）、没有 SafetyNet、没有 `libpairipcore.so`。
- dex 里出现的 `getSignatures` / `getInstallerPackageName` 来自 AndroidX（`PackageIdentityUtils$SignaturesCompat`）和 Play Core，不是游戏自校验代码。
- 若某版本真的加了自校验，常见形态是：`PackageManager.getPackageInfo(pkg, GET_SIGNATURES)` 比对 hash、`checkSignatures`、或 native 侧读 `/proc/self/maps` 找异常模块。
  - **methodPointer 改不了 Java 层**，遇到 Java 校验只能用 JNI hook（bhook 拦 `GetMethodID`/`CallObjectMethod`）或改 dex。
  - native 侧 `/proc/self/maps` 检测：我们的 so 在 APK 的 `lib/arm64-v8a/` 下，属于正常应用库，不在可疑路径（`/data/local/tmp` 之类），一般不会命中。
- 我们每个版本都用同一个 debug key 重签 → 证书固定；如果游戏硬编码官方证书 hash，第一次启动就会拦。目前没有。

### 5.3 Unity 自身会不会 inline hook / 干扰我们

| 问题 | 事实 | 影响 |
|---|---|---|
| Unity 有没有 `NativeDetour` API | **没有**（dump.cs 21 MB 全文 0 命中） | "Unity 官方 NativeDetour 方案"不存在，别去找 |
| Unity 会不会 patch 自己的托管方法 | 不会 | 我们的 methodPointer 不会被 Unity 覆盖 |
| 信号处理器 | Unity/debuggerd 会装 SIGSEGV/SIGABRT 处理器做崩溃上报 | 我们若自己装 handler，必须 `sigaction` 保存旧 handler 并链式转发，否则 tombstone 不生成 |
| vtable / 委托缓存 | 已建好的 vtable、已创建的 delegate 都是**指针副本** | 改 MethodInfo 对它们无效（§2.1） |
| 同程序集直接调用 | IL2CPP 生成直接 `bl` | 绕过 methodPointer（§2.1） |
| 版本演进 | 本 build `MethodInfo` 已有 `virtualMethodPointer`（+0x08）；metadata 版本 31 | 硬编码偏移迟早出事 → 只用 +0x00 + RVA 校验 |

> 结论：不存在"Unity 自己 inline hook 导致我们失效"的问题；真正的冲突点是**信号处理器**和**调用形态**。

### 5.4 SIGSEGV 排查（tombstone 全流程）

#### 第一步：拿到崩溃摘要

```bash
adb logcat -b crash -d
# F libc  : Fatal signal 11 (SIGSEGV), code 1 (SEGV_MAPERR), fault addr 0x0 in tid 12345 (UnityMain), pid 12300
```

关键信息：信号、`code`（`SEGV_MAPERR` = 访问未映射地址，`SEGV_ACCERR` = 权限不对）、`fault addr`、线程名。

#### 第二步：拿到 tombstone

| 方式 | 命令 | 适用 |
|---|---|---|
| bugreport（**无 root 唯一可靠**） | `adb bugreport bugreport.zip` → 解包后 `FS/data/tombstones/` | Android 10+ 全机型 |
| 直接读 | `adb shell ls -l /data/tombstones` → `adb pull` | root / eng 版 |
| DropBox | `adb shell dumpsys dropbox --print SYSTEM_TOMBSTONE` | 部分机型可用 |
| 退出原因 | `adb shell dumpsys activity exit-info <pkg>` | 看 `REASON_CRASH_NATIVE` |

> Android 13 起 tombstone 可能是 protobuf（`tombstone_NN.pb`），文本内容仍在 logcat 的 `DEBUG` 段里；两者结合看。

#### 第三步：符号化

```bash
# 方式 A：ndk-stack（需要未 strip 的 so 目录）
$NDK/ndk-stack -sym ./symbols/arm64-v8a -dump tombstone_00

# 方式 B：llvm-symbolizer（精确到行）
$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/llvm-symbolizer.exe \
    --obj=libsfsmp.so --functions=short 0x1a2c

# 方式 C：反汇编到指令
$NDK/.../llvm-objdump.exe -d --disassemble-symbols=hook_update libsfsmp.so
```

注意：tombstone 里 `#00 pc 000000000001a2c4 /data/app/.../lib/arm64/libsfsmp.so` 的 pc 是**相对该 so 的偏移**（不是绝对地址），直接拿这个偏移去符号化。

#### 第四步：按首帧归因

| 首帧位置 | 典型 fault addr | 含义 | 处理 |
|---|---|---|---|
| `libsfsmp.so (函数+0x..)` | 任意 | **我们的代码** | 用 `llvm-objdump --disassemble-symbols` 对照源码那一行 |
| `libil2cpp.so` | `0x0` / `0x8` / `0xc` / `0x10` | 空对象 / 错类（把 GameObject 当 Component 用） | 检查 `MethodInfo` 是否属于接收者的类 |
| `libil2cpp.so` 的 `Runtime::Invoke` | 任意 | `il2cpp_runtime_invoke` 参数 ABI 错 | 值类型参数要传**地址**；`exc` 必须初始化 NULL |
| `libc.so (abort+...)` | — | 断言 / 堆破坏 / `std::terminate` | 看 logcat 前面的 `Abort message` |
| `libunity.so` | 任意 | 引擎内部（渲染/场景/Transform） | 大概率是非主线程碰了 Unity 对象 |
| `libart.so` | 任意 | Java 层 / JNI 误用 | 检查 `JNIEnv` 是否属于当前线程 |

#### 第五步：二分定位

1. 用 `sfsmp-mode.txt` 切档（3 / 9 / 10 / 11），先确定"哪个功能"触发。
2. 在我们的 so 里加 `__android_log_print` 打点（v43 的 `log_app` 就是干这个的），每次进入可疑函数打一行。
3. 崩溃前最后一条日志 = 嫌疑函数；再用 `llvm-objdump` 看那条指令访问了哪个寄存器。

#### 我们踩过的坑（skill 已记录，复述给后来者）

- `Component.get_transform` 和 `GameObject.get_transform` 是**两个不同方法**，传错对象必崩（fault addr 0xc）。
- 伪造 `System.Action` 时 `method_ptr` 设了但 `method`（MethodInfo*）为 NULL → 一调用就崩。
- 在 hook 里做类型猜测的"诊断代码"（对 GameObject 调 `get_rect`）会在几百帧后杀死游戏，看起来像"功能导致崩溃"。
- `FindObjectsOfType` 在主线程可用，但每帧调用 = 每帧全场景扫描，几毫秒起步 → 用缓存 + 事件。

### 5.5 其它工程风险

| 风险 | 说明 | 缓解 |
|---|---|---|
| 只有 arm64 的 `libsfsmp.so` | APK 同时带 `armeabi-v7a` 的 libil2cpp；32 位设备上 `System.loadLibrary("sfsmp")` 会 `UnsatisfiedLinkError` | 要么只发 arm64 包（manifest `abiFilters`），要么补一份 v7a |
| 每次重装清存档 | 重签名包与官方签名不同，只能卸载重装；`Android/data/<pkg>` 会被清 | 备份到 `Android/media/<pkg>`（卸载保留） |
| Crashlytics 上报 | 崩溃会经 Firebase 上报给开发商 | 测试期断网或 hosts 屏蔽 |
| 掉帧 | hook 里做重活（`FindObjectsOfType`、`Instantiate`） | 队列 + 每帧预算（§3） |
| 版本漂移 | RVA、混淆名、字段偏移全变 | 一张"版本适配检查表"（附录 B），每次更新逐项核对 |

---

## 6. 落地路线（建议）

### v45：主线程队列 + 双 ticker + 状态快照（**零 inline hook**）

1. 把 `sfsmp_queue.c` 接进 `v43.c`，替换 `g_do_clone` 标志。
2. 保留 `VNDAREHWRIQNAAKC.Update` 的 methodPointer hook；世界内再加 `GameManager.Update`（8 ms 限流）。
3. 每帧（或每 4 帧）采集快照：
   - `WorldTime.main` → `worldTime`(+0x28)、`timewarpSpeed`(+0x30)
   - `GameManager.main` → `rockets`(+0x40) 的 `_size`/`_items` 指针集合
   - 每个 rocket → `location`(+0x38) → `position`(+0x28)/`velocity`(+0x30)（都是 `Obs<T>` 包装，按 §4.8 取值）
4. 快照 diff 出事件（火箭新增/消失、时间跳变），投递给网络层。
5. 所有"应用远端状态"的动作走队列，在 drain 里执行。

**验收**：连续 30 min 无 crash；`dropped=0`；`max_batch<64`；帧率与未注入版本差 < 5%。

### v46：时间同步

- 远端 `worldTime` 偏差 > 0.5 s 或 `timewarpSpeed` 不一致 → 投递 `task_apply_warp` 调 `SetTimewarpIndex_ForLoad`，必要时调 `SetState`。
- 若发现本地玩家能随意改倍率，再考虑 inline hook `SetState` 做拦截（此时才引入 ShadowHook）。

### v47：实体同步

- 先用 `rockets` diff 覆盖 90%。
- 需要"创建前分配网络 ID"时，再 inline hook `SpawnBlueprint`（1,528 B，安全）。
- 需要"销毁原因"时，再 hook `DestroyRocket`（208 B，入口有 `adrp`，可重定位）。

### 不建议做的事

- ❌ 对 `VNDAREHWRIQNAAKC.Update` 做 inline hook（20 B，零余量）。
- ❌ 对 `Rocket.OnDestroy`（28 B）/ 任何 ≤16 B 的方法做 inline hook。
- ❌ 同时 hook 同一方法的 methodPointer 和函数体（必然出现"一半调用进、一半不进"）。
- ❌ 在 worker 线程直接调任何 Unity 对象 API。
- ❌ 把 `mi[0]` 之外的结构体偏移硬编码（本版本已经多出 `virtualMethodPointer`）。

---

## 附录 A：数据来源与可复现方法

| 数据 | 来源 | 复现方式 |
|---|---|---|
| 方法 RVA | `dump/script.json` 的 `ScriptMethod[].Address` | 本报告用 Python 读取并建索引 |
| 方法长度 | 相邻 `ScriptMethod` 地址差（上界） | 同上 |
| 入口指令 / BTI / PAC | APK 内 `lib/arm64-v8a/libil2cpp.so`（56,998,304 B，SHA-256 与 `work/build/work/` 一致） | 按 ELF program header 做 RVA→文件偏移，读 16 B |
| `MethodInfo` 布局 | `dump/il2cpp.h:117` | dump 工具按该 il2cpp 版本生成 |
| `VirtualInvokeData` | `dump/il2cpp.h:5` | 同上 |
| Unity 版本线索 | `global-metadata.dat` 版本 = **31**；`libunity.so` 里只有 `2018.3.0a1`（**干扰项**，skill 已警告） | 读 metadata 头 8 字节 |
| Play Integrity 集成 | dex 字符串 / 类名 | `zipfile` 读 `classes*.dex` 搜 `integrity` / `playIntegrityToken` |
| 队列自测 | `sfsmp_queue.c` 的 `MPQ_TEST` 段 | `gcc -DMPQ_TEST` + TSan |

统计口径的局限：

- 长度是"下一个方法地址 − 本方法地址"，包含对齐填充和未列出的编译器辅助函数 → **真实短函数比例只会更高**。
- 只统计了 `ScriptMethod` 里的 136,034 个托管方法；不包含 icall、泛型实例、编译器生成的闭包。

## 附录 B：版本适配检查表（换游戏版本时逐项核对）

| # | 检查项 | 怎么查 | 期望 |
|---|---|---|---|
| 1 | `MethodInfo.methodPointer` 是否仍在 +0x00 | `il2cpp.h` 的 `struct MethodInfo` | 第一个字段 |
| 2 | 类名/命名空间是否变化 | `class_from_name(img, ns, cls)` 非 NULL | 每个 hook 点都打日志 |
| 3 | 方法 RVA 是否变化 | `(*(void**)mi) - base` == dump RVA | 全部匹配才安装 |
| 4 | 方法长度是否仍 ≥ 32 B | dump.cs 相邻 RVA | inline hook 点必须满足 |
| 5 | 入口前 16 B 是否新增 `bti`/`pacibsp` | 读 16 B | 若有，trampoline 要相应处理 |
| 6 | 字段偏移（worldTime/timewarpSpeed/rockets/location） | dump.cs 的 `// 0x..` | 与代码常量一致 |
| 7 | `VirtualInvokeData` 是否仍是两份指针 | `il2cpp.h` | 决定虚方法策略 |
| 8 | Play Integrity 是否新增/启用 | dex 搜 `integrity`、抓包看后端 | 影响能否继续用重签名包 |

## 附录 C：参考

| 主题 | 链接 |
|---|---|
| ShadowHook 手册 | https://github.com/bytedance/android-inline-hook/blob/main/doc/manual.md |
| ShadowHook 仓库 | https://github.com/bytedance/android-inline-hook |
| Dobby | https://github.com/jmpews/Dobby |
| ByteHook（PLT hook） | https://github.com/bytedance/bhook |
| Play Integrity 判定值 | https://developer.android.com/google/play/integrity/verdicts |
| Play Integrity 补救/篡改说明 | https://developer.android.com/google/play/integrity/remediation |
| ndk-stack | https://developer.android.com/ndk/guides/ndk-stack |
| MonoMod RuntimeDetour | https://monomod.github.io/api/MonoMod.RuntimeDetour.html |
| BepInEx IL2CPPDetourMethodPatcher（NativeDetour 组合的真实实现） | https://github.com/BepInEx/BepInEx/blob/master/BepInEx.IL2CPP/Hook/IL2CPPDetourMethodPatcher.cs |

---

*本文所有 RVA 针对 SFS Android 1.6.00.22（metadata v31）。换版本后 RVA 全部作废，按附录 B 重新核对。*



