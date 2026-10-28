# REPORT_mp_net_wire — android/src/mp_net.c ↔ Server/cpp 协议逐字段核对

> 只读核对：D:/SFS-Multiplayer/android/src/mp_net.c（412 行）↔ D:/SFS-Multiplayer/Server/cpp/src（权威目录；ServerCpp-backup-2026-09-18/、1/Server/ 是副本）。未改源码、未编译、未改 git。2026-10-23。

## 0. 结论 + 必修清单

**总判定：线格式层逐字段一致——外层帧 / KCP 封帧 / 握手 / kind 枚举 / RocketState / CreateRocket / UpdateRocketPrimary 都没有发现会导致 “create rocket 不被受理” 的错位或位宽错误。**
真正表现为“手机端毫无反应 / 位置更新突然被丢”的是接收路径与协议行为缺口，按严重度：
- **S1 致命** mp_net.c:300-310 接收缓冲 4096 且 ikcp_recv 返回 -3 就 break（ikcp.c:378-384 不消费消息）→ 任一条 >4096 的 KCP 消息（世界快照里一枚真实火箭就是一条 CreateRocket，world.cpp:273-292）**永久堵死接收队列**，连 CreateRocket 回执都收不到。
- **S2 高** 无 kind==6 Disconnect 分支（mp_net.c:369-371 只处理 kind==5）→ 服务端拒绝原因（transport.cpp:44-60 的 payload 文本）被丢弃，日志只剩 “waiting ack...”，**这是“探针日志里看不出原因”的头号嫌疑**。
- **S3 高** 不回 Pong（同上）→ 服务端每 2s 发 Ping(kind=4)（lifecycle.cpp:127-131），frames.cpp:254-284 只有收到匹配 Pong 才下发 UpdateWorldTime，世界时间永不同步。
- **S4 高** 从不发 UpdatePlayerControl(type=4) → controlled_rocket_ 无手机；存活只靠 commands.cpp:250 的 10s last_update，权威只靠 owner 兜底，断更 10s 后 60s 宽限即被当碎片删（commands.cpp:235-269）。
- **S5 中** JoinRequest(type=0) 无效且误导（mp_net.c:322-328、331-340）：服务端 Hello 即认证入服（frames.cpp:87-246），handle_packet 无 kJoinRequest 分支（掉进 packets.cpp:985-987 的 default），L340 无条件 g_joined=1 也错。
- **S6 中** mp_net.c:301 只跑 9000 轮 ≈90 秒后停更。另有 S7-S10 低危：plen 整除截断、Ack 不存 player_id/token、每轮只收一个 datagram、注释（:1-7）与实现不符——见 §9。

---

## 1. 外层帧（TcpFrame）

服务端编码：protocol/codec.cpp:43-66；结构体 protocol/codec.hpp:16-32；解码 codec.cpp:68-100（要求 encoded.size()==4+body、kind∈1..12、payload_bits∈[0, payload*8]）。

| mp_net.c 行号 | 服务端 file:line | 判定 | 说明 |
|---|---|---|---|
| :141 body = 9 + nbytes | codec.cpp:51-54：bodyLen = 9 + payload.size() | OK | body 只含 kind+seq+payloadBits+payload，不含长度前缀 |
| :143 w_u32(out, body) | codec.cpp:55 写 i32 bodyLen 小端 | OK | 小端 4 字节 |
| :144 out[4] = kind | codec.cpp:56-57 write_u8(kind) | OK | 1 字节 |
| :145 w_u32(out+5, seq) | codec.cpp:58-59 write_i32(sequence) | OK | i32 小端；服务端只在 Hello 校验 seq==2（frames.cpp:91），其余不校验顺序 |
| :146 w_u32(out+9, nbits) | codec.cpp:60-61 write_i32(payload_bits) | OK | 位数而非字节数 |
| :147 payload 从 out+13 | codec.cpp:62 | OK | — |
| :140 nbytes=(nbits+7)/8 | codec.cpp:52 | OK | 位级包向上取整字节，服务端用 payload_bits 再裁位 |
| :223 / :238 传 w.bit（真实位长） | codec.cpp:60 | OK | 不是硬编码长度，payload_bits 精确；也不触发尾随字节问题（服务端 Packet 路径本就不查尾随） |
| :151-160 build_frame（字节版，Hello/Ping 用） | 同上 | OK | bits = n*8 |
| 头注释 :1-7 五段布局 | codec.hpp:16-32 | OK | 描述与实现一致 |

