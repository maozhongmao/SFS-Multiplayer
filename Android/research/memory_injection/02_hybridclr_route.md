# 02 · 卖家 HybridCLR / IL 运行时路线逆向

> 目标：判断能否把 **PC 端 C# 联机模组（MultiplayerSFS.Client / .Mod）** 直接跑进安卓 SFS 1.6.00.22 的 IL2CPP 进程，
> 复用卖家 SFS Pro 的做法（`libpatch.so`）。
>
> 分析对象（只读）：
> - `D:/SFS-Multiplayer/android/work/native/libpatch.so` —— 1,922,488 B，sha256 `e9a1610490c3b48c84f71529ef677393163a7f97f0ddf54631d0f147f7b8b017`（= 卖家 0930 版，与 `assets/bootstrap/libpatch.so` 逐字节一致）
> - `D:/SFS-Multiplayer/android/work/build/seller_libpatch.so` —— 2,060,672 B，sha256 `1f3d6979892a0678…3f78d34`（= 卖家 1004 版）
> - `D:/SFS-Multiplayer/android/work/native/libil2cpp.so` —— 56,129,120 B，sha256 `eee179ed563c50e0…`（= 卖家 1004 APK 内 `lib/arm64-v8a/libil2cpp.so`，已核对哈希）
> - `D:/SFS-Multiplayer/android/work/work/libil2cpp.so` —— 56,998,304 B，sha256 `2fadd661666bcbe1…`（另一份，疑似本机 1.6.00.22）
> - 真机日志 `work/build/native_diag.txt`、`work/build/log_now.txt`
>
> 工具：`research/memory_injection/tools/`（pyelftools + capstone，全部自写，无 IDA）
> 状态：**边做边写**，每节末尾标注证据级别（实证 / 推断）。

---

## 0. 结论先行（TL;DR）

1. **`libpatch.so` 里没有 HybridCLR 解释器主循环。** 全库 15,195 条字符串里没有任何 `interpreter / opcode / ILInstruction / MethodBridge` 相关串；
   与 HybridCLR 有关的只剩两个符号：`hybridclr::metadata::VTableSetUp::BuildByType`（真实代码）和
   `il2cpp::vm::MetadataCache::RegisterInterpreterAssembly`（**是一个 `ret` 空桩，RVA 0xad588**）。
2. 它真正的形态是 **"il2cpp 运行时 API 桩 + RVA 补丁器 + 反射桥 + ImGui 叠加层"**：
   - 175 个导出符号里有 **132 个** il2cpp / hybridclr 的 VM 符号（`il2cpp::vm::*`、`il2cpp::icalls::*`、
     `il2cpp_baselib::*`、`il2cpp_codegen_*`，以及 `Unbox_internal` 这类 C 名字的 VM helper），**全部是同一个模板的跳板**：
     ```asm
     adrp x16, #0x1d6c80      ; 函数指针表基址（.bss 起点）
     ldr  x16, [x16, #slot]   ; 每符号一个 8 字节槽
     cbz  x16, fail
     br   x16
     fail: mov x0, #0 ; ret
     ```
     （实证：0xac980 / 0xac9b8 / 0xad4a8 … 全部同一形状，槽步长 8）
   - 表里的指针由 **`dlsym("libil2cpp.so")`**（C API 那 22 个）和 **硬编码 RVA**（内部 C++ 符号）填充。
3. 因此 `libpatch.so` **不是"自带解释器"，而是"寄生在某个特定 libil2cpp 构建上的补丁包"**。
   它把"能不能跑 C# DLL"这件事完全押在 **libil2cpp 的确切版本** 上 ——
   我拿它去打我们的 1.6.00.22，80 项补丁 **0 命中**（实证，见 §5.4）。
4. 对我们（SFS **1.6.00.22**）的直接结论：
   **不能拿卖家的 `libpatch.so` 直接用** —— ①它压根不含解释器；②RVA 表对我们 0/80 命中；
   ③0930 版没有授权门，但 1004 版有（CEK / "not licensed" 拒载），复用会撞门。
   "自己上 HybridCLR 跑自编 DLL"**理论可行、工程上不划算**（需要与游戏完全一致的 il2cpp 源码工程）。
   **工程建议：走原生 C++ + 按名 hook（路线 C）**，这也正是卖家自己实际在做的事。

---

## 1. 引导链：谁在什么时候把 `libpatch.so` 拉起来

### 1.1 `JNI_OnLoad` 不是入口（反直觉，但实证）

`JNI_OnLoad` @ **RVA 0x12747c**（长 212 B）：

```asm
0x12748c  adrp x20, #0x125f000
0x127494  ldr  w8, [x20, #0x91c]    ; 静态 once 标志（.bss）
0x127498  cbnz w8, #0x127530        ; 已初始化 -> 直接返回
0x12749c  ...                        ; 构造 sigaction 结构
0x1274a4  adr  x8, #0x15f924        ; handler
0x1274ac  movk w9, #0x800, lsl #16  ; flags = 0x08000004
0x1274c4  bl   #0x1cda90            ; sigemptyset
0x1274d8  bl   #0x1cdaa0            ; sigaction(SIGSEGV=0xb, ...)
0x1274f8  bl   #0x1cdaa0            ; sigaction(SIGABRT=6, ...)
0x12752c  bl   #0x122998            ; log "XC-TRACE: handler installed (pos=%d)"
0x127540  mov  w0, #6 ; movk w0,#1  ; return JNI_VERSION_1_6 = 0x10006
```

它**只装崩溃处理器**，不做任何加载。真正的启动在 `DT_INIT_ARRAY`。

### 1.2 `DT_INIT_ARRAY`（5 项，实测重定位后）

`.init_array` @ 0x1d4b48，5 个 `R_AARCH64_RELATIVE`：

| # | 目标 | 作用 |
|---|---|---|
| 0 | 0x1c8c8c | 读 `__system_property_get` 判设备（"ro.build.version.release" 之类），置标志 |
| 1 | 0x1c92c0 | 同上，`pthread_once` 风格守卫 |
| 2 | **0xd35a4** | **`pthread_create(&t, 0, 0xd35dc, 0)` + `pthread_detach`** ← 主工作线程 |
| 3 | 0xeadf0 | 装 SIGHUP/SIGPIPE 等信号（`sigaction` × 4） |
| 4 | 0x123e60 | `dlopen("libEGL.so")` → `dlsym("eglSwapBuffers")` → `install_inline_hook_to` → **ImGui 叠加层**（日志 `IMGUI: eglSwapBuffers hooked (overlay ON)`） |

> 实证：0xd35a4 处 `bl 0x1cd980 ; -> pthread_create`、`bl 0x1cd990 ; -> pthread_detach`；
> 线程函数 0xd35dc 一进来就 `sub sp, sp, #1, lsl #12; sub sp, sp, #0x3b0`（≈4.9 KB 栈帧）。

### 1.3 主线程 0xd35dc 的完整时序（这是"启动链"的答案）

