#!/usr/bin/env bash
# 编译 Android 注入库 libsfsmp.so（arm64-v8a + armeabi-v7a）
# 用法： NDK=/d/sfs-tools/ndk/android-ndk-r27c ./build.sh
set -euo pipefail
: "${NDK:?请先设置 NDK 环境变量，例如 NDK=/path/to/android-ndk-r27c}"

case "$(uname -s)" in
  MINGW*|MSYS*|CYGWIN*) HOST=windows-x86_64; SUF=.cmd ;;
  Darwin*)              HOST=darwin-x86_64;  SUF= ;;
  *)                    HOST=linux-x86_64;   SUF= ;;
esac
BIN="$NDK/toolchains/llvm/prebuilt/$HOST/bin"
[ -x "$BIN" ] || { echo "找不到 NDK 工具链: $BIN"; exit 1; }

CFLAGS="-shared -fPIC -O2 -I."
SRC="v64.c mp_net.c ikcp.c"

"$BIN/aarch64-linux-android24-clang$SUF" $CFLAGS -o libsfsmp-arm64.so $SRC
"$BIN/armv7a-linux-androideabi24-clang$SUF" $CFLAGS -o libsfsmp-armv7.so $SRC

ls -l libsfsmp-arm64.so libsfsmp-armv7.so
