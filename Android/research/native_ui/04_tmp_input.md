# 04 原生窗里的 TMP 输入框：克隆 / 使用 / 密码 / 软键盘 / 读值（SFS 安卓 1.6.00.22）

> 材料（全部只读分析，未改工程、未连设备）
> - `D:/sfs-tools/apk/dump-official/dump.cs` + `script.json`（.22 官方 dump，597,338 行）
> - `D:/SFS-Pro/work/libil2cpp.so`（.22 官方；`.text` VA 0x13BD4D0、`il2cpp` 段 VA 0x166435C，**VA == 文件偏移**）
> - 反汇编器：`D:/SFS-Pro/work/dis_name.py`（capstone + script.json 符号化，`python dis_name.py 0xRVA [条数]`）
> - 既有成果：`D:/SFS-Pro/build/agent_E_native_ui.md`（卖家逆向 / R-A 克隆路线）、`D:/SFS-Pro/build/native_button_clone_study.md`（v32 按钮克隆 8 步）、`D:/sfs-tools/so/src/v41.c`（我方现状）
>
> 结论全部带 RVA 与指令级证据，凡属**推断**（未在真机验证）都标注 ⚠。

---

## 0. TL;DR（先看这 8 条）

1. `TUXXAURTZSIFPXTZ`（dump.cs:20505）只是 `TMP_InputField` 的**薄包装**：`mobileTextBox 0x20`（安卓专属 SFS 自带输入菜单 `LINRJGANWEEFRCUR`）、`TMProTextBox 0x28`（真 TMP）、`0x30` 是“两个组件已解析”标志。**主菜单活实例 = `LKSRSZGMFKUEATEP.worldNameField(0x48)`**。
2. 读值两条路：`wrapper.get_Text`（0x18D94AC，有 mobile 走 mobile，否则直接读 `[TMP+0x210]`）或**直读 `[TMP_InputField+0x210]` = `m_Text`**（0x2F2B5D4 = `ldr x0,[x0,#0x210]; ret`，最短、零分配、拿到的是**未遮蔽的真值**）。
3. 写值：`TMP_InputField.set_text`（0x2F2B5DC）→ `SetText(value, sendCallback=true)`（0x2F2B5E4）→ `UpdateLabel` → 尾调用 `SendOnValueChanged`（0x2F2BBC4）。**只触发 `onValueChanged`，不触发 `onEndEdit`**（实测反汇编：0x2F2B700 `b SendOnValueChanged`）。要静默写值用 `SetTextWithoutNotify`（0x2F2B718）。
4. **密码隐藏是靠 `m_InputType==2` + `m_AsteriskChar(0x178)` 在 `UpdateLabel` 里遮蔽**：`String.CreateString(m_AsteriskChar, m_Text.Length)`（0x2F2B954–0x2F2B97C）。设 `contentType = 7 (Password)` 会由 `EnforceContentType`（0x2F2CA10）自动置 `m_InputType=2 / m_KeyboardType=0 / m_LineType=0 / m_CharacterValidation=0`；软键盘据此传 `secure=true`（0x2F30284 `mov w4,#1`）。
5. 软键盘：`ActivateInputField()`（0x2F36F30）**只置 `m_ShouldActivateNextUpdate(0x261)`**，真正弹键盘在 **`LateUpdate`（0x2F2F710）→ `ActivateInputFieldInternal`（0x2F2FF6C）**：`EventSystem.SetSelectedGameObject(自己的 GO)` + `TouchScreenKeyboard.Open(...)`。所以**必须**：主线程、GO active、组件 enabled、`m_TextComponent≠null` 且 `font≠null`、`Interactable=1`；并且**跨帧**（SFS 自己都用 0.1s 延迟，见 §3.1）。
6. 克隆沿用 v32 已跑通的链路：`Object.Instantiate(Object,Transform,bool)`（`g_mInst3`）→ `SetParent` / `SetSiblingIndex` / `sizeDelta` / `localScale`，再 `GetComponent(TMP_InputField 类型)` 拿 TMP。**唯一新增的风险是克隆体带着原 `onValueChanged/onEndEdit` 监听**（原窗的 CreateWorld 逻辑），必须像 v32 清 `clickEvent` 那样**换一个新的空 UnityEvent**。
7. `worldNameField` 不可用时的可靠替代（比乱找实例好）：**`SFS.UI.DEOTNULUBFZWSSUA.textInput`（静态字段，`Awake` 里赋值的 `VCJMEXUSBHCVBJHA`）**，它是游戏自带的「多行文本输入对话框」，一次 `Open(标题, 说明, Action<string[]>, closeMode, elements[])` 就能拿到 3 行原生输入框（`Element(name, placeholder)` 静态工厂造行），**且它是 UI 根预制体上常驻的**，level0 就有实例（见 §4.3）。
8. 兜底路径：`FindObjectsOfType(TMP_InputField, includeInactive=1)` → 任意一个实例都能 `Instantiate`（含 inactive 的，克隆后 `SetActive(true)`）；level2 里另有 `RWLYPHTVNCFXMWYC.longitudeInput(0xB8) / heightInput(0xC8)` 两个活输入框。

---

## 1. dump 侧事实（问题 1）

### 1.1 `TUXXAURTZSIFPXTZ`（dump.cs:20505，TypeDefIndex 551）

```
public class TUXXAURTZSIFPXTZ : MonoBehaviour
{
    // Fields
    public LINRJGANWEEFRCUR mobileTextBox;   // 0x20  ← SFS 自带安卓输入菜单（不是 TMP！）
    public TMP_InputField   TMProTextBox;    // 0x28  ← 真 TMP 输入框
    private bool            KMMKEFMINBH;     // 0x30  ← CMLOJBJFFFE 里的“已解析/已初始化”标志
    // Properties: string PEDMKFBBODP {get;set;}（即 Text）， GameObject PCMNDOLFMLG {set;}（即 Holder）
}
```

方法（RVA 全部逐条反汇编核对过）：

| 方法 | RVA | 反汇编结论 |
|---|---|---|
| `CMLOJBJFFFE`（懒解析，私有 0x18D9360） | 0x18D9360 | 若 `[this+0x30]==0`：`GetComponent<LINRJGANWEEFRCUR>()`→写 `0x20`、`GetComponent<TMP_InputField>()`→写 `0x28`（`bl 0x1A448DC = UnityEngine.Component$$GetComponent<T>`），最后 `[this+8]=1`。**两个字段是“同一 GameObject 上取组件”**，不是子物体。 |
| `get_Text` | 0x18D94AC | `CMLOJBJFFFE()` → 若 `mobileTextBox(0x20)!=null` 则 `tail LINRJGANWEEFRCUR.get_Text`（0x1759A94）；否则若 `TMProTextBox(0x28)!=null` 则返回 **`[0x28+0x210]` = TMP 的 `m_Text`**（0x18D9560 `ldr x0,[x8,#0x210]`），两者皆空返回 null。 |
| `set_Text` | 0x18D9578 | `CMLOJBJFFFE()` → 有 mobile 则 `LINRJGANWEEFRCUR.set_Text`（0x1758FFC）；有 TMP 则 `TMP_InputField.set_text`（0x2F2B5DC）。**双写**。 |
| `Open` | 0x18D9654 | ① 若 `mobileTextBox!=null` → 调它虚表槽 `[vt+0x1C8]`（= 菜单 `Open()`）；② 若 `TMProTextBox!=null` → `new Action(this, IFJGLLJNJCA)`（0x291CDC4 = `System.Action..ctor`，目标方法 `[0x3456000+0x8B0]`）→ `GMGBCNEOEIC.PAGIBFDOPID(s0=0.1f, x0=action)`（0x17B5704）。 |
| `Close` | 0x18D9404 | 若 `mobileTextBox!=null` → `tail [vt+0x1B8]`（= 菜单 `Close()`）。**不动 TMP**。 |
| `IFJGLLJNJCA`（编译器生成的 Action 体） | 0x18D948C | `x0=[this+0x28]`（TMP）；非空则 `br [vt+0x398]`（= **`ActivateInputField()`**）；空则 `bl 0x1572A9C`（托管 NRE 抛出）。 |

