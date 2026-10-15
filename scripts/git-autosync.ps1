# scripts/git-autosync.ps1
# Purpose: commit changed source and push it to the beta branch (pre-release + offsite backup).
# Triggers: logon + every 30 minutes (Windows Task Scheduler task SFS-Multiplayer-git-autosync).
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads .ps1 as ANSI when there is no BOM,
#       and non-ASCII text has broken the parser here before (see build-release.ps1).
#
# Bandwidth rules (important):
#   * no changes  -> exit before touching the network at all (zero bytes sent or received)
#   * changes     -> git push sends only the objects the remote does not already have
#   * push rejected (remote moved) -> fetch + rebase + retry once, then give up and log
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
    if (-not (Test-Path '.git')) { Write-Log 'SKIP not a git repo'; exit 0 }

    # 1) local changes only; nothing changed -> no network at all
    $porcelain = git status --porcelain 2>&1
    if (-not $porcelain) { Write-Log 'OK no changes, skipped (no network)'; exit 0 }

    git add -A 2>&1 | Out-Null
    $staged = git diff --cached --name-only 2>&1
    if (-not $staged) { Write-Log 'OK staged empty (all changes ignored)'; exit 0 }
    $count = ($staged | Measure-Object).Count

    $msg = 'auto: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' (' + $count + ' files)'
    git commit -q -m $msg 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Log 'WARN commit failed'; exit 1 }
    Write-Log ('COMMIT ' + (git log --oneline -1))

    # 2) push first: the normal case downloads nothing
    $push = git push origin $Branch 2>&1
    # record the push output verbatim (locale independent): it contains the object counts,
    # e.g. "Total 3 (delta 1), reused 0" -- proof that only new objects travel.
    $tail = ($push | Where-Object { $_ -match '\S' } | Select-Object -Last 2) -join ' | '
    if ($LASTEXITCODE -eq 0) {
        Write-Log ('PUSH ok [' + $tail + ']')
        exit 0
    }

    # 3) rejected -> fetch just this branch, rebase, retry once
    Write-Log ('PUSH rejected; fetch + rebase: ' + (($push | Select-Object -Last 1)))
    git fetch -q origin $Branch 2>&1 | Out-Null
    git rebase -q "origin/$Branch" 2>&1 | Out-Null
    if ((Test-Path '.git/rebase-merge') -or (Test-Path '.git/rebase-apply')) {
        git rebase --abort 2>&1 | Out-Null
        Write-Log 'PUSH FAILED rebase conflict; aborted, needs manual fix'
        exit 1
    }
    $push2 = git push origin $Branch 2>&1
    if ($LASTEXITCODE -eq 0) {
        $tail2 = ($push2 | Where-Object { $_ -match '\S' } | Select-Object -Last 2) -join ' | '
        Write-Log ('PUSH ok (after rebase) [' + $tail2 + ']')
    } else {
        Write-Log ('PUSH FAILED ' + (($push2 | Select-Object -Last 2) -join ' | '))
    }
} catch {
    Write-Log ('ERROR ' + $_.Exception.Message)
}