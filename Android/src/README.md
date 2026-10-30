# SFS-Multiplayer · Android 注入层源码

官方《Spaceflight Simulator》1.6.00.22 的联机 mod 在 Android 上的 **native 注入层**。
本目录只包含我们自己写的代码 + 第三方 KCP 实现，**不包含游戏本体、也不包含任何反编译产物**。

## 文件

| 文件 | 作用 |
|---|---|
| `v64.c` | 主逻辑：JNI 入口、il2cpp 反射（类/方法/字段解析）、游戏内 UI 面板与按钮、火箭结构读写、场景跳转与联机状态驱动 |
| `mp_net.c` | 网络层：KCP 会话、握手（Hello/HelloAck/JoinRequest）、帧编解码、位姿与结构上传、心跳与重连 |
| `mp_types.h` | 两者共享的类型与常量 |
| `ikcp.c` / `ikcp.h` | 第三方 KCP 实现（skywind3000/kcp，MIT），见 `THIRD-PARTY.md` |

## 构建

需要 Android NDK（r27 验证过）。产物是注入进游戏的 `libsfsmp.so`：

```bash
NDK=/path/to/android-ndk-r27c
BIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin

# arm64-v8a
$BIN/aarch64-linux-android24-clang -shared -fPIC -O2 \
    -I. -o libsfsmp-arm64.so v64.c mp_net.c ikcp.c

# armeabi-v7a
$BIN/armv7a-linux-androideabi24-clang -shared -fPIC -O2 \
    -I. -o libsfsmp-armv7.so v64.c mp_net.c ikcp.c
```

Windows 下把 `linux-x86_64` 换成 `windows-x86_64`，并把 `aarch64-linux-android24-clang` 换成同名 `.cmd`。

也可以用本目录的 `build.sh`：`NDK=... ./build.sh`。

## 与安装器的关系

安装器（`android/patchapp`）会把这里编出来的 `libsfsmp.so` 和 `hook.dex` 一起塞进官方 APK：

1. 改写 `AndroidManifest.xml` 的启动类为 `com.sfsmp.patch.HookActivity`、包名改为 `com.sfsmp.sfs`（共存模式）；
2. 改写底座 `classes2.dex`（跳板字符串 + 注入 Bridge/Ticker 类）；
3. 追加 `lib/<abi>/libsfsmp.so` 与 `classes4.dex`；
4. 重签名。

`HookActivity.onCreate` 调 `Bridge.bridgeInit()` → `System.loadLibrary("sfsmp")` → `JNI_OnLoad` → 本目录源码开始工作。

## 服务器地址

**代码里没有任何默认服务器地址。** 玩家在联机弹窗里填什么就连什么：

- 地址为空 → 不连接、不进世界，页面标签提示「请先填写服务器地址（IP:端口），再点连接」；
- 支持 `IP:端口`、`[IPv6]:端口`、纯 IPv6（默认端口 9806）、`host:port`、纯 IPv4。

## 版本适配

代码里写死的 RVA 与混淆类名对应 **SFS 1.6.00.22（versionCode 936，arm64-v8a）**。
官方更新版本后这些偏移会失效，需要重新做一次元数据核对后更新常量。

## 许可

本目录代码以 GPL-3.0-or-later 发布（每个源文件带 SPDX 头）；
第三方 KCP 为 MIT，详见 `THIRD-PARTY.md`。