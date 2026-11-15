# 发布清单（V2.0.2 · 开源前检查）

> 用途：把「可以公开」和「绝对不能公开」的东西分开写清楚，逐项打勾后再上传。

## 一、可以发布

| 类别 | 路径 | 说明 |
|---|---|---|
| 服务端源码 | `Server/cpp/src`、`Server/cpp/include`、`Server/cpp/CMakeLists.txt` | 自研 C++ 服务端 |
| 手机端源码 | `android/src/v64.c`、`android/src/mp_net.c`、`android/src/mp_types.h`、`android/src/ikcp.c(第三方)` | mod 本体；旧版本快照 `v28.c`…`v64.c` 待你逐个过 |
| 补丁器源码 | `android/patchapp/src`、`android/patchapp/hooksrc`、`android/patchapp/res`、`build.ps1` | 纯 Java，不含游戏代码 |
| PC 客户端源码 | `Client/src` | C# mod |
| 文档 | `README.md`、`CHANGELOG.md`、`LICENSE.md`、`CONTRIBUTING.md`、`SECURITY.md`、`ROADMAP.md`、`docs/patcher.md`、`docs/mobile-feasibility.md`、`docs/plugin-*.md`、`docs/server-logging.md`、`docs/branch-policy.md` | 需逐篇再读一遍，确认没有夹带逆向内容 |
| 发布产物 | `发布/产出/` 下的 APK / DLL / 服务端可执行文件 | 由构建脚本产出 |

## 二、绝对不能发布（历史泄露风险点）

| 路径 | 内容 | 处理 |
|---|---|---|
| `android/dump/`（153 MB） | `dump.cs`、`il2cpp.h`、`script.json` —— 对商业游戏的 il2cpp 逆向导出 | 不进仓库、不上传 |
| `ref/PCSFS/`（737 个 .cs） | 反编译得到的游戏 C# 源码 | 不进仓库、不上传 |
| `android/work/`（924 MB） | 反编译/中间产物（jadx_out、dex、备份 APK 等） | 不进仓库、不上传 |
| `docs/recon-crash-analysis.md`、`docs/recon-remote-render.md` | 针对商业游戏的逆向分析记录 | 不进仓库、不上传 |
| `docs/sfs-1.6-development/` | 逆向工具链与技能包 | 不进仓库、不上传 |
| `.audit/`、`android/checkpoints/` | 自动审计/存档快照 | 不进仓库 |
| `bad_files.txt`、`upload_success*.txt`、`大文件过滤清单.txt`、`新建文本文档.txt` | 内部清单/临时文件 | 不进仓库 |
| `patchapp/keys/`、`patchapp/keystore/` | 签名私钥 | 永不公开（已写入 .gitignore） |
| 任何第三方商店（5play 等）重编译的 APK | 含他人加固/保护代码 | 不随仓库分发 |

## 三、上传前逐项打勾

- [ ] `git status` 里没有任何上面「二」的路径
- [ ] `dump.cs` / `il2cpp.h` / `script.json` 不在任何提交里（含历史提交）
- [ ] `ref/PCSFS` 的 737 个 .cs 一个都没进去
- [ ] `docs/` 里每篇 md 都通读过，没有贴游戏反编译代码/类名映射表
- [ ] 私钥、证书、keystore 没有出现在任何一次提交
- [ ] APK 里不含游戏本体（安装器只是补丁器）
- [ ] LICENSE 与各源文件 SPDX 头一致

## 四、git 历史里有旧泄露的处理建议

如果确认以前某次提交已经把逆向文档推上去过：

1. 该仓库**不要再直接公开**（即便删文件，历史里仍在）；
2. 需要的话用 `git filter-repo` 重写历史，或**新建一个干净仓库**只推「一、可以发布」的文件；
3. 公开前用 `git log --all --oneline -- <敏感路径>` 自检一遍。

> 本次建议：**先新建干净仓库、只推白名单文件**，旧仓库保留为私有备份。
