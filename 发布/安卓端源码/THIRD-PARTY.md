# 第三方组件

## KCP（ikcp.c / ikcp.h）

- 项目：[skywind3000/kcp](https://github.com/skywind3000/kcp)
- 许可：MIT
- 用途：可靠 UDP 传输（ARQ + 选择性重传），本项目的网络层 `mp_net.c` 基于它做会话管理
- 改动：未修改协议实现，仅按项目需要配置 `ikcp_nodelay(1, 10, 0, 1)`、`ikcp_setmtu(1400)`、`ikcp_wndsize(256, 512)`，使其与服务端一致

原始版权声明保留在 `ikcp.c` / `ikcp.h` 文件头部。