---

## 2. KCP 封帧与 UDP 输出

| mp_net.c 行号 | 服务端 file:line | 判定 | 说明 |
|---|---|---|---|
| :121-125 mp_output → send(g_fd, buf, len, 0) | net/kcp_transport.cpp:291-303 udp_output → sendto(buf,len) | OK | **两边都发裸 KCP 段，UDP 数据报上没有长度前缀**。任务里“mp_output 加 4 字节前缀”的说法与代码不符（注释 :1-7 有误导），但实现是正确的 |
| :198-206 send_msg = [u32 n][frame]，ikcp_send(buf, n+4) | kcp_transport.cpp:531-556 send：写 [u32 len][data]，注释 536-542 “千万不要再加一层” | OK | 4 字节小端长度前缀在 **KCP 消息体内部**，长度 = 帧字节数 |
| :312-315 n / len=r_u32(rx) / f=rx+4 | kcp_transport.cpp:558-575 receive 读 u32 len 后剥 4 字节 | OK | 与服务端读客户端消息的方式一致 |
| :288 ikcp_nodelay(1,10,0,1) | kcp_transport.cpp:314-361 同样 1/10/0/1 | OK | — |
| :289 ikcp_wndsize(256,256) | kcp_transport.cpp:314-361 wndsize(256,512) | OK | 各自 ownd/rwnd，无需相同 |
| 未调用 ikcp_setmtu（默认 1400） | kcp_transport.cpp:314-361 setmtu(1400)；kcp_transport.hpp:27-40 kMtu=1400 | OK | 默认值相同 |
| :285 ikcp_create(mp_now_ms()&0x7fffffff) 自选 conv | kcp_transport.cpp:314-361 按数据报里的 conv 建会话 | OK | 服务端不校验 conv 来源；同 conv 换 IP 会重建 |
| KCP 段最小 24 字节 | kcp_transport.cpp:430-453 drain_udp 要求 r>=24 | OK | 标准 kcp 24 字节头 |
| 消息上限 | kcp_transport.hpp:40 kMaxMessageBytes=8MB | OK | 客户端 4096 接收缓冲是自己的限制（S1），不是服务端 |

结论：KCP 封帧完全一致，任务里担心的“4 字节前缀不一致”并不存在（前缀在消息体、不在 UDP 段）。

---

## 3. 握手

服务端：protocol/handshake.cpp（EncodeHello 126-137 / DecodeHello 156-167 / EncodeAck 139-154 / DecodeAck 169-184）、handshake.hpp:23-25、frames.cpp:87-246。

| mp_net.c 行号 | 服务端 file:line | 判定 | 说明 |
|---|---|---|---|
| :166 magic 0x31465353 | handshake.cpp:115-122 校验；handshake.hpp:23-25 | OK | “SFS1” 小端 |
| :167 version 2 | handshake.hpp:24 kHandshakeVersion=2 | OK | 不符时服务端回 “unsupported handshake version” |
| :169-171 每串 = u32 长度 + raw UTF8 | handshake.cpp:58-64 AppendString（u32 字节长度 + raw）；Cursor::String 97-106 | OK | **不是 Lidgren 7bit 编长**，客户端这里也确实用 u32 —— 与 handshake 一致 |
| :169-171 顺序 user, pass, solar | handshake.cpp:156-167 DecodeHello 同序 | OK | username / password / solar_system_name |
| :172 resumeId 传 -1 → 0xFFFFFFFF | 读 i32 → -1 | OK | 语义 = 全新玩家 |
| :173 resumeToken u32 长 + 空串 | 同上 | OK | 长度 0 |
| :163 need 精确计算、无多余字节 | handshake.cpp:109-113 RequireNoTrailing | OK | “android”,“”,“”,-1,“” = 35 字节，不会触发 “handshake payload has trailing data” |
| :294 帧 kind=1、seq=2 | frames.cpp:91 要求 frame.sequence == kHandshakeVersion(2) | OK | 这里的 seq 是“版本号”语义，不是计数器 |
| :292 用户名 “android” 硬编码 | frames.cpp:87-246：trim/长度/重名/密码校验 | OK（有运维前提） | 服务端若配了密码或已有同名玩家，会被 deny_join → 原因只在 Disconnect 帧里（S2） |
| :322-328 发 JoinRequest(type=0) 帧 | 服务端分发表无 case kJoinRequest（只有 protocol/packets.cpp:118-120 IsValidPacketType 承认该枚举） | **MISMATCH** | **Hello 即认证**（frames.cpp:87-246：成功即 handshaked=true + 分配 player_id + HelloAck + initial_state），不存在独立的 JoinRequest 认证步骤；该包落 default（packets.cpp:985-987），只加 unknown_frames_ 计数 |
| :178-184 lid_str 单字节长度 | lidgren_buffer.hpp:10-16（字符串 7 位变长长度） | **MISMATCH**（无害） | 单字节写法只在 len<128 时与 7bit varint 相同；该包无人解析，删掉最干净 |
| :243-258 mp_log_ack 只读前 44 字节 | handshake.cpp:139-154 EncodeAck = magic, version, u32 player_id, 4×f64, u8 difficulty, 3 串 | OK | 前 44 字节对齐正确；但 player_id / udp_session_token / resume_token 被丢弃（S8） |

