# Server — 联机服务端

Spaceflight Simulator 的独立联机服务端（不依赖游戏进程）。仓库里有**两条线**：

| 目录 | 是什么 | 状态 |
|---|---|---|
| `cpp/` | **C++ 服务端（当前主线）**，UDP/KCP，交叉编译到 x64 / arm64 / **armv7(musl)** | 线上与目标设备跑的就是它 |
| `src/` | .NET(C#) 服务端原型（1.2.0 时代，Lidgren 线协议）| 仍在仓库里，但不是主线 |

## 目录结构

```
Server/
├─ cpp/                     C++ 服务端
│   ├─ src/                 源码（KCP/UDP 传输、世界与火箭同步、权威分配、P2P、插件系统）
│   ├─ tests/               线格式/一致性测试
│   ├─ third_party/quickjs/ 插件引擎（quickjs-ng，MIT，见该目录 VENDORED.txt）
│   ├─ docs/                设计文档：线格式、ADR、性能对比
│   ├─ plugins/             服务端自带的示例插件（QuickJS 字节码源码）
│   └─ build-release.ps1    一次产出全部发布资产（客户端两包 + 服务端四目标 + SHA256SUMS）
├─ src/ tests/              .NET 服务端与其测试
├─ third_party/             第三方依赖（Lidgren 等，含各自 LICENSE）
├─ server.example.yml       **C++ 服务端**的配置示例（现行格式）
├─ server.example.json      **.NET 服务端**的配置示例（旧格式，勿混用）
├─ SfsMultiplayer.sln       .NET 解决方案
├─ LICENSE                  MPL-2.0
└─ THIRD-PARTY-NOTICES.md   第三方组件声明
```

## 快速开始（C++ 主线）

```bash
# 编译（Windows 上跑 PowerShell；需要 tools/ 下的 zig）
powershell -ExecutionPolicy Bypass -File Server/cpp/build-server-exp.ps1   # armv7 实验构建
powershell -ExecutionPolicy Bypass -File Server/cpp/build-release.ps1 -Version 1.4   # 全部发布资产

# 运行（前台；控制台可直接敲命令）
./sfs-server-kcp-arm --port 9806 --max-players 16 --state /path/state.json

# 配置：默认读 data/server-config.json（不存在则用内置默认值）
#   命令行的优先级：命令行 > 环境变量 > 配置文件 > 内置默认值
```

服务端控制台命令：help status say kick stop players 等；
插件可以新增自己的命令（见 [`../Plugin/`](../Plugin/)）。

## 插件系统（QuickJS）

编译期 -DSFS_PLUGINS=1 打开、运行期 SFS_PLUGINS=0 可临时关掉。
**关闭时产物与未引入前逐字节一致**（quickjs 源码不参与构建）。

```bash
SFS_PLUGINS=1 SFS_PLUGIN_PATH=my_plugin.qbc ./sfs-server-kcp-arm --port 9806
```

示例与用法见 [`../Plugin/`](../Plugin/)（5 个分类示例 + 综合示例）。

## 现在有什么

- **传输**：KCP over UDP（单端口、按 conv 区分会话），会话恢复、心跳/RTT、断线重连；
- **世界同步**：世界快照、火箭状态（冷火箭用压缩 blob 常驻）、权威分配、玩家控制权；
- **P2P**：直连通道信令与距离切换（中继仍可用）；
- **作弊策略**：服务端配置决定的作弊开关，逐项放行/拦截，客户端只执行；
- **功能**：配置文件（JSON）、日志分类/级别/彩色、插件系统（QuickJS 字节码）；
- **存档**：主档 + 备份，退出时保存。

## 文档索引

- 线格式与协议：cpp/docs/WIRE_FORMAT.md、cpp/src/protocol/PLUGIN_API.md
- 设计决策：cpp/docs/ADR-001-kcp-transport.md、ADR-002-easytier-networking.md、ADR-003-proxy-protocol-and-commands.md
- 性能：cpp/docs/PERF_COMPARISON.md
- 插件：[`../Plugin/`](../Plugin/)
