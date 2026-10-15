// KCP 探针：默认做握手烟测；加 control 参数做"控制权回执"复现。
//   dotnet run -- 192.168.1.5 9807            握手烟测（打印 KCP_HELLO_ACK_OK）
//   dotnet run -- 192.168.1.5 9807 control    握手后请求控制权，观察请求者是否收到回执
using System.Net;
using System.Net.Sockets;
using System.Text;
using MultiplayerSFS.Common.Transport.Kcp;

var host = args.Length > 0 ? args[0] : "192.168.1.5";
var port = args.Length > 1 ? int.Parse(args[1]) : 9807;
var mode = args.Length > 2 ? args[2] : "smoke";

const byte KindHello = 1;
const byte KindHelloAck = 2;
const byte KindPacket = 3;
const byte PacketUpdatePlayerControl = 4;

uint conv = (uint)Environment.TickCount;
using var udp = new UdpClient(AddressFamily.InterNetwork);
var endpoint = new IPEndPoint(IPAddress.Parse(host), port);
using var kcp = new KcpContext(conv, (data, _) => udp.Send(data, data.Length, endpoint));
kcp.SetNoDelay(1, 20, 2, 1); kcp.SetMtu(1400); kcp.WndSize(128, 128);

static void Put32(List<byte> b, int value) { b.Add((byte)value); b.Add((byte)(value >> 8)); b.Add((byte)(value >> 16)); b.Add((byte)(value >> 24)); }
static void PutString(List<byte> b, string s) { var x = Encoding.UTF8.GetBytes(s); Put32(b, x.Length); b.AddRange(x); }
// Lidgren 变长长度前缀（7bit）：普通业务包的字符串用它，不是 u32
static void PutVarU32(List<byte> b, uint value)
{
    while (value >= 0x80) { b.Add((byte)(value | 0x80)); value >>= 7; }
    b.Add((byte)value);
}
static void PutVarString(List<byte> b, string s)
{
    var x = Encoding.UTF8.GetBytes(s);
    PutVarU32(b, (uint)x.Length);
    b.AddRange(x);
}
static byte[] WrapFrame(byte kind, int seq, List<byte> payload)
{
    var frame = new List<byte>();
    Put32(frame, 9 + payload.Count);   // inner length = kind(1) + seq(4) + bits(4) + payload
    frame.Add(kind);
    Put32(frame, seq);
    Put32(frame, payload.Count * 8);   // payload length in BITS
    frame.AddRange(payload);
    var message = new List<byte>();
    Put32(message, frame.Count);       // outer length = frame length
    message.AddRange(frame);
    return message.ToArray();
}

var pending = new List<byte>();
var frames = new List<(byte kind, int seq, byte[] payload)>();

void DrainFrames()
{
    while (pending.Count >= 4)
    {
        var outerLength = pending[0] | (pending[1] << 8) | (pending[2] << 16) | (pending[3] << 24);
        if (outerLength <= 0 || pending.Count < outerLength + 4) return;
        var body = pending.GetRange(4, outerLength);
        pending.RemoveRange(0, outerLength + 4);
        if (body.Count < 9) continue;
        var kind = body[4];
        var seq = BitConverter.ToInt32(body.ToArray(), 5);
        // 帧体布局：[innerLen(4)][kind(1)][seq(4)][bits(4)][payload...] -> payload 从 13 开始
        frames.Add((kind, seq, body.GetRange(13, body.Count - 13).ToArray()));
    }
}