```
0xd35dc  线程入口
├─ [A] opendir("<ext>/Mods/Custom_Assets/Parts") + readdir
│      └─ 对每个 *.pack：读全文件 → strstr("\"CodeAssembly\":\"") → base64 解码
│         → store_dll / register_dll → 明文写回 Custom_Assets/CodeAssemblies/<name>.dll
│         → 把 .pack 里该项改写成 "CodeAssembly":null（.tmp + rename，一次性消费）
├─ [B] 0xd3cbc  bl 0x124ffc                     ; 读 InfoOverload/colors.txt 之类配置
├─ [C] 0xd3cc0  usleep(0x2dc6c0 = 3,000,000us)  ; 粗暴等 3 秒，等游戏把 libil2cpp 拉起来
├─ [D] 0xd3ccc  bl stubs_resolve (0xad5b4)      ; dlopen("libil2cpp.so", RTLD_NOLOAD=4) + 22×dlsym
├─ [E] 0xd3cd0  ldr w8,[0x1d8000+0xde4]; cbz → 0xd4184   ; 标志含义未定标，为 0 就直接进重试块
│      └─ 0xd4184 是重试块：
│           if (!il2cpp_domain_get) { dlopen("libil2cpp.so",4); resolve("il2cpp_domain_get");
│                                     resolve("il2cpp_domain_get_assemblies"); }
│           if (!domain_get || !domain_assemblies) → 0xd3cdc
│           domain = domain_get(); n = domain_get_assemblies(domain,&count)
│           把 Il2CppAssembly* 缓存进 0x1d83d0（上限 256）
│      └─ 0xd3cdc: usleep(0xf4240 = 1s) × 最多 0x27(39) 次  ← **真正的"等 IL2CPP"循环，总预算 ≈ 39 s**
├─ [F] 0xd3d04  bl 0xeae94   ; 补解析 il2cpp_class_get_field_from_name / il2cpp_field_get_name 等
│      0xd3d08  bl 0xeb420   ; 读 Mods/sfs_parts_census.txt（调试/统计）
├─ [G] 0xd3d10  mkdir("/storage/emulated/0/Android/media/<pkg>/Mods", 0777)
├─ [H] 0xd3d20  bl 0xaf738                       ; 建 Mods/Custom_Assets/... 子目录
├─ [I] 0xd3d24  interceptor_store_dll("uitools.dll", 0x8b3c3 /*.rodata 内嵌 PE*/, 0xe00=3584)
│      0xd3d40  interceptor_register_dll("uitools.dll")   ← **内置 shim 先注册**
├─ [J] 0xd3d4c  scan_mods_dir("<ext>/Mods")                     ┐
│      0xd3d68  scan_mods_dir("<ext>/Android/data/<pkg>/files/Mods") │ 三个根
│      0xd3d84  scan_mods_dir("/data/data/<pkg>/files/Mods")     ┘
├─ [K] 0xd3d88… 遍历同一批目录，读每个 mod 的 Textures/icon.png（UI 图标）
└─ …后续：加载各 mod 的配置、hook 安装、ImGui 初始化
```

关键 RVA 一览：

| 符号 | RVA | 说明 |
|---|---|---|
| `JNI_OnLoad` | 0x12747c | 仅装 SIGSEGV/SIGABRT handler，返回 0x10006 |
| `init_array[2]` | 0xd35a4 | 起主线程 |
| 主线程 | 0xd35dc | 上面整条时序 |
| `stubs_resolve` | 0xad5b4 | 解析 libil2cpp 的 22 个 C API |
| 等 IL2CPP 重试块 | 0xd4184 | `dlopen(RTLD_NOLOAD)` + domain 查询 |
| 重试睡眠 | 0xd3cdc | 1 s × 39 |
| `scan_mods_dir` | 0x130ba4 | Mods 目录扫描器 |
| `interceptor_store_dll` | 0xaf938 | 收字节、查黑名单、入 16 槽表 |
| `interceptor_register_dll` | 0xb032c | 只登记名字（**不碰 il2cpp**） |
| `interceptor_resolve_dll_image` | 0xaff50 | 在 `il2cpp_domain_get_assemblies` 结果里按名找 image |
| `interceptor_class_from_name` | 0xae97c | 反射：按 (asm, ns, name) 找类 |
| `interceptor_runtime_invoke` | 0xb08ec | 反射调用 |
| `bridge_inject_all_types` | 0xb221c | 遍历每个 DLL 的 TypeDef，绑定到 il2cpp 类 |
| `bridge_call_all_entry_points` | 0xb2674 | 调 `OnLoad` / `Main` |
| `bridge_call_all_onupdate` | 0xb7e8c | 每帧调 `OnUpdate` |
| RVA 补丁表 | 0x1d0828 | 80 项 × 0x20 B（见 §5） |
| 补丁器 | 0x13097c | 校验原指令字后写新指令字 |

### 1.4 `stubs_resolve` 到底解析了什么（0xad5b4）

```asm
0xad5f8  adrp x0, #0x1d7000 ; add x0,#0x70   → .data 里的 12 字节库名缓冲
0xad600  mov  w1, #2                          ; RTLD_NOW
0xad604  bl   dlopen
0xad610  adr  x1, #0x27f42                    ; "il2cpp_array_new"
0xad618  bl   dlsym
0xad620  ldr  w8,[x0]                         ; ★ 取函数第一条指令
0xad628  cmp  x9, #5                          ; ★ opcode 高 6 位 == 0b000101 = B
0xad634  add  x8, x0, w8, sxtw                ; ★ 顺着 b 跳板跟 4~5 跳
…
0xad6c0  str  x8, [x9, #0xc80]                ; 写入 0x1d6c80（跳板表第 0x80 槽）
```

两个细节值得记：

1. **库名是 XOR-0x5A 加密的**：`adr x0,#0x8aada` 指向 `"63836h9**t)5"`，经 `0x126608`（`eor w11,w11,#0x5a` 循环）
   解出 **`"libil2cpp.so"`**。同一把钥匙也用于授权 URL（与旧报告 `ANALYSIS.md` 一致）。
2. **它不信任 dlsym 的返回值**，会跟 `b` 跳板链（IL2CPP 的导出常常是 `b real_impl` 的一跳桩）。
   跟链上限 5 跳，越界则 `mov x8, xzr`。

解析到的 22 个 C API（按出现顺序，全部实证）：

```
il2cpp_array_new                      il2cpp_array_get_first_element_address
il2cpp_array_get_byte_length          il2cpp_class_from_type
il2cpp_raise_exception                il2cpp_assembly_get_image
il2cpp_class_from_name                il2cpp_domain_get_assemblies
il2cpp_class_init                     il2cpp_string_new_utf16
il2cpp_value_box                      il2cpp_codegen_runtime_class_init
il2cpp_array_length                   il2cpp_type_get_class_or_element_class
il2cpp_class_get_method_from_name     il2cpp_string_new
il2cpp_gchandle_new                   il2cpp_object_new
il2cpp_runtime_invoke                 il2cpp_domain_assembly_open
il2cpp_thread_attach                  il2cpp_thread_detach
```

