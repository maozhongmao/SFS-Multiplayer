# 卖家版 SFS Pro 安卓包 —— .NET DLL 加载/执行子系统 逆向报告

> 目标：`com.FLDPXIce.SpaceflightSimulatorPro`（SFS-Pro1004 / SFS-Pro0930 两版，全部只读分析）
> 分析日期：2026-10-15　工作目录：`D:\SFS-Pro`
> 产出目录：`D:\SFS-Pro\build\seller_assets\`（本报告与提取物均在此）
> 方法：zipfile 枚举、pyelftools/capstone 反汇编、jadx 反编译对照、字符串 XOR 解密、内嵌 PE 元数据解析
> 关联文件：`D:\SFS-Pro\ANALYSIS.md`（授权系统总体报告，本报告不重复其结论）

---

## TL;DR（先看这 10 条）

1. **答案：能。** 该包内置的 **HybridCLR IL 解释器**（`libpatch.so`）可以把第三方 .NET DLL（netstandard2.0 系）注册进 il2cpp 运行时并解释执行。
2. **包内不含任何 mod DLL**（只有引导用 `assets/bootstrap/libpatch.so`）。DLL 是**运行时**从共享存储的 `Mods/` 目录扫描/读取的——由卖家的"注入器"在激活后下发（这正是"一机一密/份份不同"的落点）。
3. **引导链**：Java `PeUi.ensureBootstrapPatch()` 把 `assets/bootstrap/libpatch.so(+.sig)` 释放到 `/data/data/<pkg>/files/patches/` → `libs.so`（SFS_LOADER）用 **ED25519** 验签后 `dlopen` 它 → `libpatch.so` 自更新（`Mods/update` → `.new/.bak` 轮换）。1004 与 0930 的 `assets/bootstrap/libpatch.so` 与两版 native 提取物**逐字节一致**（见 §2）。
4. **DLL 数据流**：`Mods/**/*.dll`（≤1MB）或 `mod.xml` 里 `"CodeAssembly":"X"`（≤60MB）→ `interceptor_store_dll(name,bytes,len)` → 可选 **"SFSD" AES-CBC 加密容器**（用授权 **CEK** 解密；无授权/无 CEK 直接拒载）→ PE 校验 → `interceptor_register_dll` → **HybridCLR `MetadataCache::RegisterInterpreterAssembly`**。
5. **入口约定**：被加载程序集里名为 `Main` / `Engine` 的类型入口一次性调用（`bridge_call_all_entry_points`），名为 `OnUpdate` 的成员**每帧**调用（`bridge_call_all_onupdate`）；另有 `call_deferred_loads` 延迟装载。
6. **能力面**：反射（按名取类型/方法/字段、`runtime_invoke`）、泛型、线程（`Interlocked*`、Baselib futex、`Thread::GetThreadStaticData`）、数组、P/Invoke（`il2cpp::vm::PlatformInvoke::Resolve`）、GC 固定。**没有** `Reflection.Emit`/动态程序集；**没有 HarmonyLib**。
7. **给 mod 的桥**：①游戏侧 il2cpp 程序集直接可解析（`UnityEngine.*`、`Assembly-CSharp`、`Unity.TextMeshPro`、`UnityEngine.UI`、`Newtonsoft.Json`、`UniTask`…，见 §6.1）；②内嵌 shim `UITools.dll`（`Bridge.UITools` v1.1.6，仅 Init/Version 标记）；③40+ 个 `native*` JNI 回桥（驱动 Java 侧 ModUIHelper 的 UI/MP 聊天/授权）；④`hook_register[_csharp|_prefix|_postfix]`（≤64 个）+ `install_inline_hook[_to]` 原生 hook。
8. **对 Harmony 类 mod（你的 SFS-Multiplayer）**：**原 DLL 直接丢进去不可行**。Harmony/MonoMod 依赖 JIT/动态方法，解释器域不支持；需**重编译为 netstandard2.0 + 用卖家 hook API 改写补丁类 + 拆掉 Costura 内嵌依赖**。可行但是一次真实移植工程（§7 给了逐项清单）。
9. **授权联动不是装饰**：`store_dll refused (not licensed)`、`unwrap: no CEK -> refuse encrypted dll`、`method_interp blocked (not licensed/tampered)` —— 未授权时该框架**主动拒绝加载 DLL**；1004 的 PULL/GRANT 把判定散进 mod 层。想在别人的包上"白嫖"跑 mod 会撞这道门；自己做同套机制则无此限制。
10. **顺带物证**：卖家内购"解锁"用了一张写死的 Google Play 订单 payload（`GPA.3329-5205-4660-89981`）；字符串表用 **XOR 0x5A** 混淆（与授权服务器 URL 同一把钥匙）；卖家自己的 5 个 mod 全是 C# DLL：`PartEditor++.dll / InfoOverload.dll / ImageCoverMod.dll / SmartSASProto.dll / ANAIS.dll`。

---

## 1. 包内物证清单（本次提取，`build/seller_assets/`）

| 文件 | 说明 |
|---|---|
| `1004/`, `0930/` 下 `libpatch.so`, `libpatch.so.sig`, `version.txt`, `build_info`, `baseline.prof(m)`, `RuntimeInitializeOnLoads.json`, `ScriptingAssemblies.json`, `boot.config`, `MultiplayerIcon.png` | 从 APK `assets/` 提取的关键小文件 |

关键哈希（结论重要）：

```
1004 APK assets/bootstrap/libpatch.so      sha256 1f3d6979892a067880e51d9a8ff8035716ce86c5687fe57479e722f0a3f78d34  2,060,672 B
      = native1004/libpatch.so              （逐字节相同）