void Pump(int milliseconds)
{
    var deadline = DateTime.UtcNow.AddMilliseconds(milliseconds);
    var datagrams = 0;
    var bytes = 0;
    var icmp = 0;
    var before = frames.Count;
    var any = new IPEndPoint(IPAddress.Any, 0);
    while (DateTime.UtcNow < deadline)
    {
        kcp.Update((uint)Environment.TickCount);
        kcp.Flush();
        // 同步收割：避免 ReceiveAsync 竞速丢包（早期版本用 WhenAny+Delay 会把未完成的接收任务丢掉）
        while (udp.Available > 0)
        {
            byte[] datagram;
            try
            {
                datagram = udp.Receive(ref any);
            }
            catch (SocketException ex) when (ex.SocketErrorCode == SocketError.ConnectionReset)
            {
                icmp++;   // 对方回 ICMP 端口不可达，对 UDP 无害，继续
                continue;
            }
            datagrams++;
            bytes += datagram.Length;
            kcp.Input(datagram, 0, datagram.Length);
            var buffer = new byte[65536];
            int n;
            while ((n = kcp.Recv(buffer, 0, buffer.Length)) > 0)
                for (var i = 0; i < n; i++) pending.Add(buffer[i]);
        }
        DrainFrames();
        Thread.Sleep(1);
    }
    Console.WriteLine($"  [pump] datagrams={datagrams} udpBytes={bytes} newFrames={frames.Count - before} pendingBytes={pending.Count} icmp={icmp}");
}

// ---- 1) 握手 ----
var hello = new List<byte>();
Put32(hello, 0x31465353); Put32(hello, 2);
// 用户名必须每次唯一：服务端默认 block_duplicate_names=on，重名会被直接拒绝（第二个探针握手超时）
PutString(hello, "probe-" + Guid.NewGuid().ToString("N").Substring(0, 8)); PutString(hello, ""); PutString(hello, "");
Put32(hello, -1); PutString(hello, "");
var helloBytes = WrapFrame(KindHello, 2, hello);
kcp.Send(helloBytes, 0, helloBytes.Length);
kcp.Flush();
Console.WriteLine($"PROBE_LOCAL_PORT={((IPEndPoint)udp.Client.LocalEndPoint!).Port} conv={conv}");

Pump(3000);
var ack = frames.FirstOrDefault(f => f.kind == KindHelloAck);
if (ack.payload == null) { Console.WriteLine("KCP_HELLO_ACK_TIMEOUT"); Environment.ExitCode = 1; return; }

var playerId = BitConverter.ToInt32(ack.payload, 8);   // magic(4) + version(4) + playerId(4)
// JoinResponse 布局：magic(4) version(4) playerId(4) updateRocketsPeriod(8) chatCooldown(8) worldTime(8) ...
double ackWorldTime = BitConverter.ToDouble(ack.payload, 28);
Console.WriteLine($"HELLO_ACK_OK playerId={playerId} ackWorldTime={ackWorldTime:F1}");

// ---- 1b) 接收通道对照：Ping -> Pong（证明握手后服务端能主动发包给本会话） ----
frames.Clear();
var pingBytes = WrapFrame(4, 100, new List<byte>());
kcp.Send(pingBytes, 0, pingBytes.Length);
kcp.Flush();
Pump(1500);
var gotPong = frames.Any(f => f.kind == 5);
Console.WriteLine(gotPong ? "PROBE_PING_OK (收到 Pong)" : "PROBE_PING_FAIL (未收到 Pong)");

// ---- 1c) 无状态对照：ServerInfoRequest(9) -> ServerInfoResponse(10) ----
frames.Clear();
var infoBytes = WrapFrame(9, 0, new List<byte>());
kcp.Send(infoBytes, 0, infoBytes.Length);
kcp.Flush();
Pump(1500);
var gotInfo = frames.Any(f => f.kind == 10);
Console.WriteLine(gotInfo ? "PROBE_INFO_OK (收到 ServerInfoResponse)" : "PROBE_INFO_FAIL (未收到 ServerInfoResponse)");

// ---- 1d) 服务端心跳 Ping 应该在 2 秒内到达 ----
frames.Clear();
Pump(3000);
var serverPings = frames.Count(f => f.kind == 4);
Console.WriteLine($"PROBE_HEARTBEAT serverPings={serverPings} (期望 >=1)");