> 另有 `il2cpp_domain_get`（0x25000+0x1ed）在 0xd41dc 处单独解析。

**这就是全部"原生 ↔ IL"的接触面**：只有 23 个 C API。没有 `il2cpp_gchandle_free`、没有 `il2cpp_field_get_value`、
没有 `il2cpp_class_get_methods`——需要遍历的地方全是**直接读结构体偏移**（见 §3）。

---

## 2. 导出面：132 个「il2cpp 跳板」+ 23 个 C API

### 2.1 跳板农场（Thunk farm）

`libpatch.so` 共 **175 个导出**，其中 **132 个**落在 `.text` 的 `0xac770 – 0xad5b4` 区间，
`st_size` 全是 **0**，每条都是同一模板（实证：0xac980 / 0xac9b8 / 0xac9d4 …）：

```asm
adrp x16, #0x1d6c80       ; 指针表基址 = .bss 起点
add  x16, x16, #0xc80
ldr  x16, [x16, #0x%02x]  ; 每符号独立槽（8 字节步长）
cbz  x16, .null
br   x16
.null:
mov  x0, #0               ; 返回 NULL（x0）/ 0（w0）
ret
```

**含义**：libpatch 的这份代码是**从 HybridCLR 的 il2cpp fork 源码编译出来的**，
但它把所有 `il2cpp::vm::*` 内部函数都编译成了「通过函数表间接调用」，
函数表在 `.bss 0x1d6c80`，运行期由 `stubs_resolve` / RVA 解析填充。

覆盖的命名空间（实证，dynsym）：

```
il2cpp::vm::{Array, Class, Object, String, Field, Assembly, Image, Thread, Runtime,
             InternalCalls, MetadataCache, GlobalMetadata, MetadataMalloc/MetadataCalloc,
             PlatformInvoke::Resolve, Exception::*}
il2cpp::icalls::mscorlib::System::{Type, RuntimeType, RuntimeTypeHandle, Threading::Interlocked}
il2cpp::gc::GarbageCollector::{AllocateFixed, FreeFixed, CallWithAllocLockHeld}
il2cpp::metadata::{GenericMetadata::*, GenericMethod::*, Il2CppTypeHash, Il2CppTypeEqualityComparer}
il2cpp_baselib::{Baselib_SystemFutex_Wait/Notify, Baselib_Thread_GetCurrentThreadId}
il2cpp_codegen_{runtime_class_init, delegate_begin_invoke, delegate_end_invoke,
                 raise_null_reference_exception, profiler_method_enter/exit}
hybridclr::metadata::VTableSetUp::BuildByType
```

### 2.2 只有 23 个函数是「真·dlsym」

`stubs_resolve`（0xad5b4）对 `libil2cpp.so` 做 `dlopen(name, RTLD_NOW)` + 22 次 `dlsym`，
外加 0xd41dc 的 `il2cpp_domain_get`。**除此之外全库没有别的 dlsym 调用点**。

推论（**强推断**）：剩下 ~110 个 `il2cpp::vm::*` 跳板的槽位**只能靠硬编码 RVA** 填。
这正是「`il2cpp-00 … il2cpp-79` 这种符号名 + RVA 表」存在的理由 —— 见 §5。

### 2.3 `dlopen` 用 `RTLD_NOLOAD`

`0xaffa8` / `0xb2278` / `0xd41d0` / `0xb01a8` 全部是 `mov w1, #4` → **RTLD_NOLOAD**。
即 libpatch **从不主动加载 libil2cpp**，只探测「游戏是否已经把它加载进来」。
这与 §1.3 的 39 次 × 1 s 重试配合，构成「等 IL2CPP」。

---

## 3. DLL 从哪来、怎么校验、有什么限制

### 3.1 三个搜索根（字符串实证）

```
/storage/emulated/0/Android/media/com.FLDPXIce.SpaceflightSimulatorPro/Mods
/storage/emulated/0/Android/data/com.FLDPXIce.SpaceflightSimulatorPro/files/Mods
/data/data/com.FLDPXIce.SpaceflightSimulatorPro/files/Mods
```

### 3.2 目录扫描器 `scan_mods_dir` @ 0x130ba4（逐条反汇编）

```c
opendir(dir);
while ((de = readdir(d))) {
    if (de->d_name[0] == '.') continue;
    if (!ends_with(de->d_name, ".dll")) continue;        // 比较串 ".dll" @0x1b2df
    if (strcmp(de->d_name, "SFSMod.dll") == 0) continue; // 框架保留名 @0x1605e
    for (i = 0; i < dll_count; i++)                      // 16 槽去重，步长 0xA0
        if (strcmp(slot[i].name, de->d_name) == 0) goto next;
    snprintf(path, "%s/%s", dir, de->d_name);
    f = fopen(path, "rb");
    fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
    if (size >= 0x100000) goto next;                     // ★ 硬上限 1 MiB（严格小于）
    buf = malloc(size); fread(buf, 1, size, f); fclose(f);
    interceptor_store_dll(de->d_name, buf, size);
    interceptor_register_dll(de->d_name);
    log("FOUND-ROOT: %s/%s (%ld bytes)");
    mkdir("Mods/<basename>", 0777);                      // 归位成 Mods/<名字>/<名字>.dll
    rename(path, "Mods/<basename>/<名字>.dll");
    free(buf);
}
```

> 1 MiB 判定的汇编：`mov x9, #-0x100001; add x8, size, x9; cmp x8, x9; b.ls skip`
> → 无符号比较，**size >= 0x100000 即跳过**（实证 0x130d90–0x130d9c）。

### 3.3 `interceptor_store_dll` @ 0xaf938（16 槽 + 前缀黑名单）

```c
if (!name || !data || len < 1) return;
if (g_count > 0xf) return;                       // 最多 16 个 DLL
for (i = 0; i < 16; i++)                          // 前缀黑名单，strncmp(name, prefix, strlen(prefix))
    if (strncmp(name, bl[i], strlen(bl[i])) == 0) return;
for (i = 0; i < g_count; i++) if (!strcmp(slot[i].name, name)) return;   // 去重
slot = &g_slots[g_count++];                       // 0x1d71a8，步长 0xA0
strncpy(slot->name, name, 0x7f);
slot->size = MIN(len, 0x100000);
slot->data = malloc(slot->size); memcpy(slot->data, data, slot->size);
return pe_validate_and_enum_types(slot->data, slot->size, NULL, NULL);   // tail-call 0xafa98
```

**16 条前缀黑名单（实证，逐条解引用 0x1d0308）**：

```
Microsoft   System    Mono.      Unity.     UnityEngine.  netstandard  Newtonsoft.
ICSharpCode.  JetBrains.  nunit.  xunit.  SkiaSharp  HarfBuzzSharp  Firebase.  Google.  mscorlib
```

即：**不允许由 Mods 目录提供/覆盖 BCL 与 Unity 程序集**，要用就直接引用游戏 build 里已有的。

### 3.4 `interceptor_register_dll` @ 0xb032c —— 它**不做任何加载**

