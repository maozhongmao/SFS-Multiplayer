# build-release.ps1 - produce the complete release set in one shot.
#
# Why this script exists:
#   1) The 1.5 and 1.6 client packages share one OutputPath, and the MSBuild
#      incremental clean deletes whichever was not produced this run.
#      Building 1.5 after 1.6 makes the 1.6 dll disappear.
#      So: build each separately, stage, then merge.
#   2) The server has four targets (windows-x64 / linux-x64 / linux-arm64 / linux-armv7),
#      with the bundled zig. zig refuses to mix C and C++ in one invocation,
#      so C and C++ are compiled to objects first and linked after.
#   3) Releases must ship SHA256 sums (see wiki/Build-and-Release.md).
#
# Usage:  powershell -ExecutionPolicy Bypass -File build-release.ps1 -Version 1.3.0.7

param([Parameter(Mandatory=$true)][string]$Version)

$ErrorActionPreference = "Stop"
$repo   = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path   # 仓库根（脚本在 Server/cpp/ 下）
$root   = $repo
$zig    = Join-Path $root "tools\zig-x86_64-windows-0.16.0\zig.exe"
$src    = Join-Path $root "Server\cpp\src"
# quickjs 头目录（插件引擎需要；关闭 SFS_PLUGINS 时不会被用到，多一个 -I 无副作用）
$qjsInc = Join-Path $root "Server\cpp\third_party\quickjs"
$proj   = Join-Path $root "Client\src\MultiplayerSFS.Client\MultiplayerSFS.Client.csproj"
$relDir = Join-Path $root ("Client\release-V" + $Version)
$stage  = Join-Path $root ("artifacts\release-V" + $Version)

if (-not (Test-Path $zig))  { throw "zig not found: $zig" }
if (-not (Test-Path $proj)) { throw "client project not found: $proj" }

# The csproj is the single source of truth for the version; verify it matches.
$csprojText = Get-Content $proj -Raw -Encoding UTF8
if ($csprojText.IndexOf($Version) -lt 0) {
    throw "csproj does not contain version $Version - update AssemblyName/Version/OutputPath first"
}

New-Item -ItemType Directory -Path $stage -Force | Out-Null

# ---------------- client ----------------
Write-Host "== client 1.6 / 1.5 =="
$packages = @(
    @{ gv = "v16"; suffix = "1.6" },
    @{ gv = "v15"; suffix = "1.5" }
)
foreach ($p in $packages) {
    if ($p.gv -eq "v15") {
        & dotnet build $proj -c Release -p:GameVersion=v15 -v quiet -nologo
    } else {
        & dotnet build $proj -c Release -v quiet -nologo
    }
    if ($LASTEXITCODE -ne 0) { throw ("client " + $p.suffix + " build failed") }
    $base = "SFS-Multiplayer-" + $p.suffix + "-V" + $Version
    Copy-Item (Join-Path $relDir ($base + ".dll")) $stage -Force
    Copy-Item (Join-Path $relDir ($base + ".pdb")) $stage -Force
    Write-Host ("   " + $base + ".dll")
}
# Put both packages back into the release dir (each build prunes the other).
Get-ChildItem $stage -Filter "SFS-Multiplayer-1.*.dll" | Copy-Item -Destination $relDir -Force
Get-ChildItem $stage -Filter "SFS-Multiplayer-1.*.pdb" | Copy-Item -Destination $relDir -Force

# ---------------- server ----------------
Write-Host "== server windows-x64 / linux-x64 / linux-arm64 / linux-armv7 =="
$cpps = Get-ChildItem $src -Recurse -File -Filter *.cpp | Where-Object {
    $_.FullName -notlike "*attic*" -and
    $_.Name -ne "main_minimal.cpp" -and
    $_.Name -ne "sfs_server_min.cpp" -and
    $_.Name -ne "mini_server.cpp"
}
$cFiles = @()
$cFiles += Get-ChildItem $src -Recurse -File -Include *.c | Where-Object { $_.FullName -like "*third_party*" }
# quickjs 放在 Server/cpp/third_party/quickjs（在 $src 之外）；只收库文件，
# qjs.c/qjsc.c 等自带 main 的工具有 duplicate symbol 风险。
$qjsLib = @('quickjs.c','libregexp.c','libunicode.c','dtoa.c')
$cFiles += Get-ChildItem (Join-Path $root "Server\cpp\third_party\quickjs") -File -Filter *.c -EA SilentlyContinue |
    Where-Object { $qjsLib -contains $_.Name }