**三条可用结论**

- SFS 自己就是：**开窗前 0.1 秒延迟再激活输入框**（`GMGBCNEOEIC.PAGIBFDOPID` 是 UniTask 延迟工具，`0x17B5740 → 0x17B564C`；常量 0.1f 在文件偏移 0x8E20E4）。我方照抄这个节奏即可规避“当帧 ActivateInputField 无效”。
- ⚠ **不要调 `wrapper.Open()`**：在安卓上它会**同时**打开 SFS 自带输入菜单（`LINRJGANWEEFRCUR`，会盖住/接管输入）。要弹系统键盘请**直接对 TMP 组件调 `ActivateInputField`**。
- `wrapper.get_Text/set_Text` 是**最省事的读写入口**（一次调用处理 mobile/TMP 分支）；但注意它的 `set_Text` 走 `TMP.set_text`，**会触发 `onValueChanged`**。

### 1.2 `TMP_InputField`（dump.cs:473578，TypeDefIndex 7650）关键偏移

```
0x100 m_SoftKeyboard            TouchScreenKeyboard   ← 非空即“键盘已开”
0x108 m_RectTransform
0x110 m_TextViewport
0x128 m_TextComponent           TMP_Text              ← 必须非空（ActivateInputField 第一判）
0x138 m_Placeholder             Graphic               ← 占位文本（TMP_Text，基类方法可调）
0x170 m_ContentType             enum(Standard0…Alphanumeric4 Name5 Email6 Password7 Pin8 Custom9)
0x174 m_InputType               enum(Standard0 AutoCorrect1 Password2)  ← 密码遮蔽的唯一开关
0x178 m_AsteriskChar            char（默认 '•' 0x2022）
0x17C m_KeyboardType            TouchScreenKeyboardType
0x180 m_LineType                enum(SingleLine0 MultiLineSubmit1 MultiLineNewline2)
0x184 m_HideMobileInput         bool
0x185 m_HideSoftKeyboard        bool
0x188 m_CharacterValidation     enum
0x19C m_CharacterLimit          int                   ← Open 时直接作为 characterLimit 传给 OS
0x1A0 m_OnEndEdit               SubmitEvent(UnityEvent<string>)
0x1A8 m_OnSubmit                SubmitEvent
0x1D0 m_OnValueChanged          OnChangeEvent(UnityEvent<string>)
0x1E0 m_OnValidateInput         OnValidateInput 委托
0x210 m_Text                    string                ← 真值（未遮蔽）
0x220 m_ReadOnly                bool
0x260 m_AllowInput              bool
0x261 m_ShouldActivateNextUpdate bool                 ← ActivateInputField 唯一的动作
0x2D8 isAlert                   bool
```

方法 RVA（节选，全部与 script.json 对得上）：

| 方法 | RVA |
|---|---|
| `get_text` | 0x2F2B5D4（`ldr x0,[x0,#0x210]; ret`） |
| `set_text` | 0x2F2B5DC（→ `SetText(v,true)`） |
| `SetText`(私有) | 0x2F2B5E4 |
| `SetTextWithoutNotify` | 0x2F2B718 |
| `set_contentType` / `get_contentType` | 0x2F2C99C / 0x2F2C994 |
| `EnforceContentType`(私有) | 0x2F2CA10 |
| `set_asteriskChar` / `get_asteriskChar` | 0x2F2CFE8 / 0x2F2CFE0 |
| `set_characterLimit` | 0x2F2C580 |
| `set_inputType` / `set_lineType` / `set_keyboardType` | 0x2F2CCC4 / 0x2F2CB20 / 0x2F2CD70 |
| `set_placeholder` / `set_textComponent` | 0x2F2BEC4 / 0x2F2BE48 |
| `set_readOnly` | 0x2F2CF2C |
| `ActivateInputField` | **0x2F36F30** |
| `ActivateInputFieldInternal`(私有) | **0x2F2FF6C** |
| `DeactivateInputField`(私有) | **0x2F2E698**（内部调 `TouchScreenKeyboard.set_active(false)`，0x2F2E7A8） |
| `LateUpdate` | **0x2F2F710**（消费 0x261） |
| `OnDeselect` / `OnSelect` | 0x2F370A4 / 0x2F37044 |
| `SendOnValueChanged` / `SendOnEndEdit` / `SendOnSubmit` | 0x2F2BBC4 / 0x2F35510 / 0x2F336F8 |
| `TouchScreenKeyboardShouldBeUsed`(私有) | 0x2F2F2F4 |
| `UpdateLabel`(私有) | 0x2F2B7A0（遮蔽代码在 0x2F2B954） |
| `TMP_Text.set_text` / `get_text`（**基类**，给 placeholder/label 用） | 0x2F376B4 / 0x2F375D0 |

（`Selectable` 继承自 UIBehaviour：`m_Interactable` 在 **0x0xD8**，dump.cs:502804。）

### 1.3 密码：字符怎么被藏起来（问题 1 的核心）

**(a) 设 `contentType` 时的自动推导 —— `EnforceContentType` 0x2F2CA10（逐条反汇编）**

```
w8 = [this+0x170]            ; m_ContentType
if (w8 == 7 /*Password*/):
    w9 = 0   ; m_KeyboardType  = Default
    w10 = 0  ; m_CharacterValidation = None
    w8  = 2  ; m_InputType     = Password   ← 关键
    [this+0x180] = 0        ; m_LineType = SingleLine
（Pin=8 分支：lineType=0、inputType=1、kbType=4；其余分支不涉及密码）
[this+0x174] = w8 ; [this+0x17C] = w9 ; [this+0x188] = w10
tail → SetTextComponentWrapMode
```
另外 `set_contentType` 自身只在值变化时写 `0x170`（`SetPropertyUtility.SetStruct<Int32Enum>`），所以**首次设 7 一定会生效**；重复设不会重复走 `EnforceContentType`。

**(b) 显示遮蔽 —— `UpdateLabel` 0x2F2B954**

```
0x2F2B954  ldr  w8, [x19, #0x174]     ; m_InputType
0x2F2B95C  cmp  w8, #2                ; == Password ?
0x2F2B960  b.ne #0x2F2B980            ; 不是 → 原样显示
0x2F2B968  ldr  w2, [x21, #0x10]      ; 长度 = 文本长度
0x2F2B96C  ldrh w1, [x19, #0x178]     ; m_AsteriskChar
0x2F2B978  bl   System.String$$CreateString   ; 生成 len 个 '•'
```
（全 `.text`+`il2cpp` 段字节扫描：`ldrh …, #0x178` 全局**只有 2 处** —— `get_asteriskChar` 与 `UpdateLabel`。这就是“密码框为何显示圆点”的硬证据。）
`m_Text(0x210)` **保持明文**，只是渲染时替换 → 我方读取密码**不受影响**。

**(c) 系统键盘 secure —— `ActivateInputFieldInternal` 0x2F30244 起**