if (mode == "timewarp")
{
    // 时间加速裁决验证：发 Request，看服务端回 Applied 还是 Notice。
    // 单人（在线=1）：倍率 1 才放行；倍率>1 且没人控制火箭 -> 拒绝。
    // 多人（在线>1）：个人 1~5x 放行（且必须回给请求者本人）；超范围 -> 拒绝。
    double mult = args.Length > 3 ? double.Parse(args[3]) : 3.0;
    frames.Clear();
    var tw = new List<byte> { 24 };            // PacketType.TimeWarp
    tw.Add(0);                                 // Operation = Request
    Put32(tw, 0);                              // VoteId
    Put32(tw, playerId);                       // RequesterId（服务端会用认证身份覆盖）
    PutVarString(tw, "probe");                 // RequesterName
    tw.AddRange(BitConverter.GetBytes(mult));  // Multiplier
    tw.Add(0);                                 // Approved
    tw.AddRange(BitConverter.GetBytes(0.0));   // WorldTime
    Put32(tw, 0);                              // TimeoutSeconds
    PutVarString(tw, "");                      // Message
    var twBytes = WrapFrame(KindPacket, 50, tw);
    kcp.Send(twBytes, 0, twBytes.Length);
    kcp.Flush();
    Console.WriteLine($"SENT TimeWarp request multiplier={mult}");

    Pump(2500);

    static string ReadVarString(byte[] p, ref int off)
    {
        uint len = 0; int shift = 0;
        while (true) { byte bb = p[off++]; len |= (uint)(bb & 0x7F) << shift; if ((bb & 0x80) == 0) break; shift += 7; }
        string s = Encoding.UTF8.GetString(p, off, (int)len);
        off += (int)len;
        return s;
    }

    var replies = frames.Where(f => f.kind == KindPacket && f.payload is { Length: > 0 } && f.payload[0] == 24).ToList();
    Console.WriteLine($"RECV TimeWarp replies={replies.Count}");
    foreach (var f in replies)
    {
        // 注意：这个线格式里 bool 只占 1 **位**（Lidgren 风格位打包），所以 approved 之后的
        // world_time / timeout / message 全部是位对齐的，不能按字节解。
        // 前缀（type/operation/voteId/requesterId/name/multiplier）是字节对齐的，够做断言了；
        // 尾部原样打 hex 留档。
        try {
        int off = 1;
        byte op = f.payload[off]; off += 1;
        int voteId = BitConverter.ToInt32(f.payload, off); off += 4;
        int reqId = BitConverter.ToInt32(f.payload, off); off += 4;
        string reqName = ReadVarString(f.payload, ref off);
        double m = BitConverter.ToDouble(f.payload, off); off += 8;
        bool approved = (f.payload[off] & 0x01) != 0;   // 位序 LSB-first（实测 0xA3/0x75 最低位都是 1）
        string opName = op switch { 0 => "Request", 3 => "Applied", 4 => "Cancelled", 5 => "Notice", _ => $"op{op}" };
        Console.WriteLine($"  <- TimeWarp {opName} mult={m} approved={approved} reqId={reqId} voteId={voteId} name=\"{reqName}\"");
        Console.WriteLine($"     tail({f.payload.Length - off}B)={BitConverter.ToString(f.payload, off, f.payload.Length - off)}");
        } catch (Exception ex) { Console.WriteLine($"  decode failed: {ex.Message}"); }
    }
    Console.WriteLine(replies.Count > 0 ? "TIMEWARP_RESULT 收到回执" : "TIMEWARP_RESULT 未收到任何回执");
    return;
}
if (mode == "doublehello")
{
    // 复现"客户端握手重发"：ack 到达前客户端还会按指数退避重发 Hello。
    // 期望：重发的 Hello 只拿到 ack，【不再重放整份世界快照】。
    int baselineCreate = frames.Count(f => f.kind == KindPacket && f.payload is { Length: > 0 } && f.payload[0] == 9);
    Console.WriteLine($"DOUBLE_HELLO 首次加入收到的 CreateRocket 快照={baselineCreate}");

    frames.Clear();
    Thread.Sleep(150);
    kcp.Send(helloBytes, 0, helloBytes.Length);   // 重发同一个 Hello
    kcp.Flush();
    Pump(6000);

    int acks = frames.Count(f => f.kind == KindHelloAck);
    int create = frames.Count(f => f.kind == KindPacket && f.payload is { Length: > 0 } && f.payload[0] == 9);
    int connected = frames.Count(f => f.kind == KindPacket && f.payload is { Length: > 0 } && f.payload[0] == 2);
    int timewarp = frames.Count(f => f.kind == KindPacket && f.payload is { Length: > 0 } && f.payload[0] == 24);
    Console.WriteLine($"DOUBLE_HELLO 重发后 HelloAck={acks} CreateRocket={create} PlayerConnected={connected} TimeWarp={timewarp}");
    Console.WriteLine(create == 0
        ? "DOUBLE_HELLO_OK 重发只回 ack，没有重放世界快照（已修复）"
        : "DOUBLE_HELLO_BAD 重发仍然重放了初始状态（未修复）");
}

