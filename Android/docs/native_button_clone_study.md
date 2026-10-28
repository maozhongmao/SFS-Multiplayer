# 你 2026 年「安卓原生克隆按钮」成果研究笔记

> 分析对象：`E:\SFS-Multiplayer\analysis-android-apk\`（2026-08 的备份，完整保留）
> 结论：**按钮克隆这条链路你的实现是完整、真实跑通过的最小方案**（原生 il2cpp 直调，非 HybridCLR，非 Java 悬浮层）。v0.02 的登录面板源码在，但**没有编译进 .so**（见 §7）。

---

## 0. 一句话总结

`libmain/libil2cpp` 里塞一段 ARM64 shellcode → `dlopen` 自己的 `libSFSMPTrampoline.so` → trampoline 等 IL2CPP 就绪后 `dlsym` 拿 il2cpp API → 改 `MethodInfo->methodPointer` 挂钩 `Update` 和按钮点击方法 → 在 Update 里用 `Object.Instantiate` 克隆主菜单的 Play 按钮、改字、排版、拦点击。

---

## 1. 文件地图（哪些是你的成果）

| 文件 | 大小 | 作用 |
|---|---|---|
| `trampoline/trampoline_il2cpp_native_button_clone_probe.c` | 34.8KB / 660 行 | **核心源码：按钮克隆 + 点击接管** |
| `trampoline/trampoline_il2cpp_native_button_clone_v002_login.c` | 53.4KB / 1099 行 | v0.02：克隆 + 登录面板 + TCP 联机（**源码，未编译**） |
| `trampoline/arm64-v8a/libSFSMPTrampoline_native_button_clone_probe.so` | 27,464B | **唯一真实的探针构建产物**（含 hooked_update 等符号） |
| `trampoline/arm64-v8a/libSFSMPTrampoline_native_button_clone_v002_login.so` | 6,040B | ⚠ 空壳（无 login 字符串、无 worker 符号）——v0.02 没编进去 |
| `patch_il2cpp_inject_trampoline.py` | 154 行 | **注入器**：改 libil2cpp `.init_array[0]`，塞 shellcode → dlopen trampoline |
| `build_1.6.00.14_il2cpp_native_button_clone_probe.py` | 115 行 | 重打包：替换 libmain、加 trampoline、注入语言键、zipalign、apksigner |
| `build_1.6.00.14_il2cpp_native_button_clone_v002_login.py` | 124 行 | 同上（v0.02，同时替换 libil2cpp） |
| `generated-1.6.00.14/` | dump.cs 20.6MB / script.json 59.9MB / DummyDll 82 个 | Il2CppDumper 产物，**RVA 的唯一来源** |
| `generated_dot1/` | dump.cs 21MB | 另一个版本（1.6.1？）的 dump，供对比 |
| `toolchain/android-ndk-r26d/` | — | 编译 NDK（已离线备好） |
| 其余 `trampoline_*.c`（20 个） | 0.2–12KB | 前期试探：dlopen/线程/domain/entrypoint/imgui 按钮等 |

---

## 2. 注入链（三层）

### 2.1 为什么不用 DT_NEEDED
`patch_il2cpp_inject_trampoline.py` 头部注释原话：
> `dlopen` from `.init_array` **bypasses the DT_NEEDED private-library restriction**（Android linker 不允许 APK 私有库直接写进依赖表，但 init_array 里以正确命名空间 dlopen 可以）。

### 2.2 具体做法（patch_il2cpp_inject_trampoline.py）
1. 读 `libil2cpp.so` 的 program headers，建 VA→文件偏移映射。
2. 在 `.rela.dyn` 里找 `r_offset == 0x32a6ec0`（`.init_array[0]`）的重定位项，读原 addend `0x15d9b9c`（原初始化函数）。
3. 用 LIEF 新增一个 **RX LOAD 段**（256B 占位，`force_relocate=True`），build 后回读拿到它被分配的 VA。
4. 生成 ARM64 shellcode（手写指令 + `adrp/add` 立即数修补）：
   ```
   stp x29,x30,[sp,#-16]!
   adrp/add x0 -> "libSFSMPTrampoline.so"
   mov  x1, #2                    // RTLD_NOW
   adrp x2 -> 0x32fbc90           // dlopen 的 GOT 槽
   ldr  x2,[x2] ; blr x2          // dlopen(...)
   ldp x29,x30,[sp],#16
   adrp/add x3 -> 0x15d9b9c       // 原 init_array[0]
   br   x3                        // 跳回原函数
   ```
5. 把 `.init_array[0]` 的 rela addend 改成 shellcode VA。加载时 linker 调 init_array → 我们的 shellcode → dlopen trampoline → 回到原初始化，**不打乱初始化顺序**。

历史变体（可忽略）：`patch_libmain_inject_dlopen.py`（改 libmain）、`patch_il2cpp_utf16_*.py`、`patch_libmain_lief*.py`、`repack_*.py` —— 都是同一个思路的不同落点/编码尝试。

---

## 3. trampoline 运行时初始化（worker 线程）

```
__attribute__((constructor)) start()
  └─ pthread "sfsmp-ui-wait"
      ├─ 最多 30s：dlopen("libil2cpp.so", RTLD_NOW|RTLD_NOLOAD) + dlsym 22 个 il2cpp API
      │          + dladdr 拿模块基址
      ├─ 最多 30s：轮询 基址+0x34c0700 处的 domain 指针非空（等 IL2CPP 初始化完）
      ├─ il2cpp_thread_attach(domain)
      ├─ 取 image：UnityEngine.CoreModule.dll、Assembly-CSharp.dll
      ├─ resolve_unity()：16 个 Unity 方法（见 §6 常量表）
      ├─ 取游戏类：SFS.UI.PJLFPTMJUFGFCLXQ（按钮组件）
      │            SFS.UI.BDQTZLEYUDSAXHIV（文本，get_Text/set_Text）
      │            SFS.UI.ClickUnityEvent / HoldUnityEvent
      │            SFS.Input.GWBAFZUDKCWNMVZC.Update（输入/UI 模块）
      └─ 双重 methodPointer 替换（见 §5），然后线程名变 sfsmp-ui-ready，每秒刷状态名
```

### 方法解析的关键技巧：`method_checked()`
不信任名字，**名字 + 参数个数 + `methodPointer - 模块基址 == 预期 RVA` 三重校验**：

```c
while ((m = api.class_methods(k, &iter))) {
    p = (const MethodPrefix *)m;
    if (!p->method_pointer || strcmp(api.method_name(m), name) ||
        api.param_count(m) != argc) continue;
    if ((uintptr_t)p->method_pointer - base == rva) return m;   // RVA 对上了才算
}
```
→ 防同名重载、防混淆改名（RVA 是编译期常量，名字变了也能对上）。

---

## 4. 按钮克隆（create_clone，8 步）

在 `hooked_update` 里，帧数 >20 且 CAS 抢到锁后执行一次；失败则下一帧重试。

1. **找源按钮**：`UnityEngine.Object.FindObjectsOfType(按钮类, 0)`（0 = 不含 inactive）
2. 遍历：`Component.get_gameObject` → `get_activeInHierarchy` 必须是 true
3. 子文本：`GameObject.GetComponentInChildren(文本类, includeInactive=1)` → `get_Text`
4. **文本匹配 20 语种 "Play"**（UTF-16 常量表：Play/Spelen/Jocă/开始游戏/…）→ 命中即源按钮
5. `Object.Instantiate(source_go, parent, worldPositionStays=true)` —— **克隆 GameObject 并立刻挂到同父节点**
6. 排版：`sizeDelta ← 源`、`SetSiblingIndex(源+1)`（紧挨在 Play 下面）
7. 改名：`set_name(clone_go, "Multiplayer")`、`set_name(clone_text, "Multiplayer")`
8. `GetComponent(clone_go, 按钮类型)` → 得到克隆按钮；记录 `start_game_transform/label` 供后续同步

### 每帧维护（created 之后）
- `apply_multiplayer_label()`：读**源按钮当前**文本 → 查 20 语种表 → `set_text(克隆文本, 对应语种 Multiplayer 串)`（切语言自动跟着变）
- `sync_button_size()`：每帧同步 `sizeDelta` 和 `localScale`
- **失效重建**：源 GO 不 active / 克隆没父节点（场景切换销毁）→ 全部清空，下一帧重新克隆

---

## 5. 点击接管（核心技巧：改 MethodInfo，不写 inline hook）

`MethodInfo` 布局（arm64，Unity 2019+ 典型）：
```c
typedef struct { void *method_pointer; void *virtual_pointer; void *invoker; const char *name; } MethodPrefix;
```
直接 `__atomic_store_n(&((MethodPrefix*)m)->method_pointer, 我们的函数)` 替换两个方法：

| 被替换的方法 | RVA | 替换为 |
|---|---|---|
| `SFS.Input.GWBAFZUDKCWNMVZC.Update` | 0x15f45a0 | `hooked_update`（先调原函数，再跑克隆/同步逻辑） |
| `SFS.UI.PJLFPTMJUFGFCLXQ.OEPPJOODGHN`（按钮点击处理） | 0x1611670 | `hooked_button_click`（`instance == 克隆按钮` → 拦截；否则调原函数） |

> 为什么可行：MonoBehaviour 的 Update 和 UnityEvent 的回调都经 IL2CPP 运行时的 `methodPointer` 分发，改指针即接管，不用写机器码 inline hook。

### 两个必须处理的坑（源码里已修）
1. **长按事件**：克隆会继承源按钮的 `holdEvent`（长按也会"开始游戏"）→ 给克隆按钮塞一个**新建的空 `HoldUnityEvent`**（`object_new + object_init + field_set`）。
2. **按下反馈**：克隆按钮没有源按钮的按压动画数组（字段 `KGGBLPFAMDI`）→ 从源按钮 `field_get` 出来共享给克隆，否则按下去没反应。

---

## 6. 常量表（全部对应 SFS 1.6.00.14，换版本必须重取）

### probe 版（§1 核心源码）
| 常量 | 值 | 含义 |
|---|---|---|
| DOMAIN_SLOT_RVA | 0x34c0700 | il2cpp domain 全局槽（私有，非导出符号） |
| UPDATE_RVA | 0x15f45a0 | SFS.Input.GWBAFZUDKCWNMVZC.Update |
| FIND_OBJECTS_RVA | 0x2e32ec0 | Object.FindObjectsOfType (2 参) |
| INSTANTIATE_PARENT_RVA | 0x2e32974 | Object.Instantiate (3 参) |
| COMPONENT_GO_RVA | 0x2e2ac64 | Component.get_gameObject |
| GO_COMPONENT_RVA | 0x2e2ad4c | GameObject.GetComponent |
| GO_CHILD_COMPONENT_RVA | 0x2e2afac | GetComponentInChildren |
| GO_TRANSFORM_RVA | 0x2e2d6e0 | get_transform |
| GO_ACTIVE_RVA | 0x2e2da78 | get_activeInHierarchy |
| TRANSFORM_PARENT_RVA | 0x2e38488 | get_parent |
| TRANSFORM_GET/SET_SIBLING_INDEX_RVA | 0x2e39d30 / 0x2e39c74 | 兄弟序号 |
| TRANSFORM_GET/SET_LOCAL_SCALE_RVA | 0x2e38d0c / 0x2e38de0 | 缩放 |
| OBJECT_NAME_RVA / SET_NAME_RVA | 0x2e31f54 / 0x2e3209c | get_name / set_name |
| RECT_POS_RVA / SET_RECT_POS_RVA | 0x2e37798 / 0x2e3785c | anchoredPosition |
| RECT_GET/SET_SIZE_DELTA_RVA | 0x2e3791c / 0x2e379e0 | sizeDelta |
| TEXT_GET_RVA / TEXT_SET_RVA | 0x16db410 / 0x16db6a0 | SFS.UI.BDQTZLEYUDSAXHIV.get_Text/set_Text |
| BUTTON_CLICK_RVA | 0x1611670 | PJLFPTMJUFGFCLXQ.OEPPJOODGHN |
| CLICK_CTOR_RVA / HOLD_CTOR_RVA | 0x1769d68 / 0x1758cfc | ClickUnityEvent/HoldUnityEvent .ctor |

### v0.02 追加
| 常量 | 值 |
|---|---|
| GAMEOBJECT_ADD_COMPONENT_RVA | 0x2e2d6dc |
| TRANSFORM_SET_PARENT_RVA | 0x2e38eb0 |
| BEHAVIOUR_SET_ENABLED_RVA | 0x2e2a3a0 |
| TMP_INPUT_GET/SET_TEXT_RVA | 0x2d9cd34 / 0x2d9cd3c |
| TMP_INPUT_SET_CONTENT_TYPE_RVA | 0x2d9e0fc |
| TMP_INPUT_SET_KEYBOARD_TYPE | 0x2d9e4d0 |
| TMP_TEXT_GET/SET_TEXT_RVA | 0x2da8d30 / 0x2da8e14 |
| ENTER_WORLD_RVA | 0x160adb8（SFS.WorldBase.URMFFTHHDRBSDLHS.EnterWorld，2 参） |

注入器常量：`INIT_ARRAY_VA=0x32a6ec0`、`ORIG_INIT_VA=0x15d9b9c`、`DLOPEN_GOT_VA=0x32fbc90`。

---

## 7. v0.02 登录扩展（源码分析，⚠ 未编译）

`trampoline_il2cpp_native_button_clone_v002_login.c` 在克隆基础上加了：

1. **登录面板**（`create_login_panel`）——**不克隆输入框，直接现造**：
   - `clone_input_field()`：`object_new(GameObject)` + `AddComponent(TMP_InputField)` + `set_parent` + `sizeDelta(400×60)` + `set_contentType(Standard)` + 占位文字
   - three 个输入框（server ip / port / username）+ `create_login_button()`（`AddComponent(UnityEngine.UI.Button)`）
   - 兄弟序号排在源按钮 +2 ~ +5
2. **TCP 登录**（`login_thread`）：
   - 从输入框取 ip/port/name（int 手解析，默认 9806）→ `tcp_connect_and_hello(ip, port, 5000ms)` → 发 Hello 等 JoinAck
   - 成功 → 状态文本 "connected" → `EnterWorld("default", NULL)`
3. **点击路由**：`hooked_button_click` 里 `instance == login_button` → `submit_login()`

⚠ **实测提醒**：`libSFSMPTrampoline_native_button_clone_v002_login.so` 只有 6,040B、无任何 login 符号 —— 说明 v0.02 **没有真正编出来过**（或产物丢了）。你当时跑通的只有 probe 版（27KB，克隆按钮 + 点击拦截日志）。

---

## 8. 换版本 / 换包要改什么

1. **重跑 Il2CppDumper**（`generated-1.6.00.14/` 是旧包产物）→ 新 `dump.cs` / `script.json` / `DummyDll`
2. 用 script.json 重取 §6 全部 RVA；`domain` 槽 0x34c0700 要重找（dump 里搜 `il2cpp::vm::Domain` 或从 `il2cpp_domain_get` 反汇编追）
3. **混淆类名/方法名重查**：`PJLFPTMJUFGFCLXQ`、`BDQTZLEYUDSAXHIV`、`OEPPJOODGHN`、`GWBAFZUDKCWNMVZC`、`URMFFTHHDRBSDLHS`、字段 `KGGBLPFAMDI` —— 每个版本都会变
4. 20 语种 Play 文本表按需更新（新增语言/改文案时）
5. `.init_array` / `dlopen` GOT / 原 init 地址随 libil2cpp 重定位
6. 打补丁前确认 ABI 仍为 arm64-v8a（这包只有 arm64）

---

## 9. 已知坑（从代码和历史产物里看出来的）

1. **RVA 全硬编码** → 版本一更新即失效，工具链必须"dump → 重定位 → 编译"一条龙
2. **v0.02 的 .so 是空壳** → 要用就得自己重新编译（源码在，49KB 那版）
3. **TMP_InputField 只挂组件、没建子 TMP_Text/背景** → 输入框大概率看不见/点不到，需要补：子 GameObject + `TMP_Text` + `RectTransform` 锚点 + `Image` 背景（或干脆克隆游戏内已有的输入框）
4. `FindObjectsOfType(…, 0)` 只找 active；主菜单重建后克隆会失效 → 源码每帧检测并重建（别删这段）
5. 重打包 = 换签名 → **必须先卸载官方版**；存档在 `Android/media/<包名>/` 不受影响，但**不能走 Play 商店更新**（签名冲突，需等新版本重新打）
6. 与卖家 HybridCLR 路线对比：**纯原生无托管运行时**（27KB vs 2MB），胜在轻、无耦合；代价是逻辑只能写 C，用不了 C#/Harmony
7. `add_multiplayer_key()`（build 脚本里）往游戏语言文件注入 `Multiplayer=加入联机` —— 当时是备选取词方案，probe 最终没用它（用硬编码表），可保留当"取当前语言"的另一种手段

---

## 10. 复现步骤（当时环境已验证）

```bash
# 1) 编译 trampoline（NDK 已在 toolchain/android-ndk-r26d）
aarch64-linux-android21-clang -shared -O2 -fPIC \
  -Wl,--no-undefined trampoline_il2cpp_native_button_clone_probe.c \
  -llog -ldl -o libSFSMPTrampoline.so

# 2) 注入 libil2cpp（生成 libil2cpp_injected.so）
python patch_il2cpp_inject_trampoline.py

# 3) 重打包 + 对齐 + 签名（脚本里 ZIPALIGN/JAVA/APKSIGNER 指向 D:\HBuilderX 下的工具）
python build_1.6.00.14_il2cpp_native_button_clone_probe.py

# 4) 安装（先卸载原版）
adb install -r repack-1.6.00.14/sfsmp-1.6.00.14-native-button-clone.apk
# 验证：logcat -s SFSMP  看 phase= / 线程名 sfsmp-ui-created
```

---

## 11. 和你现在"可撤回补丁工具"的关系

这套东西天生符合"可撤回"：
- 只做 **APK 层重打包**（libil2cpp 加一段 shellcode + 多一个 .so），游戏代码/资源语义不动；
- 回滚 = 卸载重装官方包（存档在 Android/media 保留）；
- 不含任何破解/授权逻辑（这个包里没有）。
唯一硬约束：**每个官方版本号都要重跑 dump→重定位→编译**（§8），建议把这套做成脚本化流水线。

---

## 12. 【瓶颈复盘】点击克隆按钮 → 跳到游戏自带页面

### 12.1 根因（三层，都有证据）

1. **克隆继承了源按钮的 `clickEvent`**
   `PJLFPTMJUFGFCLXQ` 字段：`holdEvent` @0x40、`clickEvent` @0x48（`ClickUnityEvent : UnityEvent<DAKNBAKFFBG>`），另有 `onDown/onUp/onClick/onLongClick…` 一组 `CIJNBDHOMPF<T>` 回调容器。
   `Object.Instantiate` 会连同**序列化的 persistent call** 一起复制 → 点克隆按钮 = 执行源按钮的原动作 → 打开游戏自带页面（也就是 `GWBAFZUDKCWNMVZC.OpenScreen` 拉起的屏幕）。
   你当时只把 `holdEvent` 换成空事件，**`clickEvent` 没动** —— 这是第一层。

2. **拦截点选错了分发机制**
   你 hook 的是 Slot 16 接口方法 `OEPPJOODGHN(DAKNBAKFFBG)`（类声明为 `MonoBehaviour, LLFDCJPDPEP, BGBBMDNIBHB, EEBMHKOJAFE`，点击/输入走这三个接口）。
   IL2CPP 的分发规则：
   | 调用方式 | 走什么 | 改 methodPointer 有用吗 |
   |---|---|---|
   | 同程序集内直接调用 | 编译期直接地址 | ❌ |
   | 接口/虚调用 | 类 vtable（`virtualMethodPointer`） | ❌（大概率） |
   | 反射 / 委托 / Unity 消息（Update 等） | `MethodInfo->methodPointer` | ✅ |
   所以你**钩住了 Update**（克隆能建出来），但**钩不住按钮点击**（游戏动作照跑）——症状完全吻合。

3. **反汇编佐证**
   `OEPPJOODGHN @0x1611670` 开头是：类初始化懒加载检查（`ldrb [0x34b3000+0x20b]`）→ 读 `[this+0xB8]` 状态位 → 转到 `onKeyUp(0xB0)` 的事件分发分支 → 分发内部再按事件对象走。
   结论：它是**输入事件的多路分发口**，不是"点击动作"本身；单换它的 methodPointer 拦不干净。

### 12.2 方案 A（推荐，最稳）：切断继承 + 自己检测点击

**Step 1 — 清空克隆的 clickEvent（照抄你 holdEvent 的做法）**
```c
/* click_class 已经从 Assembly-CSharp 解析到 SFS.UI.ClickUnityEvent */
void *empty_click = click_class ? api.object_new(click_class) : NULL;
if (empty_click) {
    api.object_init(empty_click);
    api.field_object_set(clone_button, click_field, empty_click);   /* clickEvent @0x48 */
    mark(240, "click-cleared");
}
```
这一步之后，**点克隆按钮绝不会再触发任何游戏页面**。

**Step 2 — 在 hooked_update 里自己检测点击（不需要托管委托）**
```c
/* 引擎 extern 方法 RVA=-1，解析时只按 类+名字+参数个数，不要做 RVA 校验 */
get_mouse_down = method_by_name(core, "UnityEngine", "Input", "GetMouseButtonDown", 1);
get_mouse_up   = method_by_name(core, "UnityEngine", "Input", "GetMouseButtonUp", 1);
get_mouse_pos  = method_by_name(core, "UnityEngine", "Input", "get_mousePosition", 0);
rect_contains  = method_checked(ui_image, "UnityEngine", "RectTransformUtility",
                                "RectangleContainsScreenPoint", 3, 0x30BD83C, base);
set_active     = method_checked(core, "UnityEngine", "GameObject", "SetActive", 1, 0x2E2D910, base);

Vec3 m = get_mouse_pos(NULL);                    /* Vector3 */
bool inside = rect_contains(clone_transform, (Vec2){m.x, m.y}, NULL, contains_info);
if (get_mouse_down(0, md_info) && inside) pressed = 1;
if (get_mouse_up(0, mu_info) && pressed && inside) { /* 打开目标页面 */ pressed = 0; }
```
Android 上触摸默认映射鼠标键 0；个别机型不映射时改用 `Input.touchCount` + `GetTouch(0).position`。
Overlay 画布的 `cam` 传 NULL；Screen Space - Camera 画布传 `canvas.worldCamera`。

**Step 3 — 打开"目标页面" = 自己的面板**
游戏的页面系统是 `BKFEKAMPHBWLAMTA`（屏幕基类）+ `GWBAFZUDKCWNMVZC.OpenScreen(Func<BKFEKAMPHBWLAMTA>)`，需要**托管 Func 委托 + 自己的屏幕类**——原生侧造不出来。
正确做法（v002 已经在做的方向）：面板用 `GameObject + AddComponent` 自己搭，常驻 inactive，点击时 `SetActive(true)`，关闭按钮 `SetActive(false)`。

### 12.3 方案 B（保留原生点击手感）：改 PersistentCall

克隆后遍历 `clickEvent.m_Calls`（`UnityEventBase` 内部 `List<BaseInvokableCall>`，元素即 `PersistentCall`）。字段偏移（dump 已确认）：

| PersistentCall 字段 | 偏移 |
|---|---|
| m_Target | 0x10 |
| m_TargetAssemblyTypeName | 0x18 |
| m_MethodName | 0x20 |
| m_Mode | 0x28 |
| m_Arguments | 0x30 |
| m_CallState | 0x38 |

- 置 `m_CallState = 0`（Off）→ 继承动作失效（等效方案 A 的 Step 1）；
- 想跑自己的逻辑：把 `m_MethodName` 改成**你已 hook 过 methodPointer 的方法名**——UnityEvent 的反射 invoke 会经过 methodPointer，这时你的 hook 才生效。
- 优点：按压音效/发光全保留；缺点：依赖事件内部结构，跨版本更脆。

### 12.4 方案 C（最省事）：不克隆，现造按钮

v002 的 `create_named_button` 路线：全新 `GameObject + AddComponent(UnityEngine.UI.Button)`，零继承行为，绝不跳页；配方案 A 的轮询检测。
缺点：失去 SFS 原生按钮外观（要自备背景图/字体/点击音效）。

### 12.5 顺手纠正两个坑

1. **`GWBAFZUDKCWNMVZC` 是屏幕栈管理器**（`OpenScreen/CloseStack/CurrentScreen`），不是输入模块 —— "跳到游戏自带页面"就是它 `OpenScreen` 拉起的。你拿它的 `Update` 当"每帧 tick"没问题，但别指望它处理点击。
2. v002 清 holdEvent 时用 `image_named(..., "SFS.UI.dll")` 去解析 `HoldUnityEvent` —— probe 里明明是用 `Assembly-CSharp`（cs）解析成功的，`SFS.UI.dll` 很可能不存在 → `class_from_name` 返回 NULL → `object_new(NULL)` 有崩溃风险。统一用 cs 解析并判空。

### 12.6 快速验证（10 分钟）

把 Slot 16 的 `methodPointer`（offset 0）**和** `virtualMethodPointer`（offset 8）两个都换掉再点一次：
- 被拦住 → 说明分发走 virtual，方案 B 的 `m_MethodName` 改法可行；
- 还是跳页 → 死心走方案 A/C（轮询检测），别再纠缠 hook。

### 12.7 本次新增常量

| 项 | 值 |
|---|---|
| GameObject.SetActive | 0x2E2D910（1 参） |
| RectTransformUtility.RectangleContainsScreenPoint | 0x30BD83C（3 参） |
| UnityEventBase.AddListener(BaseInvokableCall) | 0x2E3BEAC（备用） |
| 按钮字段：holdEvent / clickEvent / onClick | 0x40 / 0x48 / 0x68 |
| PersistentCall：m_CallState / m_MethodName | 0x38 / 0x20 |
| Input.*（引擎 extern） | RVA=-1，只按名字解析 |