```
w8 = [this+0x174] (m_InputType) ; w9 = [this+0x180] (m_LineType)-1 ; w7 = [this+0x19C] (characterLimit)
if (m_InputType == Password):  w2 = 0(autofocus) ; w3 = multiline ; w4 = 1(secure!) ; w5 = isAlert
else:                          w2 = (m_InputType==AutoCorrect) ; w4 = 0
bl TouchScreenKeyboard$$Open(text=m_Text, kb=0x17C, autofocus=w2, multiline=w3, secure=w4, alert=w5, placeholder=x6, characterLimit=w7)
→ str x0,[this+0x100]  (m_SoftKeyboard)
```

**结论**：密码框只需 `set_contentType(7)`；系统键盘会自动变 secure；`onEndEdit` 提交后读 `[TMP+0x210]` 拿到明文。若要换遮蔽字符：`set_asteriskChar('*')`（0x2F2CFE8）。

### 1.4 SFS 自带的两个“输入框宿主”（后面 §4 要用）

| 类 | dump | 字段 | 说明 |
|---|---|---|---|
| `LINRJGANWEEFRCUR : NQEUSBNLKGCFHQVR`（SFS.Input） | 36590 | `holder 0x28 / Text textField 0x30 / StringBuilder text 0x48 / Action<string> onInputFinished 0x50 / TouchScreenKeyboardType keyboardType 0x58 / TouchScreenKeyboard 0x60` | **安卓专属输入菜单**：自己开 `TouchScreenKeyboard` 并把内容镜像到一个 uGUI `Text`。`Open/OnOpen/ProcessInput/OnClose` 见 0x1758F50 / 0x17597E4 / 0x1759ACC / 0x1759724。 |
| `XPYDSZFDOLBQODKZ`（SFS.UI） | 28472 | `titleText 0x20 / titleTextHolder 0x28 / **textBox 0x30 (TUXXAURTZSIFPXTZ)`** | **一行“标题 + TMP 输入框”**，是多行输入对话框的行预制体。 |
| `VCJMEXUSBHCVBJHA : NQEUSBNLKGCFHQVR`（SFS.UI） | 11116 | `elementPrefab 0x28 / cancelButtonText 0x30 / confirmButtonText 0x38 / holder 0x40 / **elementsParent 0x48** / Action<string[]> 0x50 / GIPJGGBNOKP closeMode 0x58,0x5C / **元素数组 XPYDSZFDOLBQODKZ[] 0x60**` | **游戏自带“多行文本输入”对话框**：`Open(title, message, Action<string[]>, closeMode, elements[])`（0x186786C，5 参）/ 重载（0x1872DAC）；静态工厂 `Element(name, placeholder)`（**0x1867758**，内部 `Instantiate(instance.elementPrefab)` → `titleText.set_Text(name)` → `textBox.set_Text(placeholder)`）。 |
| `DEOTNULUBFZWSSUA`（SFS.UI，UI 注册表） | 47736 | `static read 0x0 / settings 0x8 / **textInput 0x10** / options 0x18 / load 0x20 / loading 0x28`（实例镜像 `_read 0x20 … _textInput 0x30`）；`Awake`(0x17EF4F8) 逐个写静态槽 | **一行代码拿到活的多行输入框对话框**：读 `[DEOTNULUBFZWSSUA.static_fields + 0x10]` 即 `VCJMEXUSBHCVBJHA` 实例。 |
| `RWLYPHTVNCFXMWYC`（SFS.World） | 41114 | `longitudeInput 0xB8 / heightInput 0xC8`（TUXX），`static main 0x0` | level2（进游戏后）的传送/作弊菜单，两个活输入框。 |

---

## 2. 方案 A（主推）：克隆 `worldNameField` → 挂进原生窗 → 读值

### 2.0 为什么克隆而不是新建

新建 `GameObject + AddComponent(TMP_InputField)` 需要自己接 `m_TextComponent / m_TextViewport / m_Placeholder / m_CaretColor / font`（`UpdateLabel` 一旦遇到 `m_TextComponent==null` 就 `bl 0x1572A9C` 抛托管 NRE，直接穿透 native 帧崩进程）。克隆 `worldNameField` 一次性继承：背景图、字体、viewport、caret、placeholder、`RectMask2D`、`LayoutElement`、`Selectable` 全套过渡色。这也正是卖家 `MP-NICK-ROW borrowedForm=1` 的做法（agent_E §1.6）。

### 2.1 逐步 native 调用序列（与 `v41.c` 现有管线对齐）

约定：`g_gm(klass,"名",参数个数)` = `il2cpp_class_get_method_from_name`；`g_ri(m, obj, params, &exc)` = `il2cpp_runtime_invoke`；`g_sNew("…")` = 托管 string；`g_unbox(r)` 取值类型返回值；`char_name/set_name` 同 v41。

**步骤 0：解析（只做一次，放在 resolve 段）**

```c
void* inCls   = img_class("Assembly-CSharp","", "TUXXAURTZSIFPXTZ");     /* 0x28=TMP, 0x20=mobile */
void* tmpCls  = img_class("Unity.TextMeshPro","TMPro", "TMP_InputField");/* 或按 dump 类名直接找 */
void* tmpTxt  = img_class("Unity.TextMeshPro","TMPro", "TMP_Text");       /* 基类，用于 placeholder/label */
g_mTmpGetText   = g_gm(tmpCls,"get_text",0);            /* 0x2F2B5D4 等价 */
g_mTmpSetText   = g_gm(tmpCls,"set_text",1);            /* 0x2F2B5DC */
g_mTmpSetTextNN = g_gm(tmpCls,"SetTextWithoutNotify",1);/* 0x2F2B718：不触发 onValueChanged */
g_mTmpActivate  = g_gm(tmpCls,"ActivateInputField",0);  /* 0x2F36F30 */
g_mTmpDeactiv   = g_gm(tmpCls,"DeactivateInputField",0);/* 0x2F2E698【私有，见 §2.3 注意】 */
g_mTmpSetCType  = g_gm(tmpCls,"set_contentType",1);     /* 0x2F2C99C */
g_mTmpSetAster  = g_gm(tmpCls,"set_asteriskChar",1);
g_mTmpSetLimit  = g_gm(tmpCls,"set_characterLimit",1);
g_mTmpSetRead   = g_gm(tmpCls,"set_readOnly",1);
/* 布局（RectTransform）与 v41 现有 g_mSetSizeDelta/g_mGetSizeDelta 同源，另需： */
g_mSetAnchorMin = g_gm(rectCls,"set_anchorMin",1);
g_mSetAnchorMax = g_gm(rectCls,"set_anchorMax",1);
g_mSetPivot     = g_gm(rectCls,"set_pivot",1);
g_mSetAnchPos   = g_gm(rectCls,"set_anchoredPosition",1);
g_mSetLocalScale= g_gm(trCls,"set_localScale",1);
g_mSetActive    = g_gm(goCls,"SetActive",1);
g_mGetActiveSelf= g_gm(goCls,"get_activeSelf",0);
/* UnityEvent：照 v32 清 clickEvent 的写法，new 一个新的空事件塞回字段 */
g_mUECtor       = g_gm(ueCls,".ctor",0);   /* UnityEngine.Events.UnityEvent */
```

**步骤 1：取源实例（按优先级）**

