# git 自动同步（beta 分支）

## 它做什么

- **触发**：登录时 ／ 每 30 分钟（Windows 计划任务 `SFS-Multiplayer-git-autosync`）
- **动作**：`git add -A`（按 `.gitignore` 过滤）→ 有变化就提交 → 推送到 `beta` 分支
- **日志**：`D:\SFS-Multiplayer\git-autosync.log`（每行一条：COMMIT / PUSH ok / PUSH FAILED）
- **脚本**：`scripts/git-autosync.ps1`

> ⚠️ 该脚本**必须保持纯 ASCII**：PowerShell 5.1 在没有 BOM 时按 ANSI 读取 `.ps1`，
> 中文注释会把解析器直接搞崩（本项目历史上踩过，脚本里也写了这条警告）。

## 什么会进库、什么绝不会

**不会**：`Client/refs/**`（游戏本体参考程序集，版权物）、`.audit/`（含你的蓝图存档）、
`_quarantine/`、`Rollback-Mods/`、`Spaceflight-Simulator-MultiplayerMod-main*`（同行 GPL 项目）、
`tools/zig-*`、`bin/obj`、`build/`、`artifacts/`、`release*/`、`*.zip`、日志文件。

## 手动跑一次

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\SFS-Multiplayer\scripts\git-autosync.ps1
```

## 任务管理

```powershell
Get-ScheduledTask -TaskName 'SFS-Multiplayer-git-autosync'                            # 状态
Start-ScheduledTask -TaskName 'SFS-Multiplayer-git-autosync'                          # 立即跑
Disable-ScheduledTask -TaskName 'SFS-Multiplayer-git-autosync'                        # 暂停
Unregister-ScheduledTask -TaskName 'SFS-Multiplayer-git-autosync' -Confirm:$false     # 删除
```

## 凭据

本机 git 凭据可用（实测推送成功）。以后若失效：手动 `git push` 一次，按提示输入 GitHub 凭据
（或 PAT），之后又会全自动。

## 实测记录（2026-10-15）

造改动 → 自动 `COMMIT` + `PUSH ok`；清理后再跑 → 再次 `COMMIT` + `PUSH ok`；
远端 `beta` 与本地 HEAD 哈希一致。

## 注意

- 这是**预发布分支**：内容可与正式发布不一致，正式发布仍走 `build-release.ps1` + Releases。
- 自动提交的 message 形如 `auto: 2026-10-15 20:21:19 (3 files)`，便于回溯。
