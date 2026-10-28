# SPDX-License-Identifier: GPL-3.0-or-later
# SFS patch installer - standalone app build (aapt2 + javac + d8 + uber-apk-signer)
#
# 版本号策略（按用户 2026-10-25 澄清）：
#   - 本文件构建的是【安装器自身】，版本号手动维护（VERSION 文件），不每构建 +1；
#     ./build.ps1 -Bump 才会把第 4 段 +1（发新功能/修 bug 时手动跑一次）。
#   - 真正需要每次 +1 的是【打给游戏的补丁包】的 versionCode，那部分在 patcher 里做
#     （见 Axml.bumpVersionCode）。
param([switch]$Bump)
$ErrorActionPreference = 'Continue'

$root      = 'D:/SFS-Multiplayer/android/patchapp'
$aapt2     = 'D:/HBuilderX/plugins/uts-development-android/static/win/aapt2.exe'
$androidJar= 'D:/HBuilderX/plugins/uts-development-android/static/android.jar'
$r8        = 'D:/HBuilderX/plugins/uniapp-runextension/utsServer_jar/r8-8.10.21.jar'
$signer    = 'D:/sfs-tools/android/uber-apk-signer.jar'
$ks        = Join-Path $root 'keystore/sfsmp-release.jks'
$build     = Join-Path $root 'build'
$out       = Join-Path $root 'out'

$verFile = Join-Path $root 'VERSION'
if (-not (Test-Path $verFile)) { [IO.File]::WriteAllText($verFile, '2.0.1.0') }
$ver = ([IO.File]::ReadAllText($verFile)).Trim()
$p = $ver.Split('.')
if ($p.Length -ne 4) { $p = @('2','0','1','0'); $ver = '2.0.1.0' }
if ($Bump) {
  $p[3] = [string]([int]$p[3] + 1)
  $ver = $p[0] + '.' + $p[1] + '.' + $p[2] + '.' + $p[3]
  [IO.File]::WriteAllText($verFile, $ver)
  Write-Output ('==> version bumped to ' + $ver)
} else {
  Write-Output ('==> version ' + $ver + ' (unchanged, use -Bump to raise)')
}
$vc = [int]$p[0]*1000000 + [int]$p[1]*10000 + [int]$p[2]*100 + [int]$p[3]

Write-Output '==> clean'
Remove-Item $build -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $build 'gen'),(Join-Path $build 'classes'),(Join-Path $build 'dex'),$out | Out-Null
Get-ChildItem $out -Filter '*.apk' -EA SilentlyContinue | Remove-Item -Force -EA SilentlyContinue

Write-Output '==> assets (hook.dex / libsfsmp.so / keystore)'
$assets = Join-Path $root 'assets'
Remove-Item $assets -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Force -Path $assets | Out-Null
$hookStub = Join-Path $build 'hookstub'
$hookCls  = Join-Path $build 'hookclasses'
$hookDex  = Join-Path $build 'hookdex'
New-Item -ItemType Directory -Force -Path $hookStub,$hookCls,$hookDex | Out-Null
$stubSrc = Join-Path $root 'hooksrc/stub/com/teamcuriosity/addons/ExtendedUnityActivity.java'
& javac -encoding UTF-8 --release 11 -nowarn -cp $androidJar -d $hookStub $stubSrc 2>&1 | Select-Object -First 6 | Out-String
$hookMain = Get-ChildItem (Join-Path $root 'hooksrc/main') -Recurse -Filter *.java | ForEach-Object { $_.FullName }
& javac -encoding UTF-8 --release 11 -nowarn -cp ($androidJar + ';' + $hookStub) -d $hookCls @hookMain 2>&1 | Select-Object -First 8 | Out-String
$hookClasses = Get-ChildItem $hookCls -Recurse -Filter *.class | ForEach-Object { $_.FullName }
& java -cp $r8 com.android.tools.r8.D8 --release --min-api 26 --lib $androidJar --output $hookDex @hookClasses 2>&1 | Select-Object -First 8 | Out-String
if (-not (Test-Path (Join-Path $hookDex 'classes.dex'))) { Write-Output '!! hook.dex build failed'; exit 1 }
Copy-Item (Join-Path $hookDex 'classes.dex') (Join-Path $assets 'hook.dex') -Force
$latestSo = Get-ChildItem 'D:/SFS-Multiplayer/android/so-out' -Filter 'v1*.so' | Sort-Object LastWriteTime | Select-Object -Last 1
Copy-Item $latestSo.FullName (Join-Path $assets 'libsfsmp.so') -Force
# 私钥/证书导出成 PKCS#8 + DER：Android 上 PKCS12 provider 解不了 JDK25 生成的 p12（PBES2/AES），
# 直接用 KeyFactory + CertificateFactory 构造最稳。
# 密钥/证书由 PackAssets 直接从 keys/ 加密写入 assets/，不经 PowerShell 复制（PowerShell 该步曾静默失败）
Write-Output ('    hook.dex ' + (Get-Item (Join-Path $assets 'hook.dex')).Length + ' B, libsfsmp.so ' + $latestSo.Name + ' ' + $latestSo.Length + ' B')

