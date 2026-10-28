# 诊断求助：安卓 SFS 注入后卡在「加载中」

## 环境
- 游戏：Spaceflight Simulator 安卓 **1.6.00.22**（`com.StefMorojna.SpaceflightSimulator`），官方包重打包 + 重签名
- 注入：`libsfsmp.so`（我们自己的 native 库，NDK r27c `aarch64-linux-android24-clang`，arm64-v8a）
- 加载方式：`UnityPlayerActivity.onCreate` 里 `System.loadLibrary("sfsmp")` → `JNI_OnLoad` 起 pthread → `sleep` 等 IL2CPP 起来 → `dlopen("libil2cpp.so")` + `dlsym`
- 设备：vivo V2046A / Android 13 / arm64-v8a / 无 root
- 游戏进程正常存活（pid 在），**没有 native crash、没有 ANR、logcat 无 FATAL**

## 症状
点「开始游戏」→ 进入世界后卡在 **「加载中」**（屏幕中央文字），永远不结束。
此时：
- 游戏主循环仍在跑（我们的 hook 计数器持续增长：f=1500 → 2400）
- 主菜单单例指针 `mmInst` 仍非空
- 没有任何异常日志

## 我们在游戏里做了什么（按时间顺序）
1. **替换 `ScreenManager`（混淆名 `VNDAREHWRIQNAAKC`）.Update 的 methodPointer**：
   ```c
   void** mi = (void**)method;         // il2cpp MethodInfo
   mprotect(page, PROT_READ|WRITE|EXEC);
   g_orig = mi[0];                     // methodPointer
   mi[0] = hook_update;                // 替换
   ```
   `hook_update` 每帧做：帧计数、主菜单按钮逻辑、`poll_click`、最后 `g_orig(self)` 转调原函数。
2. 早期版本里，`hook_update` 每 60 帧调用一次 `FindObjectsOfType(RocketType)` 读火箭位置 —— 已确认这个会**在加载期间卡死主线程**，已移除。
3. 现在改成：只在写文件 `sfsmp-probe-now.txt=1` 时才手动触发一次 `FindObjectsOfType`。
4. 另外还**替换过 `WorldTime`（`VCQSQMYGGZECNBPE`）.Update 的 methodPointer**（现在 hook 体是空的，只转调原函数）。

## 关键疑问（请逐条判断）
1. 卡「加载中」但主循环仍在跑，是否说明 `SceneManager.LoadSceneAsync` 的 `isDone/allowSceneActivation` 永远没被推进？在 IL2CPP 里，什么情况会让异步加载停在最后一帧？
2. **替换 `MonoBehaviour.Update` 的 `MethodInfo.methodPointer`（偏移 +0）是否会影响 Unity 的协程/场景加载？** 已知该 build 的 MethodInfo 布局是 `methodPointer(+0) / virtualMethodPointer(+8) / invoker_method(+0x10)`，虚调用走 vtable（+8）。我们改的是 +0。
3. 是否有可能是**存档在第一次卡死时被写坏**（加载中途被强杀），导致之后每次加载同一存档都卡住？如何从 `/sdcard/Android/data/<pkg>/files/` 判断存档是否损坏？
4. 如果 hook 本身没问题，**加载卡住还有哪些常见原因**（缺资源、Shader 变体、`WaitForEndOfFrame` 协程、`AsyncOperation.progress` 停在 0.9）？
5. 有没有更安全的观察手段：在不注入任何 hook 的干净包里，如何确认是「存档问题」还是「注入问题」？

## 我们已经准备做的对照实验
- A. 装官方原版 APK（不注入），加载同一个存档 → 判断存档是否损坏
- B. 注入包里**新建一个世界**（不加载旧存档）→ 判断是否与存档相关
- C. 注入包但**不安装任何 hook**（只 dlopen，不做 methodPointer 替换）→ 判断是否 hook 引起

## 日志样本
```
v44: hb f=1500 mmInst=0x7560491a00 mmField=0x75c0456a78 sg=0x7656dfcd40 mode=12
v44: hb f=1800 mmInst=0x7560491a00 ...
v44: hb f=2100 ...
v44: hb f=2400 ...
```
（f 是 hook_update 的帧计数；mmInst 是主菜单单例）
