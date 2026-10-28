# SPDX-License-Identifier: GPL-3.0-or-later
<#
  make-patcher.ps1 -- SFS ?????????????????
  ????????APK -> ?????-> ??? patcher smali -> ?????? Activity -> ?????-> ???
  ??????????????? adb install????????????

  ?????    .\make-patcher.ps1                # ????????25 ???
    .\make-patcher.ps1 -SkipDecode     # ?????????????????smali ?????? 20 ??
  ?????ndroid/patcher/out/patcher-signed.apk
#>
[CmdletBinding()]
param(
  [string]$OfficialApk = 'D:/sfs-tools/apk/official-sfs.apk',
  [string]$WorkDir     = 'D:/sfs-tools/apk/apk-decode',
  [string]$Apktool     = 'D:/sfs-tools/android/apktool.jar',
  [string]$Signer      = 'D:/sfs-tools/android/uber-apk-signer.jar',
  [string]$Root        = 'D:/SFS-Multiplayer/android/patcher',
  [switch]$SkipDecode
)

$ErrorActionPreference = 'Stop'

function Step($m) { Write-Host "==> $m" -ForegroundColor Cyan }
function Ok($m)   { Write-Host "    ok  $m" -ForegroundColor Green }
function Bad($m)  { Write-Host "    !!  $m" -ForegroundColor Red }

$smaliSrc = Join-Path $Root 'smali'
$buildDir = Join-Path $Root 'build'
$outDir   = Join-Path $Root 'out'
New-Item -ItemType Directory -Force -Path $buildDir, $outDir | Out-Null

# 1-inflate
if (-not $SkipDecode) {
  Step 'inflate official apk'
  if (-not (Test-Path $OfficialApk)) { Bad "missing apk: $OfficialApk"; exit 1 }
  if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
  & java -jar $Apktool d -f $OfficialApk -o $WorkDir
  if ($LASTEXITCODE -ne 0) { Bad 'apktool d failed'; exit 1 }
  Ok "inflated: $WorkDir"
} else {
  Step 'reuse inflated tree'
  if (-not (Test-Path $WorkDir)) { Bad "no tree at $WorkDir"; exit 1 }
  Ok $WorkDir
}

# 2-inject
Step 'inject patcher smali'
$dest = Join-Path $WorkDir 'smali_classes2'
if (-not (Test-Path $dest)) { New-Item -ItemType Directory -Force -Path $dest | Out-Null }