反汇编全过程只有：查黑名单 → 查重 → `strncpy` 名字 → 日志 `REG: DLL %d registered: '%s'` → `count++`。
**没有一次 `dlsym` / `il2cpp_*` 调用**（实证：0xb032c–0xb0458 内全部 `bl` 目标只有
`strlen / strncmp / strcmp / strncpy / 0xb0488(log)`）。

### 3.5 `.pack` 里的 `"CodeAssembly"` 通道（0xd378c 起）

这是 0930 版**唯一**的「大体积代码」入口（`mod.xml` 在本版只剩删除逻辑，见 0xd47d0–0xd47f4 的三次 `remove()`）：

```c
opendir("<ext>/Mods/Custom_Assets/Parts");
for each "*.pack":
    read whole file (>= 0x3C00001 则跳过 = 60 MiB 上限)
    p = strstr(json, "\"CodeAssembly\":\"");
    if (!p) continue;
    name = basename_without_ext + ".dll";            // 截断 ".pack" 后补 ".dll"
    value = until next '"';
    bytes = base64_decode(value);                    // 0xd3864 起的手写 base64
    interceptor_store_dll(name, bytes, len);
    interceptor_register_dll(name);
    slot->from_pack = 1;                             // [slot+0x98]
    write("<ext>/Mods/Custom_Assets/CodeAssemblies/<name>", bytes);   // 明文落盘
    replace json: "\"CodeAssembly\":\"...\"" -> "\"CodeAssembly\":null"
    write "<pack>.tmp" -> rename()                   // 一次性消费
```

> 这是「资源包内嵌代码」的走私通道：把 DLL 塞进任意 `.pack` 的 JSON，框架会**自动解出来、落盘、并抹掉原文**。

### 3.5b 关于 `mod.xml` 的更正

`docs/seller_dll_runtime_report.md` §4.3 称「读 `Mods/mod.xml` → `strstr("\"CodeAssembly\":\"")` → ≤60 MB」。
**本次实测不支持这个说法**：

- 0930 版里三个 `mod.xml` 路径（0x23c49 / 0x2a78a / 0x2e7bd）的**唯一**引用点是 0xd47d0–0xd47f4 的三次 `bl remove()`
  （`adrp x0,#0x2a000; add #0x78a` → `bl 0x1cda60 ; -> remove`），**没有 `fopen`、没有 `strstr`**。
- `"CodeAssembly":"` 这个串（0x1dc98）的**唯一**引用点是 0xd378c，位于 `.pack` 扫描循环里（§3.5）。
- 1004 版里 `CodeAssembly` 也只有同样两处；三处 `mod.xml` 字符串的引用点集中在 0xe63xx（推断同为清理逻辑，未逐条反汇编）。

**结论**：本框架的"清单式加载"实际是 **`.pack` 内的 `CodeAssembly` 字段**（≤60 MiB，base64），
`mod.xml` 只是被删除的遗留物（可能供卖家的注入器一次性投递，读完即删）。
对我们的意义：**没有"把一个 xml 丢进去就能加载"的旁路**，只能走 `Mods/*.dll`（≤1 MiB）或伪造 `.pack`。


### 3.6 `pe_validate_and_enum_types` @ 0xafa98（1208 B，手写 PE/CLI 解析）

不是 `Assembly.Load`，而是**只读地**走一遍 CLR 元数据：

```
MZ(0x5A4D) → e_lfanew@0x3C → "PE\0\0" → OptionalHeader.Magic 0x10b/0x20b
→ DataDirectory[14] (CLI header) RVA+Size 非 0
→ CLI header → Metadata RVA/Size
→ "BSJB"(0x424A5342) → StreamHeader 数 → 找 "#~" 与 "#Strings"
→ 读 "#~" 的 row counts / heap sizes / valid 位图 → 遍历 TypeDef 表
→ 输出结构（0x408 B）：
     +0x000  const char* typeName[]
     +0x200  const char* typeNamespace[]
     +0x400  int         typeCount
```

**关键**：这个函数**没有任何 `il2cpp_*` 调用、没有分配 Il2CppClass、没有写任何 VM 表**
（实证：0xafa98–0xaff50 的 `bl` 目标只有 `strlen/strcmp/dlopen/dlsym/xor/0xb01bc/0x1c8c60/memcpy`）。

---

## 4. 「把游戏类暴露给 IL 运行时」—— 实际存在的东西

### 4.1 `bridge_inject_all_types` @ 0xb221c

```c
mscorlib = find_assembly_by_substr("mscorlib");        // 从 0x1d83d0 的缓存里 strstr
objectKlass = il2cpp_class_from_name(mscorlib_image, "System", "Object");   // 只为验证 API 可用
for (each dll in g_slots[0..15]) {
    if (name == "uitools.dll" || name == "modui.dll") continue;
    if (already_in_injected_list(dll)) continue;        // 0x3c2288，上限 16
    pe_validate_and_enum_types(dll->data, dll->size, cb=0xb2600, out=&info);
    for (i = 0; i < info.typeCount; i++) {
        ns = info.ns[i]; nm = info.name[i];
        if (ns == "UnityEngine" || ns == "Unity" || ns == "UnityEditor") continue;
        if (strncmp(ns, "System", 6) == 0) continue;
        if (strncmp(ns, "SFS.", 4) != 0) continue;      // 只处理 SFS.* 命名空间
        if (strncmp(ns, "SFS.Pro", 7) == 0) continue;
        klass = il2cpp_class_from_name(img, ns, nm);         // ← 在游戏侧程序集里找同名类（img 来源未完全定标）
        dll->classMap[j++] = klass;                          // 存进 slot+0x90 的表（calloc(0x1000)，[+8]=DLL 名）
    }
}
```

**它做的事**：把 mod DLL 里**与游戏同名的 `SFS.*` 类型**在游戏侧解析出 `Il2CppClass*`，建一张 name→klass 映射。
这**不是**「把 mod 类型注册进 VM」，而是「给 mod 类型找对应的游戏类」（用于后续 hook / 反射调用）。
注意：**没有任何一步把 mod 的 `Il2CppImage` 创建出来**。

### 4.2 伪造 `MethodInfo` 的委托桥 —— `interceptor_trigger_injection` @ 0xb071c

这是全库唯一「看起来像 reverse-pinvoke」的地方，但它是**硬编码的冒烟测试**：

```c
dll = find_slot(name);
if (!dll->data) return;
mscorlib = find_assembly_by_substr("mscorlib");
f_class_from_name = *(void**)(0x1d7080);              // 跳板表第 1 槽
objectKlass = f_class_from_name(mscorlib_image, "System", "Object");
mi = calloc(1, 0x140);                                 // ★ 手工拼一个 MethodInfo
*(char**)(mi+0x10) = strdup("Hello");
*(char**)(mi+0x18) = strdup("TestMod");
*(void**)(mi+0x20) = mi;
*(uint8_t*)(mi+0x2a) = 0x12;  *(uint8_t*)(mi+0x3a) = 0x12;
*(int*)(mi+0xf8) = 0x10;      *(int*)(mi+0x100) = 0x10;
*(void**)(mi+0x30) = mi;      *(int*)(mi+0x118) = 0x100001;
*(void**)(mi+0x48) = mi;      *(void**)(mi+0x58) = objectKlass;
*(void**)(mi+0x78) = mi;      *(uint8_t*)(mi+0x130) = 1;  *(uint8_t*)(mi+0x135) = 0xFF;
```