if (mode == "upload")
{
    // 端到端验证 UploadLog 线格式：type(31) + varint string + varint string + i32 长度 + gzip 字节
    var lines = new List<string>();
    for (int i = 0; i < 500; i++) lines.Add($"probe log line {i} —— 用于验证压缩上传链路");
    byte[] raw = Encoding.UTF8.GetBytes(string.Join("\n", lines));
    byte[] gz;
    using (var ms = new MemoryStream())
    {
        using (var gzStream = new System.IO.Compression.GZipStream(
            ms, System.IO.Compression.CompressionMode.Compress, true))
        {
            gzStream.Write(raw, 0, raw.Length);
        }
        gz = ms.ToArray();
    }
    var uploadPayload = new List<byte> { 31 };   // PacketType.UploadLog
    PutVarString(uploadPayload, "probe.log");
    PutVarString(uploadPayload, "probe-e2e");
    Put32(uploadPayload, gz.Length);
    uploadPayload.AddRange(gz);
    var uploadBytes = WrapFrame(KindPacket, 40, uploadPayload);
    kcp.Send(uploadBytes, 0, uploadBytes.Length);
    kcp.Flush();
    Console.WriteLine($"SENT UploadLog gz={gz.Length} raw={raw.Length} player={playerId}");
    Pump(2500);
    Console.WriteLine("UPLOAD_DONE");
    return;
}

if (mode == "time")
{
    // 时间链验证：回 Pong 给服务端 Ping，打印服务端下发的 UpdateWorldTime（应递增）
    var deadline = DateTime.UtcNow.AddSeconds(16);
    int seenPings = 0, seenWorldTimes = 0;
    while (DateTime.UtcNow < deadline)
    {
        frames.Clear();
        Pump(400);
        foreach (var f in frames)
        {
            if (f.kind == 4 && f.payload != null)
            {
                seenPings++;
                // 必须原样回显服务端 Ping 的 payload（服务端校验 sent_ticks == last_ping_ticks）
                var pongBytes = WrapFrame(5, f.seq, new List<byte>(f.payload));
                kcp.Send(pongBytes, 0, pongBytes.Length);
                kcp.Flush();
                Console.WriteLine($"  <- Ping seq={f.seq} payload={f.payload.Length}B  已回 Pong");
            }
            else if (f.kind == 3 && f.payload != null && f.payload.Length >= 9 && f.payload[0] == 6)
            {
                double wt = BitConverter.ToDouble(f.payload, 1);
                seenWorldTimes++;
                Console.WriteLine($"  <- UpdateWorldTime = {wt:F3}  (t={DateTime.UtcNow:HH:mm:ss})");
            }
        }
    }
    Console.WriteLine($"TIME_RESULT pings={seenPings} worldTimePackets={seenWorldTimes}");
    return;
}

