# SFS 手机端联机 —— 阶段总结（2026-10-21）

> 目标包：`com.StefMorojna.SpaceflightSimulator` 1.6.00.22（官方包，重打包 + 重签名 + 注入 `libsfsmp.so`）
> 设备：vivo V2046A（Android 13 / SDK33 / arm64-v8a / 1080×2376），无 root
> 工作目录：`D:\SFS-Multiplayer\android`（原 `D:\SFS-Pro` 已并入 `android\work`）
> 铁律：**`D:\SFS-Multiplayer\1` 只读，绝不改动**（自动同步仓库）

---

## 一、今天真正做完的事

### 1. 主菜单「多人联机」按钮（已完成，稳定）
- 克隆官方 `Play Button` → 清 `clickEvent(0x48)` / `holdEvent(0x40)` / **`onClick(0x68)`** → 禁用按钮组件 → 自己用 `RectangleContainsScreenPoint` 轮询命中。
- 关键教训：**不清 `onClick@0x68` 会跳回游戏原页面**（点「多人联机」进「我的世界」就是这个原因）。
- 位置：`src/v43.c` 的 `mainmenu_button()` / `poll_click()`。

### 2. 原生登录弹窗（已完成，稳定）
- 放弃克隆窗口（两次崩溃），改用游戏自带的多行输入对话框：
  `SFS.UI.DEOTNULUBFZWSSUA.textInput.Open(返回文案, 确认文案, Action<string[]>, closeMode, Element[])`
- `VCJMEXUSBHCVBJHA.Element(标签, 初始值)` —— **第二参数是真实内容，不是占位符**（踩过：输入框里预填了字）。
- 三行中文：`服务器 (IP:端口 或域名)` / `用户名` / `密码 (可留空)`；密码 `set_contentType(7)` 圆点遮蔽。
- 打开时弹原生 toast 状态行（`EJIHSVFRLSOQRBJE.main.Log`），证明链路活着。
- 输入实时抓取（轮询元素数组 `+0x60` → `textBox +0x30` → TMP `m_Text +0x210`）。
- 当前档位：`sfsmp-mode.txt = 11`。

### 3. 崩溃排查记录（都定位到根因）
| 现象 | 根因 | 修复 |
|---|---|---|
| 按钮点了没反应 | `page_show_login` 里 `if (g_login_page) return;` 早退 | 存在则先销毁重建 |
| 装完没按钮 | `read_mode()` 只读一位，`10` 被读成 `1` | 放宽到 0..99 |
| 找不到 `- World Creation -` | 用 `Assembly-CSharp` 的 image 查 `UnityEngine.Transform` | 改 `UnityEngine.CoreModule`（`coreImg`） |
| 点开就卡死/闪退 | 克隆窗后 `SetParent(..., true)` 触发布局重算；以及诊断代码拿 **GameObject** 调 `Transform.get_rect` | 放弃改父级；删掉错误诊断 |
| 弹窗 1 秒后崩 | 同上（240 帧一次的诊断代码） | 删除后稳定 |

### 4. 存档点
`D:\SFS-Multiplayer\android\checkpoints\v44_native_textinput_ok`（源码 + .so + APK + 文档 + 截图 + `MANIFEST.sha256`）

---

## 二、研究成果（10 份，全部带证据）

### A. 安卓原生 UI（`android/research/native_ui/`）
| # | 结论 |
|---|---|
| 00 | 总路线（先修 mode 8 非法调用，再谈页面） |
| 01 | 卖家页面 = 5 个页面 + 1 入口；`DrawText/DrawFullButton` 的 int 是 **layoutPriority 不是行号**；每页默认 14 行；翻页按钮 priority=`INT_MAX`/`INT_MAX-1` |
| 02 | 主菜单**拿不到** `NOJZIZCFSEXSUEEX`（0 实例、无工厂）；可克隆 `- World Creation -/Holder`、`- Worlds Menu -/Holder`、`- Mods Menu -/Holder`、`- Update Popup -` 整根 |
| 03 | `Open` 零委托**非法**（会先 `Close` 再 NRE）；`Action<T>` 回调的 `x1` = `inst` 本身；`Draw*` 只入队，必须回调内画 |
| 04 | TMP：读 `m_Text +0x210`；`set_contentType(7)` 开密码；`ActivateInputField` 需跨帧；另有原生多行输入对话框 `DEOTNULUBFZWSSUA.textInput` |
| 05 | 三个窗根真名带短横线；**只克隆 `Holder`，不克隆 `- X -` 根**（根的 Awake 会写静态单例）；按钮无 `RemoveAllListeners` |

