# 卖家 SFS Pro「多人联机」原生页面 —— 结构规格还原

> 目标：把卖家 `libpatch.so` 里**多人在线 UI 的页面结构**还原成可照抄的规格：几页 / 每页几行 / 每行什么控件 / 文案来源 / 分页按钮挂法 / 点击回调 / 输入框借用。
> 材料：`D:/SFS-Pro/build/seller_libpatch.so`（2,060,672 B，PT_LOAD#0 `p_offset==p_vaddr`，故 **文件偏移 == VA**，下文 `0x…` 均为 VA）、`D:/SFS-Pro/build/libpatch_strings.pkl`（16,167 条字符串 + 偏移）、`D:/sfs-tools/apk/dump-official/dump.cs`（.22 官方）、`D:/SFS-Pro/build/agent_E_native_ui.md`。
> 方法：capstone AArch64 全 `.text`（`0xb39a0`–`0x1ef070`）线性反汇编 + ADRP/ADD 字符串交叉引用 + 逐函数人工阅读。**只读分析，未改工程、未连设备。**
> 状态：**已完成**（§3 分页数学、§4 Smart SAS 行、§5 联机页行级清单、§6 输入框借用、§7 照抄规格、§8 风险 + 附录 B 复核记录）。两处静态无法定序的点标在 §8。

---

## 0. 结论速览

1. 卖家的「多人联机」**不是一个窗口，是 5 个可交互页面 + 1 个入口按钮**。它们全部长在**游戏自己的窗口**里（StatsMenu 抽屉 `CLCPKKVBMCAKFMPN` / 主菜单 `level0` 的窗体），没有自建 Canvas。
2. 页面结构**不是硬编码表**，而是运行时算出来的：
   ```
   pages = ceil(rows / pageSize)          // rows = 本轮"漏斗"放行的行数
   pageSize = clamp(cfg, 4, 40)           // cfg 默认 14
   ```
   所以「每页几行」= **14 行（默认）**，可在 `Mods/sfs_settings.json` 的 `"part_menu_rows_per_page"` 改，或直接写 `Mods/pmp_rows.txt`（一个整数），硬钳位 `[4,40]`。
3. **行的顺序不是调用顺序，而是 `priority` 升序**：`DrawText/DrawFullButton` 的第 1 个 `int` 不是行号，是 `layoutPriority`；分页器对整张行表按 priority 排序后**切片**，只把当前页的行放行（其余行在"漏斗"里就被丢掉，根本不实例化）。
4. **翻页按钮**：两个 `DrawFullButton`，`priority = INT_MAX`（上一页）与 `INT_MAX-1`（下一页），`enabled = (page>0)` / `(page<pages-1)`。因为 priority 最大，它们永远排在所有内容行之后 —— 这就是它们"总在底部"的原因。
5. **标题自带页码**：`DrawTitle` 被 hook，当 `pages>=2` 时把标题改写成 `"<原标题>   <page+1> / <pages>"`（**三个空格**）。
6. **点击回调不碰参数**：所有按钮委托都是"伪造的 `System.Action`"，执行体只往全局 `[0x3cec30]` 写 `+1/-1`（翻页）或置标志位；真正的业务在 `Update` tick 里做。这是卖家能跨版本不崩的核心。
7. **输入框是"借"的，不是造的**：克隆游戏现成的 TMP 输入框行（`"InputField (TMP)"`），`borrowedForm=1`，并把克隆体上的 `onSubmit/onEndEdit` 全部清空，防止触发原表单逻辑。

### 0.1 全页速查表（一眼看完"几页几行几控件"）

| 页面 | 入口函数 | 标题 | 行数 | 每行控件 | 分页方式 |
|---|---|---|---|---|---|
| 主菜单入口 | `0xEC33C` | `多人联机` / `Multiplayer` | 1 | 全宽按钮 + `MultiplayerIcon.png` | — |
| 服务器列表 | `0xED4B8` | `多人服务器` / `Multiplayer Server` | **N 张卡**，每卡 6 个控件（名称、状态、进入/连接/删除/编辑 4 按钮）+ 页级 2 按钮 | 卡=克隆世界卡；按钮=克隆 `Resume`/`Row 1`/`Delete World`/`Rename` | 游戏自带 `Scroll Window`（无翻页按钮） |
| 添加/编辑服务器 | `0x154014` | 改写 `World Title` | **3 输入行** + 标题 + 2 按钮 | `Server Name` / `Password` / `Server IP`（TMP 输入） | — |
| 加入服务器 | `0xC0C58` | `多人服务器` | **3 输入行 + 1 昵称行 + 1 状态行** | 名称 / IP / 密码 / 昵称（TMP 输入）、`Status: …` | — |
| 直连 | `0x104AB0` | `直接连接` | **1 输入行 + 1 按钮**（按 P3 复用，待上机核对） | IP 输入、`Connect Server` | — |
| Smart SAS | `0x14C9A4` | `Smart SAS` | **1 占位行 + 3 按钮行** | `Prograde` / `Target` / `Surface`（单选项，`selected` 高亮） | PMP 分页，14 行/页，底栏 `<   Previous` / `Next   >` |

---

## 1. 材料与方法（可复现）

```bash
# 1) 字符串表（已有）
libpatch_strings.pkl  -> [(file_off, "string"), ...]   16167 条
# .rodata 位于 0x15950..0x9b8fc，且在 PT_LOAD#0 内 => VA == file_off
# 2) 全 .text 反汇编 + ADRP/ADD 交叉引用
research/native_ui/xref2.py   # 输出 string VA -> [引用指令地址]
research/native_ui/dump.py    # 带字符串注释的区间反汇编
research/native_ui/fn.py      # 由引用地址回溯函数起始
```

要点：`.text` 从 `0xb39a0` 开始（不是 0），`.rodata` 与代码同段，所以字符串引用全部是 `ADRP + ADD`（不是 literal pool），交叉引用可信度高。

---

## 2. 全局架构：卖家的两套 UI 机制

逆向出来的调用图（箭头 = 实际 `bl`/`blr`）：

