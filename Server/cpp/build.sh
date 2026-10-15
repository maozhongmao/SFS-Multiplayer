#!/usr/bin/env bash
# 全量构建服务端：
#   arm-linux-gnueabihf -> build/zig-arm/sfs-server-kcp-arm   （板子 9806 生产）
#   x86_64-windows-gnu  -> build/zig-win/sfs-server-kcp-win.exe（本机测试）
#
# 必须全量编译：只要改过 server.hpp 里的结构体（如 Connection），只重编部分 .cpp 会让
# 各编译单元的结构体布局不一致 -> 内存踩坏 -> 运行时随机崩溃（已踩过一次）。
set -e
cd "$(dirname "$0")"
ZIG="${ZIG:-D:/SFS-Multiplayer/tools/zig-x86_64-windows-0.16.0/zig.exe}"

SRCS_CPP="src/platform/socket_platform.cpp src/net/kcp_transport.cpp src/net/lidgren_buffer.cpp
src/protocol/codec.cpp src/protocol/handshake.cpp src/protocol/packets.cpp src/protocol/server_info.cpp
src/protocol/server_version.cpp
src/protocol/types.cpp src/protocol/world_loader.cpp src/server/connection.cpp src/server/frames.cpp
src/server/initial_state.cpp src/server/lifecycle.cpp src/server/p2p.cpp src/server/packets.cpp
src/server/commands.cpp src/server/transport.cpp src/server/util.cpp src/server/world.cpp
src/server/join_guard.cpp src/server/dock.cpp src/server/dock_math.cpp
src/state_store.cpp src/server/rocket_blob.cpp src/main.cpp"

# 编译/链接档位：
#   默认（不设 EXP）= 生产档 -O2，与历史产物完全一致。
#   EXP=1            = 服务端实验档（ROM 优先）：-Os + LTO + 函数/数据分段 + gc-sections + strip。
#                      验收看 build/exp-arm 的体积与板上 RSS，两档二进制行为必须一致。
COMMON_FLAGS="-O2"
LINK_FLAGS=""
if [ "$EXP" = "1" ]; then
    # 注：EXP 档不启用 -flto。实测 zig 对 armv7 全量 LTO 超过 9 分钟仍未编完，
    # 而体积收益的大头来自 -Os + -ffunction-sections/-fdata-sections + --gc-sections + strip。
    COMMON_FLAGS="-Os -ffunction-sections -fdata-sections -fno-unwind-tables -fno-asynchronous-unwind-tables"
    LINK_FLAGS="-Wl,--gc-sections -s"
fi

build_target() {
    local target="$1" outdir="$2" outfile="$3"
    shift 3
    rm -rf "$outdir"
    mkdir -p "$outdir"
    "$ZIG" cc -target "$target" $COMMON_FLAGS -Isrc -I. -DNDEBUG -c src/third_party/kcp/ikcp.c -o "$outdir/ikcp.o"
    # miniz 是 C 源码（deflate/inflate）。上游 3.1.2 是拆分版：miniz.c 只有公共部分，
    # 压缩/解压实现分别在 miniz_tdef.c / miniz_tinfl.c，三个都要编。
    # 必须用 cc 驱动；塞进 SRCS_CPP 会报 "invalid argument '-std=c++17' not allowed with 'C'"
    for f in src/third_party/miniz/miniz.c src/third_party/miniz/miniz_tdef.c src/third_party/miniz/miniz_tinfl.c; do
        "$ZIG" cc -target "$target" $COMMON_FLAGS -Isrc -I. -DNDEBUG -c "$f" -o "$outdir/$(basename "$f" .c).o"
    done
    for f in $SRCS_CPP; do
        local bn
        bn=$(basename "$f" .cpp)
        if [ "$f" = "src/protocol/packets.cpp" ]; then bn=protocol_packets; fi
        "$ZIG" c++ -target "$target" -std=c++17 $COMMON_FLAGS -Isrc -I. -DNDEBUG -c "$f" -o "$outdir/$bn.o"
    done
    "$ZIG" c++ -target "$target" $COMMON_FLAGS -o "$outfile" "$outdir"/*.o "$@" $LINK_FLAGS
    echo "  构建完成: $outfile"
}

# EXP 档只出板子那一个目标，跳过生产五平台，避免误覆盖已发布产物。
if [ "$EXP" = "1" ]; then
    echo "=== EXP（服务端实验档 · ROM 优先）==="
    build_target arm-linux-gnueabihf build/exp-arm build/exp-arm/sfs-server-kcp-arm -lpthread
    ls -l build/exp-arm/sfs-server-kcp-arm
    exit 0
fi

echo "=== ARM（板子）==="
build_target arm-linux-gnueabihf build/zig-arm build/zig-arm/sfs-server-kcp-arm -lpthread
echo "=== Windows（本机测试）==="
build_target x86_64-windows-gnu build/zig-win build/zig-win/sfs-server-kcp-win.exe -lws2_32

# ---- 发布产物（V1.2.5.1 五平台）：命名与发布说明里的下载列表严格一致 ----
# 版本号改动时请同步改这里，不要覆盖上一版的 dist 目录（已发布产物不可覆盖）。
DIST="build/dist-v1.2.5.1"; mkdir -p "$DIST"
echo "=== 发布产物（五平台）==="
build_target arm-linux-gnueabihf   build/rel-armv7    "$DIST/SFS-Multiplayer-Server-linux-armv7"      -lpthread
build_target x86_64-linux-gnu      build/rel-x64      "$DIST/SFS-Multiplayer-Server-linux-x64"        -lpthread
build_target aarch64-linux-gnu     build/rel-arm64    "$DIST/SFS-Multiplayer-Server-linux-arm64"      -lpthread
build_target aarch64-windows-gnu   build/rel-winarm64 "$DIST/SFS-Multiplayer-Server-windows-arm64.exe" -lws2_32
build_target x86_64-windows-gnu    build/rel-win64    "$DIST/SFS-Multiplayer-Server-windows-x64.exe"  -lws2_32
echo "=== 五平台 SHA-256 ==="
sha256sum "$DIST"/* 2>/dev/null

echo "=== SHA-256 ==="
sha256sum build/zig-arm/sfs-server-kcp-arm build/zig-win/sfs-server-kcp-win.exe