名字写死 `"TestMod"` / `"Hello"`，**没有给 `methodPointer`（+0x00）赋值**，函数也没有返回任何东西。
结论：这是**作者验证 `MethodInfo` 结构体布局的残留代码**，不是可用的 reverse-pinvoke 桥。

### 4.3 `hook_register_csharp` @ 0xb764c —— 真正被用的「挂 C# 方法」机制

```c
if (g_hook_count > 0x3f) return -1;                    // 最多 64 条
slot = &g_hooks[g_hook_count];                          // 0x1eb198，步长 0x168
strncpy(slot->cls,  cls,  0x7f);
strncpy(slot->meth, meth, 0x7f);
snprintf(slot->sig, 0x40, "%s", arg4 ? arg4 : meth);
slot->flags = arg3;  slot->state = 1; slot->enabled = 0;
slot->fn    = hookptr;                                  // +0x160
```

**注册时只写表，不解析、不 patch**。真正安装发生在后面的 installer（属于 04 线 hook 技术选型）。
重点是：**它的 key 是 (类名, 方法名) 字符串，不是 MethodInfo 指针**，
安装时必须做一次 `il2cpp_class_from_name` + `il2cpp_class_get_method_from_name` ——
这说明**卖家自己的 mod 也是「按名字找方法再 patch 机器码」**，与 IL 解释器无关。

---

## 5. RVA 补丁表：80 个「il2cpp-NN」站点，硬绑定 libil2cpp 构建

### 5.1 表结构（实证）

- 表基址 **0x1d0828**（0930）/ **0x1f23f0**（1004），**80 项 × 0x20 字节**：

```c
struct patch_entry {          // 0x20 bytes
    uint32_t rva;             // +0x00  libil2cpp 的**虚拟地址**（不是文件偏移）
    uint32_t orig;            // +0x04  期望的原指令字（校验用）
    uint32_t new;             // +0x08  要写入的指令字
    uint32_t pad;             // +0x0c
    const char *name;         // +0x10  "il2cpp-00" … "il2cpp-79"
    const char *note;         // +0x18  "mov w8, wzr -> mov w8, #1"
};
```

### 5.2 应用器 `apply_il2cpp_patch_table` @ 0x13097c（逐条反汇编）

```c
if (g_state) return;                       // 0x3d70430，只跑一次
base = find_libil2cpp_base_by_dl_iterate_phdr(cb=0x12c2e0);    // 0x1309c4；cb 内 strstr(phdr->dlpi_name, "libil2cpp.so")
for (i = 0; i < 80; i++) {
    uint32_t *p = (uint32_t*)(base + e[i].rva);
    if (*p == e[i].new) { already++; continue; }        // 已打过
    if (*p != e[i].orig) {                              // 版本不符
        if (skipped++ < 2) log("IL2CPP-PATCH: skip %s at +0x%x got=%08x want=%08x (%s)");
        continue;
    }
    mprotect(page_align(p), page_size, PROT_READ|PROT_WRITE|PROT_EXEC);
    *p = e[i].new;
    __clear_cache(p, p+4);
    mprotect(page_align(p), page_size, PROT_READ|PROT_EXEC);
    applied++;
}
g_state = (failed || skipped) ? 2 : 1;
if (g_state == 2) log("IL2CPP-PATCH: DEFECT - libil2cpp differs from the build this table targets; "
                      "entitlement patch incomplete");
```

> `0x130a30` `ldr w19,[x8,#8]` = new；`0x130a3c` `ldr w7,[x8,#4]` = orig；`0x130a20` `ldr w5,[x8]` = rva（实证）。

### 5.3 这 80 项到底在干什么（按 (orig→new) 聚类）

| 条数 | orig | new | 语义 |
|---|---|---|---|
| 17 | `2a1f03e8` = `mov w8, wzr` | `52800028` = `mov w8, #1` | 布尔返回值强制 true |
| 17 | `52800088` = `mov w8, #4` | `52800028` = `mov w8, #1` | 枚举/分级返回值降为 1 |
| 17 | `52800068` = `mov w8, #3` | `52800028` = `mov w8, #1` | 同上 |
| 12 | `12000108` = `and w8, w8, #1` | `52800008` = `mov w8, #1` | 位测试恒真 |
| 7 | `2a1f03e1` = `mov w1, wzr` | `52800021` = `mov w1, #1` | 参数强制 1 |
| 5 | `12000108` | `52800028` | 同上（另一种注释） |
| 1 | `52800020` = `mov w0, #1` | `52800000` = `mov w0, #0` | 反向：恒 false |
| 3 | `f945e108` / `f945d908` = `ldr x8,[x8,#0xbc]` / `#0xb2` | `f945dd08` = `ldr x8,[x8,#0xba]` | 换字段偏移（读另一个成员） |
| 2 | `54000380` / `54000249`（`b.ls` / `b.hi`） | `d503201f` = `nop` | 跳过分支 |

**判定：这是"内购/授权解锁"补丁表**，不是 HybridCLR 相关。
旁证：`libs.so` 里同名的 `il2cpp-00 … il2cpp-79` 字符串、日志
`EARLY-IL2CPP-PATCH: begin (about to write entitlement words)`、
`EARLY-FIX: parts=unlock rva=0x177bdb8 -> 1400001a`（写一条 `b` 跳过函数）。

### 5.4 版本绑定的定量证据（本次亲手跑的对照实验）

**注意**：表里的 rva 是**虚拟地址**；`libil2cpp.so` 的 RX 段 `p_offset=0x1375d90 / p_vaddr=0x1379d90`，
所以**文件偏移 = vaddr - 0x4000**。第一版脚本按文件偏移读，得到"80/80 全不符"的假结论；
改用 program header 映射后：

| 补丁表 | 目标 libil2cpp | 结果 |
|---|---|---|
| 1004 (`seller_libpatch.so` @0x1f23f0) | `native/libil2cpp.so` = **1004 APK 原配**（eee179ed，56,129,120 B） | **MATCH 80 / MISMATCH 0** |
| 0930 (`native/libpatch.so` @0x1d0828) | 同上 | **MATCH 80 / MISMATCH 0** |
| 1004 表 | `work/libil2cpp.so`（2fadd661，56,998,304 B） | **MATCH 0 / MISMATCH 80** |
| 0930 表 | `work/libil2cpp.so` | **MATCH 0 / MISMATCH 80** |

再判定哪一份才是**我们自己的 1.6.00.22**：用 `dump/script.json` 随机抽 300 个 `ScriptMethod.Address`，
在两边反汇编首条指令，看是否落在合法函数开头：