### B. 内存注入 / 联机落地（`android/research/memory_injection/`）
| # | 结论 |
|---|---|
| 01 | PC 模组 73 个目标方法：41 按名可解析、23 按 RVA、9 不明；缺口 = 进世界入口（`LoadSave`+`SceneLoader.LoadWorldScene`）与 `Rocket.OnFixedUpdate`；UI patch 整类作废（依赖 UITools 模组） |
| 02 | **卖家不是 HybridCLR**：`libpatch.so` 无 IL 解释器，是「类名+方法名」hook 框架 + 80 个 RVA 补丁；复用其跑 C# 模组**不可行**；自建 HybridCLR 缺匹配 il2cpp 源码，不划算 |
| 03 | 协议层**零改动可复用**（ikcp/codec/handshake 等 9 文件已交叉编译 aarch64 通过）；最小握手包 54 字节；全量约 47 人日，MVP 31 人日 |
| 04 | 主 hook 用 **methodPointer 替换**（现用方案，实测稳定）；需要拦参数化/静态调用才上 ShadowHook；主线程任务队列 C 实现已通过 TSan；候选 RVA 齐全 |
| 05 | MVP 30–44 人日（7.5–11 周）；M0 spike 0.5–1 人日（对象创建 + 坐标换算，唯一能推翻方案的一环）；两个范围决策 D1（先飞行后连）、D2（远端用代理标记） |

---

## 三、路线结论（定稿）

```
UI 层：原生弹窗（已完成）→ 后续再美化（声明页/状态行）
联机层：原生 C++ 客户端
         ├─ 复用 Server/cpp 的 KCP + 协议（9 个文件）
         ├─ methodPointer 替换挂主线程 tick（现方案）
         ├─ 按方法名 / RVA 调 il2cpp_*（混淆类名 + dump 映射）
         └─ 世界侧：读 Rocket 位置/姿态 → 发包；收包 → 建代理标记
```

**不做**：卖家式 IL 运行时、部件级重建、手机端建世界、时间加速投票。

---

## 四、我的工作方式（这次踩出来的规矩）

1. **先研究、后动手**：不确定的方向先派子 agent 各自独立出报告（同方向多份，互相校验）。
2. **每份报告边做边写**：子 agent 中断就全丢，只有落盘的部分能救回来。
3. **主 agent 自己不留长任务**：编译/装机/日志三段式，每段都有可观察输出。
4. **崩溃先看 tombstone**：`logcat -b crash` 拿到 `#08 libsfsmp.so (hook_update+1652)` 这种帧，再用 `llvm-objdump` 对到具体指令，比猜快十倍。
5. **可逆优先**：所有实验都留「回退到上一版」的路径（存档点 + 旧 .so）。
6. **不碰 `1/` 目录**，所有产出落在 `android/`。

---

## 五、当前状态 / 下一步

- 手机：v45 / mode 11，原生中文输入弹窗稳定，按钮正常。
- 待办（按优先级）：
  1. **M0 spike**：安卓世界内创建可见对象 + 读火箭位置姿态（0.5–1 人日，决定成败）
  2. M1 网络地基（KCP + 握手 + 重连，5–7 人日）
  3. 登录页接值 + 自动补端口 / SRV / CNAME / 真 ping
  4. 声明页（原生窗，P2/P3）
- 阻塞：API 余额见底，重活暂停；M0 只需 2–3 次编译装机，可随时开工。