```
                    ┌─────────────────────────────────────────────┐
   (A) 零件菜单分页  │ PMP = Part Menu Pager                       │
                    │ hook: Open(0x137ABC) EFLOBBJGMGA(0x137F18)  │
                    │       DrawTitle(0x1380E0) Close(0x138368)   │
                    │       GetPreferredSize(0x138460)            │
                    │ 用途: 把"零件菜单(StatsMenu 抽屉)"的行分页  │
                    │       + 插入 Smart SAS 页                   │
                    └─────────────────────────────────────────────┘
                    ┌─────────────────────────────────────────────┐
   (B) 联机页面组    │ MPUI = 克隆原生行/窗体                       │
                    │ 0xC0C58  home(状态+昵称行+服务器卡)         │
                    │ 0xED4B8  服务器列表(卡片)                   │
                    │ 0x154014 添加服务器表单(名称/IP/密码)       │
                    │ 0x104AB0 直连/进入服务器                    │
                    │ 0x14C9A4 Smart SAS 页                       │
                    └─────────────────────────────────────────────┘
```

两套的**连接点**是 `SmartSAS.Hooks.UseXxx`（见 §4）与服务器卡的点击回调（§5）。

---

## 3. PMP 分页器：页数 / 每页行数 / 行顺序（已完全还原）

### 3.1 每页行数：`get_rows_per_page()` @ `0x136920`

```c
int get_rows_per_page(void)                 // 0x136920
{
    if (g_rows_per_page & 0x80000000)       // [0x1f86d0]，bit31 = "未初始化"
        return g_rows_per_page;

    int v = read_int_file("Mods/pmp_rows.txt");      // 0x13797c(name,-1,NULL)
    if (v < 0) {                                     // 读失败 => 走 settings
        char *js = read_file(".../Mods/sfs_settings.json");   // 0x1ef290 fopen
        char *p  = strstr(js, "\"part_menu_rows_per_page\":");  // 0x16a8d
        v = p ? atoi(p + 0x1a) : 0;                  // ★ +0x1a = 跳过 key+冒号
        if (v <= 0) v = 14;                          // ★ 默认 14 行
    }
    if (v < 4)  v = 4;                               // ★ 下限 4
    if (v > 40) v = 40;                              // ★ 上限 40
    g_rows_per_page = v | 0x80000000;
    log("PMP-ROWS: %d");                             // 0x1f86d0 缓存
    return v;
}
```

| 项 | 值 | 依据 |
|---|---|---|
| 默认每页行数 | **14** | `mov w0,#0xe` @ `0x1369f4` |
| 下限 / 上限 | **4 / 40** | `0x1369f8`–`0x136a14` |
| 覆盖优先级 | `Mods/pmp_rows.txt` > `sfs_settings.json` > 14 | `0x136954` / `0x13696c` |
| 缓存 | `[0x1f86d0]`，bit31 作初始化标记 | `0x13694c` |

> 实现建议：**先按 14 行/页做**，再留一个 `rows_per_page` 变量读同一份 json key（key 名可以不同，但保持 4..40 钳位）。

### 3.2 行表 + 每页切片：`set_page_slice()` @ `0x17E0BC`

函数签名（由调用现场 `0x1370F4` 与内部用法反推）：

```c
void set_page_slice(Slot *s, int rowCount, int pageSize);   // 0x17E0BC
```

```c
void set_page_slice(Slot *s, int rows, int pageSize)
{
    int n = (rows < 0x100) ? rows : 0x100;      // ★ 最多 256 行
    memset(&s->keep[0], 0, 0x400);              // [s+0x958..0xD57]，256×int 位图

    int idx[256];
    for (int i = 0; i < n; i++) idx[i] = i;

    if (n > 1) {
        /* ★ 按 priority 升序排序（插入排序，键 = s->prio[i]，即 [s+0x15c+i*8]） */
        for (int i = 1; i < n; i++) {
            int v = idx[i], pv = (v < rows) ? s->prio[v] : 0;
            int j = i;
            while (j > 0) {
                int u = idx[j-1];
                int pu = (u < rows) ? s->prio[u] : 0;
                if (pv < pu) { idx[j] = u; j--; } else break;
            }
            idx[j] = v;
        }
    }
    int start = s->page * pageSize;             // [s+0xD58] 当前页(0-based)
    int end   = start + pageSize;
    if (end > n) end = n;
    for (int k = start; k < end; k++)
        if (idx[k] <= 0xFF) s->keep[idx[k]] = 1;  // ★ 只有本页的行置 1
}
```

关键点：

| 项 | 值 | 依据 |
|---|---|---|
| 行位图 | `Slot+0x958`，`int[256]`（`0x400` 字节） | `0x17e168 memset`、`0x17e3d0 str w10,[x11,#0x958]` |
| 行优先级 | `Slot+0x15c + idx*8`（与 `+0x158` 的 idx 成对） | `0x17e304 ldr w13,[x12,#0x15c]` |
| 最大行数 | 256 | `cmp w1,#0x100; csel` @ `0x17e0e4` |
| 切片公式 | `[page*pageSize, min(page*pageSize+pageSize, rows))` | `0x17e178`–`0x17e190` |

### 3.3 "漏斗"：`EFLOBBJGMGA` hook @ `0x137F18` —— 决定这一行画不画

`EFLOBBJGMGA` 是 StatsMenu 内部**每一行都必经**的加行函数（3 参）。卖家 hook 它作为漏斗：

```c
void hook_funnel(Menu *this, int priority, void *label, int32_t kind /*w3*/)   // 0x137F18
{
    Slot *s = slot_for(this);                     // 8 槽表 [0x3c70f20]，步长 0xDA0
    if (g_skip /*[0x19caba0] bit0*/) goto forward; // 静默放行（测量轮）

    int idx = s->rows_seen;                        // [s+0xC]  ★ 本轮到第几行
    if (s->n_elems <= 0xFF) {                      // [s+0x154]
        s->elem_idx [n] = idx;                    // [s+0x158 + n*8]
        s->elem_prio[n] = priority;                // [s+0x15c + n*8]
        s->n_elems++;
    }
    s->rows_seen = idx + 1;

    if (s->slicing /*[s+0xD60]*/) {
        if (idx > 0xFF || s->keep[idx] == 0) {      // 不在本页
            if (slice_verbose()) log("  PMP-DROP: idx=%d prio=%d", idx, priority);
            return;                                // ★ 不转原函数 => 该行根本不生成
        }
        if (slice_verbose()) log("  PMP-ROW: idx=%d prio=%d", idx, priority);
    }
forward:
    return g_funnel_tramp(this, priority, label, kind);   // [0x3cec50]
}
```

