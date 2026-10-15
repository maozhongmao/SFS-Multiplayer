cmake_minimum_required(VERSION 3.16)
project(SfsMultiplayerServerCpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sfs_protocol STATIC
    src/protocol/codec.cpp
    src/protocol/server_version.cpp
)
target_include_directories(sfs_protocol PUBLIC src)
target_compile_options(sfs_protocol PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

add_executable(sfs-server-kcp
    src/main.cpp
    src/platform/socket_platform.cpp
    src/net/kcp_transport.cpp
    src/protocol/handshake.cpp
    src/protocol/server_info.cpp
    src/server/connection.cpp
    src/server/frames.cpp
    src/server/initial_state.cpp
    src/server/lifecycle.cpp
    src/server/p2p.cpp
    src/server/packets.cpp
    src/server/transport.cpp
    src/server/util.cpp
    src/server/commands.cpp
    src/server/world.cpp
    src/server/dock.cpp
    src/server/dock_math.cpp
    src/third_party/kcp/ikcp.c
)
target_include_directories(sfs-server-kcp PRIVATE src .)
target_link_libraries(sfs-server-kcp PRIVATE sfs_protocol)
target_compile_options(sfs-server-kcp PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

if (WIN32)
    target_link_libraries(sfs-server-kcp PRIVATE ws2_32)
endif()

add_executable(protocol_codec_smoke tests/protocol_codec_smoke.cpp)
target_link_libraries(protocol_codec_smoke PRIVATE sfs_protocol)
target_compile_options(protocol_codec_smoke PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic>
)

enable_testing()
add_test(NAME protocol_codec_smoke COMMAND protocol_codec_smoke)
