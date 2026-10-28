# SPDX-License-Identifier: GPL-3.0-or-later
# Builds the SFS-Multiplayer patch installer pieces (desktop driver).
#   out\classes-tool   patcher classes (jvm)
#   out\classes-hook   hook + stub classes (android)
#   out\hook.dex       dex of the hook classes
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$out = Join-Path $root 'out'
$androidJar = 'D:/HBuilderX/plugins/uts-development-android/static/android.jar'
$r8 = 'D:/HBuilderX/plugins/uniapp-runextension/utsServer_jar/r8-8.10.21.jar'
$d8 = 'com.android.tools.r8.D8'

New-Item -ItemType Directory -Force -Path $out | Out-Null
foreach ($d in @('classes-tool', 'classes-hook', 'dex')) {
    $p = Join-Path $out $d
    if (Test-Path $p) { Remove-Item $p -Recurse -Force }
    New-Item -ItemType Directory -Force -Path $p | Out-Null
}

$toolSrc = Get-ChildItem (Join-Path $root 'src-tool') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName }
Write-Output "javac tool: $($toolSrc.Count) files"
& javac -nowarn -encoding UTF-8 -d (Join-Path $out 'classes-tool') $toolSrc
if ($LASTEXITCODE -ne 0) { throw 'javac tool failed' }

$hookSrc = @()
$hookSrc += (Get-ChildItem (Join-Path $root 'src-hook') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
$hookSrc += (Get-ChildItem (Join-Path $root 'src-stub') -Recurse -Filter '*.java' | ForEach-Object { $_.FullName })
Write-Output "javac hook: $($hookSrc.Count) files"
& javac -nowarn -encoding UTF-8 -source 11 -target 11 -classpath $androidJar -d (Join-Path $out 'classes-hook') $hookSrc 2>&1 | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'javac hook failed' }

$classFiles = Get-ChildItem (Join-Path $out 'classes-hook') -Recurse -Filter '*.class' | ForEach-Object { $_.FullName }
Write-Output "d8: $($classFiles.Count) classes"
& java -cp $r8 $d8 --min-api 23 --lib $androidJar --output (Join-Path $out 'dex') $classFiles
if ($LASTEXITCODE -ne 0) { throw 'd8 failed' }
Get-ChildItem (Join-Path $out 'dex') | Select-Object Name, Length | Format-Table -AutoSize | Out-String -Width 100
Write-Output 'BUILD_OK'