> **这是整份规格里最重要的一条**：分页不是"隐藏行"，而是**在加行时就把非本页的行丢掉**。所以内存里永远只有本页的行，`PMP-COUNT: layout_elements=N menu_elements=M` 里 `N ≈ pageSize(+2 个翻页按钮)`。

### 3.4 每页渲染驱动：`Open` hook @ `0x137ABC`

```c
void *hook_open(Menu *this, Func f, Action<Menu> build, bool push, Action a1, Action a2)  // 0x137ABC
{
    Slot *s = slot_for_or_register(this);          // 上限 8 槽，满则 "slots full?"
    s->open_count++;                               // [s+8]

    int built = (this->elements /*0xA0*/ != NULL) ? this->elements->_size : -1;  // 0x137BAC
    int rows  = s->rows_seen;                      // [s+0xC]

    if (verbose) log("PMP-INNER#%d: page=%d/%d kept_rows=%d built_elements=%d title=\"%s\"",
                     s->open_count, s->page+1, s->pages, rows, built, s->title);

    int pageSize = get_rows_per_page();             // 0x136920
    int pages    = (rows >= 1) ? (rows + pageSize - 1) / pageSize : 1;   // ★ 页数
    int req      = read_int_file("pmp_page.txt", -1, &delta);            // 0x13797C

    log("PMP-PASS#%d: funnel_rows=%d built_elements=%d title=\"%s\" pageSize=%d pages=%d "
        "pagingReq=%s buttons=%s (this pass is unfiltered = measuring) tracker=%p",
        s->open_count, rows, built, s->title, pageSize, pages,
        req_ok ? "on" : "off",  buttons ? "yes" : "no", g_tracker);

    s->pages = pages;
    if (pages < 2 || need_skip) { s->built=0; s->panel=NULL; return NULL; }   // 单页不分页

    if (s->on_build == NULL || g_open_tramp == NULL) { ... return NULL; }

    /* ---- 选页 ---- */
    int page;
    if (s->paged /*[s+0xB4]*/) {
        if (!req_ok)            page = 0;
        else if (strcmp(s->title, ...) != 0) page = 0;
        else                    page = s->page;
    } else                      page = req_ok ? ... : 0;

    if (delta != 0) page += delta;             // pmp_page.txt 里的增量
    else            page -= 1;                 // ★ 见 §3.6 的坑
    if (page < 0) page = 0;
    if (page >= pages) page = pages - 1;

    s->page  = page;
    s->d68   = 1;                              // "本帧正在分页"
    rebuild(s);                                // 0x17DD3C（重建 + 重开）
    log("PMP-OUTER-DONE: page=%d/%d rows=%d pages=%d", s->page+1, s->pages, rows, pages);
    return s->panel;
}
```

### 3.5 标题页码：`DrawTitle` hook @ `0x1380E0`

```c
void hook_draw_title(Menu *this, Il2CppString *title)      // 0x1380E0
{
    Slot *s = slot_for(this);
    if (s->title_valid /*[s+0x10]*/) { s->title_valid=1; }
    memcpy(s->title_buf /*[s+0x14], 0xA0 bytes*/, title, 0xA0);   // 0x1381B8 memcpy
    if (s->slicing && title && s->pages >= 2) {
        char buf[0xE0];
        snprintf(buf, 0xE0, "%s   %d / %d", title->chars, s->page+1, s->pages);  // 0x1381F0
        title = il2cpp_string_new(buf);          // 0x138264 动态解析的符号
    }
    return g_title_tramp(this, title);
}
```

| 项 | 值 |
|---|---|
| 标题缓冲 | `Slot+0x10` = 有效标志/长度，`Slot+0x14` = 原始字符串**对象拷贝**（0xA0 字节 ≈ 72 字符） |
| 页码格式 | `"%s   %d / %d"` → **三个空格**，`page+1` / `pages` |
| 触发条件 | `slicing && pages >= 2` |

### 3.6 翻页按钮：`0x17F1E8`–`0x17F2D4`

```c
/* 标签构造（0x17EEF8 / 0x17EF18，两个 snprintf） */
snprintf(lbl_prev, n, "<   %s", "Previous");   // → "<   Previous"   （三个空格）
snprintf(lbl_next, n, "%s   >", "Next");       // → "Next   >"       （三个空格）

/* 上一页 */
drawFullButton(this,              /* x0 = 菜单实例 */
               INT_MAX,           /* w1 = 0x7FFFFFFF  ← layoutPriority */
               lbl_prev,          /* x2 = "<   Previous" 字符串对象 */
               (mode==2) ? NULL : del_prev,   /* x3 = Action（NULL = 灰按钮） */
               s->page > 0);      /* w4 = enabled */         // 0x17F244-0x17F25C

/* 下一页 */
drawFullButton(this, INT_MAX-1, lbl_next, del_next, s->page < s->pages-1);  // 0x17F2B4
```

| 项 | 值 | 依据 |
|---|---|---|
| 上一页标签 | `"<   Previous"`（`"<   %s"` + `"Previous"`） | `0x17EEF8` / `0x17EF00` |
| 下一页标签 | `"Next   >"`（`"%s   >"` + `"Next"`） | `0x17EF18` / `0x17EF20` |
| 上一页 priority | `0x7FFFFFFF` | `mov w1,#0x7fffffff` @ `0x17F244` |
| 下一页 priority | `0x7FFFFFFE` | `mov w1,#0x7ffffffe` @ `0x17F2B4` |
| enabled | `page>0` / `page<pages-1` | `cset w4,gt` / `cset w4,lt` |
| NULL-action 模式 | `mode==2 ⇒ x3=NULL`（按钮可见但灰） | `csel x24,xzr,x8,eq` @ `0x17F210` |
| mode 来源 | `read_int_file("pmp_mode.txt")`，`0..2` | `0x17F1C4` → `"PMP-BTN: mode=%d (1=ours 2=NULL-action 0=none)"` |

