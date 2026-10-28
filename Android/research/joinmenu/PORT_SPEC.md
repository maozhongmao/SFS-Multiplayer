# 端游 JoinMenu → 安卓端迁移规格

来源：`Client/src/MultiplayerSFS.Client/MultiplayerSFS.Mod/JoinMenu.cs`（706 行，V1.4，就是用户截图那版）
参考图：`D:/SFS-Multiplayer/android/checkpoints/v44_native_textinput_ok/screens/03_pc_joinmenu_reference.png`
声明全文：`research/joinmenu/statement_en.txt`（5,171 字符，纯英文）

---

## 1. PC 版常量（照抄即可）

| 名称 | 值 | 说明 |
|---|---|---|
| ContentWidth | 940 | 内容总宽 |
| RowGap | 20 | 行/列间距 |
| LabelWidth | 300 | 左侧标签宽 |
| InputWidth | 620 | 右侧输入宽（940-300-20） |
| RowHeight | 50 | 单行高 |
| ButtonWidth | 460 | 按钮宽（(940-20)/2） |
| ButtonHeight | 90 | 按钮高 |
| StatementInnerPadding | 15 | 声明区内部留白 |
| StatementExpandedHeight | 320 | 声明区高度 |
| StatusHeight | 24 | 状态行高（失败时 ×2） |
| DefaultPort | 9806 | 默认端口 |
| 窗口标题 | `Multiplayer SFS - Join Menu` | — |

## 2. PC 版控件树

```
Window (Vertical, spacing 20, padding 10)
├─ row (Horizontal, spacing 20)
│   ├─ labelColumn (Vertical, spacing 20)
│   │   ├─ Label "Server"    300×50  MidlineLeft
│   │   ├─ Label "Username"  300×50
│   │   └─ Label "Password"  300×50
│   └─ inputColumn (Vertical, spacing 20)
│       ├─ TextInput 620×50  (endpoint, 形如 1.2.3.4:9806；解析失败变红)
│       ├─ TextInput 620×50  (username, Trim)
│       └─ TextInput 620×50  (password, 不 Trim)
├─ statementArea (Vertical, spacing 8)
│   ├─ statementWindow 940×320  (可关闭窗, 垂直滚动, opacity .45)
│   │   │  标题 = "User Statement (accepted)" / "User Statement (read to the end)"
│   │   └─ Label 910×auto, FontSize 18, TopLeft, ContentSizeFitter(Vertical)
│   │       └─ 文本 = statement_en.txt (5,171 字符)
│   │       └─ Space 910×48（底部留白，滚到底判定用）
│   └─ statusLabel 940×24, FontSize 18, MidlineLeft
│       成功：`Server: <addr>   Client V1.4 (handshake 2)   Ping: 12 ms   Players: 3/8`
│       失败：`Server: unreachable` + 第二行原因（高度 48）
└─ buttons (Horizontal, spacing 20)
    ├─ Button "Back"        460×90 → Close()
    └─ Button "Join Server" 460×90 → CheckAndJoin()
        未读完声明时文案 = "Read to the end to continue" 且禁用
```

行为要点
- 声明「滚到底」判定：`statementScroll` 到底部时把 `statementRead=true`，标题与按钮文案同步切换。
- 接受状态持久化：`PlayerPrefs["multiplayersfs.statement.accepted"] == "2026-08-16"`。
- 上次连接：`multiplayersfs.last.server` / `multiplayersfs.last.username` 回填。
- 状态轮询：KCP 探针（默认端口 9806），失败显示 unreachable；`joinAllowedByStatus` 控制按钮可用。

## 3. 安卓端能力对照

