# 分支职责与外部 PR 隔离流程

## 三个分支

| 分支 | 职责 |
|---|---|
| `main` | **发布线**。只接受已经在 `beta` 验证过的内容。玩家与文档都以它为准。 |
| `beta` | **预发布 + 异地备份 + 外部 PR 隔离区**（三合一，见下）。 |
| 个人/fork 分支 | 贡献者自己的开发分支，改动通过 PR 进 `beta`。 |

## beta 的三项职责

1. **预发布**：最新源码先到这里，稳定后再并进 `main`（正式发布仍走 GitHub Releases + `build-release.ps1`）。
2. **异地备份**：登录时 + 每 30 分钟自动提交并推送（见 `docs/git-autosync.md`），
   只上传变化的对象；无变化时脚本在联网前就退出。
3. **外部 PR 隔离区**：别人从 fork 提交的合并请求**先落到 `beta`**，在隔离区里审、编译、实测；
   确认没问题再并进 `main`。`main` 因此始终保持可发布状态。

## 建议在 GitHub 上做的设置

- **Settings → General → Default branch**：可把默认分支设为 `beta`，
  这样外部 fork 的 PR 默认就打向 `beta`（不想改默认分支，就在 README/CONTRIBUTING 里写明"请向 `beta` 提交"）。
- **Settings → Branches → Branch protection**：给 `main` 加保护（禁止直接 push、要求 PR），
  `beta` 保持开放以便自动同步与外部 PR。

## 审查外部 PR 的固定动作

1. 看 diff：**有没有夹带**编译产物、游戏本体程序集、存档、密钥、以及与 GPL 项目雷同的代码。
2. 在 `beta` 上编译：`dotnet build Client/src/MultiplayerSFS.Client/MultiplayerSFS.Client.csproj -c Release`
   与 `ServerCpp/build-release.ps1`（或至少 armv7 目标）。
3. 进游戏实测一次（进世界、开关作弊项、看日志 `[SFS-MP]`）。
4. 通过后：把 `beta` 并入 `main`，再发 Release。

## 自动同步与隔离区的配合

自动同步脚本有三道护栏，避免在"隔离区"里帮倒忙：

- **不在 `beta` 分支上** ⇒ 跳过（绝不把别人的检出内容提交到别的分支）；
- **正在 merge/rebase** ⇒ 跳过（不打断合并审查）；
- **无变化** ⇒ 在联网前退出（不产生任何流量）。

## 合并前置检查（避免噪声）

```powershell
git diff --name-status origin/main origin/beta        # 看清 A/M/D/R
git diff --name-only --ignore-all-space origin/main origin/beta   # 应与会话时一致 ⇒ 无空白/行尾噪声
```

`.gitignore` 采用"**主仓库原块原样置顶 + 本分支补充**"的写法，
所以与 `main` 的 diff 只有新增行，合并时不会互相覆盖规则。