**为什么用 INT_MAX**：`DrawFullButton` 的第 1 参是 `layoutPriority`（.22 里 `HKAZNXTEGNSSTQLF.layoutPriority` 在 `+0x3C`），VerticalLayout 按它排序；给最大值就保证翻页按钮永远在**所有内容行之后**（最底部）。

### 3.7 点击 → 翻页的完整链路（零托管回调）

```
用户点 ">" 
  └─ Unity onClick → 卖家伪造的 Action.invoke
       └─ stub @ 0x17F39C:  str w9(=+1), [0x3cec30];  x0=0; ret      ← 只写全局，不碰参数
            └─ 每帧 tick (0xE05C0/0xE06E8 里): 
                 d = [0x3cec30]; if (d) { 清标志; change_page(d); }
                   └─ change_page: page+=d; clamp[0..pages-1]; 
                        set_page_slice(); rebuild();   // 0x17DD3C
                          └─ Close(tramp) → Open(tramp) → 漏斗重放 → 只放行新页的行
```

| 日志串 | 含义 |
|---|---|
| `PMP-BTN: CLICK prev -> queued (-1)` / `... next -> queued (+1)` | stub 被调用 |
| `PMP-BTN: tick apply delta=%d menu=%p (frame context, safe)` | tick 消费 |
| `PMP-BTN: change_page REFUSED (rebuild in progress)` | 重入保护 |
| `PMP-BTN: change_page out of range %d (page=%d pages=%d)` | 越界 |
| `PMP-BTN: page now %d/%d` | 翻页成功 |
| `PMP-AUTO: flip %d at frame %d (page %d/%d)` | 自动翻页（调试用，`pmp_autopage.txt`） |

> ⚠️ **坑（照抄时必看）**：`0x137E70` 在 `delta==0` 时执行 `page -= 1`。也就是**每开一次窗、只要 `pmp_page.txt` 不存在，页码就自减**。卖家靠 `pmp_page.txt`/`0xB4` 标志规避；我们实现时**不要照抄这一句**，直接 `page = clamp(page + delta)`。

---

## 4. Smart SAS 页：`UseXxx` 回调（PMP 体系里的"一页"）

这是 PMP 分页器里**唯一一个由卖家自己注册的页**（"零件属性面板分页"里的额外页），也是 `UseXxx` 回调的唯一来源。

### 4.1 注册一行：`add_page_row(label, "Type.Method", selected)` @ `0x13E6F8`

```c
void add_page_row(Slot-less /*global page table*/, const char *label, const char *dotted, int selected)
{
    if (row_count >= 0xFF) return;
    if (cb_count  >= 0xFF) return;

    char ns[?], meth[?];
    if (!split_dotted(dotted, &ns, &meth)) return;      // 0x13DD80：按 '.' 切成"类"与"方法"
    cb_ns  [cb_count] = ns;                             // 0x3f48d8 + cb*8
    cb_meth[cb_count] = meth;                           // 0x3f50d8 + cb*8
    cb_count++;                                         // 0x3f48d4

    int i = row_count++;
    snprintf(page_names[i], 0x100, "%s", label);        // 0x3cf114 + i*0x100
    page_cb   [i] = cb_index;                           // 0x3df514 + i*4   ← 指向回调表
    page_sel  [i] = (selected != 0);                    // 0x3df114 + i*4   ← 选中态（高亮）
    dirty = 1;                                          // 0x3df0a28 / 0x3f48d0 → 触发重建
}
```

数据结构（**这就是"页"模型**）：

| 数组 | 地址 | 元素 | 说明 |
|---|---|---|---|
| 行名 | `0x3cf114 + i*0x100` | `char[256]` | 每行 256 字节，可放中文 |
| 回调索引 | `0x3df514 + i*4` | `int` | 指向回调表下标 |
| 选中态 | `0x3df114 + i*4` | `int` | 1 = 该行高亮/当前生效 |
| 行数 | `0x3ce0d10` | `int` | 上限 `0xFF` |
| 回调 类/方法 | `0x3f48d8` / `0x3f50d8` | `char*` | `+i*8`，由 `0x13DD80` 切分 |

### 4.2 这一页的 3 行（`0x14E404`–`0x14E464`，逐行实证）

| # | 行文案 | 回调（dotted path） | 选中条件 | 代码 |
|---|---|---|---|---|
| 0 | `Prograde` | `SmartSAS.Hooks.UsePrograde` | `menu->mode(0x458) == 0` | `0x14E404`–`0x14E424` |
| 1 | `Target` | `SmartSAS.Hooks.UseTarget` | `mode == 4` | `0x14E428`–`0x14E444` |
| 2 | `Surface` | `SmartSAS.Hooks.UseSurface` | `mode == 1` | `0x14E448`–`0x14E464` |

```c
add_page_row(tbl, "Prograde", "SmartSAS.Hooks.UsePrograde", menu->mode == 0);  // 0x14E424
add_page_row(tbl, "Target",  "SmartSAS.Hooks.UseTarget",   menu->mode == 4);  // 0x14E444
add_page_row(tbl, "Surface", "SmartSAS.Hooks.UseSurface",  menu->mode == 1);  // 0x14E464
log("panel: built natively (callbacks -> SmartSAS.Hooks.UseXxx)");             // 0x14E490
```

> 页脚那行文本 `panel: built natively (callbacks -> SmartSAS.Hooks.UseXxx)` 就是**一行业务无关的纯文本行**，证明"页 = 一串行，行可以是文本/按钮/输入框"。
>
> 另外在 3 行之前（`0x14E3C0`–`0x14E3EC`）还有一条**占位行**：名字缓冲 `memset(0xFF, 0x100)`（等于空名），`page_sel = -1`（既非选中也非未选），回调索引 `-1`（不可点）。它的作用是把 Smart SAS 与前一组行隔开——**照抄时要保留**，否则第一行会贴着上一组。

### 4.3 点击回调怎么落地

```
点击行 i
  └─ 取 page_cb[i] → (ns, meth)
       └─ il2cpp_class_from_name(image, ns.namespace, ns.name)
            └─ il2cpp_class_get_method_from_name(klass, meth, 0)
                 └─ 造 Action 委托（§3.7 同一套伪造手法）→ 挂到该行的 onClick
```
证据：`0x14E4D8` 动态解析 `il2cpp_object_get_class`、`0x14E4F8` 解析 `il2cpp_class_get_method_from_name`（同一函数体内按需 `dlsym`），说明**回调是按名字解析的**，不写死 RVA —— 所以卖家换游戏版本不用改回调表。

