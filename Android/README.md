# Android 侧（SFS 手机端联机 UI / 注入补丁）

来源：2026-10-21 从 `D:/SFS-Pro` + `D:/sfs-tools` 复制合并而来，未改动任何原文件。

## 目录
| 目录 | 内容 |
|---|---|
| `src/` | 注入库源码（`v30.c` … `v43.c`，48 个文件；当前主线 = `v43.c`，其中 mode 11 = 原生输入弹窗） |
| `so-out/` | 编译产物 `v43.so` / `v44.so`（arm64-v8a） |
| `apk/` | 已注入并签名的官方包 `live-aligned-debugSigned.apk`（110,298,060 B） |
| `dump/` | 1.6.00.22 官方 IL2CPP dump（`dump.cs` 21 MB、`script.json`、`il2cpp.h`、`stringliteral.json`、`DummyDll/`） |
| `research/native_ui/` | 5 份子 agent 研究报告 + `00_ROUTE.md` 总路线 + 反汇编小工具 |
| `checkpoints/v44_native_textinput_ok/` | 可用存档点（含自己的 APK 副本、README、MANIFEST） |
| `tools/` | `pack-so.ps1`（打包签名）、`scan_apk.py` |
| `docs/` | 逆向/克隆/卖家分析报告（agent_A/B/C/E、ANALYSIS.md、native_button_clone_study.md 等） |
| `screens/` | 28 张过程截图（主菜单、克隆窗、原生输入框、崩溃现场） |

## 构建（需要 NDK，路径在本机 `D:/sfs-tools/ndk/android-ndk-r27c`）
```bash
CC="D:/sfs-tools/ndk/android-ndk-r27c/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android24-clang.cmd"
"$CC" -shared -fPIC -O2 -Wall -o "so-out/v44.so" "src/v43.c"
powershell -NoProfile -ExecutionPolicy Bypass -File "tools/pack-so.ps1" -SoName v44
```
`pack-so.ps1` 会把 `so-out/<name>.so` 注入 `apk/live-aligned-debugSigned.apk` 的 `lib/arm64-v8a/libsfsmp.so` 并重签名。

## 安装 / 切档（测试机 vivo V2046A `3069634870003H8`）
```bash
ADB="D:/HBuilderX/plugins/vnmy-adbWifiConnect/adb.exe"; S=3069634870003H8
"$ADB" -s $S install -r --no-incremental apk/live-aligned-debugSigned.apk
"$ADB" -s $S shell "echo 11 > /sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-mode.txt"
"$ADB" -s $S shell "am force-stop com.StefMorojna.SpaceflightSimulator; am start -n com.StefMorojna.SpaceflightSimulator/com.teamcuriosity.addons.ExtendedUnityActivity"
```

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
