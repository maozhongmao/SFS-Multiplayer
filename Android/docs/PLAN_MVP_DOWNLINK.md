# 手机端 MVP：把别人的位置同步进来（能玩版）

目标（用户口径）：**手机端能看到其他玩家的火箭并跟着动 = 完成**。
不做：玩法对齐、时间对齐、聊天、改名、分发安装器、外观精修、稳定性加固（除下面第 0 步这一行）。

---

## 0. 先堵一个洞（5 分钟）
`android/src/mp_net.c` 收包缓冲 4096 → 65536。
现在只要有一条 >4096 的消息（别人带完整结构的 CreateRocket 就可能超），KCP 接收队列会永久堵死，后面所有位置包都收不到。
一行改动，必须做，否则第 2 步之后随时"突然不动了"。

## 1. 下行数据落地（30 分钟）
- `mp_net.c`：`mp_remote_t` 增加 `char planet[40]`，type=11 解析时把星球名存下来（现在只存了坐标，星球名丢在局部变量里）。
- `mp_net.c`：新增 `int mp_remote_count(void)` 暴露数量。
- `v64.c`：每 60 帧打一条 `v80: remotes=N id=.. planet=.. pos=(..) rot=..`，确认手机端真的持续收到 PC 的位置。
产出：日志里能看到对方的实时坐标在变。

## 2. 在手机上把对方"造"出来（最大工作量，2~3 小时）
关键发现（dump.cs:22852-22905）：游戏自带火箭工厂类 **SFS.World.WQPAXLGGCOAUZEOA**（MonoBehaviour, TypeDefIndex 628）：
- `rocketPrefab` 字段 0x20
- **`public static void LoadRocket(RocketSave save, out bool ok)` RVA 0x18F31D8** ← 我们要用的
- `DestroyRocket(Rocket, reason)` RVA 0x18F1C7C
- `MergeRockets` / `SpawnBlueprint` / private `GMCNNPHNIAN(JointGroup, ...)` 备用

做法（不自己 Instantiate，避免以前那条 libunity 崩溃死路）：
1. 取本机火箭 → `RocketSave` 对象（`RocketSave.ctor(Rocket)` RVA 0x1852640，v73 已经在用）。
2. 把这个 save 的 location 改成对方的（planet / position / velocity）、rotation 改成对方姿态。
   ⇒ 替身外观 = 我们自己那枚火箭（MVP 够用；要精确外观再解析对方 CreateRocket 的 parts）。
3. `runtime_invoke(LoadRocket, NULL, {save, &outOk})`，让游戏自己把火箭造出来。
4. 造之前/之后各调一次 `FindObjectsOfType(Rocket)`，差集里那个新的就是它，记进 `id → Rocket*` 表。
产出：进世界后手机里多出一枚火箭。

## 3. 让它跟着动（1 小时）
- 每帧（或每 50ms）对每个远端 rocket：
  - `worldPos = 本机火箭 worldPos + (对方 planet 相对坐标 − 我方 planet 相对坐标)`（同一星球时这个换算就是纯平移）
  - `Transform.set_position(worldPos)`、`set_localEulerAngles.z = rot`
- 不做插值、不做速度预测（能玩优先）。
产出：对方在 PC 上动，手机里的替身同步移动。

## 4. 清理与多目标（30 分钟）
- 最多 16 个远端（`g_remote[16]` 已有）；超过 5 秒没收到更新的隐藏（`SetActive(false)`）。
- 离开 World_Mobile 时全部销毁（`DestroyRocket` 或 SetActive 关闭），回主菜单不留残影。

## 5. 验收（30 分钟）
同服三端：PC + 手机A + 手机B。
- 手机A 能看到 PC 和手机B 的火箭，位置误差 < 50 m（视觉上对得上就行）。
- 断开一方后 5 秒内替身消失。
- 连续跑 10 分钟不崩、不卡加载。

---

## 总工时
约半天（0.5 天）。对比原 5 阶段计划（3~5 天）——差别就是砍掉了：稳定性加固、外观精确、玩法对齐、分发、时间同步。

## 已知风险与兜底
| 风险 | 兜底 |
|---|---|
| LoadRocket 在联机态下报错/崩溃 | 退回 `rocketPrefab`(0x20) + `GMCNNPHNIAN` 私有工厂；再不行只做"地图/文字标点"版本 |
| 游戏物理把替身拽走 | 我们每帧强写 Transform，视觉上永远听我们的（代价：物理不参与，仅显示） |
| 对方与我方不在同一星球 | MVP 只处理同星球（都是 Earth），跨星球留到后面 |
| LoadRocket 影响存档 | 只在 World_Mobile 内调用、离开即销毁；测试用专门存档 |

## 建议的执行顺序
0 → 1 → 2（先只支持 1 个远端）→ 3 → 4 → 5。