| libil2cpp | 命中率 |
|---|---|
| `native/libil2cpp.so`（卖家 1004） | **55 %** |
| `work/libil2cpp.so`（2fadd661） | **95 %** ← 我们的 1.6.00.22（推断，依据 `dump/script.json` 地址命中率） |

**结论（实证）**：
1. 卖家 0930 与 1004 两个 `libpatch.so` 打的是**同一份 libil2cpp**（两者表 80/80 都能对上），
   说明卖家所有版本共用同一个游戏原生构建；
2. 我们的 **1.6.00.22 是另一份 libil2cpp**，卖家的 80 个站点**全部错位**；
3. 推而广之：任何"按 RVA 打补丁/挂钩子"的方案（包括 libpatch 里那 ~110 个靠 RVA 填充的跳板）
   在 1.6.00.22 上**必须重新生成整张表**，不能复用。

### 5.5 真机日志旁证

`work/build/native_diag.txt`（卖家包在我们机器上的实跑日志）：

```
EARLY-IL2CPP-PATCH: begin (about to write entitlement words)
EARLY-IL2CPP-PATCH: base=0x763920b000 total=80 applied=80 already=0 skipped=0 failed=0
EARLY-FIX: parts=unlock rva=0x177bdb8 -> 1400001a
EARLY-FIX: astro_off=0 unlock_parts=1 (files: sfs_astro_off.txt / sfs_unlock_parts.txt)
```

`applied=80 / skipped=0` 证明这套表在**卖家那份 libil2cpp** 上是 100% 命中的 —— 与 §5.4 的静态对照完全一致。

---

## 6. 决定性否定结论：`libpatch.so` 里没有 IL 解释器

这是本报告最重要、也最容易被符号名误导的一点。五条独立证据：

| # | 证据 | 观察 |
|---|---|---|
| 1 | `MetadataCache::RegisterInterpreterAssembly` @ **0xad588** | 反汇编只有一条 **`ret`**（`st_size=0`）。真正的 HybridCLR 实现会去改 `MetadataCache` 的 s_AssemblyMap / 分配 `Il2CppImage`。 |
| 2 | 字符串域 | 15,195 条字符串里 **0 条** 命中 `interpreter / Interpreter / opcode / ILInstruction / MethodBridge / TransformContext / hi_op`（唯一命中 `opcode` 的是 libunwind 的 `DWARF opcode not implemented`）。 |
| 3 | 解释器 dispatch 表 | 在 `.data.rel.ro` 里找"连续指向 `.text` 的 8 字节重定位"，**最长 run = 0**；在 `.rodata` 里找"连续指向 `.text` 的 u32"（switch 跳转表形态），**最长 run = 14**。HybridCLR 的 256+ opcode 分发表不存在。 |
| 4 | `interceptor_register_dll` | 只写名字表，零 `il2cpp_*` 调用（§3.4）。 |
| 5 | `pe_validate_and_enum_types` | 只读解析 PE/CLI 元数据，输出 TypeDef 名字数组，不创建任何 VM 对象（§3.6）。 |

再加两条体量证据：
- `.text` 只有 **0x120d38 = 1,182,520 B**；其中已确认含 **lnjector**（inline hook 引擎，
  字符串 `unsupported arm64 register` / `unsupported restore location for register` / `reinterpret_cast`）、
  **Dear ImGui + OpenGL3 后端**（`ImGui_ImplOpenGL3_CreateDeviceObjects` 等 40+ 条）、
  以及大量按功能写死的业务代码（`CREW-PATCH` / `PICKGRID` / `MP-*` / `ANAIS` / `SmartSAS`）。
  HybridCLR 的解释器 + metadata 在 arm64 上单是 `.text` 就 ≥1.5 MB，**放不下**。
- `.bss` = 0x108c788 = **17.3 MB**，看着像"解释器栈"，实际是 ImGui 字体图集 + JNI 缓存 + 各功能缓冲区。

**因此**：早前 `docs/seller_dll_runtime_report.md` 的结论「包内置 HybridCLR IL 解释器，可以把第三方 .NET DLL 注册进 il2cpp 并解释执行」
是**从符号名推断出来的，未验证符号体**；实测该符号是空桩。
该报告的 §7「能，但要移植」应下调为「**在我们手上的这两份 libpatch 里不能**」。

> 保留意见（诚实标注）：卖家可能还有第 4 个版本的 libpatch（由他们的注入器按机器下发），
> 我们只拿到 0930 / 1004 两份。要 100% 排除，需要按 §8.2 的探针实验在真机上验证一次。

---

## 7. 可行性结论

### 7.1 三条路线的判定

| 路线 | 做法 | 判定 | 理由 |
|---|---|---|---|
| **A. 直接复用卖家 libpatch 跑我们的 DLL** | 把 `MultiplayerSFS.Mod.dll` 丢进 `Mods/` | **不可行** | ①无解释器（§6）；②`register_dll` 不加载（§3.4）；③RVA 表对我们 0/80 命中（§5.4）；④若想自带 `System.*` / `UnityEngine.*` 兼容层会被黑名单直接拒（§3.3） |
| **B. 自己上 HybridCLR（官方路线）** | 用 HybridCLR 的 il2cpp fork 重编译 `libil2cpp.so` | **理论可行，工程代价极高** | 需要**与游戏完全一致的 Unity/il2cpp 源码工程**才能编出 ABI 兼容的 `libil2cpp.so`；我们只有二进制，没有工程。替代是"补丁式"（像 libpatch 那样用 RVA 挂解释器），要自己写 100+ 个 hook 并逐版本重做 |
| **C. 原生 C++ 移植 + 按名 hook** | 把联机逻辑用 C++ 实现，用 il2cpp C API + inline hook 挂到游戏方法上 | **推荐** | 卖家的 5 个"mod"实际就是这么做的（`hook_register_csharp` 的 key 是类名+方法名，§4.3）；与 03 线（KCP/协议/世界同步 C++ 移植）天然合流 |

### 7.2 如果一定要走 B（跑真正的 C# DLL），前置清单

按重要性排序，每条都给出**验收标准**：

1. **版本对齐**（硬门槛，不可绕过）
   - 需要：游戏 Unity 版本 + il2cpp 版本（从 `global-metadata.dat` 头部版本号 + `libil2cpp.so` 的 build-id 判定）。
   - 验收：能编译出与 `work/libil2cpp.so`（2fadd661…）**导出符号表一致、`.text` 段布局可对齐**的 `libil2cpp.so`。
   - 现状：**不具备**。这是整个路线 B 的卡点。
2. **编译引用换血**
   - 不能用 PC 的 `Assembly-CSharp.dll` / `UnityEngine.dll`（Mono 版，签名与 il2cpp 版不同）。
   - 必须用 `android/dump/DummyDll/`（**已有 84 个程序集**，含 `Assembly-CSharp.dll` 2.6 MB、`UnityEngine.CoreModule.dll`、`mscorlib.dll`、`UnityEngine.UI.dll`、`Newtonsoft.Json.dll`）。
   - 验收：mod 工程 `ReferencePath` 指向 DummyDll，`dotnet build` 0 error。