$copied = 0
Get-ChildItem $smaliSrc -Recurse -Filter '*.smali' | ForEach-Object {
  $rel = $_.FullName.Substring($smaliSrc.Length).TrimStart('\','/')
  $to  = Join-Path $dest $rel
  $toDir = Split-Path $to -Parent
  if (-not (Test-Path $toDir)) { New-Item -ItemType Directory -Force -Path $toDir | Out-Null }
  Copy-Item $_.FullName $to -Force
  $copied++
}
# 清掉历史尝试留下的类，保证产物里只有 patcher/smali 下的东西
$legacy = Join-Path $dest 'com/sfsmp'
$keep = @(Get-ChildItem $smaliSrc -Recurse -Filter '*.smali' | ForEach-Object { $_.FullName.Substring($smaliSrc.Length).TrimStart('\','/') })
Get-ChildItem $legacy -Recurse -Filter '*.smali' -EA SilentlyContinue | ForEach-Object {
  $rel = $_.FullName.Substring($dest.Length).TrimStart('\','/')
  if ($keep -notcontains $rel) { Bad "drop stale $rel"; Remove-Item $_.FullName -Force }
}
Ok "copied $copied smali -> $dest"

# 3-lifecycle
# patcher/smali/com/teamcuriosity/addons/ExtendedUnityActivity.smali 是我们维护的注入版，
# 存在就以它为真相源覆盖工作树（原版逻辑已完整保留在里面）。
Step 'wire launcher activity'
$target = Join-Path $WorkDir 'smali_classes2/com/teamcuriosity/addons/ExtendedUnityActivity.smali'
if (-not (Test-Path $target)) { Bad "missing $target"; exit 1 }

$override = Join-Path $smaliSrc 'com/teamcuriosity/addons/ExtendedUnityActivity.smali'
$needApply = 'Lcom/sfsmp/patch/Installer;->apply()V'

if (Test-Path $override) {
  Copy-Item $override $target -Force
  Ok 'activity smali overridden from patcher/smali'
} else {

$txt = Get-Content $target -Raw

if ($txt -match [regex]::Escape($needApply)) {
  Ok 'already wired, skip'
} else {
  $hasOnCreate = $txt -match '\.method\s+public\s+onCreate\(Landroid/os/Bundle;\)V'
  if (-not $hasOnCreate) {
    $lines = @(
      '.method public onCreate(Landroid/os/Bundle;)V',
      '    .locals 0',
      '',
      '    invoke-super {p0, p1}, Lcom/unity3d/player/UnityPlayerActivity;->onCreate(Landroid/os/Bundle;)V',
      '',
      '    invoke-static {}, Lcom/sfsmp/patch/Installer;->apply()V',
      '',
      '    return-void',
      '.end method',
      '',
      ''
    )
    $inject = ($lines -join "`r`n")
    $idx = $txt.IndexOf('.method')
    if ($idx -lt 0) { Bad 'no .method in activity smali'; exit 1 }
    $txt = $txt.Substring(0, $idx) + $inject + $txt.Substring($idx)
    Ok 'added onCreate + Installer.apply()'
  } else {
    $pat = '(\.method\s+public\s+onCreate\(Landroid/os/Bundle;\)V[\s\S]*?invoke-super[^\r\n]*[\r\n]+)'
    $m = [regex]::Match($txt, $pat)
    if (-not $m.Success) { Bad 'no invoke-super inside onCreate'; exit 1 }
    $insert = "    invoke-static {}, Lcom/sfsmp/patch/Installer;->apply()V`r`n`r`n"
    $txt = $txt.Insert($m.Index + $m.Length, $insert)
    Ok 'inserted Installer.apply() after invoke-super'
  }
  Set-Content -Path $target -Value $txt -Encoding UTF8 -NoNewline
}
}

# 4-verify
Step 'verify injection'
$entry = Join-Path $dest 'com/sfsmp/patch/Installer.smali'
if (-not (Test-Path $entry)) { Bad 'Installer.smali not copied'; exit 1 }
$stale = Get-ChildItem $WorkDir -Recurse -Filter '*.bak' -EA SilentlyContinue
if ($stale) { $stale | ForEach-Object { Bad "drop stale $($_.FullName)"; Remove-Item $_.FullName -Force } }
Ok 'entry class in place'

# 5-repack
Step 'apktool build'
$unsigned = Join-Path $buildDir 'patcher-unsigned.apk'
if (Test-Path $unsigned) { Remove-Item $unsigned -Force }
$sw = [Diagnostics.Stopwatch]::StartNew()
& java -jar $Apktool b $WorkDir -o $unsigned
if ($LASTEXITCODE -ne 0) { Bad 'apktool b failed'; exit 1 }
$sw.Stop()
Ok ("built {0:N1}s  {1:N0} B" -f $sw.Elapsed.TotalSeconds, (Get-Item $unsigned).Length)

# 6-sign
Step 'uber-apk-signer'
# java 往 stderr 写 WARNING 时 PowerShell 会当错误抛，这里只按产物判定成败
$priorStderr = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
  & java -jar $Signer --apks $unsigned --out $outDir --allowResign 2>&1 | Out-Null
} finally {
  $ErrorActionPreference = $priorStderr
}

$signed = Get-ChildItem $outDir -Filter '*Signed.apk' -EA SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $signed) { Bad "no signed apk in $outDir"; exit 1 }
$final = Join-Path $outDir 'patcher-signed.apk'
if (Test-Path $final) { Remove-Item $final -Force }
Move-Item $signed.FullName $final -Force
Ok ("signed {0:N0} B" -f (Get-Item $final).Length)

Write-Host ''
Write-Host "ARTIFACT: $final" -ForegroundColor Yellow
Write-Host ('INSTALL : adb install -r --no-incremental "' + $final + '"') -ForegroundColor Yellow
