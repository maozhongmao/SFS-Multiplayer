# Android 侧（SFS 手机端联机 UI / 注入补丁）

## 目录
| 目录 | 内容 |
|---|---|
| `src/` | 注入库源码（`v30.c` … `v43.c`，48 个文件；当前主线 = `v43.c`，其中 mode 11 = 原生输入弹窗） |
| `research/native_ui/` | 5 份子 agent 研究报告 + `00_ROUTE.md` 总路线 + 反汇编小工具 |
| `tools/` | `pack-so.ps1`（打包签名）、`scan_apk.py` |

## 重要约束
- 目标包 `com.StefMorojna.SpaceflightSimulator` 1.6.00.22，只对**官方包**打补丁（不含破解）。
- 克隆 UI 铁律：**克隆 `Holder`，不克隆 `- X -` 根**（根上 Awake 会写静态单例）；`Instantiate` 只用 3 参版 RVA `0x2FC1B58`。
- 手搓委托 `Action<T>` 的 `+0x28` 必须是真实 `MethodInfo*`，为 NULL 时一调用就崩 → 当前 `textInput` 回调传 NULL，**不要按弹窗底部按钮**。
- 二进制体积：本目录约 375 MB（含 APK），提交到 git 前建议用 LFS 或把 `apk/`、`checkpoints/*/build/*.apk` 加入 `.gitignore`。
