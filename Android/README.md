# Android 侧（SFS 手机端联机 UI / 注入补丁）

## 目录
| 目录 | 内容 |
|---|---|
| `src/` | 注入库源码（`v30.c` … `v43.c`，48 个文件；当前主线 = `v43.c`，其中 mode 11 = 原生输入弹窗） |
| `dump/` | 1.6.00.22 官方 IL2CPP dump（`dump.cs` 21 MB、`script.json`、`il2cpp.h`、`stringliteral.json`、`DummyDll/`） |
| `research/native_ui/` | 5 份子 agent 研究报告 + `00_ROUTE.md` 总路线 + 反汇编小工具 |
| `checkpoints/v44_native_textinput_ok/` | 可用存档点（含自己的 APK 副本、README、MANIFEST） |
| `tools/` | `pack-so.ps1`（打包签名）、`scan_apk.py` |

## 档位（`sfsmp-mode.txt`）
| mode | 行为 | 状态 |
|---|---|---|
| 11 | 游戏原生输入弹窗 `DEOTNULUBFZWSSUA.textInput`（三行输入） | ✅ 稳定 |
| 10 | 克隆 `- World Creation -/Holder` / `- Update Popup -` | ❌ 崩溃，勿用 |
| 9 | level2 `NOJZIZCFSEXSUEEX.Open + Draw*` 抽屉 | 需进世界，未验证 |
| 3 | Android Dialog 登录页（兜底） | ✅ 可用 |
| 5 / 6 | Mods 窗实验 | 实验 |

## 重要约束
- 目标包 `com.StefMorojna.SpaceflightSimulator` 1.6.00.22，只对**官方包**打补丁（不含破解）。
- 克隆 UI 铁律：**克隆 `Holder`，不克隆 `- X -` 根**（根上 Awake 会写静态单例）；`Instantiate` 只用 3 参版 RVA `0x2FC1B58`。
- 手搓委托 `Action<T>` 的 `+0x28` 必须是真实 `MethodInfo*`，为 NULL 时一调用就崩 → 当前 `textInput` 回调传 NULL，**不要按弹窗底部按钮**。
- 二进制体积：本目录约 375 MB（含 APK），提交到 git 前建议用 LFS 或把 `apk/`、`checkpoints/*/build/*.apk` 加入 `.gitignore`。

## 下一步
1. 登录页对齐 PC `JoinMenu`（见 `checkpoints/v44_native_textinput_ok/UI_SPEC_joinmenu.md`）
2. 读值：轮询 `VCJMEXUSBHCVBJHA` 元素数组 `+0x60` → `textBox +0x30` → TMP `m_Text +0x210`
3. 接入联机客户端（内存注入 / hook 网络层）