# ---- 轻壳：内置资源 XOR 加密（密钥 = 签名证书摘要派生，见 Pack.java）----
& javac -encoding UTF-8 --release 11 -d (Join-Path $build 'tools') (Join-Path $root 'tools/PackAssets.java') 2>&1 | Select-Object -First 5 | Out-String
& java -cp (Join-Path $build 'tools') PackAssets $assets @(
    (Join-Path $assets 'hook.dex'),
    (Join-Path $assets 'libsfsmp.so'),
    (Join-Path $root 'keys/sfsmp.key'),
    (Join-Path $root 'keys/sfsmp.crt')) 2>&1 | ForEach-Object { '    ' + $_ }
Write-Output '==> aapt2 compile'
& $aapt2 compile --dir (Join-Path $root 'res') -o (Join-Path $build 'res.zip') 2>&1 | Out-String

Write-Output '==> aapt2 link'
$apk = Join-Path $build 'patchapp-unsigned.apk'
$linkOut = & $aapt2 link -o $apk -I $androidJar --manifest (Join-Path $root 'AndroidManifest.xml') --auto-add-overlay -A (Join-Path $root 'assets') (Join-Path $build 'res.zip') --java (Join-Path $build 'gen') --min-sdk-version 26 --target-sdk-version 33 --version-code $vc --version-name $ver 2>&1
$linkOut | Out-String
if (-not (Test-Path $apk)) { Write-Output '!! aapt2 link failed'; exit 1 }

Write-Output '==> javac'
$srcs = @()
$srcs += (Get-ChildItem (Join-Path $root 'src') -Recurse -Filter *.java | ForEach-Object { $_.FullName })
$srcs += (Get-ChildItem (Join-Path $build 'gen') -Recurse -Filter *.java | ForEach-Object { $_.FullName })
$cpAll = $androidJar + ';' + (Join-Path $root 'lib/apksig-classes.jar')
$javacOut = & javac -encoding UTF-8 --release 11 -nowarn -cp $cpAll -d (Join-Path $build 'classes') @srcs 2>&1
$javacOut | Select-Object -First 15 | Out-String
if (-not (Get-ChildItem (Join-Path $build 'classes') -Recurse -Filter *.class -EA SilentlyContinue)) { Write-Output '!! javac failed'; exit 1 }

Write-Output '==> d8'
$classes = (Get-ChildItem (Join-Path $build 'classes') -Recurse -Filter *.class | ForEach-Object { $_.FullName })
$d8Out = & java -cp $r8 com.android.tools.r8.D8 --release --min-api 26 --lib $androidJar --output (Join-Path $build 'dex') @classes (Join-Path $root 'lib/apksig-classes.jar') 2>&1
$d8Out | Select-Object -First 10 | Out-String
if (-not (Test-Path (Join-Path $build 'dex/classes.dex'))) { Write-Output '!! d8 failed'; exit 1 }

Write-Output '==> merge classes.dex'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$dexBytes = [IO.File]::ReadAllBytes((Join-Path $build 'dex/classes.dex'))
$zip = [IO.Compression.ZipFile]::Open($apk, 'Update')
$old = $zip.GetEntry('classes.dex')
if ($old) { $old.Delete() }
$e = $zip.CreateEntry('classes.dex', [IO.Compression.CompressionLevel]::Optimal)
$st = $e.Open()
$st.Write($dexBytes, 0, $dexBytes.Length)
$st.Close()
$zip.Dispose()
Write-Output ('    dex ' + $dexBytes.Length + ' bytes')

Write-Output '==> sign (fixed keystore)'
$signOut = & java -jar $signer -a $apk -o $out --ks $ks --ksAlias sfsmp --ksPass sfsmp2025 --ksKeyPass sfsmp2025 2>&1
$signOut | Select-Object -Last 4 | Out-String

$signed = Get-ChildItem $out -Filter '*.apk' | Sort-Object LastWriteTime | Select-Object -Last 1
if ($signed) {
  Write-Output ('ARTIFACT: ' + $signed.FullName + '  ' + $signed.Length + ' B  v' + $ver + ' (' + $vc + ')')
} else { Write-Output '!! sign failed' }