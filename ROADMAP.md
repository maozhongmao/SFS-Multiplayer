# 更新计划

本文件记录**接下来要做的事**，以及**已经完成并已随版本发布**的部分。

> 本文件只描述状态，不承诺具体日期。已发布版本的行为请以 Releases 与项目维基为准。

## 当前版本

- PC：`SFS-Multiplayer-1.6-V2.1`，版本 `2.1.0.0`
- Android：`2.1.5.0`（versionCode 959）
- 服务端协议版本：`2.1.0`
- 底包：官方 `1.6.00.22`（Android 当前开发底包 versionCode 897；待重新生成注入底包）

## 已完成

### 补丁安装器

- 独立 App，读取官方 APK 就地打补丁，重签名后以共存包名 `com.sfsmp.sfs` 安装。
- 不覆盖官方原版，不重新分发游戏本体。
- 支持中文 / 英文，跟随系统语言；其他语言回退英文。

### Android 同步

- KCP 连服、握手、位姿与结构同步。
- 确定性 PartId（与 PC 同款 FNV + 位置算法），用于 CreateRocket / joints / stages / DestroyPart。
- 本机零件转移保护：零件移动到另一枚本地火箭时不再误发 DestroyPart。
- 同 gid 生命周期：不再反复 DestroyRocket / CreateRocket；新结构先发 CreateRocket，回执后再销毁旧 gid。
- 接收 type10 DestroyRocket，清理幽灵火箭与碰撞体。
- 发射台清理：进入世界/换箭/发射时清理 50m 内远端无主火箭；世界中每 2 秒持续清理。
- 修复 `rr_check_alive` 的列表误判：远端火箭不再批量消失 6 秒。
- 修复远端 Location 悬挂指针崩溃。
- 协议接收：
  - type6 世界时间
  - type12 次级状态（油门/RCS/转向/输入）
  - type15 引擎
  - type16 轮子
  - type17 助推器
  - type18 降落伞
  - type19 移动模块
  - type20 资源
  - type27 改名
  - type28 温度
  - type2/3 玩家上下线
- 模块状态写回远端零件变量（engine_on / wheel_on / booster_primed / fuel_percent / animation_state / deploy_state / state / state_target）。

### PC 端

- KCP 状态探针 framing 修复；Join 状态 Ping/Players 可用。
- 地址/用户名/密码只做红字提示，不阻止 Join；版本不一致只提示。
- Join 失败日志空引用保护，避免 Join 失败把游戏炸掉。
- Cheats 加入 HubManager / BuildManager 菜单。
- EndMissionMenu：清理 A 占位文本；无效 branch 返回空列表，修复 Destroy 按钮 KeyNotFound。
- 同 gid 结构“纯子集”原地更新，避免整枚 Destroy/Spawn。
- 改名轮询发送（`PollLocalRocketRenames`）。

### 服务端

- 版本字符串统一 `2.1.0`。
- 插件沙箱移除：不再检查能力位，不再限制内存/栈/指令/API 调用预算。
- 插件 API 扩展：
  - 元数据 `sfs.plugin`
  - 生命周期 `onLoad/onUnload/onTick`
  - 文件 API：readFile/writeFile/listDir/deleteFile/mkdir/exists/renameFile
  - `sfs.askUser`
- 删除未使用的 demo 插件引擎。
- 插件源码可单独编译通过。

## 尚未完成

### Android

- 本地火箭注册表 + 分级/拆分独立 gid（目前仍是同 gid 覆盖的过渡方案）。
- 发射台碰撞隔离：IL2CPP 裁掉 `Physics2D.IgnoreCollision`，反射不可用；目前靠清理无主火箭而不是 IgnoreCollision。
- 约 1 秒远端重建空档（PC 收到同 gid CreateRocket 后的重建路径）仍未彻底消除。
- 轮子悬空自转不同步：PC 协议只带 WheelOn，没有轮速。
- PC 端改名读到 `rocketName` 为空，改名同步实际未生效。
- 897 底包的“注入版补丁底包”尚未重新生成；之前已验证底包被 build 清理删除，当前只有手机拉回的临时底包。

### 服务端插件

- 插件源码尚未接入 `build.sh` 的生产构建；当前服务端二进制不包含插件引擎。
- 仍缺对比同行的 API：原始包级钩子、面板回传、自定义消息拦截、整枚火箭完整状态写入。

### 发布

- 安装器 APK 已构建，但 vivo 系统安装确认被拦截，尚未完成设备安装验证。
- 文档目录只保留在本地，不再上传仓库。

## 协议与许可

- 手机端整体（注入层、跳板、安装器）以 **GPL-3.0** 发布，条款见 `Android/LICENSE`。
- 仓库其余部分保持 **MPL-2.0**。

## 参与与反馈

- 使用问题与联机求助：QQ 群 **679991439**。
- 想提前试改动、参与验证：通过联系邮箱沟通。
