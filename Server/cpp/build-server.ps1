# Build the C++ server (sfs-server-kcp) in one shot.
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File build-server.ps1 [-Clean]
param([switch]$Clean)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$cmake = (Get-Command cmake -ErrorAction SilentlyContinue)
if (-not $cmake) {
    Write-Host 'ERROR: cmake not found in PATH.' -ForegroundColor Red
    Write-Host 'Install CMake (https://cmake.org/download/) or run this on the machine that built the server before.'
    exit 1
}
Write-Host ("cmake: " + $cmake.Source)

if ($Clean -and (Test-Path build)) { Remove-Item build -Recurse -Force }

$generator = if (Get-Command ninja -ErrorAction SilentlyContinue) { 'Ninja' } else { 'default' }
if ($generator -eq 'Ninja') {
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
} else {
    cmake -S . -B build
}
if ($LASTEXITCODE -ne 0) { Write-Host 'configure FAILED' -ForegroundColor Red; exit $LASTEXITCODE }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { Write-Host 'build FAILED' -ForegroundColor Red; exit $LASTEXITCODE }

$exe = Get-ChildItem build -Recurse -Filter 'sfs-server-kcp*' -File |
       Where-Object { $_.Extension -in '.exe','' } | Select-Object -First 1
Write-Host ('OK -> ' + $exe.FullName) -ForegroundColor Green