3. **TFM 与裁剪**
   - 目标框架 **netstandard2.0**（HybridCLR 支持面最广）；去掉 net48 专属 API。
   - 去掉 **Costura.Fody**（`Assembly.Load(byte[])` 在解释器域不可用），依赖拆成独立 DLL。
   - 去掉 **Harmony / MonoMod**（依赖 `Reflection.Emit` / `DynamicMethod`，解释器域没有 JIT）。
     → 我们 `MultiplayerSFS.Mod.Patches/` 下的大量 Harmony 补丁**必须逐个改写成显式 hook 调用**。
   - 验收：`ildasm`/`ILSpy` 里无 `Reflection.Emit`、无 `Assembly.Load(byte[])`、无 `DynamicMethod`。
4. **AOT 泛型**
   - 解释器能处理多数泛型实例化，但 **AOT 已编译的泛型方法**（如 `List<T>.Add` 对值类型）需要"补充元数据"。
   - 需要 `AOTGenericReferences.cs`（HybridCLR 生成）或 `link.xml` 保留，且 `Il2CppCodeGen` 侧不能把这些类型裁掉。
   - 我们的风险点：`Dictionary<int, PlayerState>`、`List<Part>`、`UniTask<T>`、`Newtonsoft` 的 `JsonConverter<T>`。
   - 验收：真机跑一遍所有泛型路径，无 `ExecutionEngineException` / `MethodAccessException`。
5. **依赖体积与命名**
   - 单 DLL < 1 MiB（卖家扫描器硬限），或走 `.pack` 的 `CodeAssembly`（≤60 MiB，base64 膨胀 4/3）。
   - 名字不能撞 16 条前缀黑名单 → `Lidgren.Network.dll` 可以，`System.Buffers.dll` / `Microsoft.*` 不行（要内联或改名）。
6. **网络栈**
   - 好消息：`dump.cs` 里 **`System.Net.Sockets` 存在**（`class Socket` 15 处），说明 BCL 未被裁掉，
     Lidgren 的 `Socket`/`UdpClient` 有希望直接用（仍需真机验证 `socket()` P/Invoke 在解释器域是否放行）。
   - 坏消息：`Socket` 是 AOT 代码，解释器调用它没问题；但 **`SocketAsyncEventArgs` 回调进解释器**需要 reverse-pinvoke，HybridCLR 支持但有坑。

### 7.3 风险矩阵

| 风险 | 概率 | 影响 | 缓解 |
|---|---|---|---|
| 拿不到匹配的 il2cpp 源码工程 | **高** | 路线 B 直接死 | 转 C；或改用"补丁式解释器"但需自建 RVA 表 |
| Harmony 补丁改写工作量 | **高** | 数周 | 先做"最小可玩集"（只保留连接/同步），其余砍掉 |
| AOT 泛型缺失 | 中高 | 运行期崩 | 提前用 `AOTGenericReferences` + 真机回归 |
| 游戏完整性校验（APK 签名/文件哈希） | 中 | 改包即拒 | 已有 `libs.so` 的 ED25519 机制说明卖家也改包，需自行处理 |
| 反作弊/封号 | 低 | 账号 | 单机/私服场景忽略 |
| 每次游戏更新重做 | **高** | 长期维护成本 | 用"符号名 + 特征码"而非纯 RVA（见 04 线） |

### 7.4 我们模组的「现状体检」（只读统计，`Client/src`）

| 指标 | 实测值 | 对移植的含义 |
|---|---|---|
| 源文件 | **112** 个 `.cs` | 全量搬 = 大工程 |
| 代码量 | **16,042** 行 | 同上 |
| 目标框架 | `net48` | 必须改 `netstandard2.0`（若走 B） |
| 程序集名 | `SFS-Multiplayer-1.6-V1.4.2.5` | 无冲突 |
| 打包 | **Costura.Fody 5.7.0**（单 DLL 内嵌依赖） | 与解释器域**根本不兼容**，必须拆包 |
| 关键引用 | `Assembly-CSharp`、`Assembly-CSharp-firstpass`、`0Harmony`、`Lidgren.Network`、`UITools`、`Unity.*`、`DnsClient` | `0Harmony` 必须去；`UITools` 在安卓侧只有 `Init/Version` 空壳 |
| Harmony 使用 | **18** 个文件含 `HarmonyLib` / `HarmonyPatch` | 路线 B 需全部改写；路线 C 直接删（改用原生 hook） |
| `Reflection.Emit` / `DynamicMethod` | **3** 处 | 解释器域不可用，必须改写 |

**结论**：即使"跑通 hello-world DLL"成立，把 16k 行 + 18 个 Harmony 补丁搬过去仍需数周；
而 §6 已证明**连 hello-world 都跑不起来**。所以 02 线的净产出是「**排除 HybridCLR，转向原生**」，
这与 03 线（KCP/协议/世界同步 C++）和 04 线（hook 选型）是同一个方向。

### 7.5 给 03/04 线的接口建议（本报告的直接可复用物）

1. **按名 hook 而不是按 RVA**：`hook_register_csharp` 的设计（类名+方法名 → 延迟解析）值得抄，
   因为它在游戏小版本更新时只需改解析、不用改表。
2. **`dlopen("libil2cpp.so", RTLD_NOLOAD)` + `dl_iterate_phdr` 找基址** 是可靠且无侵入的取基址方式（§1.3 [E]）。
3. **`dlsym` 后跟 `b` 跳板链**（最多 5 跳）必须做，否则会拿到 il2cpp 的导出桩而不是真实实现（§1.4）。
4. **23 个 C API 足够**做「找类→找方法→调用/挂钩」：`il2cpp_domain_get_assemblies`、`il2cpp_class_from_name`、
   `il2cpp_class_get_method_from_name`、`il2cpp_runtime_invoke`、`il2cpp_object_new`、`il2cpp_string_new`、
   `il2cpp_array_new`、`il2cpp_gchandle_new`、`il2cpp_thread_attach`。
5. **需要遍历成员时直接读结构体偏移**（本报告实测：`Il2CppClass.methods @ +0x108`、`method_count @ +0x110`、
   `MethodInfo.name @ +0x18`、`methodPointer @ +0x00`）——但**这些偏移随 il2cpp 版本变化**，必须用 §8.2 的方式在目标机上校验。

---

## 8. 验证实验（最小可证伪）

### 8.1 实验 1：确认「DLL 只被登记、不被执行」（成本 30 分钟，结论最硬）

1. 写 `HelloMod`（netstandard2.0，**不引用任何游戏程序集**）：

```csharp
namespace HelloMod {
    public static class Main {
        public static void OnLoad()  { System.IO.File.WriteAllText(
            "/storage/emulated/0/Android/media/com.FLDPXIce.SpaceflightSimulatorPro/Mods/hello_onload.txt", "onload"); }
        public static void OnUpdate(){ /* 每帧 append 一行 */ }
    }
    public static class Probe { public static string Ping() => "pong"; }
}
```

