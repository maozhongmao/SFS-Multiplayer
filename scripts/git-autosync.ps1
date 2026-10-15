# scripts/git-autosync.ps1
# 作用：把工作区里"最新源码"的变化自动提交并推送到 beta 分支。
# 用途：开机自启 + 每 30 分钟一次 —— 预发布分支 + 网络备份。
# 特性：
#   * 只提交 .gitignore 允许的内容（游戏程序集/存档/工具链/构建产物一律不进库）
#   * 没有变化就什么都不做（不会制造空提交）
#   * 推送失败只写日志，不会反复弹窗打扰；日志里给出修复提示
param(
    [string]$Repo = 'D:\SFS-Multiplayer',
    [string]$Branch = 'beta',
    [string]$LogFile = 'D:\SFS-Multiplayer\git-autosync.log'
)
$ErrorActionPreference = 'Continue'
function Write-Log([string]$m) {
    $line = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + '  ' + $m
    Add-Content -Path $LogFile -Value $line -Encoding UTF8
}
try {
    Set-Location $Repo
    if (-not (Test-Path '.git')) { Write-Log 'SKIP 不是 git 仓库'; exit 0 }

    $porcelain = git status --porcelain 2>&1
    if (-not $porcelain) { Write-Log 'OK 无变化，跳过'; exit 0 }

    git add -A 2>&1 | Out-Null
    $staged = git diff --cached --name-only 2>&1
    if (-not $staged) { Write-Log 'OK 暂存区为空（变更都被忽略规则挡掉）'; exit 0 }
    $count = ($staged | Measure-Object).Count

    $msg = 'auto: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' (' + $count + ' files)'
    git commit -q -m $msg 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Log 'WARN commit 失败'; exit 1 }
    Write-Log ('COMMIT ' + (git log --oneline -1))

    $push = git push origin $Branch 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Log 'PUSH ok'
    } else {
        Write-Log ('PUSH FAILED ' + (($push | Select-Object -Last 3) -join ' | '))
        Write-Log 'HINT 需要一次凭据配置：见 docs/git-autosync.md（配置一次后即可全自动）'
    }
} catch {
    Write-Log ('ERROR ' + $_.Exception.Message)
}