| PC 控件 | 安卓是否有现成 | 方案 |
|---|---|---|
| 窗口 Window(标题栏) | ✅ 克隆 `- Update Popup -` 整根（无 Awake，`OnOpen` 只 SetActive） | P2 |
| Label | ✅ 任意克隆体上的 Text 子节点 + `set_Text` | P2 |
| TextInput | ✅ `- World Creation -/Holder/Rows Frame` 里的 TMP 输入（1 个）或 `XPYDSZFDOLBQODKZ` | P1/P2 |
| 密码圆点 | ✅ `set_contentType(7)` → `EnforceContentType` 自动遮蔽 | P1 |
| 滚动声明区 | ⚠️ 需 ScrollRect + ContentSizeFitter，游戏里 `Mods Menu/Scroll Window` 可克隆 | P3 |
| 按钮 | ✅ 克隆官方按钮（`HKAZNXTEGNSSTQLF`，清 0x40/0x48/0x68 + 禁用组件 + 自轮询） | P1 |
| ModGUI Builder | ❌ 手游无 `SFS.UI.ModGUI` / `UITools` | 全部手搓 |

## 4. 分三阶段落地

### P1（低风险，先定型文案）
当前用 `DEOTNULUBFZWSSUA.textInput`（mode 11），只改参数：
1. 三个元素：`Server` / `Username` / `Password`（与 PC 同名，去掉中文）
2. 说明行改成状态行：`Server: unreachable   Client V1.4 (handshake 2)   Ping: --   Players: --`
3. 读值后：密码元素 `textBox(0x30)` → TMP `set_contentType(7)`
4. 不动布局（textInput 是固定模板，做不出 940 宽与声明区）

### P2（中等风险，壳+行布局）
1. 克隆 `- Update Popup -` 作为窗口壳（标题 = `Multiplayer SFS - Join Menu`），验证稳定性（v44 时期那次崩溃已定位为本地诊断代码，不是克隆本身）
2. 在壳里放：3 行「Label + 输入」→ 克隆 `- World Creation -/Holder/Rows Frame` 的子行（它本身就是 label+input 行）
3. 底部两键：克隆官方按钮，改文案为 `Back` / `Join Server`
4. 先不做声明区，状态行用一行 Text

### P3（高风险，声明滚动区）
1. 克隆 `- Mods Menu -/Holder/Scroll Window`（带 ScrollRect）作为声明容器
2. 塞入一个 Text（5,171 字符，FontSize 18，TopLeft）+ 底部 48 高空白
3. 监听滚动到底 → 切标题与按钮文案（对应 PC 的 `statementRead`）

## 5. 关键 RVA（1.6.00.22）
| 用途 | RVA |
|---|---|
| `Instantiate(Object,Transform,bool)` | 0x2FC1B58（1 参版会崩） |
| `TMP_InputField.set_text` / `SetTextWithoutNotify` | 0x2F2B5DC / 0x2F2B718 |
| `TMP_InputField.EnforceContentType` | 0x2F2CA10 |
| `TMP_InputField.ActivateInputField` | 0x2F36F30（跨帧弹键盘） |
| `m_Text` 字段偏移 | +0x210（明文） |
| `DEOTNULUBFZWSSUA.textInput` | 静态字段；`VCJMEXUSBHCVBJHA.Open/Element` |
| `EJIHSVFRLSOQRBJE.main.Log(string)` | 0x16D64C4（原生 toast） |

## 6. 已知坑（必须写进代码注释）
1. 手搓 `Action<T>` 的 `+0x28` 必须是真实 `MethodInfo*`；为 NULL 一调用就崩 → 目前 textInput 回调传 NULL，**别按弹窗按钮**。
2. 克隆窗只克隆 `Holder`，不克隆 `- X -` 根（根上 Awake 会写静态单例）。
3. `SetParent` 改父级 + 布局重算会卡死/崩 → 克隆时直接用 3 参 Instantiate 指定父级。
4. 诊断代码里 `get_rect` 只能对 `RectTransform` 调，传 GameObject 会崩（已踩）。
5. 文案保持英文：SFS 手游字体不含中文，会渲染成方框；中文只出现在我们自己画的 Android View 里。
