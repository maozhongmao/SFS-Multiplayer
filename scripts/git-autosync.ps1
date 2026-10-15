# scripts/git-autosync.ps1
# Purpose: commit the latest source changes and push them to the beta branch.
# Used by: logon trigger + every 30 minutes -> pre-release branch + offsite backup.
# NOTE: keep this file ASCII-only. PowerShell 5.1 reads .ps1 as ANSI when there is no BOM,
#       and a non-ASCII comment has broken the parser here before (see build-release.ps1).
# Behaviour:
#   * only commits what .gitignore allows (game assemblies / saves / toolchains / build output stay out)
#   * does nothing when there are no changes (no empty commits)
#   * a failed push is logged only; it will not pop dialogs or retry in a loop
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

    $porcelain = git status --porcelain 2>&1
    if (-not $porcelain) { Write-Log 'OK no changes, skipped'; exit 0 }

    git add -A 2>&1 | Out-Null
    $staged = git diff --cached --name-only 2>&1
    if (-not $staged) { Write-Log 'OK staged empty (all changes ignored)'; exit 0 }
    $count = ($staged | Measure-Object).Count

    $msg = 'auto: ' + (Get-Date -Format 'yyyy-MM-dd HH:mm:ss') + ' (' + $count + ' files)'
    git commit -q -m $msg 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Log 'WARN commit failed'; exit 1 }
    Write-Log ('COMMIT ' + (git log --oneline -1))

    $push = git push origin $Branch 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Log 'PUSH ok'
    } else {
        Write-Log ('PUSH FAILED ' + (($push | Select-Object -Last 3) -join ' | '))
        Write-Log 'HINT check credentials (docs/git-autosync.md)'
    }
} catch {
    Write-Log ('ERROR ' + $_.Exception.Message)
}
