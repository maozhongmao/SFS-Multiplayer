# build-one.ps1 - build ONE server target, clean readable errors.
# Usage: build-one.ps1 -Target linux-armv7
param([Parameter(Mandatory=$true)][string]$Target)

$repo = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$src  = Join-Path $repo "Server\cpp\src"
$qjs  = Join-Path $repo "Server\cpp\third_party\quickjs"
$zig  = Join-Path $repo "tools\zig-x86_64-windows-0.16.0\zig.exe"
$stage = Join-Path $repo "artifacts\release-V1.4.2.5"
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$map = @{
  "windows-x64"    = @{ arch = "";                     ext = ".exe" }
  "windows-arm64"  = @{ arch = "aarch64-windows-gnu";   ext = ".exe" }
  "linux-x64"      = @{ arch = "x86_64-linux-musl";     ext = "" }
  "linux-arm64"    = @{ arch = "aarch64-linux-musl";    ext = "" }
  "linux-armv7"    = @{ arch = "arm-linux-musleabihf";  ext = "" }
}
if (-not $map.ContainsKey($Target)) { throw ("unknown target: " + $Target) }
$arch = $map[$Target].arch

$objDir = Join-Path $repo ("Server\cpp\build\one-" + $Target)
Remove-Item $objDir -Recurse -Force -EA SilentlyContinue
New-Item -ItemType Directory -Path $objDir -Force | Out-Null

$defs = @("-DSFS_PLUGINS=1","-DSFS_PLUGINS_QUICKJS=1","-D_GNU_SOURCE","-DCONFIG_BIGNUM=0","-DCONFIG_STACK_CHECK=1")
$base = @("-Os","-DNDEBUG","-ffunction-sections","-fdata-sections")

$objs = New-Object System.Collections.ArrayList
$cppList = Get-ChildItem $src -Recurse -File -Filter *.cpp | Where-Object {
  $_.FullName -notlike "*attic*" -and $_.Name -ne "main_minimal.cpp" -and
  $_.Name -ne "sfs_server_min.cpp" -and $_.Name -ne "mini_server.cpp" }
$cList = @()
$cList += Get-ChildItem $src -Recurse -File -Filter *.c | Where-Object { $_.FullName -like "*third_party*" }
foreach ($n in @("quickjs.c","libregexp.c","libunicode.c","dtoa.c")) {
  $f = Join-Path $qjs $n
  if (Test-Path $f) { $cList += Get-Item $f }
}

Write-Host ("== " + $Target + " : " + $cppList.Count + " cpp, " + $cList.Count + " c ==")
$fail = 0
foreach ($f in $cppList) {
  $o = Join-Path $objDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
  [void]$objs.Add($o)
  if ($arch -ne "") { & $zig c++ -target $arch -std=c++17 @base @defs -I $src -I $qjs -c $f.FullName -o $o }
  else              { & $zig c++ -std=c++17 @base @defs -I $src -I $qjs -c $f.FullName -o $o }
  if ($LASTEXITCODE -ne 0) { Write-Host ("  CPP-FAIL " + $f.Name); $fail++; if ($fail -ge 2) { exit 2 } }
}
foreach ($f in $cList) {
  $o = Join-Path $objDir ($f.Directory.Name + "_" + $f.BaseName + ".o")
  [void]$objs.Add($o)
  if ($arch -ne "") { & $zig cc -target $arch -std=c11 @base @defs -I $src -I $qjs -c $f.FullName -o $o }
  else              { & $zig cc -std=c11 @base @defs -I $src -I $qjs -c $f.FullName -o $o }
  if ($LASTEXITCODE -ne 0) { Write-Host ("  C-FAIL " + $f.Name); $fail++; if ($fail -ge 2) { exit 2 } }
}
if ($fail -gt 0) { exit 2 }

$exe = Join-Path $stage ("SFS-Multiplayer-Server-" + $Target + $map[$Target].ext)
if ($Target -like "windows-*") {
  if ($arch -ne "") { & $zig c++ -target $arch @objs "-Wl,--gc-sections" "-s" -lws2_32 -o $exe }
  else              { & $zig c++ @objs "-Wl,--gc-sections" "-s" -lws2_32 -o $exe }
} else {
  & $zig c++ -target $arch @objs "-Wl,--gc-sections" "-s" -o $exe
}
if ($LASTEXITCODE -ne 0) { Write-Host "  LINK-FAIL"; exit 3 }
$item = Get-Item $exe
Write-Host ("  OK " + $item.Name + "  " + $item.Length + " bytes  sha256 " + (Get-FileHash $exe -Algorithm SHA256).Hash.Substring(0,16).ToLower())