```
1) LKSRSZGMFKUEATEP 实例（dump.cs:5926）→ 读 +0x48 = worldNameField(TUXX)
   拿 LKSRSZGMFKUEATEP 的方式与 v41 取主菜单实例相同（FindObjectsOfType(类, 0) + activeInHierarchy 校验）。
   ⚠ worldNameField 是 inspector 序列化引用：即使「Create World」窗当前未打开（GO inactive），
      引用本身仍然有效，且 Instantiate 对 inactive 物体**同样可用**（克隆体的 activeSelf 会跟随源 = false → 必须 SetActive(true)）。
2) 否则：FindObjectsOfType(TMP_InputField 类型, includeInactive=1) → 任一实例（见 §4.2）
3) 否则：LKSRSZGMFKUEATEP 实例(+0x48) 为空 → 走 §4.3 的 textInput 对话框
```

**步骤 2：克隆并挂到原生窗的某一行**

```c
/* 源：wrapper 组件（或它的 gameObject，两者等价） */
void* srcWrap = *(void**)((char*)worldsMenu + 0x48);
void* srcGo   = g_ri(g_mGetGO, srcWrap, NULL, &exc);     /* Component.get_gameObject */

/* 父亲：我方原生窗某一行（R-A 克隆整窗里的行容器 or 自建 RectTransform 容器） */
void* parTr   = rowTransform;                            /* ★ 见 §2.2 的“别把输入框塞进 LayoutGroup” */

void* ip[3]; int32_t wps = 0;                            /* 0 = 保持 local，UI 必须用 false */
ip[0]=srcGo; ip[1]=parTr; ip[2]=&wps;
void* cloneGo = g_ri(g_mInst3 /*Object.Instantiate(Object,Transform,bool)*/, NULL, ip, &exc);
```

**步骤 3：激活 + 取组件**

```c
int32_t one = 1; void* a[1]; a[0]=&one;
g_ri(g_mSetActive, cloneGo, a, &exc);                    /* 源若 inactive，克隆体也是 inactive */

void* t[1]; t[0]=tmpType;                                /* tmpType = TMP_InputField 的 Il2CppType* */
void* tmp = g_ri(g_mGetComponent, cloneGo, t, &exc);      /* ★ 拿 TMP_InputField */
void* wrapClone = g_ri(g_mGetComponent, cloneGo, (void*[]){inType}, &exc); /* 可选：拿 wrapper */
```

**步骤 4：清空克隆体带过来的监听（**必做**）**

克隆的 `TMP_InputField` 里 `m_OnValueChanged(0x1D0)` / `m_OnEndEdit(0x1A0)` / `m_OnSubmit(0x1A8)` 仍然指向**原窗**（`LKSRSZGMFKUEATEP.CreateWorld` 一类），用户一回车就会去建世界/改存档名。做法（与 v32 清 `g_offClick` 完全一致）：

```c
void* ueCls = img_class("UnityEngine.CoreModule","UnityEngine.Events","UnityEvent");
for (int off = 0x1A0; off <= 0x1D0; off += 8) {     /* 0x1A0,0x1A8,0x1B0,0x1B8,0x1C0,0x1C8,0x1D0 */
    void* ne = g_mNewObj(ueCls);                      /* il2cpp_object_new */
    if (!ne) continue;
    g_ri(g_mUECtor, ne, NULL, &exc);                  /* 空 UnityEvent */
    *(void**)((char*)tmp + off) = ne;                 /* 直接覆盖字段；旧事件对象自然被 GC */
}
```
> 也可以对旧事件调 `RemoveAllListeners()`（`UnityEvent` 上 0 参）——两种都行；**换新对象**更省心（旧对象可能被别处 gc handle 引用）。
> ⚠ `m_OnValidateInput(0x1E0)` 若被原窗设过，同样建议置 0（否则可能按原窗规则过滤字符，比如只允许字母数字）。

**步骤 5：让行看起来对（label / placeholder / 尺寸）**

```c
/* placeholder（TMP 自带，非空时才显示） */
void* ph = *(void**) ((char*)tmp + 0x138);
if (ph) { void* p[1]; p[0]=g_sNew("服务器地址"); g_ri(tmpTxtSetText /*TMP_Text.set_text*/, ph, p, &exc); }
/* placeholder 的 GameObject 也可能需要 SetActive(true)：它的 active 状态是独立的 */

/* 尺寸：稳妥的做法 = 水平拉伸 + 固定高 */
void* ctr = g_ri(g_mGoTransform, cloneGo, NULL, &exc);
float v2[2];
v2[0]=0.f; v2[1]=1.f; { void*p[1];p[0]=v2; g_ri(g_mSetAnchorMin, ctr, p, &exc); }   /* anchorMin=(0,1) */
v2[0]=1.f; v2[1]=1.f; { void*p[1];p[0]=v2; g_ri(g_mSetAnchorMax, ctr, p, &exc); }   /* anchorMax=(1,1) */
v2[0]=0.5f;v2[1]=1.f; { void*p[1];p[0]=v2; g_ri(g_mSetPivot,    ctr, p, &exc); }
v2[0]=-24.f; v2[1]=64.f; { void*p[1];p[0]=v2; g_ri(g_mSetSizeDelta, ctr, p, &exc); }/* 左右各留 12pt，高 64 */
float ap[2]; ap[0]=0.f; ap[1]=-(60.f + rowIdx*72.f); { void*p[1];p[0]=ap; g_ri(g_mSetAnchPos, ctr, p, &exc); }
float s3[3]={1.f,1.f,1.f}; { void*p[1];p[0]=s3; g_ri(g_mSetLocalScale, ctr, p, &exc); }
if (g_mForceRebuild) { void*p[1]; p[0]=parTr; g_ri(g_mForceRebuild, NULL, p, &exc); }
```
（v35/v36 里那套“读源 `sizeDelta` → 写克隆 → 按 `rect.width` 比例修正”的做法**不适合输入框**：源在别的父节点、别的锚点下；上面这套「锚点拉伸」才是最稳的，因为它不依赖任何父节点布局。）

**步骤 6：按行设语义（3 行输入框）**

```c
/* 第 0 行 Server：普通文本，限长 */
int32_t limit = 64; { void*p[1];p[0]=&limit; g_ri(g_mTmpSetLimit, tmp, p, &exc); }

/* 第 1 行 Username：同上 */

/* 第 2 行 Password：★ 只这一句就够 */
int32_t ct = 7;                     /* TMP_InputField.ContentType.Password */
{ void*p[1]; p[0]=&ct; g_ri(g_mTmpSetCType, tmp, p, &exc); }   /* EnforceContentType 自动：inputType=2、kb=Default、单行 */
/* 可选：换遮蔽字符 */
uint16_t ast = '*'; { void*p[1]; p[0]=&ast; g_ri(g_mTmpSetAster, tmp, p, &exc); }
/* 可选：彻底禁读改（两段式登录反而更安全） */
/* int32_t ro=1; {void*p[1];p[0]=&ro; g_ri(g_mTmpSetRead, tmp, p, &exc);} */
```

**步骤 7：读值（每帧或 onEndEdit）**

```c
static int str_to_ascii(void* s, char* out, int cap){          /* 复用 v41 u16_to_ascii + g_schars */
    out[0]=0; if(!s) return 0;
    int len = *(int*)((char*)s + 0x10);                        /* String.m_stringLength */
    uint16_t* ch = g_schars(s);
    if(!ch) return 0;
    int k=0; for(int j=0;j<len && k<cap-1;j++){ unsigned c=ch[j]; out[k++]=(c<128)?(char)c:'?'; }
    out[k]=0; return k;
}
/* 法 1（最省）：直读 m_Text —— 密码拿到的是明文 */
void* srv = *(void**)((char*)tmpServer + 0x210);
str_to_ascii(srv, g_server, sizeof(g_server));
/* 法 2：走 wrapper.get_Text(0x18D94AC)（在安卓上等价，且更“官方”） */
```
> **不要**用 `get_text` 的 invoker 每帧轮询（会分配托管字符串 + 走一次 invoke）；直读 `0x210` 零分配。`m_Text` 在编辑过程中就是实时的（TMP 每次按键写 `m_Text`），不需要等 `onEndEdit`。

