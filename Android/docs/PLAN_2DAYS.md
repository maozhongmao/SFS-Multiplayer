# 2 天冲刺计划：手机端「能进去玩」

> 目标（v1 最低可玩）：手机端能连服务器、自己的火箭被服务器登记、能收到别人的火箭位置并在屏幕上看到标记、自己的位置能同步出去。
> 不管：部件级同步、外形还原、时间加速、聊天、断线重连体验。
> 已具备：注入 ✓ / il2cpp 反射 ✓ / 主菜单按钮 ✓ / 原生输入框 ✓ / KCP 握手 ✓ / 已入服并收到广播包 ✓ / 读本地火箭位姿（双精度）✓

---

## 里程碑与时间

### A. 登记自己的火箭（0.5 天，最关键的一步）
1. 按 `Server/cpp/src/protocol` 的字段顺序实现 `RocketState` 最小序列化：
   - `rocket_name`、`world_time`、`location`(pos/vel/planet)、`rotation`、`angular_velocity`、`throttle_percent`
   - **parts / joints / stages 全空数组**（服务端只校验数量上限与有限性，空表能过）
2. 发 `CreateRocket`（type=9）：`[worldTime][localId][globalId=-1][forLaunch=false][RocketState]`
3. 每 50ms 发 `UpdateRocketPrimary`（type=11）：`[worldTime][rocketId][pos][vel][planet][rotation][angularVel][structureHash=0]`
4. 验收：服务端日志出现 `create rocket global_id=…`；`packets_in` 持续增长；服务器 periodic 的 rockets 数 +1。

> 为什么先做这个：没有 CreateRocket，服务端不会为我们的火箭建条目，位置更新会被丢弃。

### B. 看到别人（1.0 天）
1. 收 `CreateRocket`（别人的）→ 记录 rocketId + 初始位置
2. 收 `UpdateRocketPrimary` → 更新缓存（**已实现**）
3. 渲染：克隆本机火箭的一个视觉子物体（不克隆 Rigidbody2D/碰撞体）→ 每帧：
   - 换算：`markerWorld = 本机火箭 worldPos + (远端 pos − 本机 planet 相对 pos)`
   - 设置 `transform.position` 与 `localEulerAngles.z = rotation`
4. 只允许在 `World_Mobile` 场景里创建/更新；离开场景全部销毁
5. 验收：PC 端起飞后，手机上出现一个跟随移动的标记。

### C. 联调与稳定（0.5 天）
- 两端同时在线跑 10 分钟：不崩、不卡加载、标记不抖（<50m 误差）
- 异常包忽略（type 22 等空位包已经出现，必须丢弃而不是崩）
- 收 `DestroyRocket` 时移除标记

### D. 收尾（0.5 天）
- 存档点（源码 + .so + APK + 文档）
- 更新 `HANDOVER-mobile-patcher.md`：KCP/握手/包格式/坑
- 手机端保留原生输入框作为服务器地址入口

---

## 关键风险（按概率）

| 风险 | 影响 | 对策 |
|---|---|---|
| `RocketState` 序列化字段顺序错 | 服务端解析失败 / 静默丢弃 | 严格照 `protocol/packets.hpp` 的 serialize 顺序，先发最小包验证 |
| 坐标换算错误（行星相对 vs 世界坐标） | 标记位置漂移 | 用「本机火箭」做锚点算差值；先在同一星球验证 |
| 克隆体带物理/碰撞 | 游戏崩或物理炸 | 只克隆 Sprite/子网格，或克隆后禁用 Collider2D/Rigidbody2D |
| 在加载场景创建对象 | 卡加载（已踩过） | 场景门控：只有 `World_Mobile` 才做 |
| 服务器没别人在线 | 无法验证 B | 先让 PC 端连同一服务器放火箭 |

---

## 今天就要做的第一件事
实现 A：最小 `RocketState` + `CreateRocket`，一轮编译装机，看服务端日志是否出现 `create rocket`。