if (mode == "recvcheat")
{
    // 服务端侧端到端验证：握手后等服务端下发 22 号 UpdateCheatStatus，解出 enabled + 16 位位图。
    // 线上布局：Write(bool) = 1 bit，Write(ushort) = 16 bit 小端，紧接其后。
    //   dotnet run --project .smoke-kcp -- 127.0.0.1 9806 recvcheat
    Console.WriteLine("RECVCHEAT listening 8s ...");
    var cheatDeadline = DateTime.UtcNow.AddSeconds(8);
    int cheatSeen = 0;
    while (DateTime.UtcNow < cheatDeadline)
    {
        frames.Clear();
        Pump(400);
        foreach (var f in frames)
        {
            if (f.kind == KindPacket && f.payload is { Length: >= 3 } && f.payload[0] == 22)
            {
                cheatSeen++;
                int raw = f.payload[1] | (f.payload[2] << 8);   // 位流：bit0=enabled，bit1..16=flags
                bool enabled = (raw & 1) != 0;
                int flags = (raw >> 1) & 0xFFFF;
                Console.WriteLine("RECVCHEAT pkt22 enabled=" + enabled + " flags=0x" + flags.ToString("X4") + " len=" + f.payload.Length);
            }
        }
    }
    Console.WriteLine(cheatSeen > 0 ? ("RECVCHEAT_OK 收到 " + cheatSeen + " 个 22 号策略包") : "RECVCHEAT_NONE 没收到 22 号包");
    return;
}

if (mode == "snaps")
{
    // 快照限流验证：客户端对"附近每一枚火箭"各发一条 RequestRocketSnapshot（TcpFrameKind=8），
    // 一次性突发（与 LocalManager.RequestNearbyRocketSnapshots 的真实行为一致），
    // 数服务端到底回了几份 CreateRocket(type=9)。
    //   dotnet run -- 192.168.1.5 9806 snaps 6
    // 初始快照在握手阶段就到达并被上面的对照组清空了，这里主动再要一份世界快照来采收火箭 ID。
    frames.Clear();
    var wsBytes = WrapFrame(7, 150, new List<byte>());   // 7 = RequestWorldSnapshot
    kcp.Send(wsBytes, 0, wsBytes.Length);
    kcp.Flush();
    Pump(2500);
    var ids = frames.Where(f => f.kind == KindPacket && f.payload is { Length: > 17 } && f.payload[0] == 9)
                    .Select(f => BitConverter.ToInt32(f.payload, 13)).Distinct().OrderBy(x => x).ToList();
    Console.WriteLine($"SNAPS world rockets harvested={ids.Count} ids=[{string.Join(",", ids)}]");
    var sizes = frames.Where(f => f.kind == KindPacket && f.payload is { Length: > 17 } && f.payload[0] == 9)
                      .Select(f => f.payload.Length).ToList();
    if (sizes.Count > 0)
        Console.WriteLine($"SNAPS snapshot payload bytes min={sizes.Min()} max={sizes.Max()} avg={(int)sizes.Average()}");
    int take = args.Length > 3 ? int.Parse(args[3]) : Math.Min(6, ids.Count);
    var pick = ids.Take(take).ToList();
    if (pick.Count == 0) { Console.WriteLine("SNAPS_NO_ROCKETS"); return; }

    frames.Clear();
    int snapSeq = 200;
    foreach (var id in pick)
    {
        var payload = new List<byte>();
        Put32(payload, id);
        var b = WrapFrame(8, snapSeq++, payload);   // 8 = RequestRocketSnapshot
        kcp.Send(b, 0, b.Length);
        kcp.Flush();
        if (args.Length > 4 && args[4] == "spaced") Pump(1200);   // 对照组：请求间隔 > 1s
    }
    Console.WriteLine($"SENT RequestRocketSnapshot x{pick.Count} (burst) ids=[{string.Join(",", pick)}]");

    Pump(3000);
    var replies = frames.Where(f => f.kind == KindPacket && f.payload is { Length: > 17 } && f.payload[0] == 9)
                        .Select(f => BitConverter.ToInt32(f.payload, 13)).ToList();
    Console.WriteLine($"SNAPS requested={pick.Count} replied={replies.Count} ids=[{string.Join(",", replies)}]");
    Console.WriteLine(replies.Count == pick.Count
        ? "SNAPS_OK 每个请求都在 1 次突发里被应答"
        : $"SNAPS_THROTTLED 只应答 {replies.Count}/{pick.Count} —— 限流键疑似只按连接，不按火箭");
    return;
}