---

## 5. 联机页面组：行级清单（克隆原生行）

这 4 个页面**不画 Draw\* 行**，而是**克隆游戏自己的窗体行**。所有行都来自同一个模板路径：

```
"- Worlds Menu -" → "Holder" → "Rows" → "Name"        // 新建世界窗的输入行
"- Worlds Menu -" → "Holder" → "Scroll Window" → "World List (Scroller)"   // 世界列表
```

### 5.1 页面总表

| # | 页面 | 入口/函数 | 标题文案 | 行来源 |
|---|---|---|---|---|
| P0 | 主菜单入口按钮 | `0xEC33C` | `多人联机` / `Multiplayer` | 克隆主菜单按钮 + `MultiplayerIcon.png` 图标 |
| P1 | 服务器列表 | `0xED4B8` | `多人服务器` / `Multiplayer Server` | 克隆 `World List (Scroller)` → 每服一张卡 |
| P2 | 服务器卡上的按钮 | `0xED4B8`(内) / `0x15044C` | — | 克隆 `Buttons/Row 1`、`Buttons/Row 2` |
| P3 | 添加/编辑服务器表单 | `0x154014` | 替换 `World Title` | 克隆 `Rows` 下的 `Name` 行 ×3 |
| P4 | 加入服务器表单（含昵称） | `0xC0C58` | `多人服务器` | 克隆 `Rows/Name` ×3 + 自建昵称行 |
| P5 | 直连 | `0x104AB0` / `0x112190` | `直接连接` / `Direct Connect` | 复用 P3 的 IP 行 + `Connect Server` 按钮 |
| P6 | Smart SAS（PMP 页） | `0x14C9A4` | `Smart SAS` | `add_page_row` ×3（§4） |

### 5.2 P1 服务器列表（`0xED4B8`）—— 每张卡 4 个控件 + 页级 2 个按钮

```c
/* 每台服务器一张卡 */
card = clone(scroller_child, template);                    // 0x0EDE00
set_name(card, "Multiplayer Server Card %d" /*idx*/);      // 0x0EDE60  ← 名字带序号
set_label(find_by_name(card, "Name"), isCN ? "服务器名称" : "Server Name");   // 0x0EDF78
set_label(find_by_name(card, "Stat"), status_text(...));   // 0x0EE09C   ← 状态行

/* 卡上 4 个按钮，全部从世界卡上的同名按钮克隆（源码名已实证） */
b1 = clone(card, find_child(card, "Buttons/Row 1/Resume"));      // 0x0EE0D8 → 0x0EE128 改名
     set_name(b1, "Multiplayer Enter Server %d");   set_text(b1, "Enter World"/"进入世界");  // 0x0EE120 / 0x0EE21C
b2 = clone(card, find_child(card, "Buttons/Row 1"));            // 0x0EE24C → 0x0EE264
     set_name(b2, "Multiplayer Connect Server %d"); set_text(b2, "Connect Server"/"连接服务器"); // 0x0EE288
b3 = clone(card, find_child(card, "Buttons/Row 2/Delete World"));// 0x0EE0EC → 0x0EE64C
     set_name(b3, "Multiplayer Delete Server %d");  set_text(b3, "Delete Server"/"删除服务器");
b4 = clone(card, find_child(card, "Buttons/Row 2/Rename"));      // 0x0EE100 → 0x0EE764
     set_name(b4, "Multiplayer Rename Server %d");  set_text(b4, "Rename Server"/"编辑服务器");
```

> 克隆源是 **`Resume`**（不是 `Play`），文案改写用 `set_text`（`0x15604C`），**不是** `set_label`。按钮名必须带 `%d` 后缀，否则同一页多张卡会重名、`find_by_name` 会拿错。

页级两个按钮（`0x0EE9D8`–`0x0EEF38`，克隆后 `set_anchoredPosition` 手工摆位）：

| 按钮 | 名字 | 文案 | 证据 |
|---|---|---|---|
| 添加 | `Multiplayer Add Server` | `添加服务器` / `Add Server` | `0x0EEAE4` / `0x0EEAD0` |
| 直连 | `Multiplayer Direct Connect` | `直接连接` / `Direct Connect` | `0x0EECA4` / `0x0EEC90` |

> **注意**：列表**没有分页按钮** —— 滚动交给克隆来的 `Scroll Window / World List (Scroller)` 自己做。分页按钮只出现在 P6（零件菜单）。

### 5.3 P3 添加服务器表单（`0x154014`）—— 3 输入行 + 2 按钮

```c
holder = find_child("- Worlds Menu -", "Holder") -> "Rows";      // 0x154298/0x154300
tmpl   = find_child(rows, "Name");                               // 0x15434C

/* 行 1 */
r1 = clone(rows, tmpl);  set_name(r1, "Multiplayer Server Name Row");   // 0x15458C
   set_label(r1, isCN ? "服务器名称" : "Server Name");                   // 0x154504
   input1 = get_tmp_input(r1);                                          // 0x15450C → 0x1537F8
/* 行 2 */
r2 = clone(rows, tmpl);  set_name(r2, "Multiplayer Server Password Row");// 0x154838 ("Multiplayer Password Row")
   set_label(r2, "Password");                                            // 0x154A50
   input2 = get_tmp_input(r2);  set_input_contentType(input2, Password); // 0x154B98
/* 行 3 */
r3 = clone(rows, tmpl);
   set_label(r3, isCN ? "服务器 IP" : "Server IP");                       // 0x1547A0
   input3 = get_tmp_input(r3);                                           // 0x1547AC

/* 标题 & 按钮 */
set_text(find_by_name(form, "World Title"), isCN ? "添加服务器" : "Add Server");  // 0x154BA0 → 0x154C78
bOK     = find_by_name(form, "Confirm") /*→"Create World"*/;                      // 0x154C8C → 0x154CA4
bCancel = find_child(form, "Cancel");   set_name(bCancel, "Multiplayer Cancel Add Server"); // 0x154CBC
```

