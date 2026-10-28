# SFS 多人联机 —— 原生 UI 路线（5 份子 agent 报告合并结论）

> 目标版本：SFS 安卓 1.6.00.22（`com.StefMorojna.SpaceflightSimulator`），native 注入 `libsfsmp.so`（无 root、重打包重签名）。
> 依据：`build/research/native_ui/01..05*.md`（共 165 KB）+ `agent_E_native_ui.md`。
> 结论分级：**硬证据**＝反汇编/dump/资源实证；**推断**＝标注 ⚠ 需上机。

---

## 一、五条硬结论（决定架构）

| # | 结论 | 证据 |
|---|---|---|
| 1 | 主菜单（level0）**拿不到** `NOJZIZCFSEXSUEEX`（StatsMenu 抽屉）：场景里 0 个实例，全二进制无静态字段/工厂/`AddComponent<NOJ>`，手工 new 出来是零像素空壳（15 个视觉字段全是同场景 PPtr） | 02 §1–§2（1446 GO / 2756 MB 全扫） |
| 2 | 零委托 `Open(inst,NULL,NULL,false,NULL,NULL)` **非法**：`0x18DD824 cbz x24` → NRE raiser；且 NRE 发生在 `bl Close(this)` 之后 ⇒ 先关面板、清按钮表，**有破坏性副作用** | 03 §2（`0x18DDA68`） |
| 3 | `Action<NOJ>` 回调的**面板参数在 x1，且恒等于 `inst`**（`0x18DD82C mov x1,x19`）；`x0=*(del+0x40)`、`x2=*(del+0x28)=MethodInfo*` 绝不可解引用 | 03 §1.3/§3 |
| 4 | `DrawTitle/DrawText/DrawFullButton` 只是**入队**（`List<NAABCLLJEKA>@0x98`），由 `Open` 在回调返回后 sort+实例化并 `Clear` ⇒ **内容必须在回调里画**，Open 之后再画无效 | 03 §4 |
| 5 | `DrawText/DrawFullButton` 的第一个 `int` 是 **layoutPriority 不是行号**；分页器按 priority 排序后切片，未放行的行不实例化；每页默认 14 行（`[4,40]` 钳位） | 01 §3 |

## 二、level0 可克隆窗（主菜单原生 UI 的唯一来源）

`--- UI ---`(Canvas, go=221) 直属窗根（名字**带前后短横线**）：

| 窗 | 根 / 克隆目标 | 用途 | 注意 |
|---|---|---|---|
| `- World Creation -` | 根 go=680 → **克隆 `Holder` go=214** | **登录页首选**：唯一现成 TMP 输入框（go=760）+ 自带安卓软键盘 | 克隆后改标题、改行文案 |
| `- Worlds Menu -` | 根 go=138 → **克隆 `Holder` go=210** | 服务器列表；行模板 `World Element Prefab`(go=216) 每行 4 按钮 | 滚动交原生 `Scroll Window` |
| `- Mods Menu -` | 根 go=318 → **克隆 `Holder` go=328** | 通用窗：标题+列表+底栏 | 窗内**无**标题节点，需自己加 |
| `- Update Popup -` | 根 go=467（整根可克隆） | 纯提示弹窗 | `PKVTCUBZSQHIVNVO` 无 Awake，`OnOpen` 只 SetActive |

**铁律**
- 克隆 `Holder`，**绝不克隆 `- X -` 根**：根上 `Awake` 把 `this` 写进 `static main`（`0x16C1488`）/ ModLoader 单例（`0x173029C`），克隆体会顶替真单例。
- `Instantiate` 只用 **3 参版 `0x2FC1B58`**；1 参版 `0x2FC19C4` 实测崩。
- 按钮是自定义 `HKAZNXTEGNSSTQLF`（`onClick@0x68` = 自定义委托聚合器），**没有 RemoveAllListeners**；要清点击就 `Destroy` 克隆体上的按钮组件，点击判定走 `RectTransformUtility.RectangleContainsScreenPoint` 轮询。滚动面的按钮**必须保留**（`NEUBHWOMTRBTWJHK` 收拖拽）。