**`m_Text` 的写入者（字节级扫描 `str xN,[xM,#0x210]` 的全部命中）**：`TMP_InputField.SetText`（0x2F2B684）、`Backspace`（2 处）、`Delete`（4 处）、`DeleteKey`（2 处）——即“程序化写值”和“按键/软键盘编辑”两条路径都实时落 `0x210`，所以**每帧直读 `0x210` 就能拿到正在输入的内容**（含密码明文）。

**步骤 8：关闭时收尾**

```c
void* z[1]; g_ri(g_mTmpDeactiv, tmp, NULL, &exc);   /* DeactivateInputField → TouchScreenKeyboard.set_active(false) */
/* 再销毁克隆体：Object.Destroy(cloneGo) 或直接 SetActive(false) 留着复用 */
```

### 2.2 挂载点的三个坑（实测过的布局机制）

1. **别把输入框直接塞进 `VerticalLayoutGroup`/`HorizontalLayoutGroup` 并指望 `sizeDelta`**：只要该组件的 `childControlWidth/Height = true`，每次 `Rebuild` 都会用 `LayoutElement.preferredWidth` 覆盖 `sizeDelta`（v41 里 `g_mForceRebuild` + `g_mSetSizeDelta` 的组合就是这个现象）。三种做法：
   - ✅ 挂到一个**普通的空 RectTransform 行容器**（无 LayoutGroup），输入框用步骤 5 的锚点拉伸；
   - ✅ 或保留 LayoutGroup 但给克隆体**保留/补上 `LayoutElement`**（克隆自带 `m_LayoutGroup 0x158` 只为标记，真正生效的是同 GO 上的 `LayoutElement` 组件）并设 `preferredHeight=64 / flexibleWidth=1`；
   - ✅ 或在 LayoutGroup 上关掉 `childControlWidth`（一次写字段即可）。
2. **`CanvasGroup` 的 `interactable/blocksRaycasts` 与我们无关也能打字**：软键盘走 `TouchScreenKeyboard`，TMP 直接读 `keyboard.text`，**不需要射线命中**（`ActivateInputFieldInternal` 只调 `EventSystem.SetSelectedGameObject`）。所以即使面板是全透明/不挡射线，程序化激活也能输入——这是本方案比“浮动 Java 层”省事的关键。
3. **层级顺序**：输入框必须位于最上层（`SetAsLastSibling` 或 `SetSiblingIndex`），否则键盘弹出时 SFS 自己 UI 会盖住它；`RectMask2D`（`m_TextComponentRectMask 0x118`）保留即可，不要删。

### 2.3 方法可见性 / 名字解析注意

- `DeactivateInputField`、`EnforceContentType`、`ActivateInputFieldInternal`、`TouchScreenKeyboardShouldBeUsed`、`UpdateLabel` 在 dump 里是 **private**。`il2cpp_class_get_method_from_name` **能拿到 private 方法**（它遍历 class->methods，不过滤可见性），但要用**真实名字**：dump 里是 `DeactivateInputField` / `ActivateInputFieldInternal`，RVA 已核对（0x2F2E698 / 0x2F2FF6C）。这符合 `native_button_clone_study.md §3` 的 `method_checked()` 规矩：**名字 + 参数个数 + `methodPointer - base == RVA` 三重校验**，RVA 对不上就降级（不要走激活路线，改 §4.3）。
- `ActivateInputField` 是 public（0x2F36F30），`set_contentType/set_text/get_text/SetTextWithoutNotify/set_characterLimit/set_asteriskChar/set_readOnly` 都是 public，优先用它们。
- **类名字典**：TMP 在 `Unity.TextMeshPro.dll` / namespace `TMPro`；`TMP_Text` 与 `TMP_InputField` 都可 `img_class` 查到（`.22` 的 DummyDll 里两文件都在）。placeholder 的实际运行时类型是 `TextMeshProUGUI`，但**用基类 `TMP_Text` 的方法去调它的对象是合法的**（IL2CPP 不做强制类型校验，只要 vtable 里有该槽）。

---

## 3. 软键盘：安卓上到底要做什么（问题 3）

### 3.1 链路（全部指令级核对）

```
TMP_InputField.ActivateInputField()         0x2F36F30
  ├ if (m_TextComponent(0x128) == null)            → return    (0x2F36F78 op_Equality)
  ├ if (m_TextComponent.font([0x128+0xF8]) == null) → return    (0x2F36FB4)
  ├ if (!IsActive() || !IsInteractable())          → return    (虚表槽 [vt+0x1C8] / [vt+0x2B8]；m_Interactable 在 Selectable+0xD8)
  ├ if (m_AllowInput(0x260) && m_SoftKeyboard(0x100)!=null && !TouchScreenKeyboard.active(0x2FC5384))
  │      TouchScreenKeyboard.set_active(true)                    (仅“救活已存在的键盘”)
  └ m_ShouldActivateNextUpdate(0x261) = true          ← ★ 唯一的动作
        ↓ 下一帧
TMP_InputField.LateUpdate()                 0x2F2F710
  └ if (m_ShouldActivateNextUpdate) { m_ShouldActivateNextUpdate=0; ActivateInputFieldInternal(); }
        ↓
ActivateInputFieldInternal()                0x2F2FF6C
  ├ EventSystem.get_current() (0x3270C24)；为 null → return
  ├ EventSystem.SetSelectedGameObject(gameObject)          (0x326D2F0)
  ├ TouchScreenKeyboardShouldBeUsed() (0x2F2F2F4)
  │     ├ platform==WebGL/Android → 走 TouchScreenKeyboard.isSupported(0x2FC4D64)
  │     └ 其它平台 → !TouchScreenKeyboard.isInPlaceEditingAllowed(0x2FC4E68)
  ├ TouchScreenKeyboard.set_hideInput(get_shouldHideMobileInput)   (0x2FC5320)
  └ TouchScreenKeyboard.Open(m_Text, m_KeyboardType, autofocus, multiline, secure, alert, placeholder, m_CharacterLimit)
        0x2FC4EFC @0x2F302BC  →  str x0,[this+0x100] (m_SoftKeyboard)
```

**所以“Android 上要不要额外事件”的答案：不用额外事件，但必须满足以下 6 条，且必须跨帧。**

| # | 条件 | 说明 / 检测方法 |
|---|---|---|
| 1 | 在**主线程**调用（Update hook 里） | v41 已经是 |
| 2 | GO `activeInHierarchy == true` | 克隆自 inactive 源时必须 `SetActive(true)`；父链也要 active |
| 3 | 组件 `enabled == true`（Behaviour） | 克隆体一般继承 true；`SetEnabled` 兜底 |
| 4 | `Selectable.m_Interactable == 1`（Selectable+0xD8） | 若原窗把它置灰（灰按钮那种），克隆体也是 0 → 直接写 `[tmp+0xD8]=1` |
| 5 | `m_TextComponent != null && font != null` | 克隆体天然满足（这正是要克隆的原因） |
| 6 | `EventSystem.current != null` | SFS 主菜单常驻，无风险；`FindObjectsOfType(EventSystem)` 判空兜底 |

**必须跨帧**：`ActivateInputField()` 当帧不弹键盘（只置 `0x261`，要等 `LateUpdate`）。SFS 官方做法是**延迟 0.1 秒**（`GMGBCNEOEIC.PAGIBFDOPID(0.1f, action)`，见 §1.1）；我方在 `Update` hook 里则**只要“下一帧”**即可：