# Four targets: Windows, the usual VPS case (linux x64), modern ARM boxes and Raspberry Pi 4+
# (arm64), plus the board used by this project (armv7). All Linux builds are statically
# linked against musl, so they run as-is without depending on the target glibc version.
# NOTE: keep this file ASCII only. PowerShell 5.1 reads .ps1 as ANSI when there is no BOM,
# and a non-ASCII comment has broken the parser here before.
$targets = @(
    @{ arch = "";                     name = "windows-x64"; ext = ".exe" },
    @{ arch = "aarch64-windows-gnu";  name = "windows-arm64"; ext = ".exe" },
    @{ arch = "x86_64-linux-musl";    name = "linux-x64";   ext = "" },
    @{ arch = "aarch64-linux-musl";   name = "linux-arm64"; ext = "" },
    @{ arch = "arm-linux-musleabihf"; name = "linux-armv7"; ext = "" }
)
foreach ($t in $targets) {
    $objDir = Join-Path $env:TEMP ("sfsbuild-" + $t.name)
    if (Test-Path $objDir) { Remove-Item $objDir -Recurse -Force }
    New-Item -ItemType Directory -Path $objDir -Force | Out-Null
    $objs = New-Object System.Collections.ArrayList

    # Object name = parent dir + base name; that is unique (protocol/packets.cpp vs server/packets.cpp).
    foreach ($f in $cpps) {
        $o = Join-Path $objDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
        [void]$objs.Add($o)
        if ($t.arch -ne "") { & $zig c++ -target $t.arch -std=c++17 -Os -DNDEBUG -ffunction-sections -fdata-sections -I $src -I (Join-Path $root "Server\cpp\third_party\quickjs") -DSFS_PLUGINS=1 -DSFS_PLUGINS_QUICKJS=1 -D_GNU_SOURCE -DCONFIG_BIGNUM=0 -DCONFIG_STACK_CHECK=1 -c $f.FullName -o $o }
        else                { & $zig c++ -std=c++17 -Os -DNDEBUG -ffunction-sections -fdata-sections -I $src -I (Join-Path $root "Server\cpp\third_party\quickjs") -DSFS_PLUGINS=1 -DSFS_PLUGINS_QUICKJS=1 -D_GNU_SOURCE -DCONFIG_BIGNUM=0 -DCONFIG_STACK_CHECK=1 -c $f.FullName -o $o }
        if ($LASTEXITCODE -ne 0) { throw ("server compile failed: " + $f.Name) }
    }
    foreach ($f in $cFiles) {
        $o = Join-Path $objDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
        [void]$objs.Add($o)
        if ($t.arch -ne "") { & $zig cc -target $t.arch -std=c11 -Os -DNDEBUG -ffunction-sections -fdata-sections -I $src -I (Join-Path $root "Server\cpp\third_party\quickjs") -D_GNU_SOURCE -DCONFIG_BIGNUM=0 -DCONFIG_STACK_CHECK=1 -c $f.FullName -o $o }
        else                { & $zig cc -std=c11 -Os -DNDEBUG -ffunction-sections -fdata-sections -I $src -I (Join-Path $root "Server\cpp\third_party\quickjs") -D_GNU_SOURCE -DCONFIG_BIGNUM=0 -DCONFIG_STACK_CHECK=1 -c $f.FullName -o $o }
        if ($LASTEXITCODE -ne 0) { throw ("server compile failed: " + $f.Name) }
    }

    $exe = Join-Path $stage ("SFS-Multiplayer-Server-" + $t.name + $t.ext)
    # NOTE: quote '-Wl,--gc-sections' - PowerShell parses the comma as a parameter list otherwise.
    if ($t.arch -eq "")             { & $zig c++ @($objs) -lws2_32 '-Wl,--gc-sections' '-s' -o $exe }
    elseif ($t.name -like "windows-*") { & $zig c++ -target $t.arch @($objs) -lws2_32 '-Wl,--gc-sections' '-s' -o $exe }
    else                            { & $zig c++ -target $t.arch @($objs) '-Wl,--gc-sections' '-s' -o $exe }
    if ($LASTEXITCODE -ne 0) { throw ("server link failed: " + $t.name) }
    Remove-Item $objDir -Recurse -Force
    Write-Host ("   SFS-Multiplayer-Server-" + $t.name + $t.ext)
}

# ---------------- checksums ----------------
$sums = Join-Path $stage "SHA256SUMS.txt"
$lines = New-Object System.Collections.ArrayList
Get-ChildItem $stage -File | Where-Object { $_.Name -ne "SHA256SUMS.txt" } | ForEach-Object {
    $h = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLower()
    [void]$lines.Add($h + "  " + $_.Name)
}
$lines | Set-Content $sums -Encoding ASCII

Write-Host ""
Write-Host ("release dir: " + $stage)
Get-ChildItem $stage | ForEach-Object { Write-Host ("   " + $_.Name + "   " + $_.Length) }
