# 内存注入 / 手机端联机落地研究

目标：让安卓端真正加入 SFS-Multiplayer 服务器（不只是 UI）。
约束：
- 只写本目录，**绝不改动 D:\SFS-Multiplayer\1**（自动同步仓库）
- 只读引用：Client/、Server/cpp/、docs/mobile-feasibility.md、android/dump/、android/work/
- 每份报告边做边写（每完成一步追加一节），防 API 中断丢结果

五条并行线：
01 PC 模组依赖面（游戏 API 清单）
02 卖家 HybridCLR 方案逆向（能否复用 C# 模组）
03 原生 C++ 移植可行性（KCP + 协议 + 世界同步）
04 Hook 技术选型（arm64 inline hook / 主线程分发）
05 MVP 范围与工作量（最小可玩集）