if (mode == "watchparts")
{
    // 不需要任何客户端动作：每隔几秒请求一次世界快照，打印每枚火箭的【快照载荷长度】。
    // 载荷长度与该火箭的结构（零件数/名字/位置）一一对应 —— 长度变了 = 服务端手里的结构变了。
    // 人在挂机、火箭静止时长度若还在变，说明"持续制造分叉"的那一方就是服务端。
    //   dotnet run -- 192.168.1.5 9806 watchparts 60
    int seconds = args.Length > 3 ? int.Parse(args[3]) : 60;
    var deadline = DateTime.UtcNow.AddSeconds(seconds);
    while (DateTime.UtcNow < deadline)
    {
        frames.Clear();
        var ws = WrapFrame(7, 150, new List<byte>());
        kcp.Send(ws, 0, ws.Length);
        kcp.Flush();
        Pump(2500);
        var snap = frames.Where(f => f.kind == KindPacket && f.payload is { Length: > 17 } && f.payload[0] == 9).ToList();
        var parts = snap.OrderBy(f => BitConverter.ToInt32(f.payload, 13))
                        .Select(f => BitConverter.ToInt32(f.payload, 13) + ":" + f.payload.Length);
        Console.WriteLine("WATCH " + DateTime.UtcNow.ToString("HH:mm:ss") + "  rockets=" + snap.Count + "  " + string.Join(" ", parts));
        Pump(1500);
    }
    return;
}

if (mode == "recvhash")
{
    // 回归测试接收端：监听 15 秒，把收到的 UpdateRocketPrimary(type=11) 及其【末尾 4 字节】structure_hash 打出来。
    // 用来验证新加的字段在 客户端->服务端->客户端 全程不被破坏。
    //   dotnet run -- 192.168.1.5 9806 recvhash
    Console.WriteLine("RECVHASH listening 15s ...");
    var deadline = DateTime.UtcNow.AddSeconds(15);
    var seen = new List<string>();
    while (DateTime.UtcNow < deadline)
    {
        frames.Clear();
        Pump(400);
        foreach (var f in frames)
        {
            if (f.kind == KindPacket && f.payload is { Length: > 30 } && f.payload[0] == 11)
            {
                int len = f.payload.Length;
                uint hash = BitConverter.ToUInt32(f.payload, len - 4);
                int rid = BitConverter.ToInt32(f.payload, 9);
                double wx = BitConverter.ToDouble(f.payload, 13);
                seen.Add("rocket=" + rid + " tail=0x" + hash.ToString("X8") + " x=" + wx.ToString("F1") + " len=" + len);
            }
        }
    }
    Console.WriteLine("RECVHASH packets=" + seen.Count);
    // 按火箭分组：同一个火箭的尾部值应稳定；不同火箭的尾部值应不同（结构不同 -> 哈希不同）
    var byRocket = new Dictionary<string, HashSet<string>>();
    foreach (var s in seen)
    {
        var key = s.Split(' ')[0];
        var tail = s.Split(' ')[1];
        if (!byRocket.TryGetValue(key, out var set)) { set = new HashSet<string>(); byRocket[key] = set; }
        set.Add(tail);
        if (set.Count > 1) { Console.WriteLine("  !! " + key + " 尾部值不稳定: " + string.Join(",", set)); }
    }
    Console.WriteLine("RECVHASH distinct rockets=" + byRocket.Count);
    foreach (var kv in byRocket) Console.WriteLine("  " + kv.Key + " tails=" + string.Join(",", kv.Value));
    Console.WriteLine(byRocket.Count >= 2 && byRocket.Values.All(v => v.Count == 1)
        ? "RECVHASH_STRONG 每个火箭尾部值稳定，且不同火箭不同 -> 结构相关字段"
        : "RECVHASH_WEAK 只有 1 枚火箭或尾部值不稳定 -> 结论不够强");
    return;
}

