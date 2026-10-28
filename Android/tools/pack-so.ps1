# SPDX-License-Identifier: GPL-3.0-or-later
param([Parameter(Mandatory=$true)][string]$SoName)
$ErrorActionPreference = 'Continue'
$so  = "D:\SFS-Multiplayer\Server\cpp\build\so-out\$SoName.so"
if (-not (Test-Path $so)) { Write-Output "FAIL: no $so"; exit 1 }
$dec  = 'D:\sfs-tools\apk\apk-decode'
$t2   = 'D:\sfs-tools\android'
$apk  = 'D:\sfs-tools\apk\live.apk'
$sdir = 'D:\sfs-tools\apk\livesigned'
Copy-Item $so (Join-Path $dec 'lib\arm64-v8a\libsfsmp.so') -Force
Write-Output ('packing ' + $SoName + ' (' + (Get-Item $so).Length + ' bytes) ...')
& java -jar (Join-Path $t2 'apktool.jar') b $dec -o $apk 2>&1 | Select-Object -Last 2 | ForEach-Object { '  ' + $_ }
if (-not (Test-Path $apk)) { Write-Output 'FAIL apktool'; exit 1 }
Remove-Item $sdir -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path $sdir | Out-Null
& java -jar (Join-Path $t2 'uber-apk-signer.jar') -a $apk -o $sdir 2>&1 | Select-Object -Last 2 | ForEach-Object { '  ' + $_ }
$sapk = Get-ChildItem $sdir -Filter '*.apk' -File -EA SilentlyContinue | Select-Object -First 1
if (-not $sapk) { Write-Output 'FAIL sign'; exit 1 }
Write-Output ('SIGNED_READY ' + $sapk.FullName + ' ' + $sapk.Length)