---

## 4. kind 枚举与 PacketType 数值

外层帧 kind：protocol/codec.hpp:16-32。业务包 type：protocol/packets.hpp:35-78。

| mp_net.c 行号 | 用途 | 服务端 file:line | 判定 |
|---|---|---|---|
| :294 kind=1 | Hello | codec.hpp:16-32 TcpFrameKind::Hello=1 | OK |
| :319 kind==2 | HelloAck | HelloAck=2 | OK |
| :223 / :238 / :325 kind=3 | Packet | Packet=3 | OK |
| :391 kind=4 | 客户端 Ping（空 payload） | Ping=4；frames.cpp:247-253 原样回 Pong | OK |
| :369 kind==5 | Pong | Pong=5 | OK |
| — | 缺 kind=6 Disconnect | Disconnect=6；transport.cpp:44-60 | **MISMATCH**（S2，缺失分支） |
| — | 缺 kind=7..12（快照/ServerInfo/版本） | codec.hpp:16-32 | 不必要（服务端只主动发 1/2/3/4/6） |
| :211 bw_u8(9) | PacketType.CreateRocket | packets.hpp:35-78 kCreateRocket=9；server/packets.cpp:258 | OK |
| :230 bw_u8(11) | UpdateRocketPrimary | kUpdateRocketPrimary=11；server/packets.cpp:411 | OK |
| :190 out[o++]=0 | JoinRequest | kJoinRequest=0 | 数值 OK，但服务端无处理（见 §3） |
| 未处理 2 PlayerConnected / 4 UpdatePlayerControl / 10 DestroyRocket 等 | 只落 :366-368 日志分支 | server/initial_state.cpp:20-87 会下发 2/4/22/24/21 | OK（不影响 wire），但远端缓存不会因 DestroyRocket 失效 |

枚举无错位：**1/2/3/4/5 与 9/11 全部对得上**。

---

## 5. CreateRocket（mp_send_create_rocket，:209-226）

服务端反序列化：protocol/packets.cpp:419-433（CreateRocketPacket）+ 273-293（RocketState::serialize）+ 38-54（NetLocation）+ 18-29（WriteCount = write_i32 定长）。