## 三、输入框（04）

- 读值：直读 `[TMP_InputField+0x210]`（`m_Text`，明文，零分配）。
- 写值：`set_text`（触发 onValueChanged）／`SetTextWithoutNotify`（0x2F2B718）。
- 密码：`set_contentType(7)` → `EnforceContentType` 自动 `m_InputType=2` + 圆点遮蔽，键盘 `secure=true`。
- 弹键盘：`ActivateInputField()` 只置标志，真正 `TouchScreenKeyboard.Open` 在 **LateUpdate** ⇒ 必须跨帧、GO active、`Interactable=1`。
- 克隆输入框必须**换掉 7 个 UnityEvent**（0x1A0/0x1A8/0x1B0/0x1B8/0x1C0/0x1C8/0x1D0）为空对象，`0x1E0` 置 0，否则会触发原表单逻辑。
- 更省事备选：`SFS.UI.DEOTNULUBFZWSSUA.textInput`（静态槽 +0x10）= 游戏自带多行输入对话框，`Open(title,msg,Action<string[]>,closeMode,Element[name,placeholder][])`，level0 就有实例、零克隆、自动处理键盘（代价：需伪造 `Action<string[]>` 委托）。

## 四、落地顺序（v44 → v46）

### v44（现在就能做，主菜单原生窗）
1. 删 v43 的 **mode 8**（零委托），只留 mode 9。
2. `mode 10`：运行时找 `- World Creation -` → 取 `Holder` → `Instantiate(holder, canvas, false)` → 设 `sizeDelta`（`Scroll Window` 是拉伸+负边距，必须**先 SetParent 再改**）→ 改标题/行文案 → 接「Join Server」按钮。
3. 原生 toast 兜底：`EJIHSVFRLSOQRBJE.main.Log(string)` @`0x16D64C4`（零克隆）。

### v45（进世界后，真抽屉）
- `mode 9`：`Open(inst, Func<bool>=返1, Action<NOJ>=builder, false, NULL, NULL)`；builder 里只认 `x1`（==inst），用 **klass 指针相等**判断，别 `strstr`；`Draw*` 全部在回调内。
- 排序用 priority：标题 `INT_MAX-3`、内容行递增、`<   Previous`=`INT_MAX`、`Next   >`=`INT_MAX-1`。

### v46（列表 + 表单）
- 服务器列表：克隆 `- Worlds Menu -/Holder` + `World Element Prefab`，每卡 6 控件（名称/状态/进入/连接/删除/编辑）。
- 添加/编辑表单：3 个 TMP 行（名称/密码/IP），密码行 `contentType=7`；加入表单再加昵称行。

## 五、仍需上机核对（⚠ 推断项）
1. 加入表单三行上下顺序（寄存器复用，静态无法定序）。
2. `Func<bool>` 返回方向（先返 1，面板自关则改 0，用日志判定）。
3. 克隆 `- World Creation -/Holder` 后 `Scroll Window` 的 `sizeDelta=(-50,-240)` 是否需按 1080×2376 重算。
4. 卖家直连页布局（P3 复用推断）。

---

## 附：本路线引用的 RVA
`Instantiate(3参) 0x2FC1B58` · `SetParent 0x2FC8DA8` · `NOJ.Open 0x18DD724` · `Close 0x18DC824` · `DrawTitle 0x18DD5D4` · `DrawText 0x18DDC08` · `DrawFullButton 0x18DDCF8` · `TMP.set_text 0x2F2B5DC` · `TMP.UpdateLabel 0x2F2B900` · `ActivateInputField 0x2F36F30` · `EJIHSVFRLSOQRBJE.main.Log 0x16D64C4` · `DEOTNULUBFZWSSUA.Awake 0x17EF4F8`