补充实证（`0x154A94`–`0x154B70`）：表单里**不用 Unity 的 `UnityEngine.UI.InputField`**，而是

```c
klass = il2cpp_class_from_name(image, "SFS.UI", "BEDZRJNYAMZNCWLR");   // 0x154A94 / 0x154A9C  ← 混淆类名
comp  = get_component(go, klass);                                        // 0x154A94
/* 密码行额外两步 */
il2cpp_runtime_invoke(comp, "set_contentType", {Password});              // 0x154B48
il2cpp_runtime_invoke(comp, "ForceLabelUpdate", NULL);                   // 0x154B70
```


| 行 | GameObject 名 | 标签（EN / 中文） | 控件 | 备注 |
|---|---|---|---|---|
| 1 | `Multiplayer Server Name Row` | `Server Name` / `服务器名称` | TMP 输入 | 必填 |
| 2 | `Multiplayer Password Row` | `Password` / — | TMP 输入（Password contentType） | 可空 |
| 3 | `Multiplayer Server IP Row` | `Server IP` / `服务器 IP` | TMP 输入 | 空时报 `服务器 IP（不能为空）` |
| — | 标题 `World Title` | `Add Server` / `添加服务器` | 文本 | 直接改写原文本 |
| — | `Multiplayer Cancel Add Server` | `Cancel` | 按钮 | 克隆自 `Cancel` |
| — | `Confirm` | 确认 | 按钮 | 保留原生 `Confirm` |

### 5.4 P4 加入服务器表单（`0xC0C58`）—— 3 输入行 + 昵称行 + 1 状态行

```c
rows = find_child("- Worlds Menu -","Holder")->"Rows";         // 0x0C4758
set_label(row1, isCN?"服务器名称":"Server Name");               // 0x0C49F4
set_label(row2, isCN?"服务器 IP":"Server IP");                  // 0x0C4AF8
set_label(row3, "Password");                                   // 0x0C4BFC
/* 昵称行（见 §6） */
row4 = clone(formHolder, tmpl);  set_name(row4, "Multiplayer Nickname Row");   // 0x0C8468
set_label(row4, isCN?"昵称":"Nickname");                        // 0x0C938C
/* 状态区 */
label = find_by_name(form,"Stat");  text = isCN?"多人服务器":"Multiplayer Server";  // 0x0C9CE4
status = "状态：未连接"/"Status: Not connected"                    // 0x0C9DD0
```

### 5.5 文案从哪来（★ 关键，别去游戏语言表里找）

**卖家在 .so 里内嵌了一张中英对照表**，按"游戏当前语言名是否包含 `Chinese`"二选一。**语言文件是每次现读的**，不是读游戏内部状态：

```c
/* 0x0EE160（列表页）、0x154BDC（表单页）、0x154CF8 … 每处都重复这段 */
FILE *f = fopen("/storage/emulated/0/Android/media/com.FLDPXIce.SpaceflightSimulatorPro"
                "/Saving/Settings/LanguageSettings_2.txt", "rb");
fread(buf, 1, 0xFFF, f);
g_isCN = (strstr(buf, "Chinese") != NULL);   // 0x0EE1D8 / 0x154C44 → str w8,[0x1F85A0]
label  = g_isCN ? zh : en;                   // csel
```

> 注意路径里是**卖家自己的包名** `com.FLDPXIce.SpaceflightSimulatorPro`，我们改成 `com.StefMorojna.SpaceflightSimulator`。这张表是硬编码在代码里的 `csel` 对，**没有 key、不走游戏本地化**——照抄只能抄字符串本身。

| 用途 | 英文（偏移） | 中文（偏移） |
|---|---|---|
| 窗口/页标题 | `Multiplayer Server` `0x2FA30` | `多人服务器` `0x22879` |
| 主菜单按钮 | `Multiplayer` `0x2DCD6` | `多人联机` `0x2ED98` |
| 名称行 | `Server Name` `0x1C0BF` | `服务器名称` `0x2AF26` |
| IP 行 | `Server IP` `0x25AAB` | `服务器 IP` `0x20866` |
| IP 为空 | `Server IP (cannot be empty)` `0x23812` | `服务器 IP（不能为空）` `0x25E6A` |
| 昵称行 | `Nickname` `0x18685` | （同 key，走游戏本地化） |
| 连接 | `Connect Server` `0x317AD` | `连接服务器` `0x1E0FC` |
| 删除 | `Delete Server` `0x2C18E` | `删除服务器` `0x1B773` |
| 编辑 | `Rename Server` `0x20127` | `编辑服务器` `0x2FB63` |
| 添加 | `Add Server` `0x25EA4` | `添加服务器` `0x2DD36` |
| 直连 | `Direct Connect` `0x18316` | `直接连接` `0x2EDD4` |
| 状态·已连接 | `Status: Connected` `0x168B0` | `状态：已连接` `0x2E1DA` |
| 状态·连接中 | `Status: Connecting...` `0x1768C` | `状态：正在连接...` `0x2B216` |
| 状态·失败 | `Status: Connection failed` `0x25409` | `状态：连接失败` `0x194BD` |
| 状态·未连接 | `Status: Not connected` `0x1E0DD` | `状态：未连接` `0x1E0DD` |
| 服务器名 | `Server: %s` `0x168C2` | `服务器：%s` `0x2E1ED` |
| 玩家 ID | `Player ID: %d` `0x168CD` | `玩家 ID：%d` `0x2E1FC` |
| 地址/错误 | `Address: %s` / `Error: %s` | `地址：%s` `0x194DF` / `错误：%s` `0x194D3` |

> 注意中文用的是**全角冒号 `：`**，`Server: %s` 是半角 + 空格。照抄时别改成半角。

---

## 6. 昵称输入框：`borrowedForm` 借用流程（`0xC0C58`）

卖家**不 new 输入框**，而是从"新建世界"表单里**借**一个 TMP 行出来：

