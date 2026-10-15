@echo off
setlocal
set "ROOT=%~dp0"
if not defined CXX set "CXX=clang++"
where "%CXX%" >nul 2>nul
if errorlevel 1 (
  echo C++ compiler not found. Set CXX to clang++ or g++ before running.
  exit /b 2
)
if not exist "%ROOT%build" mkdir "%ROOT%build"
"%CXX%" -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic ^
  "%ROOT%src\protocol\codec.cpp" ^
  "%ROOT%src\platform\socket_platform.cpp" ^
  "%ROOT%src\net\mini_server.cpp" ^
  "%ROOT%src\main.cpp" ^
  -I"%ROOT%src" -lws2_32 -o "%ROOT%build\sfs-server-cpp.exe"
if errorlevel 1 exit /b %errorlevel%
"%CXX%" -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wpedantic ^
  "%ROOT%src\protocol\codec.cpp" ^
  "%ROOT%tests\protocol_codec_smoke.cpp" ^
  -I"%ROOT%src" -o "%ROOT%build\protocol_codec_smoke.exe"
if errorlevel 1 exit /b %errorlevel%
"%ROOT%build\protocol_codec_smoke.exe"
if errorlevel 1 exit /b %errorlevel%
echo BUILD_AND_CODEC_SMOKE_OK
