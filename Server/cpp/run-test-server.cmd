@echo off
rem 本机测试用服务端启动器（Windows）
rem 用法：run-test-server.cmd [端口]     默认 9806
rem 存档写到 test-run\state.json，和目标设备上的存档完全分开。
setlocal
set PORT=%~1
if "%PORT%"=="" set PORT=9806
cd /d "%~dp0"

if not exist "%~dp0test-run" mkdir "%~dp0test-run"
set STATE=%~dp0test-run\state.json

echo ============================================
echo  SFS Multiplayer 本地测试服务端
echo    端口     : %PORT%
echo    存档     : %STATE%
echo    客户端填 : 127.0.0.1:%PORT%
echo    控制台   : 直接在本窗口输入 help / status / players / debris / stop
echo ============================================
echo.

"%~dp0build\zig-win\sfs-server-kcp-win.exe" --port %PORT% --max-players 16 --state "%STATE%"

echo.
echo [已退出] 服务端进程结束。
pause