```c
/* 1) 找借出方：新建世界表单的 Name 行 */
formHolder = find_child("- Worlds Menu -", "Holder");            // 0x0C81A0
rowsFrame  = find_child(formHolder, "Rows");
nameRow    = find_child(rowsFrame, "Name");                      // 模板

/* 2) 克隆一整行（带输入框组件） */
row = clone(formHolder, nameRow);                                // 0x0C8468
set_name(row, "Multiplayer Nickname Row");                       // 0x0C847C

/* 3) 从克隆体里把 TMP_InputField 抠出来（不认混淆名，认 namespace+name） */
input = find_tmp_input(row);              // 0x1537F8 → 内部用 "TMPro" / "TMP_InputField" 比对
      // 0x1539DC 还会退回按 GameObject 名 "InputField (TMP)" 查找
if (input) { g_nickInput = input; set_row_text(input, savedNick); }   // 0x0C8E30 / 0x0C8E48

/* 4) 定位：居中、按屏幕单位换算 */
MP-NICK-PLACE: centered rowW=%f rowH=%f screenUnits=%f y=%f        // 0x0C9210

/* 5) 借用标记 & 日志 */
log("MP-NICK-ROW: created row=%p input=%p holder=%p borrowedForm=%d "
    "rowSize=%dx%d screenUnits=%f yOffset=%f nick=%s");            // 0x0C8DE4
```

| 项 | 值/说明 |
|---|---|
| 借出方 | `- Worlds Menu - / Holder / Rows / Name`（新建世界窗的名称输入行） |
| 行名 | `Multiplayer Nickname Row` |
| 输入框定位 | `find_tmp_input()`：遍历组件，比 `il2cpp_class_get_namespace()=="TMPro"` + `class_get_name()=="TMP_InputField"`；兜底按名字 `InputField (TMP)`（`0x1539DC`） |
| `borrowedForm` | 1 = 该输入框来自克隆的原生表单（不是自建）；日志 `0x0C8DE4` |
| 干掉原监听 | `MP-NICK-SUBMIT: cleared onSubmit/onEndEdit on %d TMP_InputField(s)`（`0x2FF1A`）——**必须清**，否则回车会触发"创建世界" |
| 持久化 | `.../Mods/mp_nickname.txt`（`0x1827F`）+ `MP-NICK-EDIT: '%s' (saved; next Hello/connect uses it)` |
| 面板被顶掉时的守卫 | `MP-NICK-GUARD: home re-activated while editing (latch=%d) -> take back MP page`（`0x179EE`） |

---

## 7. 可直接照抄的实现规格（native，给 `libsfsmp` 用）

### 7.1 数据模型（建议直接照这个写）

```c
typedef struct Row {
    int   prio;        // ★ 排序键 = DrawText/DrawFullButton 的第 1 个 int
    int   kind;        // 0=TEXT 1=FULLBUTTON 2=INPUT 3=TITLE
    const char *label; // 文案（已按语言选好）
    void (*onClick)(void);
} Row;

typedef struct Page {
    const char *title;      // 页标题
    Row  rows[64]; int n;
} Page;

int  rows_per_page = 14;    // clamp(cfg,4,40)  ← 与卖家一致
int  page = 0;
int  pages = 0;
```

### 7.2 渲染流程（一次开窗）

```c
pages = (n_rows + rows_per_page - 1) / rows_per_page;    // ★ 与 0x137C18 同公式
if (pages < 2) page = 0;                                  // 单页不分页
sort_rows_by_prio(rows, n_rows);                          // ★ 与 0x17E2CC 同语义

open(inst, NULL, builder, 1, NULL, NULL);   // builder 里：
    DrawTitle(inst, pages > 1 ? format("%s   %d / %d", title, page+1, pages) : title);
    for (i = page*rows_per_page; i < min((page+1)*rows_per_page, n_rows); i++) {
        switch (rows[i].kind) {
        case TEXT:       DrawText      (inst, rows[i].prio, rows[i].label); break;
        case FULLBUTTON: DrawFullButton(inst, rows[i].prio, rows[i].label, del_of(i), 1); break;
        }
    }
    if (pages > 1) {
        DrawFullButton(inst, INT_MAX,   "<   Previous", del_prev, page > 0);        // ★ 0x17F244
        DrawFullButton(inst, INT_MAX-1, "Next   >",     del_next, page < pages-1);  // ★ 0x17F2B4
    }
```

### 7.3 与卖家行为对齐的 8 条硬约束

| # | 约束 | 为什么 |
|---|---|---|
| 1 | `priority` 必须是**单调递增的间隔值**（10,20,30…），翻页按钮用 `INT_MAX`/`INT_MAX-1` | 原生 VerticalLayout 按 `layoutPriority(0x3C)` 排序；卖家分页也按它排 |
| 2 | 标题页码用 `"%s   %d / %d"`（**三空格**） | `0x1381F0` |
| 3 | 每页行数默认 **14**，钳位 `[4,40]` | `0x136920` |
| 4 | 按钮 `Action` 允许 `NULL`（变灰） | `0x17F210` 的 `csel` |
| 5 | 委托 `+0x18` 必填、`+0x40`/`+0x28` 填 0，且 `gchandle_new` 扎根 | E 报告 §4.3（本 build ABI 已实证） |
| 6 | 点击回调只写全局标志，tick 里再重建 | 卖家全链路如此（§3.7） |
| 7 | 重建顺序：`Close → 改页 → Open`，**不要在 builder 里再 Open** | `Open` 第一句就是 `Close`，重入必炸 |
| 8 | 输入框**克隆**，且清 `onSubmit/onEndEdit` | `0x2FF1A` 日志 |

### 7.4 我们的落地建议（与 E 的 R-A/R-B 对齐）

- **主菜单**（level0，无 StatsMenu）：走克隆路线（P1/P3/P4 的等价物），行模板用 `- Worlds Menu -/Holder/Rows/Name`，滚动用 `World List (Scroller)`。
- **进游戏后**（level2/飞行）：走 R-B，用 `Open + Draw*` 画 P6 那种"行数组页"，分页按钮照 §3.6。
- 两条路线共用 §7.1 的数据模型，只是渲染后端不同（克隆行 vs Draw* 行）。

---

## 8. 风险与待验证