```c
/* 面板构建那帧：只记录待激活对象 */
g_activate_next_frame = tmpPassword;   /* 或 tmpServer */
/* 之后的 Update 里 */
if (g_activate_next_frame) { void* t = g_activate_next_frame; g_activate_next_frame = NULL;
    g_ri(g_mTmpActivate, t, NULL, &exc); }
```
⚠ 不要在 `Instantiate` 的同一次 invoke 里立刻 `ActivateInputField`：此时 ReactTransform 尚未参与一次布局，且 `LateUpdate` 未必在本帧还会跑 → 会“点了没反应”。

### 3.2 键盘类型 / 提示 / 遮挡

- **键盘类型**：`m_KeyboardType(0x17C)` → `TouchScreenKeyboardType`（0 Default，1 ASCIICapable，2 NumbersAndPunctuation，3 URL，4 NumberPad，5 PhonePad，6 NamePhonePad，7 EmailAddress，8 NintendoNetworkAccount，9 Social，10 Search，11 DecimalPad…）。**服务器地址**建议 `0`（或 3=URL），**用户名** `1`，**密码**由 `contentType=Password` 自动回到 `0` 并把 `secure` 打开。
- **`m_HideMobileInput(0x184)` → `TouchScreenKeyboard.hideInput`**（0x2FC5320）：安卓上置 true 会**隐藏“输入预览条”**（Unity 自己那条 overlay）。SFS 的 TMP 输入框若原样克隆，值随源（assume false）。想让界面更干净可设 `[tmp+0x184]=1`；**但注意**：某些机型/Unity 版本在 `hideInput=1` + 无 in-place editing 时表现为“没有输入反馈”，建议**保持默认**。
- **`m_HideSoftKeyboard(0x185)`**：置 true 会让 `TouchScreenKeyboardShouldBeUsed()` 走“不弹键盘”的分支（TMP 里用于 PC/Quest 的手柄输入）。**千万不要置 1**。
- **`isAlert(0x2D8)`**：TMP 传给 OS 的 `alert` 参数（Android 上无效果，iOS 会把回车变“完成”）。
- **遮挡**：Android 默认 `adjustPan/adjustResize` 由 Unity 处理，键盘弹出时游戏 UI 会被压缩或平移。我方**面板必须自己保证输入框在上半屏**（例如面板 `anchoredPosition.y` 抬高），否则会被键盘挡住——这是纯 UI 层面的坑，与 TMP 无关。
- **关窗必须收键盘**：`DeactivateInputField()`（0x2F2E698）内部 `TouchScreenKeyboard.set_active(false)`（0x2F2E7A8）。若只是 `SetActive(false)` 整个克隆体，TMP 的 `OnDisable`（0x2F2E35C）也会走释放路径；两条都可用，但**推荐显式调 DeactivateInputField**（幂等）。
- **不要调 `wrapper.Open()`**（第 3 章结论已述）：安卓上它会打开 SFS 自带输入菜单 `LINRJGANWEEFRCUR`（`holder 0x28 / Text 0x30 / TouchScreenKeyboard 0x60`），把输入体验接管成“弹窗式输入”，并且**会遮挡我方面板**。

### 3.3 真机验证打点（建议日志）

```
v4x: field=%p active=%d enabled=%d interactable=%d textComp=%p font=%p evSys=%p → activate()
v4x: activate ret exc=%p  allowInput=%d  softKB=%p  kbActive=%d
v4x: after 1 frame: m_Text len=%d (=%s)
```
其中 `softKB=[tmp+0x100]`、`kbActive=TouchScreenKeyboard.get_active(0x2FC5384)`：**只要 `softKB != 0` 就说明系统键盘已被拉起**（这是最硬的判定，比肉眼看屏幕可靠）。

---

## 4. `worldNameField` 不可用时的替代（问题 4）

### 4.1 level0 到底有没有它（判定依据）

- `worldNameField` 属于 `SFS.UI.LKSRSZGMFKUEATEP`（dump.cs:5926，`worldsMenu 0x38 / createWorldButton 0x40 / worldNameField 0x48 / …`）—— “世界列表 / 新建世界”界面，**属于主菜单（level0）**：agent_E §5 的 assets 扫描在 `level0.split0` 里命中 `"Mods Menu" / "World List" / "Create World"`。
- 因此 `LKSRSZGMFKUEATEP` 实例在 level0 **存在**（“World List”是主菜单一级入口），其 `worldNameField` 是 inspector 序列化引用，**即使用户没点“Create World”（子物体 inactive），字段仍指向一个真实的、有 `m_TextComponent/font` 的实例**，可以直接 `Instantiate`。
- ⚠ 唯一真风险：如果 `LKSRSZGMFKUEATEP` 所在父物体在 level0 里是**未被实例化（未 Instantiate 的 prefab）**或整棵树被 `Destroy` 过，`worldNameField` 会是野指针 → **必须先判 `m_CachedPtr != 0`**（`UnityEngine.Object` 的 `m_CachedPtr` 在偏移 0x10；`op_Inequality`/`IsNativeObjectAlive` 亦可），再取 `get_gameObject`。

**判活清单（克隆前必查，10 行内）**
```
w = *(void**)(worldsMenu + 0x48)
if (!w) → 降级
if (*(void**)((char*)w + 0x10) == 0) → 降级            /* m_CachedPtr == 0：托管壳，native 已销毁 */
go = w->get_gameObject(); if (!go) → 降级
if (!go->activeInHierarchy) → 仍然可用（Instantiate 支持 inactive），但克隆完必须 SetActive(true)
tmp = w->GetComponent<TMP_InputField>(); if (!tmp) → 降级
tc  = *(void**)((char*)tmp + 0x128); if (!tc) → 降级   /* 源本身没接线，克隆也没用 */
```

### 4.2 通用兜底：任何 TMP_InputField 实例都能当模板

```c
/* includeInactive = 1（第 2 参传 true）：连 inactive 的都拿出来 */
void* arr = g_ri(g_mFindAll /*Resources.FindObjectsOfTypeAll 或 Object.FindObjectsOfType(TYPE,int)*/,
                 NULL, params(TMP_InputField 的 Il2CppType, &one_true), &exc);
遍历：
  - 跳过 `*(void**)((char*)o + 0x10)==0`（托管壳）
  - 跳过 `[o+0x128]==0`（没 textComponent 的坏实例）
  - 优先挑 `gameObject.activeInHierarchy == true` 的（克隆体直接可用，不用 SetActive）
  - 记录它，作为模板
```
**优先级排序（按“克隆后观感/鲁棒性”）**
1. `LKSRSZGMFKUEATEP.worldNameField(0x48)`（主菜单原生输入框，最正）
2. `XPYDSZFDOLBQODKZ.textBox(0x30)`（多行对话框的行模板，天然带 titleText 0x20 可当 label）
3. level2 才有的：`RWLYPHTVNCFXMWYC.longitudeInput(0xB8) / heightInput(0xC8)`（传送/作弊菜单；`static main 0x0` 可判是否已加载）
4. `FindObjectsOfType(TMP_InputField, 1)` 里任意一个（最坏情况）

> 注意 `FindObjectsOfType` **只能找到“已加载场景里”的对象**；`Resources.FindObjectsOfTypeAll` 会返回 prefab 资源（不可直接 Instantiate 到场景里当模板？——可以，但 prefab 上的 `font` 引用可能未解析，克隆后 `m_TextComponent.font==null` → `ActivateInputField` 直接返回）。所以**只有在 1–3 都失败时**才用它，并且必须校验 `[o+0x128]` 的 `font`。

