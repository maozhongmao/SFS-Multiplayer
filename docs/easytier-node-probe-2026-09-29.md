# EasyTier 公共节点实测结果

> 探测时间：2026-09-29
> 探测机：Windows 本机（192.168.1.6，路由 192.168.1.1）
> 探测方法：Python 原生 socket（TCP connect / UDP 探测 / DNS A 记录查询）
> 注意：EasyTier 虚拟网卡 `et_5_dcqx` 把系统 DNS 指向 `100.100.100.101`（`accept_dns = true`），
> 所以用系统 `nslookup` 探测会超时，但 DNS 本身正常（能解析 github.com / easytier.cn）。
> 下面是绕开 EasyTier DNS 后用系统解析器（`[System.Net.Dns]`）得到的真实结果。

## 一、可用的节点（建议保留）

| 节点 | 解析 IP | TCP 握手耗时 | 来源 |
| --- | --- | --- | --- |
| `tcp://et1.fuis.top:11010` | 47.103.35.100 | 0.06 s | 已在用 |
| `tcp://mc.yqst.top:11010` | 124.230.195.155 | 0.04 s | 已在用 |
| `tcp://225284.xyz:11010` | 116.237.112.65 | 0.03 s | 新增候选 |

## 二、不可用的节点（已排除）

| 节点 | 实测结果 | 原因 |
| --- | --- | --- |
| `tcp://39.108.52.138:11010` | TCP 超时 | 主机可达但端口无响应。**当前配置里已有此节点，每次启动都要白等超时，建议删除** |
| `public.easytier.top:11010` | DNS NXDOMAIN | 域名不存在 |
| `public2.easytier.top:11010` | DNS NXDOMAIN | 域名不存在 |
| `public.easytier.cn:11010` | DNS NXDOMAIN | 官方 README 里提到的域名，当前解析不到 |
| `wss://et.南梁.com` | DNS NXDOMAIN | 域名不存在 |
| `tcp://161.33.207.13:51010` | TCP 超时 | — |
| `ws://161.33.207.13:51011` | TCP 超时 | — |
| `wss://161.33.207.13:51012` | TCP 超时 | — |
| `quic://161.33.207.13:51012` | 同上端口，不通 | — |
| `faketcp://161.33.207.13:51013` | TCP 超时 | — |
| `wg://161.33.207.13:51011` | 同上端口，不通 | — |

`161.33.207.13` 的 51010~51013 四个端口全部超时，整台不可用，换协议前缀（ws/wss/wg/faketcp/quic）都走同一批端口，无效。

### 未能判定的项（不猜）

| 节点 | 说明 |
| --- | --- |
| `udp://161.33.207.13:51010` | UDP 不回探测包是正常现象，无法据此判断服务是否存在。但该主机 TCP 端口全不通，判断为不可用 |
| `udp://225284.xyz:11010` | 同一 IP 的 TCP 11010 可用；UDP 是否监听未验证（EasyTier 节点通常同时开 TCP+UDP） |

## 三、必须一起记住的三点

### 1. 全是 tcp:// 节点 = 只能做发现与中继
上面三个可用节点协议前缀都是 `tcp://`。它们的作用是让节点互相发现、以及在打洞失败时中继。
**数据走直连还是走中继，取决于打洞是否成功**，与节点本身通不通是两件事。
判断方法：在 EasyTier 的 peer 列表里看 `tunnel_proto` 列 —— 显示 `udp` 表示打洞成功（零中转延迟），显示 `tcp` 表示走中继。

### 2. 当前配置里的三个必须改的开关
配置文件：`C:\Users\Administrator\AppData\Roaming\com.kkrainbow.easytier\config.d\7c318f0a-5950-4026-9fd1-7c3cb07ae371.toml`

| 配置项 | 当前值 | 应改为 | 原因 |
| --- | --- | --- | --- |
| `enable_kcp_proxy` | `true` | `false` | SFS 传输层已经是 KCP，再套一层 EasyTier KCP proxy = 双重拥塞控制 + 双重重传，是持续高延迟的直接来源 |
| `enable_quic_proxy` | `true` | `false` | QUIC 网络风暴问题 |
| `enable_encryption` | `false` | `true` | 虚拟网内当前是明文传输 |

### 3. 已有基础设施（不需要新建）
- 虚拟网卡 `et_5_dcqx` 已建立并 connected（MTU 1380）
- ACL 里 9806 端口已在 `admin` 规则放行（TCP），本来就是为了跑 SFS 服务端准备的
- 端口转发已配：`10.0.0.0:5000 -> 192.168.1.6:5000`、`10.0.0.0:65535 -> 127.0.0.1:8188`
- 网络身份：`network_name = "异地组网"` / `network_secret = "stch666"`（该密钥属运行配置，未在此展开）

## 四、待办
- [ ] 删除坏节点 `tcp://39.108.52.138:11010`
- [ ] 加入 `tcp://225284.xyz:11010`
- [ ] 关闭 `enable_kcp_proxy` / `enable_quic_proxy`
- [ ] 打开 `enable_encryption`
- [ ] 重启 EasyTier（GUI 关闭窗口会 panic —— `tao` 库事件循环 bug，需用进程方式重启）
- [ ] 在 GUI 确认 peer 列表的 `tunnel_proto` 是否为 `udp`
- [ ] 确认 9806 在虚拟网内可从外部访问

## 五、对 SFS 项目的影响结论
**代码零改动。** EasyTier 建的是 IP 层虚拟网卡，SFS 的 KCP 传输层跑在它上面，不受影响。
适配点只有连接地址：虚拟网内填 `10.0.0.x:9806`，替代现在的公网域名 + frp 端口映射。