if (mode == "sendhash")
{
    // 回归测试发送端：先声明控制某枚火箭（取得权威），再发 12 个带 marker 哈希的 primary。
    //   dotnet run -- 192.168.1.5 9806 sendhash <rocketId> <marker>
    int rocketId = args.Length > 3 ? int.Parse(args[3]) : -1;
    uint marker = args.Length > 4 ? uint.Parse(args[4]) : 0xDEADBEEFu;
    if (rocketId < 0) { Console.WriteLine("SENDHASH 需要 rocketId"); return; }
    var ctl = new List<byte> { 4 };
    Put32(ctl, playerId);
    Put32(ctl, rocketId);
    var cb = WrapFrame(KindPacket, 60, ctl);
    kcp.Send(cb, 0, cb.Length);
    kcp.Flush();
    Console.WriteLine("SENT UpdatePlayerControl rocket=" + rocketId);
    Pump(1500);
    for (int i = 0; i < 12; i++)
    {
        var p = new List<byte> { 11 };
        p.AddRange(BitConverter.GetBytes(1234.5));
        Put32(p, rocketId);
        p.AddRange(BitConverter.GetBytes(1.0));
        p.AddRange(BitConverter.GetBytes(2.0));
        p.AddRange(BitConverter.GetBytes(3.0));
        p.AddRange(BitConverter.GetBytes(4.0));
        PutVarString(p, "Earth");
        p.AddRange(BitConverter.GetBytes(0.5f));
        p.AddRange(BitConverter.GetBytes(0.25f));
        Put32(p, (int)marker);
        var fb = WrapFrame(KindPacket, 61 + i, p);
        kcp.Send(fb, 0, fb.Length);
        kcp.Flush();
        Pump(500);
    }
    Console.WriteLine("SENT primary x12 marker=0x" + marker.ToString("X8"));
    return;
}

if (mode != "control") return;

// ---- 2) 控制权请求复现 ----
// 依次请求：-1（释放）、9999（服务端不存在的火箭），可再用命令行传入真实火箭 ID
var requestIds = new List<int> { -1, 9999 };
if (args.Length > 3 && int.TryParse(args[3], out var realId)) requestIds.Add(realId);
foreach (var rocketId in requestIds)
{
    frames.Clear();
    var payload = new List<byte> { PacketUpdatePlayerControl };
    Put32(payload, playerId);
    Put32(payload, rocketId);
    var controlBytes = WrapFrame(KindPacket, 3, payload);
    kcp.Send(controlBytes, 0, controlBytes.Length);
    kcp.Flush();
    Console.WriteLine($"SENT UpdatePlayerControl player={playerId} rocket={rocketId}");

    Pump(2500);

    var kinds = frames.Select(f => f.kind).Distinct().OrderBy(k => k).ToArray();
    Console.WriteLine($"RECV frames={frames.Count} kinds=[{string.Join(",", kinds)}]");
    foreach (var f in frames)
    {
        var desc = f.kind == KindPacket && f.payload.Length > 0
            ? $"packetType={f.payload[0]}"
            : "frame";
        Console.WriteLine($"  kind={f.kind} seq={f.seq} {desc}");
    }
    var echoed = frames.Any(f => f.kind == KindPacket && f.payload.Length >= 9 && f.payload[0] == PacketUpdatePlayerControl);
    Console.WriteLine(echoed
        ? $"RESULT rocket={rocketId}: 收到控制回执 packetType=4 -> 正常"
        : $"RESULT rocket={rocketId}: 请求者未收到控制回执 packetType=4 -> 缺陷复现");
}