### 4.3 ★ 更好的替代：直接用游戏自带的「多行文本输入对话框」（推荐做 login 的首选/并行方案）

`SFS.UI.DEOTNULUBFZWSSUA` 是 UI 注册表（`Awake` 0x17EF4F8 逐个把 `_xxx(0x20/0x28/0x30…)` 写进静态槽 `0x0/0x8/0x10…`），其中：

```
static +0x10  =  VCJMEXUSBHCVBJHA textInput    ← 多行输入对话框实例（level0/level2 都在，因为它在 UI 根上）
```

用法（**零克隆、零布局、自动处理软键盘**）：

```c
/* 1) 拿实例：FieldInfo + 静态读（复用 v41 的 g_staticGet 机制） */
void* regCls = img_class("Assembly-CSharp","SFS.UI","DEOTNULUBFZWSSUA");
void* f = il2cpp_class_get_field_from_name(regCls, "textInput");
void* dlg = NULL; il2cpp_field_static_get_value(f, &dlg);      /* == *(void**)(regCls->static_fields+0x10) */

/* 2) 造 3 行元素：静态工厂 Element(name, placeholder)  0x1867758
 *    内部：Instantiate(instance.elementPrefab(0x28)) → titleText.set_Text(name) → textBox.set_Text(placeholder)
 *    （注意：SFS 把 placeholder 当**真实文本**写进去，所以要么传空串，要么自己清）
 *    返回 XPYDSZFDOLBQODKZ*，运行时拿它的 textBox(0x30) → TMP_InputField，才谈得上“密码框” */
void* rows[3]; rows[0]=Element("Server",""); rows[1]=Element("Username",""); rows[2]=Element("Password","");
/* 密码行：立即把 contentType 设成 7（此时它还没显示，但字段已生效） */
void* ptmp = *(void**)((char*)*(void**)((char*)rows[2] + 0x30) + 0x28);   /* textBox(TUXX) → TMProTextBox */
int32_t seven=7; g_ri(g_mTmpSetCType, ptmp, params(&seven), &exc);

/* 3) 组 array：il2cpp_array_new(class_of_XPYDSZFDOLBQODKZ, 3) → 逐个写 [arr+0x20+i*8] */
/* 4) 委托：从 Open 方法的**第 3 个参数类型**取 Action<string[]> 类（v39 同款 g_mgp/g_tgoc） */
void* t3type = il2cpp_method_get_param(mOpen5, 2);
void* delCls = il2cpp_class_from_type(t3type);
void* del    = v39_make_delegate(delCls, (void*)login_cb);      /* stub 只读 x1 = string[] */

/* 5) 打开 */
g_ri(mOpen5, dlg, params(title_str, msg_str, del, &closeModeOne, arr), &exc);
/* closeMode: GIPJGGBNOKP { None=0, Current=1, Stack=2 }（dump.cs:50405）→ 用 1 或 2 */
```

- **回调**：`Action<string[]>` 的实参按 agent_E §3 的 ABI 落在 **x1**（x0 是委托的转发槽 `*(del+0x40)`）。所以 `static void login_cb(void* fwd, void* arr)` **只读 `arr`**，绝不裸解引用 `fwd`；把 `arr` 里的 3 个 string 立刻转成 ASCII 存全局（`[arr+0x20+i*8]` → `[s+0x10]` 长度、`g_schars(s)` 取 UTF-16）。
- **好处**：完全复用游戏原生 UI（含它自己的确认/取消按钮、`OnOpen/OnClose`、软键盘处理），**没有克隆、没有 UnityEvent 残留、没有布局问题**。
- **代价 / ⚠ 风险**：① 必须伪造 `Action<string[]>` 委托（老板 ABI 已实证，见 agent_E §3；v39 已有 `v39_make_delegate`，把委托对象存全局 + `gchandle` 防 GC）；② `Element()` 内部会 `Instantiate` 并 `set_Text(placeholder)`，传空串即可，避免“占位文本被当成真值”；③ 该对话框的 `Close` 模式决定它关掉谁（`none/current/stack`），登录页用 `Current(1)`；④ 它自己会 `Open` 一个屏幕菜单 → 与我方 R-A 克隆窗**同层级竞争**，二者不要同时开。

### 4.4 三方案对比（结论）

| | A：克隆 `worldNameField` 进原生窗 | B：`DEOTNULUBFZWSSUA.textInput` 对话框 | C：`FindObjectsOfType` 任意实例克隆 |
|---|---|---|---|
| 依赖实例 | `LKSRSZGMFKUEATEP.worldNameField`（level0 有） | UI 注册表静态槽（**几乎必然有**） | 任意 TMP_InputField |
| 密码框 | `set_contentType(7)` 一句 | 同左（对元素的 `textBox.TMProTextBox`） | 同左 |
| 布局 | 自己 `SetParent` + 锚点拉伸 | **零**（游戏自己排） | 自己 |
| 软键盘 | `ActivateInputField`（跨帧） | 游戏对话框自己处理 | 同 A |
| 残留监听风险 | 需换空 `UnityEvent` | 无 | 需换空 `UnityEvent` |
| 委托伪造风险 | 无 | 需 `Action<string[]>` | 无 |
| 观感 | 与原生窗一体（推荐） | 游戏原生弹窗（推荐做 login 探索版） | 看运气 |

**建议**：先上 **A**（可验证、可控、和 R-A 克隆窗一套代码），同时把 **B** 作为**快速验证工具**——B 只用 ~40 行就能验证“3 个输入框 + 密码 + 软键盘 + 读值”全链路，不需要先解决克隆窗的布局。

---

## 5. 风险清单与验证清单

| 风险 | 触发 | 规避 |
|---|---|---|
| `onValueChanged/onEndEdit` 残留指向原窗 | 克隆后直接打字 | 步骤 4：换空 `UnityEvent`（`0x1A0/0x1A8/0x1B0/0x1B8/0x1C0/0x1C8/0x1D0`），并把 `m_OnValidateInput(0x1E0)` 置 0 |
| `m_TextComponent == null` → 托管 NRE 穿透 native 栈 | 克隆到“托管壳/坏 prefab” | 克隆前查 `[srcGo→TMP+0x128]!=0`；克隆后再查一次 |
| `ActivateInputField` 无效（“点了没反应”） | 当帧调用 / GO 未激活 / Interactable=0 | 跨帧激活；`SetActive(true)`；写 `[tmp+0xD8]=1` |
| 键盘弹了但输入不进去 | 面板 `CanvasGroup` 挡射线（**不影响打字**）；真正原因是**另一个 InputField 仍持有焦点** | 先对旧输入框 `DeactivateInputField`，再激活新的 |
| 键盘不消失 | 关窗只 `SetActive(false)`（TMP `OnDisable` 0x2F2E35C 一般够，但不保证） | 显式 `DeactivateInputField`（内部 `set_active(false)`） |
| 密码显示成空白（不是圆点） | 克隆源的 `m_AsteriskChar(0x178)==0`（被原窗改过/为 0） | 显式 `set_asteriskChar('•')` 或 `'*'` |
| 克隆体尺寸被 LayoutGroup 覆盖 | 父链有 `Vertical/HorizontalLayoutGroup` 且 `childControlWidth=1` | §2.4 三种做法之一 |
| `set_text` 触发 `onValueChanged`（写占位/回填时） | 写值用 `set_text` | 回填值一律用 `SetTextWithoutNotify`（0x2F2B718）；只有“模拟用户输入”才用 `set_text` |
| 版本漂移 | 换 .23+ RVA 变 | 全部方法按 **名字+参数个数+RVA** 三重校验（`native_button_clone_study.md §3`），RVA 不符 → 降级 §4.3 |
| 线程 | 在非主线程碰 Unity | 只在 `Update` hook 里做（v41 已如此） |