| 项 | 状态 | 说明 |
|---|---|---|
| PMP 分页数学 / 行数配置 | **已实证**（反汇编逐条） | §3.1–3.3 |
| 翻页按钮 priority / enabled | **已实证** | §3.6 |
| 标题页码格式 | **已实证** | §3.5 |
| Smart SAS 3 行 + UseXxx | **已实证** | §4.2 |
| 中英文案对照 | **已实证**（UTF-8 串表，`0x1F85A0` 语言开关） | §5.5 |
| 联机页行名/克隆源 | **已实证**（GameObject 名字符串 + 克隆调用） | §5 |
| P4 三行的**上下顺序** | **推断**（受寄存器复用影响，标签与行名交叉） | 需上机核对截图 |
| 联机页是否也用 PMP 分页 | **推断：否**，用游戏自带滚动条 | `0x0ED824` 找的是 `Scroll Window` |
| `pmp_page.txt` 的 `page-=1` 怪逻辑 | **已确认存在**，不建议照抄 | §3.7 坑 |
| `mode==2`（NULL-action）用途 | 未定 | 可能是"禁用态占位"调试模式 |

---

## 附录：关键地址速查

| 地址 | 内容 |
|---|---|
| `0x136920` | `get_rows_per_page()`（14 / 4..40 / json key） |
| `0x13797C` | `read_int_file(path, def, &out)` |
| `0x137ABC` | `Open` hook（分页主控：算 pages、选页、重建） |
| `0x137F18` | `EFLOBBJGMGA` hook（漏斗：逐行放行/丢弃） |
| `0x1380E0` | `DrawTitle` hook（标题追加 `n / N`） |
| `0x17E0BC` | `set_page_slice(slot, rows, pageSize)`（排序 + 切片 + 位图） |
| `0x17DBDC` | `slice_verbose()`（日志开关，读 `Mods/<file>`） |
| `0x17DD3C` | 重建（`Close → 改页 → Open`） |
| `0x17F1E8`–`0x17F2D4` | 翻页按钮挂载点（`INT_MAX` / `INT_MAX-1`） |
| `0x13E6F8` | `add_page_row(label, "Type.Method", selected)` |
| `0x13DD80` | dotted-path 切分（类 / 方法） |
| `0x1537F8` | `find_tmp_input(row)`（TMPro / TMP_InputField） |
| `0x155ED4` | `clone(holder, template)` |
| `0xEB8F0` | `GameObject.set_name` |
| `0x1033C0` | `set_label(row, text)`（找 `Label/Text`→`Label`→`Text Top`） |
| `0x1552D4` | `set_text` |
| `0xEC33C` | 主菜单"多人联机"入口按钮 |
| `0xED4B8` | 服务器列表页 |
| `0x154014` | 添加/编辑服务器表单 |
| `0xC0C58` | 加入服务器表单（昵称行） |
| `0x14C9A4` | Smart SAS 页 |

---

## 附录 B：逐条复核记录（本次实机反汇编回读）

| 结论 | 复核方式 | 结果 |
|---|---|---|
| pages = `(n + rows - 1) / rows` | `0x137C18`：`add w8,w21,w26` / `sub #1` / `sdiv w24,w8,w26` | ✅ |
| rows_per_page 默认 14 | `0x1369D0` 与 `0x1369F4` 两处 `mov w0,#0xE` | ✅ |
| 钳位 `[4,40]` | `0x1369F8` `cmp w0,#4`、`0x136A00` `mov w9,#0x28`、`csel` 两次 | ✅ |
| 配置来源 | `0x136958` 先读 `pmp_rows.txt`，失败再读 `sfs_settings.json` 的 `"part_menu_rows_per_page":` | ✅ |
| 标题 `"%s   %d / %d"` + 仅当 `pages>=2` | `0x1381F0` 格式串、`0x1381DC` `cmp w6,#2; b.lt` 跳过、`0x1381FC` `add w5,w8,#1` | ✅ |
| 上一页 `prio=INT_MAX`，`en=page>0` | `0x17F244` `mov w1,#0x7fffffff`；`0x17F254` `cmp w8,#0` + `cset w4,gt` | ✅ |
| 下一页 `prio=INT_MAX-1`，`en=page<pages-1` | `0x17F2B4` `mov w1,#0x7ffffffe`；`0x17F29C/0x17F2CC` `sub #1` + `cset lt` | ✅ |
| Smart SAS 3 行 + 选中条件 | `0x14E404/0x14E428/0x14E448` 三组 `csel` + `bl 0x13E6F8` | ✅ |
| `add_page_row` 结构 | `0x13E6F8`：`0x13DD80` 切分 → 回调表 `0x3f48d8/0x3f50d8` → 名 `0x3cf114+i*0x100` | ✅ |
| 卡按钮克隆源 | `0x0EE0D8` `"Buttons/Row 1/Resume"`、`0x0EE0EC` `"Buttons/Row 2/Delete World"`、`0x0EE100` `"Buttons/Row 2/Rename"`、`0x0EE24C` `"Buttons/Row 1"` | ✅ |
| 语言开关 | `0x0EE160`/`0x154BDC` 读 `LanguageSettings_2.txt` + `0x0EE1D8`/`0x154C44` `strstr "Chinese"` | ✅ |
| 输入框类型 | `0x154A94` `"SFS.UI"` + `0x154A9C` `"BEDZRJNYAMZNCWLR"`；兜底 `0x1539DC` `"InputField (TMP)"` | ✅ |
| 密码行 contentType | `0x154B48` `"set_contentType"`、`0x154B70` `"ForceLabelUpdate"` | ✅ |
| P4 三行上下顺序 | 寄存器复用导致标签/行名交叉，静态无法 100% 定序 | ⚠️ 上机核对 |
| 联机页是否分页 | 列表页找的是 `Scroll Window`，无翻页按钮调用 | ⚠️ 推断为"否" |

复现命令（全部可重跑）：

```bash
cd /d/SFS-Pro/build
python research/native_ui/dump.py    0x137bd0 0x137c60   # pages 公式
python research/native_ui/dump.py    0x1369cc 0x136a90   # rows_per_page
python research/native_ui/dump.py    0x1380e0 0x138230   # 标题
python research/native_ui/dump.py    0x17f1e8 0x17f2d8   # 翻页按钮
python research/native_ui/dump.py    0x14e3e0 0x14e4a0   # Smart SAS 行
python research/native_ui/outline.py 0xed4b8  0xf0000    # 服务器列表页
```