2. `adb push HelloMod.dll /storage/emulated/0/Android/media/<pkg>/Mods/`
3. 启动卖家包，等 30 s，检查：
   - `Mods/mods_scan.txt` 是否出现 `SCAN-ENTER: <dir>` 与 `FOUND-ROOT: … HelloMod.dll (N bytes)` → **证明扫描/存储生效**
   - logcat 是否出现 `REG: DLL %d registered: 'HelloMod.dll'` → **证明登记生效**
   - `hello_onload.txt` 是否出现 → **证明执行**
4. **判定**：
   - 前两项有、第三项无 → 与 §6 结论一致（**只登记不执行**），路线 A 彻底排除；
   - 三项都有 → 说明存在我们没找到的解释器路径，需回头补 §6（用 `frida-trace` 跟 `interceptor_register_dll` 的调用栈）。

### 8.2 实验 2：确认「有没有解释器」的运行时探针（成本 1 小时）

在 `interceptor_store_dll`（0xaf938）、`interceptor_register_dll`（0xb032c）、
`bridge_call_all_entry_points`（0xb2674）三处下 `frida` Interceptor.attach，打印调用栈与参数：

```js
const m = Process.getModuleByName("libpatch.so");
["0xaf938","0xb032c","0xb2674"].forEach(o =>
  Interceptor.attach(m.base.add(o), { onEnter(a){ console.log(o, a[0], a[1], a[2]);
    console.log(Thread.backtrace(this.context, Backtracer.ACCURATE).map(DebugSymbol.fromAddress).join("\n")); }}));
```

**判定**：若 `0xb2674` 从未被调用（或调用时 `count==0`），则确认"入口点调用链根本没跑起来"。

### 8.3 实验 3：路线 B 的 hello-world（如果决定自建 HybridCLR）

1. 用 `DummyDll` 作引用，`netstandard2.0` 编 `HelloMod.dll`；
2. 用 HybridCLR 官方 `HybridCLRSettings` + `AOTGenericReferences` 生成补充元数据；
3. 目标机装一个**我们自己编译的** libil2cpp（先不管游戏，用同版本 Unity 的空工程验证 ABI）；
4. 验收顺序：`il2cpp_domain_get_assemblies` 能看到我们的 assembly → 能 `class_from_name` 找到 `HelloMod.Main` →
   能 `runtime_invoke("OnLoad")` → 能每帧 `OnUpdate`。
5. **只要第 3 步过不了（ABI/版本），路线 B 就不成立** —— 这也是最省时间的止损点。

---

## 9. 附录

### 9.1 RVA 速查（0930 `native/libpatch.so`）

| 名称 | RVA |
|---|---|
| `JNI_OnLoad` | 0x12747c |
| `init_array`（5 项） | 0x1d4b48 |
| 主线程启动 | 0xd35a4 → 线程体 0xd35dc |
| `stubs_resolve` | 0xad5b4 |
| 等 IL2CPP 重试块 | 0xd4184（睡眠在 0xd3cdc，1 s × 39） |
| `apply_il2cpp_patch_table` | 0x13097c |
| `scan_mods_dir` | 0x130ba4 |
| `interceptor_store_dll` | 0xaf938 |
| `interceptor_register_dll` | 0xb032c |
| `interceptor_resolve_dll_image` | 0xaff50 |
| `interceptor_domain_assembly_open` | 0xb32d8 |
| `interceptor_class_from_name` | 0xae97c |
| `interceptor_runtime_invoke` | 0xb08ec |
| `interceptor_object_new` | 0xb39f4 |
| `interceptor_array_new_safe` | 0xb7c84 |
| `interceptor_read_string` | 0xb7d2c |
| `interceptor_register_managed_class` | 0xb0890 |
| `interceptor_trigger_injection` | 0xb071c |
| `bridge_managed_invoke` | 0xb205c |
| `bridge_inject_all_types` | 0xb221c |
| `bridge_call_all_entry_points` | 0xb2674 |
| `bridge_call_all_onupdate` | 0xb7e8c |
| `hook_register_csharp` | 0xb764c |
| `pe_validate_and_enum_types` | 0xafa98 |
| 跳板农场 | 0xac770 – 0xad5b4（132 条，`st_size=0`） |
| `MetadataCache::RegisterInterpreterAssembly` | 0xad588（**仅 `ret`**） |
| 指针表基址 | 0x1d6c80（.bss 起点） |
| DLL 槽表 | 0x1d71a8（16 × 0xA0） |
| 黑名单表 | 0x1d0308（16 × 8 字节指针） |
| 补丁表 | 0x1d0828（80 × 0x20） |

### 9.2 关键字符串偏移（`.rodata`，0930）

| 偏移 | 内容 |
|---|---|
| 0x8aada | `63836h9**t)5` → XOR 0x5A → `libil2cpp.so` |
| 0x1dc98 | `"CodeAssembly":"` |
| 0x20167 | `"CodeAssembly":null` |
| 0x1605e | `SFSMod.dll` |
| 0x1b2df | `.dll` |
| 0x23c49 / 0x2a78a / 0x2e7bd | 三个 `Mods/mod.xml`（本版只用于 `remove()`） |
| 0x1c29c | `mods_scan.txt` |
| 0x1f626 | `il2cpp base=%p applying patches\n`（写到 `/dev/null`） |
| 0x1f647 | `IL2CPP-PATCH: DEFECT - libil2cpp differs from the build this table targets…` |
| 0x24f79 | `IL2CPP-PATCH: skip %s at +0x%x got=%08x want=%08x (%s)` |
| 0x8b3c3 | 内嵌 `uitools.dll`（3,584 B，`store_dll` 长度 0xE00） |

### 9.3 复现脚本

`research/memory_injection/tools/`：

| 文件 | 用途 |
|---|---|
| `elfinfo.py` | 列 ELF 头/段/符号（`syms` 参数导出符号表） |
| `binf.py` / `bb.py` | capstone 包装 + **应用 `.rela.dyn` 的指针解引用**（`bb.ptr()`） |
| `d2.py` | 带字符串/PLT/符号注释的反汇编器 |
| `dz.py` | PLT 映射（236 项）+ 字符串读取 |
| `verify_table.py` | **按 program header 做 vaddr→offset 映射**，校验 80 项补丁表（§5.4 的对照实验） |
| `patchtable.py` | 导出补丁表（早期版本，按文件偏移读，**有 -0x4000 误差**，已被 `verify_table.py` 取代） |
| `xref_str.py` / `callers.py` | 字符串/函数交叉引用 |

### 9.4 与其它文档的关系

- `docs/seller_dll_runtime_report.md`：**结论需修正**（见 §6）——它说"有 HybridCLR 解释器"，实测无。
  其余事实（路径、1 MiB、黑名单、ED25519、SFSD/CEK 仅存在于 1004 版）与本报告一致，可继续引用。
- `docs/ANALYSIS.md`：授权/内购部分，与 §5.3 的补丁语义互相印证。
- 03 线（原生 C++ 移植）：本报告 §7.1 路线 C 是其输入。
- 04 线（Hook 技术选型）：本报告 §4.3 / §5.5 是其输入（按名 hook + RVA 表）。