| mp_net.c 行号 | 字段 | 服务端 file:line | 判定 | 说明 |
|---|---|---|---|---|
| :211 | u8 type=9 | packets.cpp:419-433 + EncodePacket（packets.hpp:584-593 先 write_u8(type)） | OK | — |
| :212 | f64 world_time=0.0 | packets.cpp:419-433；server/packets.cpp:329 覆盖为服务端时间 | OK | 客户端值被丢弃，正好 |
| :213 | i32 local_id=1 | server/packets.cpp:340-342 回执保留原 local_id | OK | 客户端靠它把 local_id 绑到 global_id；必须保留，当前正确 |
| :214 | i32 global_id=-1 | server/packets.cpp:327-328 分配 new_id | OK | — |
| :215 | bool for_launch=0 | packets.cpp:419-433（1 位） | OK | 服务端只用于日志与分支 |
| :216 | string rocket_name=“MP-Android” | RocketState::serialize 首字段 write_string（packets.cpp:273-293） | OK | 7bit 变长 + UTF8，长度 10 |
| :217 | NetLocation x,y,vx,vy 4×f64 | packets.cpp:38-54 WriteNetLocation | OK | 64 位、LSB-first 位流 |
| :218 | string address=g_lplanet（“Earth”） | 同上：**字符串，不是 planet id** | OK | validate_finite 要求非空、长度<=kMaxNameLength、无控制字符（packets.cpp:96-105） |
| :219 | f32 rotation, f32 angular_velocity | RocketState 第 3/4 字段 | OK | 32 位位模式 |
| :220 | bool throttle_on(1) + f32 throttle_percent + bool rcs(1) | RocketState 第 5/6/7 字段 | OK | 位宽对齐 |
| :221 | i32 parts=0, joints=0, stages=0 | packets.cpp:18-29 WriteCount=write_i32（4 字节定长，非 varint） | OK | **空数组合法**：validate_rocket（packets.cpp:192-220）只查上限（kMaxCollectionCount=100000）与每项内容，不要求非空 |
| — | structureHash | CreateRocket 没有该字段（只有 UpdateRocketPrimary 有） | OK | 不需要 |
| :223 | payload_bits = w.bit | codec.cpp:60 + packets.hpp:584-593 | OK | 精确位长，不触发服务端长度校验 |

结论：CreateRocket 的字段顺序/位宽/类型全对；空 parts/joints/stages 会被接受；planet 用字符串 address；double 是 64 位 LSB。

---

## 6. UpdateRocketPrimary（mp_send_primary，:228-241）

服务端：protocol/packets.cpp:447-463（serialize）+ server/packets.cpp:411-431（处理）。

| mp_net.c 行号 | 字段 | 服务端 file:line | 判定 | 说明 |
|---|---|---|---|---|
| :230 | u8 type=11 | packets.hpp kUpdateRocketPrimary=11 | OK | — |
| :231 | f64 world_time=0.0 | packets.cpp:447-463；server/packets.cpp:421 覆盖 | OK | 客户端值被丢弃 |
| :232 | i32 rocket_id = g_global_id | 同上；server/packets.cpp:414-415 找不到火箭静默 return | OK | 回执没解析到之前不会发（:383 有 g_global_id>=0 守卫） |
| :233-234 | NetLocation 4×f64 + address string | 同上 | OK | — |
| :235 | f32 rotation, f32 angular_velocity | 同上 | OK | — |
| :236 | i32 structure_hash = 0（bw_u32） | packets.cpp:447-463 写 i32；server/packets.cpp 不校验（411-431 只 validate_finite） | OK | 位模式相同；填 0 不影响受理 |
| :238 | payload_bits = w.bit | codec.cpp:60 | OK | — |

两条受理前提（不是编码问题，但决定“会不会被丢”）：
- server/packets.cpp:414-415：rockets_.find(rocket_id) 失败 → **静默 return**；
- server/packets.cpp:416：!has_authority(rocket_id) → **静默 return**（authority 由 world.cpp:130-243 refresh_authorities 计算；创建者靠 owner_player 兜底 + 3s authority_hold 稳定持有，见 server/packets.cpp:332-336、350-352）。

---

## 7. 收包方向（mp_thread，:306-372）

