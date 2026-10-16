# 服务端常驻内存压缩（方案 B：部件数据常驻压缩 blob、按需解压）

目标：让 1.2.3.1 C++ 服务端在一块 **82 MB / ARMv7** 的目标设备上装得下 **32768 个碰撞箱**。
验收：先看实测能降多少，再定标准（用户原话）。

## 1. 实测基线（2026-09-19，板上 9807 实例）

单部件碎片（1 枚火箭 = 1 个部件 = 1 个碰撞箱），位置全部远离发射台：

| 档位 | RSS | CPU | 备注 |
|---|---|---|---|
| 0（底噪） | 0.21 MB | 0.9% | 空世界 |
| 1024 | 5.91 MB | 1.2% | 每千枚 +5.6 MB |
| 4096 | 15.98 MB | 1.9% | 每千枚 +2.9 MB（边际） |

→ 稳态 **≈ 3.4 KB / 枚**（(15.98−5.91) MB ÷ 3072）。
→ 16384 档加载期 `MemAvailable` 掉到 7.8 MB，触发内存护栏主动弃档；
   65536 档服务端已监听但加载 39 MB 存档时被压死（C++ 也撑不住）。

## 2. 钱花在哪（按结构算，已被实测吻合）

`src/protocol/packets.hpp` 的 `PartState`：一个 "Fuel Tank" 部件实带 7~9 个
`std::string`（name + number_variables 的 key + text_variables 的 key/value）
与 3 个 `std::vector`。

- 每个 `std::string`：对象 32 B + 堆上 16~32 B（SSO 之外）
- 每个 `vector<pair<...>>` 元素：~64 B + 容器本身 24 B
- 数值字段合计仅 20 B（不是瓶颈）

⇒ 大头是**字符串与容器开销**，不是坐标/温度这些数。压缩与池化的收益都在这里。

## 3. 方案

### 3.1 表示形态

- 常驻：`RocketEntry` 不再直接持有展开的 `RocketState`，改持 **压缩后的字节 blob**
  （blob 内容 = 现有 `RocketState::serialize()` 的输出，compress 一遍）。
- 热缓存：保留一个小的 LRU（例如最近 8~32 枚）为**展开态**，避免高频访问反复解压。
- 变更：模块写回 / 对接等会改部件数据，命中时把该枚标记 dirty，**淘汰时再压缩**（lazy recompress）。
- 冷火箭（本测试里 3 万枚全是冷火箭）始终以 blob 常驻 → 这才是省内存的地方。

### 3.2 压缩器

- 选 **miniz**（MIT，单 .c/.h，deflate）而不是 LZ4：
  这类数据是高度重复的短字符串，deflate 的压缩比显著更高（预期 4~6×），
  解压 ~200~400 MB/s 对"按需解压 + 热缓存"完全够；LZ4 解压更快但比不了比。
- 放 `src/third_party/miniz/`（与既有 `src/third_party/kcp/` 同样的 vendor 方式），
  自己的文件照旧带 MPL 头，第三方文件保留其原始许可声明。

### 3.3 不碰的东西

- **协议不变**：blob 用的是现有序列化器输出，发快照/收发模块包照旧走 `serialize()`。
- **存档格式不变**：`state.json` 读写路径解压后照旧。
- 客户端一行都不用改。

## 4. 改动点（按读点收敛，都是已验证的调用位置）

| 位置 | 现在 | 改后 |
|---|---|---|
| `server.hpp` `RocketEntry` | 展开的 `RocketState state` | blob + 热缓存 |
| `packets.cpp` 快照/创建包发送 | 直接读 `state` | `materialize(id)` |
| `packets.cpp` 模块包写回 | 直接改 `state.parts` | materialize + 标 dirty |
| `dock.cpp` / `dock_math.cpp` | 读 `rocket.parts` | materialize |
| `commands.cpp` `clear_debris` | 用 `state.parts.size()` | 计数改为随 blob 一起存的整数（不必解压） |
| `state_store.cpp` 存/读 | 直接序列化 | 与 blob 共用同一条序列化路径 |
| `world.cpp` / `lifecycle.cpp` | 遍历火箭 | 只读标量字段（位置/时间）时不解压 |

## 5. 验证路线

1. 编 armv7（`bash Server/cpp/build.sh`），只跑 **9807** 测试实例；生产 9806 不动。
2. 用 `.bench/mat.py cpp` 跑 512…32768 七档，与本文第 1 节基线逐档对照。
3. 断言：数值与行为不变（握手/创建/快照/存档往返），RSS 逐档下降；
   32768 档能加载完成且 `MemAvailable` 不低于护栏阈值。
4. 达标后再由用户决定是否上生产。

## 6. 风险与回滚

- 风险：解压路径被高频调用会吃 CPU（本测试 3 万枚全冷，实际几乎不命中）；
  热缓存太大会吞掉收益（先给 8，看数据再调）。
- 回滚：只动 `Server/cpp/`，`mat.py` 与基线数据都在 `.bench/`；
  旧二进制在板上 `data/ 有备份，`sha256sum` 可核。