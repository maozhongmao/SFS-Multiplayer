# build-server-exp.ps1 - server EXP profile (armv7 musl static, ROM first).
#
# Why a separate script:
#   build-release.ps1 ships the full release set with -O2 and no strip.
#   This one builds ONLY the board target (arm-linux-musleabihf) with the EXP
#   profile, into its own directory, so an experiment can never overwrite a
#   published artifact.
#
# Usage: powershell -ExecutionPolicy Bypass -File ServerCpp\build-server-exp.ps1
#
# The board runs the musl static flavor (ELF ET_EXEC, no ld-linux-armhf interp),
# so the EXP profile must use the same target to stay a drop-in replacement.
#
# Keep this file ASCII only: PowerShell 5.1 reads .ps1 as ANSI without a BOM,
# and one non-ASCII comment has broken the parser here before.

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$zig = Join-Path $root "tools\zig-x86_64-windows-0.16.0\zig.exe"
$src = Join-Path $root "ServerCpp\src"
$outDir = Join-Path $root "ServerCpp\build\exp-armv7"
$out = Join-Path $outDir "SFS-Multiplayer-Server-linux-armv7-exp"

if (-not (Test-Path $zig)) { throw "zig not found: $zig" }

$arch = "arm-linux-musleabihf"
# ROM-first flags. -Os plus section GC plus strip. No LTO on purpose: full LTO of
# this tree with zig did not finish in 9 minutes, while the size win is dominated
# by the other flags.
# NOTE: -fno-unwind-tables / -fno-asynchronous-unwind-tables are deliberately NOT
# used. They would break C++ exception unwinding (the server does catch parse and
# allocation errors), and they save only a few percent on top of the 19x win.
$cflags = @("-Os", "-ffunction-sections", "-fdata-sections", "-DNDEBUG")
$ldflags = @("-Wl,--gc-sections", "-s")

$cpps = Get-ChildItem $src -Recurse -File -Filter *.cpp | Where-Object {
    $_.FullName -notlike "*attic*" -and
    $_.Name -ne "main_minimal.cpp" -and
    $_.Name -ne "sfs_server_min.cpp" -and
    $_.Name -ne "mini_server.cpp"
}
$cFiles = Get-ChildItem $src -Recurse -File -Include *.c | Where-Object { $_.FullName -like "*third_party*" }

if (Test-Path $outDir) { Remove-Item $outDir -Recurse -Force }
New-Item -ItemType Directory -Path $outDir -Force | Out-Null
$objs = New-Object System.Collections.ArrayList

Write-Host ("== server EXP profile (" + $arch + ") ==")
foreach ($f in $cpps) {
    $o = Join-Path $outDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
    [void]$objs.Add($o)
    & $zig c++ -target $arch -std=c++17 @cflags -I $src -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { throw ("compile failed: " + $f.Name) }
}
foreach ($f in $cFiles) {
    $o = Join-Path $outDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
    [void]$objs.Add($o)
    & $zig cc -target $arch -std=c11 @cflags -I $src -c $f.FullName -o $o
    if ($LASTEXITCODE -ne 0) { throw ("compile failed: " + $f.Name) }
}
& $zig c++ -target $arch @objs @ldflags -o $out
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Remove-Item (Join-Path $outDir "*.o") -Force
$item = Get-Item $out
Write-Host ("   " + $item.Name + "  " + $item.Length + " bytes")
Write-Host ("   sha256 " + (Get-FileHash $out -Algorithm SHA256).Hash.ToLower())