| mp_net.c 行号 | 服务端 downlink 真值 | 判定 | 说明 |
|---|---|---|---|
| :310 ikcp_recv(g_kcp, rx, 4096) | kcp_transport.hpp:40 kMaxMessageBytes=8MB；ikcp.c:378-384 peeksize > len → return -3 且**不消费** | **MISMATCH（致命 S1）** | 一旦队首消息 >4096（世界里任一真实火箭在世界快照里就是一条大 CreateRocket，world.cpp:273-292），每轮都 -3 → break → 该消息永远堵在队首，后续所有包（含 CreateRocket 回执）永远读不到；服务端看客户端就像“失踪” |
| :313-315 n / len / f | kcp_transport.cpp:558-575 | OK | — |
| :316 kind=f[4] | codec.cpp:68-100 | OK | 偏移 4 正确 |
| :317-318 plen = payloadBits/8 | codec.cpp:60-61 | **需注意（S7）** | 整除**向下**截断：payload_bits 非 8 倍数时少 1 字节。当前手工读的字段都靠前、br_t 只读 128 位，所以没炸，但边界校验不可靠 |
| :320 mp_log_ack(f+13, plen) | handshake.cpp:139-154 EncodeAck | OK（不完整 S8） | 44 字节前缀解得出 magic/ver/pid/world_time；difficulty/3 串未读，player_id/token 未保存 |
| :331-339 ptype==1 JoinResponse | 服务端**不会**以 Packet type=1 下发（JoinResponse 是 HelloAck 的 payload）；且此处按单字节读 solar 长度 | **MISMATCH**（死代码） | 永远不会命中；留着会误导后续维护 |
| :340 g_joined=1（任何 kind==3） | — | **MISMATCH** | 收到 UpdateCheatStatus/ShowToast 也叫 “joined”，状态语义错 |
| :342-350 ptype==9：br_t(f+14, (plen-1)*8)，读 f64 wt / i32 lid / i32 gid | packets.cpp:419-433 前 20 字节 | OK | 字段顺序对；nbits 偏小（少 8 位以上）但不影响读前 20 字节；lid==1 && gid>=0 与回执语义一致（server/packets.cpp:340-342） |
| :352-360 ptype==11：手工偏移 f64 wt / u32 rid / 4×f64 / 1 字节串长 / f32 rot | packets.cpp:447-463 | OK（脆弱） | 顺序对齐；漏读 angular_velocity/structure_hash（不需要）；单字节串长只在 address<128 时正确（当前 “Earth” 安全） |
| :359 int alen=*q++ | lidgren_buffer.hpp 7bit varint | OK（<128） | 与 §3 的 lid_str 同源问题，暂时无害 |
| :366-368 其它 ptype 只记日志 | initial_state.cpp:20-87 会发 22/2/4/24/21 | OK | 不解析也不乱序（用 plen 定界） |
| :369-371 kind==5 只记日志 | frames.cpp:247-253 回 Pong 时 payload=i64 ticks、seq 原样 | OK | — |
| 缺 kind==4 分支 | lifecycle.cpp:127-131 每 2s 发 Ping；frames.cpp:254-284 只有收到匹配 Pong 才发 UpdateWorldTime | **MISMATCH（S3）** | 必须回 Pong：payload 原样、kind=5、seq 用原帧 seq（frames.cpp:251 用 frame.sequence 回显） |
| 缺 kind==6 分支 | transport.cpp:44-60 deny_join = Disconnect + payload=UTF8 原因 | **MISMATCH（S2）** | 这是“设备端看不到任何拒绝原因”的直接原因 |
| 未处理 ptype==10 DestroyRocket / 2 PlayerConnected | server/commands.cpp:254-263 广播 DestroyRocket | 需注意 | 远端缓存不失效，会出现幽灵火箭（不影响服务端判定） |

---

## 8. 行为层（不属线格式，但直接决定“日志里出不出 create rocket”）

| mp_net.c 行号 | 行为 | 服务端真值 | 判定 |
|---|---|---|---|
| :301 for(iter=0; iter<9000; iter++) + :394 usleep(10000) | ≈90 秒后线程退出、再不发包 | commands.cpp:250（10s last_update keepalive）+ server.hpp:271 kUncontrolledGraceSeconds=60 → 之后当碎片删除并广播 DestroyRocket | **MISMATCH（S6）** |
| :306 每轮只 recv 一个数据报 | 服务端 drain_udp 每轮 drain 一批 | 需注意（S9） | 上限约 100 包/s；初始同步（UpdateCheatStatus + N×PlayerConnected/Control + 快照…）会积压 |
| :378-382 CreateRocket **只发一次**（g_create_sent） | 服务端有 local_id 去重 + 复用回执（packets.cpp:304-325） | 需注意 | 回执若被 S1 堵住就永不重发；建议超时未拿到 gid 时重发（服务端幂等，安全） |
| :383-385 每 50ms 一条 UpdateRocketPrimary | 服务端无频控，逐条 apply+broadcast（packets.cpp:411-431） | OK | 20Hz 可接受 |
| 从不发 UpdatePlayerControl(type=4) | packets.cpp:673-722；world.cpp:130-243 用 controlled_rocket_ 做 probe | **MISMATCH（S4）** | 手机不被视为操控者：权威只靠 owner 兜底、距离判据里 probe=-1；断更 10s 后碎片清理会删掉它 |
| :387-393 每 5s 发 Ping(kind=4 空 payload) | frames.cpp:247-253 原样回 Pong | OK | 保活足够；但**只发不收**（S3） |
| :285 conv 用时间生成、:172 resume=-1 | frames.cpp 支持 resume 重连 | 需注意 | 每次重启都是新玩家；旧火箭变无主 |