0930 APK assets/bootstrap/libpatch.so      sha256 e9a1610490c3b48c84f71529ef677393163a7f97f0ddf54631d0f147f7b8b017  1,922,488 B
      = native/libpatch.so                  （逐字节相同）
1004 sig (ED25519, 64B)                    ad94da9f...93aad079aeec36e29b92b5f0dd44cb65ef2884af262671a86d84977d2f01404
0930 sig                                   2b8ab3a0757861856faf324c802e8280404f480f740a0b589ade3c117543cabf
```

其它小文件含义：`version.txt`=`85f42d62e`（**原版 SFS 的 git 版本串**，非卖家）；`build_info`="Build from MAC's MacBook Pro at 6/1/2022"（原版构建信息，证明包基于正版重打包）；`baseline.prof/.profm`=标准 ART baseline profile（与 mod 无关）；`RuntimeInitializeOnLoads.json` 列游戏自身初始化钩子。**没有发现任何随包 DLL/mods 目录/加密 DLL blob。**

> 另：`lib/arm64-v8a/` 下**没有** `libpatch.so` —— 它只以 `assets/bootstrap/` 形式随包，这正是要"释放"的原因。

## 2. 引导链：从 assets 到 libpatch.so 常驻

### 2.1 释放（Java，`com.sfsmod.PeUi.ensureBootstrapPatch`，jadx 原文核对）
- 路径：`context.getFilesDir()/patches/` → 拷 `assets/bootstrap/libpatch.so` 与 `libpatch.so.sig`（65,536 B 分块写）。
- 触发时机：`ModUIHelper.setActivity()` 每次被调时（游戏 UI Activity 建立后由 native 回调）；`ASSET_PATCH_VERSION=268`，`version.json` 达标且两文件存在则跳过，否则删除重拷。
- 同函数链上 `nativeSetActivity()` 走 JNI 进 native。

### 2.2 装载（Native，`libs.so` = "SFS_LOADER"，28,600 B；`libhb.so` = "hb.so v1086 minimal loader"）
- `libhb.so`（`DT_NEEDED libs.so`）等待 IL2CPP domain 就绪（轮询 `il2cpp_get_corlib`），日志：`IL2CPP domain ready (s.so loaded via DT_NEEDED)`；`libs.so` 也可由 Java `System.loadLibrary("s")`（`FpsOverlay` 静态块）直接拉起。
- `libs.so` 关键字符串（部分经 4 字节交织混淆，语义可由其余明文确证）：`cannot read %s`、`cannot read signature %s`、`%s.sig`、`ED25519 verify FAILED (%d) - refusing to load %s`、`libpatch.so verified + loaded at %p`、`dlopen(%s) failed: %s`、`EARLY-IL2CPP-PATCH: base=%p total=%d applied=%d ...`、`sfs_astro_off.txt / sfs_unlock_parts.txt`。
- 结论：**libs.so 读取 `files/patches/libpatch.so(+.sig)`，用 libcrypto 的 ED25519 验签，通过才 `dlopen`**；顺带在 libil2cpp 代码段打"解锁"补丁（from 文件开关）。路径在 `libpatch.so` 的 XOR-0x5A 字符串表里被完整证实：
  `/data/data/com.FLDPXIce.SpaceflightSimulatorPro/files/patches/libpatch.so`。
- `libpatch.so` 自带自更新：`%s/libpatch.so.new(.sig)` → 验签 → `%s/libpatch.so.bak(.sig)` → 原子替换；更新源目录字符串：`/storage/emulated/0/Android/media/<pkg>/Mods/update`。
- `libsfsmod.so`（5,104 B）只有一个 `JNI_OnLoad`，无其它逻辑（疑似占位/兼容位，保持原版链接名）。`libstub.so` 与 DLL 加载无关（它只管授权）。

## 3. `libpatch.so` 本体解剖（1004，2,060,672 B，181 个导出函数）

### 3.1 它是 HybridCLR + 卖家封装
硬证据（导出符号）：
- `_ZN6il2cpp2vm13MetadataCache27RegisterInterpreterAssemblyEP14Il2CppAssembly`（HybridCLR 注册解释器程序集的招牌函数）
- `_ZN9hybridclr8metadata11VTableSetUp11BuildByType...`（`hybridclr::metadata::VTableSetUp`）
- 一整套 il2cpp VM/icalls 实现被**导出**（`il2cpp::vm::Class/Array/String/MetadataCache/GlobalMetadata/Exception/PlatformInvoke::Resolve`、`GarbageCollector::AllocateFixed/FreeFixed`、`Interlocked*`、`il2cpp_baselib::Baselib_SystemFutex_*`、`il2cpp_codegen_*` 等）——这是一份"il2cpp 依赖桩"，供**被补丁过的 libil2cpp**在运行期回调（配合 `il2cpp base=%p applying patches`、`IL2CPP-PATCH: DEFECT - libil2cpp differs from the build this table targets` 日志）。
- `.bss` 高达 0x17d5ec8（≈25MB）：解释器栈/已加载 DLL 槽位等运行期内存池。

### 3.2 卖家自己的一层（导出名即 API 名）
```
interceptor_set_dll_image / resolve_dll_image / register_dll / store_dll / register_managed_class
interceptor_class_from_name / object_new / runtime_invoke / array_new_safe / read_string
interceptor_domain_assembly_open / trigger_injection / interceptor_register_dll
bridge_managed_invoke / bridge_inject_all_types / bridge_call_all_entry_points / bridge_call_all_onupdate
hook_register / hook_register_csharp / hook_register_prefix / hook_register_postfix
install_inline_hook / install_inline_hook_to / stubs_resolve / engine_script_resolve_fwd
call_deferred_loads / resolve_GetTypeLoadException_SV3 / mj_26(混淆版 JNI_OnLoad)
```

### 3.3 授权联动（与 `ANALYSIS.md` §3.2 对得上）
- 每帧/调用采样时 `PULL: ModUIHelper.licenseCached()`（JNI 反射调 Java；找不到时报 `facade missing`），日志 `GRANT: java reported (NOT) licensed`。
- 未授权时：`store_dll refused (not licensed): %s`、`method_interp blocked (not licensed/tampered)`（`0xbae14`）。
- **DLL 加密容器使用授权 CEK**（见 §4.4）。CEK 以 64 个 hex 字符常驻 `libpatch.so` 的 .bss（`0x417c1c`，运行期填充；`__strlen_chk(...)==0x40` 判定）。**具体灌入路径未完全定标**（见 §10 开放问题）。

### 3.4 内嵌资源（都在 .rodata）
- **内嵌 .NET 程序集**（`0x8f855` 起，3,584 B，MZ/PE 合法）：即卖家开发机产物 `D:\2SFS Project\testdll\UIToolsStub\obj\Debug\netstandard2.0\UITools.pdb` 对应的 `UITools.dll`（见 §6.2/§8）。
- 一段 Android 字符串资源 XML（`0x8ef78` 起，2,264 B）：`<string name="prod_new_full_bundle">` 内含 **Google Play 订单 payload**（`{"Payload":{"json":{"orderId":"GPA.3329-5205-4660-89981","packageName":...`）——配合 `billing patches done` 等字符串，说明它把游戏内购校验打补丁伪造"已购买"。
- **字符串混淆：XOR 0x5A**（与授权服务器 URL 同一把钥匙，`ANALYSIS.md` 已记）。本次已全量解密：`build/seller_assets/libpatch1004_xor5A_strings.txt`（10,822 条），其中暴露了完整路径表/内置 mod 名表（§4、§6）。

## 4. 问题1：DLL 从哪来（数据流与格式）

### 4.1 搜索根目录（全部实测字符串）
```
/storage/emulated/0/Android/media/com.FLDPXIce.SpaceflightSimulatorPro/Mods      ← 主（扫描+组织目标）
/storage/emulated/0/Android/data/com.FLDPXIce.SpaceflightSimulatorPro/files/Mods
/data/data/com.FLDPXIce.SpaceflightSimulatorPro/files/Mods                        ← 私有兜底
…/Mods/Custom_Assets/CodeAssemblies                                               ← 组件式 DLL 存放点
/data/data/com.StefMorojna.SpaceflightSimulator/…                                 ← 原版包名回退（兼容层）
```
相关格式串：`%s.dll`（`0x1a8b4`）、`%s/%s.dll`（`0x163a5`）、`%s/%s/%s.dll`（`0x2423b`）、目录创建 `%s/%s`（`0x1a7f7`）。

### 4.2 目录扫描装载器（函数 @`0x149180`，逐条反汇编确认）
1. `readdir` 遍历 `Mods/`；只收 `*.dll`（后缀比对）；**跳过 `SFSMod.dll`**（框架自身保留名）；跳过已在 16 槽表（`0x1f8da8`，步长 0xA0）里的重名。
2. `fopen("%s/%s")` 读整个文件；**大小硬限制 ≤ 0x100000（1MB）**，超限跳过。
3. `interceptor_store_dll(name, bytes, len)` → `interceptor_register_dll(name)`。
4. 日志 `FOUND-ROOT: %s/%s (%ld bytes)`；然后 `mkdir("Mods/<基名>")`＋`rename()`——**把散落的 DLL 归位成 `Mods/<名字>/<名字>.dll`** 的标准结构。
5. 之后对子目录再来一轮（`%s/%s/%s.dll` 模式），即 `Mods/<ModName>/<file>.dll` 也被装载。

### 4.3 `mod.xml` / `"CodeAssembly"` 通道（函数 @`0xe51e0` 段）
- 读一个清单文件（`Mods/mod.xml`，三个根目录都找），`strstr` 找 `"CodeAssembly":"`（`0x1ede5`），提取值 → 拼 `%s/%s.dll` → 读文件（**上限 ~60MB**）→ `interceptor_store_dll` + `interceptor_register_dll` → 把**明文 DLL 写回** `Mods/<name>.dll`（缓存/落盘）→ 最后把清单里的该项**改写为 `"CodeAssembly":null`**（`0x218ac`，一次性消费，防重复加载）。
- 对应状态位字符串：`f.dll_dispatch` / `f.dll_dispatch_done`（在注册表/日志块里记录）。

### 4.4 加密容器格式 `SFSD`（函数 @`0xb71c0`，逐条反汇编确认）
```
偏移  0..3   magic "SFSD" (0x53 0x46 0x53 0x44)
偏移  4      version = 1
偏移  5..20  IV (16B)
偏移 21..   AES-CBC 密文（长度必须是 16 的倍数：(len-21)%16==0）
```
- 密钥来自授权 **CEK**（`unwrap: no CEK -> refuse encrypted dll`；CEK 为 32B/64 hex，存 .bss `0x417c1c`；派生细节未定标）。
- 失败分支与日志全部实锤：`unwrap: bad header/version` / `bad ciphertext length` / `AES/padding failed (wrong CEK?)` / `decrypted ok`。
- 成功：`store_dll decrypted with CEK: %s (%d -> %d bytes)`，随后走同一注册路径。**普通明文 DLL 不走解密**（`unwrap` 返回 0 直接进入存储），也就是说：加密是"卖家可选的内容保护"，不是必需项。

### 4.5 `store_dll` 的准入过滤（@`0xb6bd8`）
- 槽位上限 **16 个 DLL**（`count ≤ 0xf`）。
- **名称前缀黑名单（16 条）**：`Microsoft / System / Mono. / Unity. / UnityEngine. / netstandard / Newtonsoft. / ICSharpCode. / JetBrains. / nunit. / xunit. / SkiaSharp / HarfBuzzSharp / Firebase. / Google. / mscorlib` —— 即：**不允许由 Mods 目录提供/覆盖这些核心程序集**（要用它们就直接引用游戏 build 里已有的）。
- 未授权分支：`store_dll refused (not licensed): %s`。

## 5. 问题2：怎么被执行（解释器与入口约定）

### 5.1 PE 校验与注册
- `0xb736c`：手写 PE 解析（`MZ`→`e_lfanew`→`PE\0\0`→可选头 magic 0x10b/0x20b→**遍历导入表**且对 `mscoree.dll`/`_CorDllMain` 有特判——因为托管 PE 的"导入表"是编译器造的 C++/CLI 头，HybridCLR 载入时需跳过它；内嵌 shim 里同样能看到这两个串）。
- 校验通过后进入注册，最终落到 HybridCLR 的 `MetadataCache::RegisterInterpreterAssembly`（该符号被**导出**，由被 patch 的 libil2cpp 侧调用/回调）。

### 5.2 入口约定（反汇编实证）
| 触发 | 条件 | 函数 |
|---|---|---|
| 载入后一次性 | 程序集中类型名 `Main`（比对串 `0x2ca13="Main"`，旁边还有 `Engine`） | `bridge_call_all_entry_points @0xba76c`（遍历 ≤64 程序集；跳过 `UnityEngine`/`System` 前缀与 `<` 编译器生成名） |
| 每帧 | 成员名 `OnUpdate`（比对串 `0x23f2c`），带静态/无参标志位；收集上限 128 | `bridge_call_all_onupdate @0xc0c08` |
| 延迟装载 | 载入完成后补跑 | `call_deferred_loads @0xd7d24` |
| 反射自检 | 若存在 `Mods/ReflectShimTest/ReflectShimTest.dll`（`access()` 检测），调其 `ModMain.RunTest()`，失败打 `REFLECT-SHIM: ModMain.RunTest not found` | 同 `bridge_call_all_onupdate` 内 |

> `bridge_call_all_onupdate` 内部的 `ReflectShimTest` 特判 = 卖家自留的"反射能力测试 mod"。这直接证明：**反射在解释器域可用**。

### 5.3 能力面（由导出符号与 icalls 集合推断，标注推断级）
- 反射：`interceptor_class_from_name`、`interceptor_runtime_invoke`、`interceptor_object_new`、`interceptor_read_string`、`interceptor_register_managed_class`、`RuntimeTypeHandle::internal_from_name`。（实证）
- 泛型：`GenericMetadata::{Inflate, InflateIfNeeded, GetGenericClass, ContainsGenericParameters}`、`Il2CppTypeEqualityComparer`。（实证）
- 线程：`Interlocked` 全套 icalls、`Thread::GetThreadStaticData`、Baselib futex、`FastReaderReaderWriterLock`。（实证）
- P/Invoke：`il2cpp::vm::PlatformInvoke::Resolve` 被实现并导出；`resolve DLL` 流程即 dlopen/dlsym（`libpatch.so` 自身就 `dlopen("libil2cpp.so")`——字符串 `63836h9**t)5` XOR 0x5A 解出）。（实证+推断）
- GC：`GarbageCollector::AllocateFixed/FreeFixed`、`Object::New/Box/IsInst`。（实证）
- **不支持**（关键约束）：`Reflection.Emit`/`DynamicMethod`/`Assembly.Load(byte[])` 类动态程序集（HybridCLR 解释器域限制；本框架无相关导出与字符串）；**无 Harmony/MonoMod**（全库明文+XOR 域均搜不到 `Harmony`）。

## 6. 问题3：提供了哪些桥（shim）与 API

### 6.1 游戏侧 il2cpp 程序集（直接可引用，来自 `ScriptingAssemblies.json`，131 项摘要）
`UnityEngine.*`（含 `IMGUIModule`、`UIModule`、`UnityEngine.UI.dll`、`Physics2DModule`、`UnityWebRequestModule`、`MultiplayerModule`…）、`Assembly-CSharp.dll`、`Assembly-CSharp-firstpass.dll`、`Unity.TextMeshPro.dll`、`UniTask*`、`Newtonsoft.Json.dll`、`Sirenix.OdinInspector.Attributes.dll`、`ICSharpCode.SharpZipLib.dll`、`Google.Play.Games.dll`、Firebase 系列等。
⚠️ 注意：**名单里没有 `mscorlib.dll`/`System.dll`/任何 `System.*`（除 `System.Runtime.CompilerServices.Unsafe`）**——BCL 由 Unity 的 corlib 集提供，**`System.Net.Sockets` 之类是否在 build 内未定**（对 Lidgren 是关键风险，§7）。

### 6.2 内嵌 UI shim：`UITools.dll`（完整元数据已解析）
```
Module: UITools.dll        Runtime: v4.0.30319 (metadata)   EntryPoint: 无
AssemblyRef: netstandard, UnityEngine, System(隐含)
TypeDef:  Bridge.UITools        (public static class)   method: static void Init()
          Bridge.UIToolsBuilder (public static class)   method: static ... Version()  → 返回 "1.1.6"
UserStrings: "[UITools-Bridge] Init called", "1.1.6"
MemberRef: UnityEngine.Debug::Log(string)（唯一外部调用）
PDB 路径: D:\2SFS Project\testdll\UIToolsStub\obj\Debug\netstandard2.0\UITools.pdb
```
结论：这是**"程序集身份+版本探针"级别的 shim**（占位/连通性测试），不是 PC 版 `SFS.UI.ModGUI/UITools` 的完整实现。卖家 mod（PartEditor++ 等）真正的 UI 走的是 §6.3 的 JNI 回桥。**PC mod 直接用 `SFS.UI.ModGUI` 类型会解析失败**，除非自备同名兼容实现的 DLL（名字不能撞黑名单，`UITools` 不在黑名单，可自带）。

### 6.3 JNI 回桥（native ↔ Java `com.sfsmod.ModUIHelper`，40+ 方法）
- 授权/环境：`nativeLicenseOk / nativeSetLicensed / nativeSaveLicense / nativeGetDeviceHash / nativeGetServerUrl`
- UI（ItemChooser 类）：`nativeIcCreate / nativeIcItemKind / nativeIcItemLabel / nativeIcItemInput / nativeIcItemSel / nativeIcItemInc / nativeIcItemDec / nativeIcItemCount / nativeIcItemAction / nativeIcAction / nativeIcActionValue / nativeIcSetTransform / nativeIcDestroy[All] / nativeIcStatus / nativeIcVersion / nativeSetBlurRect`
- 模块状态：`nativeModLoaded(name) / nativeFnToggle / nativeGetFnState / nativeGetScene / nativeUiSceneProbe / nativeSetHiddenParts / nativeSas* / nativePdToggle`
- 联机（框架自带简易 MP）：`nativeMpServerInfo / nativeMpSendChat / nativeMpChatCount / nativeMpChatLine / nativeMpChatWindowVisible`
- 回调进 Java：`nativeOnClick(I) / nativeOnText(I, String) / nativeOnBindCode(String)`
这些在 `JNI_OnLoad` 里以 `RegisterNatives` 动态注册（所以只导出其中 14 个符号）——**C# mod 可通过解释器调用 Java（JNI）来复用这套 UI/MP 设施**。

### 6.4 Hook API（C# 侧）
- `hook_register_csharp @0xc0380`：参数形状（反汇编）≈ `(string 类全名, string 方法名, int 旗标/序号, void* hook入口, void* 可选)`，拷贝名上限 0x7F，**表上限 64（`0x3f` 判定）**；另有 `hook_register / _prefix / _postfix`（Harmony 式前置/后置语义）。
- 原生级：`install_inline_hook / install_inline_hook_to`（卖家自用，也导出）；日志样例：`hook-install XXWN tramp=%p`、`hook-find OVVLK input=%p`。
- 卖家自己的 5 个 C# mod 走的就是这套（如 `SASHook install attempt#%d`、`SmartSASProto.dll registered %d -> %d`）。

### 6.5 Harmony 能不能用？——不能（明确）
- 全库无 `Harmony`/`MonoMod` 字符串；解释器域无 JIT/`Reflection.Emit`；0Harmony.dll 虽然名字不撞黑名单（"0Harmony"）可以放进 Mods，但其 Detour 引擎在解释器下无法生成/修补原生代码（没有 Mono 的 JIT trampoline，也没有 BepInEx/MelonLoader 那种 il2cpp 补丁层）。
- **必须移植**：把 Harmony 补丁类改写为 `hook_register[_prefix|_postfix]`（名字路径+前置/后置），或用游戏类型直接订阅（若游戏有事件/虚方法），或干脆用 `install_inline_hook` 做原生级拦截（要求你对 libil2cpp 的 RVAs 有掌控，与版本强绑定——不推荐给通用 mod）。

## 7. 问题4：可行性判断 —— 你的 SFS-Multiplayer 怎么塞

### 7.1 你的 mod 现状（`D:/SFS-Multiplayer/Client/src/`，只读核对）
`net48`；引用 `Assembly-CSharp / Assembly-CSharp-firstpass / 0Harmony / Lidgren.Network / UITools / Unity.TextMeshPro / UnityEngine.*`；依赖包 `Costura.Fody`（**把依赖内嵌进本体 DLL**）与 `DnsClient`；大量 `MultiplayerSFS.Mod.Patches/*.cs`（Harmony 补丁：暂停/回退/时间扭曲/存档分流/部件同步/UI 改写…）。

### 7.2 硬门槛（逐条对照框架能力）
| 门槛 | 事实 | 影响 |
|---|---|---|
| 授权门 | 未授权 `store_dll refused`；加密 mod 需 CEK | 在**别人的包**上跑会撞门（除非改 native）。**自己实现同套机制无此约束** |
| TFM | 解释器要求 netstandard2.0 系 | 重编译目标改为 `netstandard2.0`（或 `netstandard2.1` 视其 corlib），去掉 net48 专属 API |
| 尺寸 | 扫描路径 ≤1MB；mod.xml 路径 ≤60MB | 单个 DLL 别超 1MB（拆分） |
| 命名 | 16 个前缀黑名单；`SFSMod.dll` 保留 | 自己的依赖改名/避免 `System.*`、`Microsoft.*` 前缀 |
| Harmony | 不可用 | Patches 全部改写（工作量主体） |
| Costura | `Assembly.Load(byte[])` 不可用 | 关掉 Costura，依赖**拆成独立 DLL** 放进 `Mods/<名字>/`（≤1MB 每个） |
| 入口 | 类型名 `Main`/`Engine`（一次）+ `OnUpdate`（每帧） | 增加一个瘦入口类，把原 `Main.cs` 逻辑接进去 |
| Lidgren | 需要 `System.Net.Sockets`（build 内可能被裁剪） | **必须先实测**；不可用则：P/Invoke 到 libc 的 `socket/sendto/recvfrom`（`dlopen("libc.so")` 路径可行）或自写轻量 UDP 层 |
| UITools | 内嵌 shim 只有 `Init/Version` | UI 要么走 §6.3 JNI 回桥，要么直接用 `UnityEngine.UI`/`IMGUIModule`（都在 build 里） |

### 7.3 推荐路线 A（在卖家框架上跑你的 mod，最小改动版）
1. 新 csproj：`netstandard2.0`，引用**该安卓 build 的**代理程序集（`Assembly-CSharp` 等需从 il2cpp 侧导出/生成，或手写"仅用到的类型"桩——因为 PC 的 Assembly-CSharp.dll 是 Mono 版，签名不匹配）。
2. 拆分：`SFS-Multiplayer-1.6-V1.4.dll`（主）+ `Lidgren.Network.dll` + `DnsClient.dll`（及 `System.Buffers` 若需要 → 会被黑名单拦，需自行改名内联）分文件放 `Mods/SFSMultiplayer/`。
3. Patches → `hook_register_*` 重写（逐一对照 31 个补丁文件，本质是"目标方法名+前后置逻辑"，工作量可控）。
4. 入口类 `Main`（初始化）/`OnUpdate`（每帧 pump，把原来 Unity Update 驱动换成这个）。
5. 网络：先测 `System.Net.Sockets` 是否可用；不行就把 Lidgren 换成本项目 ServerCpp 已有的 UDP 协议封装 + P/Invoke socket。
6. 实测顺序（黑盒验证）：放一个 `HelloMod.dll`（只有 `Main`/`OnUpdate` 打印日志）→ 看 `Mods/perf.txt`、`mods_scan.txt`、`Logs/*.log` 是否出现 → 再逐步加复杂度。

### 7.4 路线 B（不依赖卖家框架；推荐给你自己的项目）
技术上你完全可以**复刻同一机制**（更干净、无授权门）：`HybridCLR`（开源）+ 自写"扫描 Mods → `RegisterInterpreterAssembly` → 反射调 `Main/OnUpdate`"的 loader（≈几百行 C++），或等价的 BepInEx-il2cpp 方案。卖家的全部"秘密"其实就是 HybridCLR + 40 个 JNI 桥 + 入口约定；**没有任何密码学级黑科技**。

### 7.5 必须 shim / 可直接用 的类型清单（按可用性分类）
| 类别 | 清单 | 处理 |
|---|---|---|
| 直接可用（build 内 il2cpp 程序集） | `UnityEngine`(CoreModule 等全部模块)、`UnityEngine.UI`、`Unity.TextMeshPro`、`Assembly-CSharp`（SFS 游戏类型）、`Assembly-CSharp-firstpass`、`Newtonsoft.Json`、`UniTask`、`UnityEngine.IMGUIModule` | 直接引用（版本要对齐安卓 build，建议从 il2cpp 生成代理） |
| 需自带兼容实现（可放进 Mods） | `SFS.UI.ModGUI`/`UITools` 的 PC API、任何第三方 helper | 自写同名类型 shim，或改写调用点；注意 ≤1MB 与命名黑名单 |
| 不可用（必须替换） | `HarmonyLib`(0Harmony) 的全部 Patch 机制、`Reflection.Emit`/`DynamicMethod`、`Assembly.Load(byte[])`、Costura 内嵌解包 | 改写为 `hook_register_*`、静态编译依赖、拆 DLL 放置 |
| 待实测 | `System.Net.Sockets`(Lidgren 依赖)、`System.Threading` 高级原语、`System.Security.Cryptography` 细节 | 先写探针 mod 打日志验证 |

## 8. 问题5：`D:\2SFS Project\testdll\UIToolsStub` 的上下文
- 该路径来自内嵌 PE 的 PDB 名：卖家在开发机建了一个 `testdll` 解决方案，子工程 `UIToolsStub` 输出 `UITools.dll`（netstandard2.0，v1.1.6），**专用于测试"自己的 shim 程序集能否被解释器正确加载/解析"**；随后在 `libpatch.so` 里内嵌该 DLL 作为**样例/探针**（也许还有"给 mod 作者演示如何写一个会被识别的程序集"的用途）。
- 结合 §5.2 的 `ReflectShimTest`（`ModMain.RunTest`）和 §4.3 的 `"CodeAssembly"` 消费逻辑，可以推断卖家的工作流：**本地写测试 DLL（UIToolsStub/ReflectShimTest）→ 丢进手机 `Mods/` → 看日志（`mods_scan.txt`/`perf.txt`/`f.dll_dispatch*`）确认装载与反射注入**。这也是给你复刻时的现成验证套路。

## 9. 旁支发现（与 DLL 机制相关，顺带记录）
- **内购伪造**：`billing patches done` + 内嵌 Play 订单 payload（`GPA.3329-5205-4660-89981`，packageName 指向本包）→ 它在 libil2cpp 侧打补丁让游戏认为已购买（这是"盗版当正版卖"的另一半）。
- **原版包名回退**：字符串表里有 `/data/data/com.StefMorojna.SpaceflightSimulator/...`，说明框架也可用于原版包（或继承自早期版本）。
- **卖家内置 mod 全清单**（DLL 名，来自 XOR 表）：`PartEditor++.dll`、`InfoOverload.dll`、`ImageCoverMod.dll`、`SmartSASProto.dll`、`ANAIS.dll`、`uitools.dll` —— 与你看到的 Java UI 类名（PdUi/FnUi/IcUi/SasUi/AnaisUi/PeUi）一一对应：**UI 壳在 Java，业务在 C# DLL**，正好是你想学的"PC DLL 上安卓"的活样本。
- **调试开关文件**：`Mods/{bml_off,bml_debug,pmp_off,pmp_debug,integrity_debug,sfs_parts_census,perf_probe,mods_gate_debug}.txt`——分析时可作为行为开关。

## 10. 置信度与开放问题

| 结论 | 置信度 | 依据 |
|---|---|---|
| HybridCLR 解释器承载 DLL 执行 | **高** | 导出符号+字符串+结构三证 |
| 引导链（assets→patches→ED25519→dlopen） | **高** | Java 反编译 + libs.so 字符串/反汇编 |
| DLL 来源=Mods 扫描/mod.xml，≤1MB/≤60MB | **高** | 逐条反汇编（0x149180/0xe51e0） |
| SFSD 容器=AES-CBC(CEK)+16B IV | **高** | 逐条反汇编 0xb71c0；CEK 来源为中 |
| 入口约定 Main/Engine/OnUpdate | 中高 | 反汇编比对串；未做真机验证 |
| 无 Harmony/无 Reflection.Emit | 中高（负向结论） | 全字符串域搜索为空+解释器常识 |
| CEK 如何进入 libpatch（JNI？文件？） | **低（开放）** | 只见 bss 槽与判定，未见灌入代码路径 |

开放问题（建议后续验证）：
1. CEK 灌入路径：hook `LicenseApi.licenseCheck/licenseFetch` 或搜 libstub 是否把 CEK 写文件（`license.dat` 之外的旁路）。
2. `System.Net.Sockets` 等 BCL 面：写一个探针 mod 在真机打日志即可确认。
3. `hook_register_csharp` 的精确参数与调用方（是否由某个"框架 API DLL"包装后给 C# 用；`SFSMod.dll` 很可能就是那个未随包的 API 程序集——它被扫描器特意跳过）。
4. `libs.so` 内部 4 字节交织混淆串的完整还原（不影响主结论）。
5. `Mods/update` 自更新通道的服务器交互形式（与注入器联动）。

## 11. 附录：本次可复现脚本位置
- 反汇编/字符串/XOR 解密、PE 元数据解析、资产提取脚本均以一次性 Python 形式执行；核心产物：
  - `build/seller_assets/libpatch1004_strings.txt`（明文串，16,167 条）
  - `build/seller_assets/libpatch1004_xor5A_strings.txt`（XOR-0x5A 解密串，10,822 条）
  - `build/seller_assets/UItoolsStub_embedded.pe`（内嵌 UITools.dll 原样，3,584 B）
  - `build/seller_assets/libpatch_embedded_strings_resource.xml`（内嵌资源 XML）
  - `build/seller_assets/1004|0930/*`（bootstrap、版本文件等）
- 关键函数地址备忘（1004）：`interceptor_store_dll=0xb6bd8`、`unwrap=0xb71c0`、`set/resolve_dll_image=0xb77fc/0xb7868`、`register_dll=0xb7c80`、`RegisterInterpreterAssembly=0xb47b8`、`bridge_call_all_entry_points=0xba76c`、`bridge_call_all_onupdate=0xc0c08`、`hook_register_csharp=0xc0380`、`call_deferred_loads=0xd7d24`、`Mods 扫描器=0x149180`、`mod.xml 解析=0xe51e0`、`内嵌 PE=0x8f855`。