**最小真机验证顺序（每步一条日志，失败即停）**
1. 打印 `worldNameField` / `textInput` 指针 + `m_CachedPtr` → 确认实例；
2. `Instantiate` 后打印 `cloneGo / tmp / [tmp+0x128] / [tmp+0x138]`；
3. 换空事件后打印 `[tmp+0x1D0]`（应等于新对象地址）；
4. 下一帧 `ActivateInputField` → 打印 `[tmp+0x261]`（应=1）→ 再下一帧打印 `[tmp+0x100]`（**非 0 = 键盘已弹**）；
5. 手动在真机敲几个字符 → 打印 `[tmp+0x210]` 的长度与内容（应实时变化）；
6. 密码行：打印 `[tmp+0x174]`（应=2）与 `[tmp+0x178]`（应≠0），肉眼确认显示为圆点；
7. 关窗 → 打印 `TouchScreenKeyboard.get_active`（应=0）。

---

## 6. 附录：RVA 速查（.22 官方 libil2cpp.so）

**SFS 侧**

| RVA | 符号 |
|---|---|
| 0x18D92C0 / 0x18D9360 | `TUXXAURTZSIFPXTZ.set_Holder` / `CMLOJBJFFFE`（懒建两组件） |
| 0x18D94AC / 0x18D9578 | `TUXXAURTZSIFPXTZ.get_Text` / `set_Text` |
| 0x18D9404 / 0x18D9654 | `TUXXAURTZSIFPXTZ.Close` / `Open` |
| 0x18D948C | `TUXXAURTZSIFPXTZ.IFJGLLJNJCA`（Action 体 → `ActivateInputField`） |
| 0x17B5704 / 0x17B564C | `GMGBCNEOEIC.PAGIBFDOPID(float,Action)` / `CLMNIEIOKHC`（延迟工具，0.1s 常量 @0x8E20E4） |
| 0x1758F50 / 0x1759A94 / 0x1758FFC | `LINRJGANWEEFRCUR.Open` / `get_Text` / `set_Text`（SFS 自带输入菜单） |
| 0x1759724 / 0x17597E4 | `LINRJGANWEEFRCUR.OnClose` / `OnOpen` |
| 0x1867758 / 0x186786C / 0x1872DAC | `VCJMEXUSBHCVBJHA.Element(name,placeholder)` / `Open`(5 参) / `Open`(6 参) |
| 0x1872CAC / 0x1872C68 / 0x1872C60 | `VCJMEXUSBHCVBJHA.HMAHBBFHNID`（取 string[]）/ `Confirm` / `CancelTextInputMenu` |
| 0x17EF4F8 | `DEOTNULUBFZWSSUA.Awake`（写静态 `textInput 0x10` 等） |

**TMP 侧**

| RVA | 符号 |
|---|---|
| 0x2F2B5D4 / 0x2F2B5DC / 0x2F2B5E4 / 0x2F2B718 | `get_text` / `set_text` / `SetText` / `SetTextWithoutNotify` |
| 0x2F2B7A0（遮蔽 @0x2F2B954）/ 0x2F2EAC8 | `UpdateLabel` / `SetCaretVisible` |
| 0x2F2C994 / 0x2F2C99C / 0x2F2CA10 | `get_contentType` / `set_contentType` / `EnforceContentType` |
| 0x2F2CD70 / 0x2F2CB20 / 0x2F2C580 / 0x2F2CFE8 | `set_keyboardType` / `set_lineType` / `set_characterLimit` / `set_asteriskChar` |
| 0x2F2BEC4 / 0x2F2BE48 / 0x2F2CF2C | `set_placeholder` / `set_textComponent` / `set_readOnly` |
| **0x2F36F30** / 0x2F2FF6C / 0x2F2F2F4 | `ActivateInputField` / `ActivateInputFieldInternal` / `TouchScreenKeyboardShouldBeUsed` |
| **0x2F2E698** / 0x2F2F710 / 0x2F2E35C | `DeactivateInputField` / `LateUpdate` / `OnDisable` |
| 0x2F2BBC4 / 0x2F35510 / 0x2F336F8 | `SendOnValueChanged` / `SendOnEndEdit` / `SendOnSubmit` |
| 0x2FC4EFC / 0x2FC5320 / 0x2FC5384 / 0x2FC540C | `TouchScreenKeyboard.Open` / `set_hideInput` / `get_active` / `set_active` |
| 0x326D2F0 / 0x3270C24 | `EventSystem.SetSelectedGameObject` / `get_current` |

**字段偏移速查**

```
TUXXAURTZSIFPXTZ : 0x20 mobileTextBox(LINRJGANWEEFRCUR), 0x28 TMProTextBox(TMP_InputField), 0x30 resolved
LKSRSZGMFKUEATEP : 0x38 worldsMenu, 0x40 createWorldButton, 0x48 worldNameField
XPYDSZFDOLBQODKZ : 0x20 titleText, 0x28 titleTextHolder, 0x30 textBox(TUXX)
VCJMEXUSBHCVBJHA : 0x28 elementPrefab, 0x30 cancelButtonText, 0x38 confirmButtonText, 0x40 holder,
                   0x48 elementsParent, 0x50 Action<string[]>, 0x58/0x5C closeMode, 0x60 XPYDSZFDOLBQODKZ[]
DEOTNULUBFZWSSUA : static 0x0 read, 0x8 settings, 0x10 textInput, 0x18 options, 0x20 load, 0x28 loading
TMP_InputField   : 0x100 m_SoftKeyboard, 0x128 m_TextComponent, 0x138 m_Placeholder, 0x170 m_ContentType,
                   0x174 m_InputType, 0x178 m_AsteriskChar, 0x17C m_KeyboardType, 0x180 m_LineType,
                   0x184 m_HideMobileInput, 0x185 m_HideSoftKeyboard, 0x188 m_CharacterValidation,
                   0x19C m_CharacterLimit, 0x1A0 m_OnEndEdit, 0x1A8 m_OnSubmit, 0x1D0 m_OnValueChanged,
                   0x1E0 m_OnValidateInput, 0x210 m_Text, 0x220 m_ReadOnly, 0x260 m_AllowInput,
                   0x261 m_ShouldActivateNextUpdate, 0x2D8 isAlert
Selectable       : 0xD8 m_Interactable
UnityEngine.Object: 0x10 m_CachedPtr（判活）
```

---

### 附：本报告用到的复现命令

```bash
cd /d/SFS-Pro/work
python dis_name.py 0x18D9654 200      # TUXXAURTZSIFPXTZ.Open
python dis_name.py 0x2F2CA10 40       # EnforceContentType（密码推导）
python dis_name.py 0x2F2B900 90       # UpdateLabel 里的圆点遮蔽
python dis_name.py 0x2F30240 35       # TouchScreenKeyboard.Open（secure 参数）
python dis_name.py 0x2F2F710 40       # LateUpdate 消费 0x261
python dis_name.py 0x17EF4F8 30       # DEOTNULUBFZWSSUA.Awake 写静态槽
```
（`dis_name.py` = capstone + `script.json` 符号表；`.22` 的代码同时位于 `.text` 与 `il2cpp` 两个 section，跨段扫描时**两个都要读**——只读 `.text` 会漏掉全部 TMP 代码。）