---

## 9. 必修清单（按严重度，给行号 + 具体改法）

> 本节只描述改法，报告未修改任何源码。

**S1（致命）mp_net.c:300-310 —— 接收缓冲/大消息**

    unsigned char rx[4096];                            // L300
    int n = ikcp_recv(g_kcp, (char*)rx, sizeof(rx));    // L310

改成：先用 int need = ikcp_peeksize(g_kcp); 取队首消息长度，need > sizeof(rx) 时把接收缓冲扩到 need（或直接分配 8MB 静态缓冲，对齐服务端 kMaxMessageBytes，kcp_transport.hpp:40）；ikcp_recv 返回 -3 时**不要 break**，扩缓冲后重试（-3 不消费消息，见 ikcp.c:378-384）。至少要保证能装下一条完整火箭的 CreateRocket。

**S2（高）mp_net.c:369-371 —— 处理 Disconnect，否则永远不知道为什么被拒**

在 else if(kind==5) 之后加 else if(kind==6 /*Disconnect*/)：把 payload（f+13，plen 字节）当 UTF8 打印（服务端写的是纯文本原因，transport.cpp:44-60），并置失败标志后跳出循环（不要静默 continue 等 90 秒）。**这条能直接解释现在“日志里什么都没有”。**

**S3（高）mp_net.c:369-371 —— 回 Pong**

加 else if(kind==4 /*Ping*/)：把本帧 payload 原样拷一份，用 build_frame(out, cap, 5 /*Pong*/, 原帧 seq（f[5..8]）, payload, plen) 发回。服务端 frames.cpp:254-284 校验 sent_ticks == last_ping_ticks 后才下发 UpdateWorldTime；seq 必须回显原帧（frames.cpp:251 就是这么回 Pong 的）。

**S4（高）mp_net.c:347-349 附近 —— 认领控制权**

拿到 g_global_id 后（ptype==9 命中的分支里）发一条 Packet type=4，payload 8 字节：

    bw_u8(4);            // PacketType.UpdatePlayerControl
    bw_i32(0);           // player_id：服务端用认证身份覆盖（packets.cpp:676-677），填 0 即可
    bw_i32(g_global_id); // rocket_id
    build_frame_bits(frame, sizeof(frame), 3, 4, w.buf, w.bit); send_msg(frame, fn);

服务端会单播回显实际控制目标（packets.cpp:703/714），可据此确认认领成功；同时广播给其他客户端。

**S5（中）mp_net.c:322-328 / 331-340 —— 删掉无效包与错误状态**
- 删 :322-328（Hello 已认证，服务端无 kJoinRequest 处理，packets.cpp default 只计未知帧）。
- 删 :331-339 整段 ptype==1 死代码（服务端不下发 Packet type=1）。
- :340 的 g_joined = 1; 改为只在 HelloAck 成功（:319-321 分支）时置位；否则会被 UpdateCheatStatus(22)/ShowToast(21) 误触发。

**S6（中）mp_net.c:301 —— 生命周期**

for(int iter=0; iter<9000; iter++) 改 while(!g_stop)（或 for(;;) + 明确退出条件），否则 90 秒后静默停更，服务端 10s keepalive + 60s 宽限后把火箭当碎片删掉（commands.cpp:235-269）。

**S7（中）mp_net.c:317-318 —— 位长不要整除**

int plen = payloadBits / 8; 改为保留 payloadBits，字节数用 (payloadBits+7)/8；br_t 初始化用 payloadBits - 8（去掉 type 字节）；手工偏移前用 (payloadBits+7)/8 做边界检查。

**S8（低）mp_net.c:243-258 —— Ack 数据不要丢**

保存 player_id（:249 已读但只打印）、udp_session_token、resume_token，并校验 magic==0x31465353 && ver==2（不符时打印），否则 resume 重连与 mod 侧状态上报都无从谈起。

**S9（低）mp_net.c:306 —— 每轮收干净**

int rn = recv(...) 改 while((rn = recv(...)) > 0){ ikcp_input(...); }，否则高延迟/初始同步时会积压到丢包。

**S10（低）mp_net.c:1-7 —— 注释纠正**

头注释把 4 字节长度前缀描述成“KCP 上”的层，容易让下一个维护者误以为要给 UDP 数据报加前缀。正确表述：**长度前缀在 KCP 消息体内（send_msg），UDP 数据报上是裸 KCP 段**。

---

## 10. 附：如果设备端日志连一行网络日志都没有，怎么定位

mp_thread 的第一条日志是 :265 的 “mp: connecting %s:%d ...”（在 socket 之前），据此二分：

1. **连 “connecting” 都没有** → mp_net_connect() 没被调用 / 含该函数的 .so 没被加载 / 日志文件不可写（路径 /sdcard/Android/data/com.StefMorojna.SpaceflightSimulator/files/sfsmp-probe.txt，:23-29，每次 mp_log 以 append 打开，失败是静默的）。这**不是协议问题**，先查 hook 与文件权限。
2. 有 “connecting” 无 “hello sent” → DNS/socket/connect 失败（:271-278 有对应日志）。
3. 有 “hello sent” 只剩 “waiting ack...” → 服务端没起/端口不对/包被防火墙丢，**或服务端拒绝了入服而原因在 Disconnect 帧里被丢掉（S2）**。先补 S2，再看日志就能区分。
4. 有 “HANDSHAKE OK” 且服务端打了 “handshake ok” 但没有 “rocket created” → CreateRocket 被 validate_rocket 拒（服务端会打 W 日志 “拒绝 CreateRocket: ... 原因=...”，packets.cpp:266-272）；若服务端连 W 日志都没有，说明根本没收到该帧 → 检查 g_have_pose 是否被 mp_set_local_pose 置位（:378 的前置条件）。
5. 服务端有 “rocket created”、手机没有 “OUR ROCKET registered” → 就是 S1（回执被大包堵在 KCP 接收队列后面）。

运维前提（非代码问题）：g_host 默认 “192.168.1.5”、g_port 9806（:116-119），必须与 PC 服务端实际监听地址一致；用户名 “android” 不能与在线玩家重名；服务端若设置了密码，“android” 用空密码会被拒（原因同样只在 Disconnect 里）。

---

## 11. 判定汇总

| 层 | 结论 |
|---|---|
| 外层帧 [bodyLen][kind][seq][bits][payload] | **OK**（逐字段一致，payload_bits 为精确位长） |
| KCP 封帧（裸段 + 消息体 [u32 len][frame]） | **OK**（“4 字节前缀位置”的担心不成立，代码是对的） |
| 握手 magic/version/字符串编码/字段顺序/resume/尾随字节 | **OK**（但“Hello 后再 JoinRequest 认证”这个假设是错的：Hello 即认证） |
| kind 枚举 1/2/3/4/5 + type 9/11 | **OK**；缺 6 Disconnect（MISMATCH） |
| CreateRocket 字段/位宽/空数组/planet/结构哈希 | **OK**（structureHash 不属该包；空 parts/joints/stages 服务端接受） |
| UpdateRocketPrimary 字段/位宽 | **OK**（structure_hash 不校验，填 0 可） |
| 收包解析顺序 | 字段顺序 **OK**（ptype 9/11）；但 S1 大消息、S2 缺 Disconnect、S3 缺 Ping 应答、S7 整除截断属 MISMATCH |
| 能否“服务端日志出现 create rocket + 位置更新不被丢弃” | **线格式已具备**；补齐 S1/S2/S3 即可稳定复现；S4/S6 决定能否长期（>60s）不丢 |

> 未覆盖：android 树内没有 ikcp.h/ikcp.c（构建靠 android/research/memory_injection/tools/out 下的 ikcp-a64.o / libsfproto-arm64.a），若实际链接的 kcp 版本与 Server/cpp/src/third_party/kcp 不同，§2 与 S1 依赖的 -3 语义需按实际版本复核。
