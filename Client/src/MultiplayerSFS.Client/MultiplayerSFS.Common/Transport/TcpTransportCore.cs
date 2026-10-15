// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using MultiplayerSFS.Common.Transport.Kcp;

namespace MultiplayerSFS.Common;

public sealed partial class TcpClientTransport : IDisposable
{
    private readonly object stateLock = new object();
    private readonly object statsLock = new object();
    private readonly Queue<TcpFrame> incoming = new Queue<TcpFrame>();
    // KCP 接收缓冲：整块 byte[] + 读写偏移，复用不分配。
    // 原先每收一个数据报都会 new byte[65536]（ProcessKcpReceived 与 ReceiveKcpMessage 各一次），
    // 状态流每秒几十个包 → 每秒数 MB 垃圾 → GC 停顿（表现为周期性卡顿 + CPU 尖峰）。
    // 只由 KCP 接收线程使用，无需加锁。
    private byte[] kcpReceiveBuffer = new byte[65536];
    // KCP 上下文被三个线程并发使用：主线程发送/Flush、KcpUpdateLoop 的 Update/Check、
    // KcpReceiveLoop 的 Input/Recv。KcpContext 内部没有任何同步，并发下会让内部的
    // 无锁链表与共享编码缓冲被同时读写 —— 丢段、重传风暴、报文错乱（表现为"数据断流"）。
    // 这里用一把锁把所有调用点串起来，不改协议、不改 KCP 参数。
    private int kcpReceiveStart;
    private int kcpReceiveEnd;
    private byte[] kcpChunkScratch = new byte[65536];
    private byte[] kcpBodyScratch = new byte[65536];

    private KcpContext kcpContext;
    private UdpClient udpSocket;
    private IPEndPoint serverEndpoint;
    private CancellationTokenSource cancellation;
    private Task kcpUpdateTask;
    private Task kcpReceiveTask;
    private uint conv;

    // KCP 内部状态被三个线程共用（接收循环 Input/Recv、更新循环 Update/Check/Flush、主线程 Send/Flush）。
    // 实测：撤掉这把锁后 KCP 首次连接就会失败（需要重连），证明 race 真实存在 —— 锁是必要的。
    private readonly object kcpLock = new object();
    private long lastReceiveTicks;
    private long sentBytes;
    private long receivedBytes;
    private long sentFrames;
    private long receivedFrames;
    private double roundTripMs;
    private double jitterMs;
    // RTT 采样窗口：保留最近 5 次，显示的 RoundTripMs/JitterMs 用这些采样的平均值
    private const int RoundTripSampleWindow = 5;
    private readonly double[] roundTripSamples = new double[RoundTripSampleWindow];
    private int roundTripSampleIndex;
    private int roundTripSampleFilled;
    private string lastDisconnectReason = "Not connected";
    private string lastPacketType = "None";
    private string remoteAddress = string.Empty;
    private volatile bool connected;
    private int disconnectQueued;
    private int sequence;

    // 自适应档位缓存：NetworkAdaptationPolicy.Evaluate 每次调用都会 new 一个 NetworkAdaptiveProfile，
    // 且 RoundTripMs/JitterMs/QueueCount 每次都要加三次 statsLock。而 AdaptiveProfile 会被插值器
    // 【每帧 × 每枚远端火箭】读取（SetState / DelayedWorldTime / IsPacketDue），发送循环每个 tick 也要读，
    // 等于每秒数千次对象分配 + 锁竞争。RTT 采样本来就是 2 秒一次（窗口 5 次取均值），
    // 档位变化远慢于 250ms，按 250ms 缓存一份等价。
    private const long AdaptiveProfileRefreshTicks = TimeSpan.TicksPerMillisecond * 250;
    private NetworkAdaptiveProfile adaptiveProfileCache;
    private long adaptiveProfileCacheTicks;

    public UdpClientTransport UdpTransport => null;

    public bool Connected => connected;

    public int QueueCount
    {
        get
        {
            var context = kcpContext;
            if (context == null) return 0;
            // KCP 内部窗口计数可能在关闭竞争时短暂出现负值，UI 只显示可发送的待处理量。
            return Math.Max(0, context.WaitSnd);
        }
    }

    public long OverwrittenStates => 0;

    public long SentBytes => Interlocked.Read(ref sentBytes);

    public long ReceivedBytes => Interlocked.Read(ref receivedBytes);

    public long SentFrames => Interlocked.Read(ref sentFrames);

    public long ReceivedFrames => Interlocked.Read(ref receivedFrames);

    public string LastDisconnectReason { get { lock (statsLock) return lastDisconnectReason; } }

    public string LastPacketType { get { lock (statsLock) return lastPacketType; } set { lock (statsLock) lastPacketType = value; } }

    public string RemoteAddress { get { lock (statsLock) return remoteAddress; } }

    public double RoundTripMs { get { lock (statsLock) return roundTripMs; } }

    public double JitterMs { get { lock (statsLock) return jitterMs; } }

    public NetworkAdaptiveProfile AdaptiveProfile
    {
        get
        {
            long now = DateTime.UtcNow.Ticks;
            NetworkAdaptiveProfile cached = adaptiveProfileCache;
            if (cached != null && now - adaptiveProfileCacheTicks < AdaptiveProfileRefreshTicks)
            {
                return cached;
            }
            NetworkAdaptiveProfile fresh = NetworkAdaptationPolicy.Evaluate(RoundTripMs, JitterMs, QueueCount);
            adaptiveProfileCache = fresh;
            adaptiveProfileCacheTicks = now;
            return fresh;
        }
    }

    public double SecondsSinceReceive
    {
        get
        {
            long ticks = Interlocked.Read(ref lastReceiveTicks);
            return ticks == 0 ? double.PositiveInfinity : Math.Max(0, (DateTime.UtcNow.Ticks - ticks) / (double)TimeSpan.TicksPerSecond);
        }
    }

    public async Task<Packet_JoinResponse> ResumeAsync(IPAddress address, int port, Packet_JoinRequest request)
    {
        if (string.IsNullOrEmpty(request.ResumeToken) || request.ResumePlayerId < 0)
            throw new InvalidOperationException("No active session token is available for resume.");
        return await ConnectCoreAsync(address, port, request, true).ConfigureAwait(false);
    }

    // conv 必须是随机的：以前用 Environment.TickCount（毫秒），同一毫秒内两次建连
    // （快速重试 / 立即重进世界）会撞同一个 conv —— 服务端把新会话当成"重复 Hello"，
    // 只回 ack 而跳过整个初始状态，客户端拿到 ack 却世界是空的（"第一次加入像失败，
    // 第二次百分百成功"）。这里用"进程随机种子 + 递增计数 + 时钟"保证同毫秒也互不相同。
    private static int convSeed = Guid.NewGuid().GetHashCode();
    private static uint convCounter;

    private static uint NewRandomConv()
    {
        uint seq = unchecked(convCounter++);
        uint value = unchecked((uint)convSeed ^ (seq * 2654435761u) ^ (uint)Environment.TickCount);
        return value == 0 ? 1u : value;
    }

    public async Task<Packet_JoinResponse> ConnectAsync(IPAddress address, int port, Packet_JoinRequest request)
    {
        return await ConnectCoreAsync(address, port, request, false).ConfigureAwait(false);
    }

    private async Task<Packet_JoinResponse> ConnectCoreAsync(IPAddress address, int port, Packet_JoinRequest request, bool preserveSession)
    {
        if (!preserveSession) Disconnect("Reconnecting");

        if (address == null || IPAddress.Any.Equals(address) || IPAddress.IPv6Any.Equals(address))
        {
            throw new ArgumentException("Enter the real IP address of the server; 0.0.0.0 and :: are not allowed.", nameof(address));
        }

        serverEndpoint = new IPEndPoint(address, port);
        udpSocket = new UdpClient(address.AddressFamily);
        // 同 UdpTransport：UdpClient(AddressFamily) 是未绑定 socket，ReceiveAsync 会抛
        // "在执行此操作前必须先调用 Bind 方法"，KCP 接收循环一起步就死。
        udpSocket.Client.Bind(new IPEndPoint(
            address.AddressFamily == AddressFamily.InterNetworkV6 ? IPAddress.IPv6Any : IPAddress.Any,
            0));
        UdpSocketGuard.DisableConnReset(udpSocket);   // 见 UdpSocketGuard：不关会被 ICMP 10054 掐死接收路径
  // 扩大内核收发缓冲：默认值（Windows 64 KB / Linux 256 KB）在满速同步下会被内核丢包，
  // 而内核丢掉的包对上层就是"丢包"，KCP 只能超时重传 —— 实测把 srtt 从 2ms 抬到近 1 秒，
  // 最后服务端按 heartbeat timeout 把整条会话判死（"玩到一半突然卡顿"）。
  // 服务端对称地在 Server/cpp/src/net/kcp_transport.cpp 里设同样的值。
  udpSocket.Client.ReceiveBufferSize = 1 << 20;   // 1 MB
  udpSocket.Client.SendBufferSize = 1 << 20;
        conv = NewRandomConv();

        cancellation = new CancellationTokenSource();

        kcpContext = new KcpContext(conv, OnKcpOutput);
        // 快速重传阈值（resend）= 0：只走超时重传。resend=2 会在抖动链路上把延迟抖动
        // 误判成丢包并抢先重传，挤占本就拥塞的链路形成正反馈。本协议不靠重传保证送达：
        // 丢掉的最新状态包由下一个状态包补上，位置偏差过大走 200 米硬同步。
        // 必须与服务端一致（Server/cpp/src/net/kcp_transport.hpp 的 kFastResend）。
        KcpTransportPresets.Apply(kcpContext, KcpTransportPresets.Relay);

        connected = true;
        Interlocked.Exchange(ref disconnectQueued, 0);
        Interlocked.Exchange(ref lastReceiveTicks, DateTime.UtcNow.Ticks);
        lock (incoming) incoming.Clear();
        lock (statsLock)
        {
            remoteAddress = address + ":" + port;
            lastDisconnectReason = string.Empty;
        }

        kcpReceiveTask = Task.Run(() =>
        {
            try { KcpReceiveLoop(cancellation.Token); }
            catch (System.Exception ex) { System.Console.Error.WriteLine("[SFS-MP] KCP receive thread exception: " + ex); }
        });
        kcpUpdateTask = Task.Run(() =>
        {
            try { KcpUpdateLoop(cancellation.Token); }
            catch (System.Exception ex) { System.Console.Error.WriteLine("[SFS-MP] KCP update thread exception: " + ex); }
        });

        var helloPayload = SessionHandshakeCodec.EncodeHello(request);
        var helloFrame = new TcpFrame(TcpFrameKind.Hello, SessionHandshakeCodec.Version,
            helloPayload, helloPayload.Length * 8);
        var helloData = TcpFrameCodec.Encode(helloFrame);
        SendKcpMessage(helloData);
        lock (kcpLock) kcpContext.Flush();

        var startTime = DateTime.UtcNow;
        Packet_JoinResponse response = null;

        int handshakeRetry = 0;
        var nextHelloTime = DateTime.UtcNow;

        while (DateTime.UtcNow - startTime < TimeSpan.FromSeconds(10))
        {
            if (cancellation.IsCancellationRequested)
                throw new TimeoutException("KCP connection timed out.");

            // 定期重发 Hello（指数退避：200ms, 400ms, 800ms... 最大 2s）
            if (DateTime.UtcNow >= nextHelloTime)
            {
                handshakeRetry++;
                var delay = Math.Min(200 * (1 << Math.Min(handshakeRetry - 1, 4)), 2000);
                nextHelloTime = DateTime.UtcNow.AddMilliseconds(delay);
                SendKcpMessage(helloData);
                lock (kcpLock) kcpContext.Flush();
            }

            while (TryDequeueIncoming(out var frame))
            {
                if (frame.Kind == TcpFrameKind.HelloAck && frame.Sequence == SessionHandshakeCodec.Version)
                {
                    response = SessionHandshakeCodec.DecodeAck(frame.Payload);
                    break;
                }
                if (frame.Kind == TcpFrameKind.Disconnect)
                    throw new InvalidOperationException(Encoding.UTF8.GetString(frame.Payload));
            }
            if (response != null) break;
            await Task.Delay(10).ConfigureAwait(false);
        }

        if (response == null)
        {
            Disconnect("Handshake timeout");
            throw new TimeoutException("KCP handshake timed out.");
        }

        return response;
    }

    private void OnKcpOutput(byte[] data, KcpContext context)
    {
        try
        {
            if (udpSocket != null && serverEndpoint != null)
            {
                udpSocket.Send(data, data.Length, serverEndpoint);
                Interlocked.Add(ref sentBytes, data.Length);
            }
        }
        catch { }
    }

    private void KcpReceiveLoop(CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                UdpReceiveResult result;
                try
                {
                    result = udpSocket.ReceiveAsync().GetAwaiter().GetResult();
                }
                catch (ObjectDisposedException) when (token.IsCancellationRequested)
                {
                    return;
                }
                catch (SocketException ex) when (!token.IsCancellationRequested)
                {
                    LogWarningSafe("[SFS-MP] KCP UDP receive failed: " + ex.Message);
                    Thread.Sleep(100);
                    continue;
                }

                try
                {
                    lock (kcpLock) kcpContext.Input(result.Buffer, 0, result.Buffer.Length);
                    ProcessKcpReceived();
                }
                catch (Exception ex)
                {
                    LogWarningSafe("[SFS-MP] KCP packet rejected: " + ex.Message);
                    // 丢掉残帧重新同步：不复位游标时，之后每个数据报都会在同一偏移重复抛同一个异常，
                    // 真实数据在缓冲里越积越多却永远解析不出来（也不会触发断线心跳，因为只有成功解码
                    // 的帧才更新接收时间）—— 接收路径被永久毒化，表现为"数据断流"，只能重启进程。
                    kcpReceiveStart = 0;
                    kcpReceiveEnd = 0;
                }
            }
        }
        catch (Exception ex) when (!token.IsCancellationRequested)
        {
            SignalDisconnect("KCP receive loop stopped: " + ex.Message, true);
        }
    }

    private void ProcessKcpReceived()
    {
        byte[] buffer = kcpBodyScratch;   // 复用：原先每收一个数据报都 new byte[65536]
        while (true)
        {
            int len = ReceiveKcpMessage(ref buffer);   // 极大帧会在内部换更大的缓冲
            if (len <= 0) break;

            Interlocked.Exchange(ref lastReceiveTicks, DateTime.UtcNow.Ticks);
            Interlocked.Add(ref receivedBytes, len);
            Interlocked.Increment(ref receivedFrames);

            // DecodeBody 会把 payload 复制成新数组，所以这里可以安全复用 buffer
            var frame = TcpFrameCodec.DecodeBody(buffer, len);
            TrackPacketType(frame);

            if (frame.Kind == TcpFrameKind.Ping)
            {
                var pong = new TcpFrame(TcpFrameKind.Pong, frame.Sequence, frame.Payload, frame.PayloadBits);
                SendKcpMessage(TcpFrameCodec.Encode(pong));
                lock (kcpLock) kcpContext.Flush();
                continue;
            }
            if (frame.Kind == TcpFrameKind.Pong)
            {
                // 服务端把我们的 Ping payload（发包时刻的 ticks）原样回显，用它算往返延迟
                if (frame.Payload != null && frame.Payload.Length >= 8)
                {
                    long sentTicks = BitConverter.ToInt64(frame.Payload, 0);
                    long deltaTicks = DateTime.UtcNow.Ticks - sentTicks;
                    if (deltaTicks > 0) RecordRoundTripSample(deltaTicks / 10000.0);
                }
                continue;
            }
            if (frame.Kind == TcpFrameKind.Disconnect)
            {
                SignalDisconnect(Encoding.UTF8.GetString(frame.Payload), false);
                return;
            }
            lock (incoming) incoming.Enqueue(frame);
        }
        kcpBodyScratch = buffer;   // 若因大帧换过缓冲，保留新缓冲继续复用
    }

    private void KcpUpdateLoop(CancellationToken token)
    {
        var lastPingTime = DateTime.UtcNow;
        try
        {
            while (!token.IsCancellationRequested)
            {
                uint current = (uint)Environment.TickCount;
                lock (kcpLock) kcpContext.Update(current);

                if (DateTime.UtcNow - lastPingTime > TimeSpan.FromSeconds(2))
                {
                    lastPingTime = DateTime.UtcNow;
                    long ticks = DateTime.UtcNow.Ticks;
                    byte[] payload = BitConverter.GetBytes(ticks);
                    var pingFrame = new TcpFrame(TcpFrameKind.Ping, Interlocked.Increment(ref sequence), payload, payload.Length * 8);
                    byte[] pingData = TcpFrameCodec.Encode(pingFrame);
                    SendKcpMessage(pingData);
                    lock (kcpLock) kcpContext.Flush();
                }

                if (SecondsSinceReceive > 10)
                {
                    SignalDisconnect("KCP heartbeat timed out (10 seconds).", true);
                    return;
                }

                uint nextUpdate;
                    lock (kcpLock) { nextUpdate = kcpContext.Check(current); }
                int sleepMs = (int)Math.Max(1, Math.Min(20, nextUpdate - current));
                Thread.Sleep(sleepMs);
            }
        }
        catch (Exception ex)
        {
            if (!token.IsCancellationRequested)
                SignalDisconnect(ex.Message, true);
        }
    }

    private void SendKcpMessage(byte[] frameData)
    {
        // 之前 sentFrames 只有声明/读取/清零，从来没有自增 —— 日志里的 sent= 恒为 0，
        // 会让人误判「客户端一个包都没发」。这里补上真正的计数。
        Interlocked.Increment(ref sentFrames);
        byte[] message = new byte[4 + frameData.Length];
        message[0] = (byte)frameData.Length;
        message[1] = (byte)(frameData.Length >> 8);
        message[2] = (byte)(frameData.Length >> 16);
        message[3] = (byte)(frameData.Length >> 24);
        Buffer.BlockCopy(frameData, 0, message, 4, frameData.Length);
        lock (kcpLock) kcpContext.Send(message, 0, message.Length);
    }

    private int ReceiveKcpMessage(ref byte[] buffer)
    {
        // 1) 把 KCP 已就绪的数据搬进复用缓冲（不分配）
        while (true)
        {
            int received;
            lock (kcpLock) { received = kcpContext.Recv(kcpChunkScratch, 0, kcpChunkScratch.Length); }
            if (received <= 0) break;
            EnsureReceiveCapacity(received);
            Buffer.BlockCopy(kcpChunkScratch, 0, kcpReceiveBuffer, kcpReceiveEnd, received);
            kcpReceiveEnd += received;
        }

        // 2) 解析外层长度前缀 + 内层 body 长度（判定规则与原先完全一致）
        int available = kcpReceiveEnd - kcpReceiveStart;
        if (available < 4) return 0;
        int length = kcpReceiveBuffer[kcpReceiveStart]
            | (kcpReceiveBuffer[kcpReceiveStart + 1] << 8)
            | (kcpReceiveBuffer[kcpReceiveStart + 2] << 16)
            | (kcpReceiveBuffer[kcpReceiveStart + 3] << 24);
        if (length < 13 || length > TcpFrameCodec.MaxPayloadBytes + 13)
            throw new InvalidDataException("Invalid KCP message length: " + length);
        if (available < length + 4) return 0;
        int bodyLength = kcpReceiveBuffer[kcpReceiveStart + 4]
            | (kcpReceiveBuffer[kcpReceiveStart + 5] << 8)
            | (kcpReceiveBuffer[kcpReceiveStart + 6] << 16)
            | (kcpReceiveBuffer[kcpReceiveStart + 7] << 24);
        if (bodyLength < 9 || bodyLength + 4 != length)
            throw new InvalidDataException("Invalid nested TCP frame length.");
        if (bodyLength > buffer.Length)
        {
            // 极大帧（如整枚大火箭的 CreateRocket）：让调用方缓冲长大后再回来，不消费数据
            // （原先这里是 new byte[len] 每次分配，改成复用缓冲后必须显式扩容）
            buffer = new byte[bodyLength];
        }

        Buffer.BlockCopy(kcpReceiveBuffer, kcpReceiveStart + 8, buffer, 0, bodyLength);
        kcpReceiveStart += length + 4;
        if (kcpReceiveStart >= kcpReceiveEnd)
        {
            // 全部消费完，归零复用，避免缓冲无限增长
            kcpReceiveStart = 0;
            kcpReceiveEnd = 0;
        }
        return bodyLength;
    }

    // 保证复用缓冲还有 extra 字节空间：先挪掉已消费前缀，实在不够才扩容（正常情况零分配）
    private void EnsureReceiveCapacity(int extra)
    {
        if (kcpReceiveBuffer.Length - kcpReceiveEnd >= extra) return;
        if (kcpReceiveStart > 0)
        {
            int used = kcpReceiveEnd - kcpReceiveStart;
            Buffer.BlockCopy(kcpReceiveBuffer, kcpReceiveStart, kcpReceiveBuffer, 0, used);
            kcpReceiveStart = 0;
            kcpReceiveEnd = used;
        }
        if (kcpReceiveBuffer.Length - kcpReceiveEnd >= extra) return;
        int needed = kcpReceiveEnd + extra;
        int newSize = kcpReceiveBuffer.Length;
        while (newSize < needed) newSize *= 2;
        Array.Resize(ref kcpReceiveBuffer, newSize);
    }
    public void Send(Packet packet)
    {
        if (!Connected || packet == null) return;

        NetPayload payload = NetPayloadCodec.Serialize(packet, true);
        var frame = new TcpFrame(TcpFrameKind.Packet, Interlocked.Increment(ref sequence), payload.Data, payload.BitLength);
        byte[] frameData = TcpFrameCodec.Encode(frame);
        SendKcpMessage(frameData);
        lock (kcpLock) kcpContext.Flush();
    }

    public static bool ShouldSendOverUdp(Packet packet)
    {
        return packet is Packet_UpdateRocketPrimary;
    }

    public static bool ShouldCoalesceState(Packet packet)
    {
        return packet is Packet_UpdateRocketPrimary;
    }

    public void RequestWorldSnapshot()
    {
        if (Connected)
        {
            var frame = new TcpFrame(TcpFrameKind.RequestWorldSnapshot, Interlocked.Increment(ref sequence), Array.Empty<byte>(), 0);
            byte[] frameData = TcpFrameCodec.Encode(frame);
            SendKcpMessage(frameData);
            lock (kcpLock) kcpContext.Flush();
        }
    }

    public void RequestRocketSnapshot(int rocketId)
    {
        if (!Connected) return;
        byte[] payload = BitConverter.GetBytes(rocketId);
        var frame = new TcpFrame(TcpFrameKind.RequestRocketSnapshot, Interlocked.Increment(ref sequence), payload, payload.Length * 8);
        byte[] frameData = TcpFrameCodec.Encode(frame);
        SendKcpMessage(frameData);
        lock (kcpLock) kcpContext.Flush();
    }

    private bool TryDequeueIncoming(out TcpFrame frame)
    {
        lock (incoming)
        {
            if (incoming.Count > 0)
            {
                frame = incoming.Dequeue();
                return true;
            }
        }
        frame = null;
        return false;
    }

    public bool TryReceive(out TcpFrame frame)
    {
        return TryDequeueIncoming(out frame);
    }

    // 记录一次 RTT 采样：维护最近 5 次，RoundTripMs 输出这些采样的平均值；
    // 采样不足 5 次时按已有次数取平均。抖动 = 相邻采样差值的平均。
    private void RecordRoundTripSample(double sampleMs)
    {
        if (double.IsNaN(sampleMs) || double.IsInfinity(sampleMs) || sampleMs <= 0) return;
        lock (statsLock)
        {
            roundTripSamples[roundTripSampleIndex] = sampleMs;
            roundTripSampleIndex = (roundTripSampleIndex + 1) % RoundTripSampleWindow;
            if (roundTripSampleFilled < RoundTripSampleWindow) roundTripSampleFilled++;

            double sum = 0;
            for (int i = 0; i < roundTripSampleFilled; i++) sum += roundTripSamples[i];
            roundTripMs = sum / roundTripSampleFilled;

            if (roundTripSampleFilled >= 2)
            {
                double jitterSum = 0;
                for (int i = 1; i < roundTripSampleFilled; i++)
                    jitterSum += Math.Abs(roundTripSamples[i] - roundTripSamples[i - 1]);
                jitterMs = jitterSum / (roundTripSampleFilled - 1);
            }
            else
            {
                jitterMs = 0;
            }
        }
    }

    public void ClearStatistics()
    {
        Interlocked.Exchange(ref sentBytes, 0);
        Interlocked.Exchange(ref receivedBytes, 0);
        Interlocked.Exchange(ref sentFrames, 0);
        Interlocked.Exchange(ref receivedFrames, 0);
        lock (statsLock)
        {
            roundTripMs = 0;
            jitterMs = 0;
            lastPacketType = "None";
            roundTripSampleIndex = 0;
            roundTripSampleFilled = 0;
            for (int i = 0; i < RoundTripSampleWindow; i++) roundTripSamples[i] = 0;
        }
    }

    public void Disconnect(string reason)
    {
        CancellationTokenSource oldCancellation;
        UdpClient oldSocket;
        KcpContext oldKcp;
        lock (stateLock)
        {
            oldCancellation = cancellation;
            oldSocket = udpSocket;
            oldKcp = kcpContext;
            udpSocket = null;
            kcpContext = null;
            cancellation = null;
            serverEndpoint = null;
            connected = false;
        }
        if (!string.IsNullOrEmpty(reason))
        {
            lock (statsLock) lastDisconnectReason = reason;
        }
        try { oldCancellation?.Cancel(); } catch { }
        try { oldSocket?.Close(); } catch { }
        try { oldKcp?.Dispose(); } catch { }
        lock (incoming) incoming.Clear();
        // 断线必须清掉半帧残留：否则残留字节会被新会话当成外层长度前缀，一次错位就永久毒化接收。
        kcpReceiveStart = 0;
        kcpReceiveEnd = 0;
    }

    public void Dispose()
    {
        Disconnect("Transport disposed");
    }

    private void SignalDisconnect(string reason, bool closeSocket)
    {
        if (string.IsNullOrEmpty(reason)) reason = "Connection closed.";
        if (Interlocked.Exchange(ref disconnectQueued, 1) != 0)
        {
            if (closeSocket)
                try { udpSocket?.Close(); } catch { }
            return;
        }
        lock (stateLock)
        {
            connected = false;
        }
        lock (statsLock) lastDisconnectReason = reason;
        var bytes = Encoding.UTF8.GetBytes(reason);
        lock (incoming) incoming.Enqueue(new TcpFrame(TcpFrameKind.Disconnect, 0, bytes, bytes.Length * 8));
        if (closeSocket)
            try { udpSocket?.Close(); } catch { }
    }

    private void TrackPacketType(TcpFrame frame)
    {
        if (frame == null) return;

        // 协议层帧级追踪：只做一次委托调用，未开启超级调试时为 null（零开销）
        if (TraceSink.FrameObserver != null)
        {
            TraceSink.FrameObserver(frame.Kind.ToString(), frame.PayloadBits, frame.Payload == null ? 0 : frame.Payload.Length);
        }

        // 只有业务包（Packet）才有"包类型"这回事。
        // 【修的是什么】以前这里不看 Kind 就直接把 payload[0] 当类型，而心跳帧的 payload 是
        // 8 字节的 .NET ticks 时间戳（服务端每 2 秒发一次，客户端也发）——
        // 于是 F8 面板那行「最后业务包」会显示 Unknown(83) 这种用时间戳首字节凑出来的垃圾值，
        // 而且数字一直在变。那不是错误，是标签和统计范围写错了。
        // 修完之后如果还看到 Unknown(N)，那才是真信号：服务端发了一个客户端不认识的业务包类型。
        if (frame.Kind != TcpFrameKind.Packet)
        {
            lock (statsLock) lastPacketType = frame.Kind.ToString();   // Ping / Pong / ...
            return;
        }
        if (frame.Payload == null || frame.Payload.Length == 0) return;
        byte rawType = frame.Payload[0];
        lock (statsLock)
        {
            lastPacketType = Enum.IsDefined(typeof(PacketType), (int)rawType)
                ? ((PacketType)rawType).ToString()
                : "Unknown(" + rawType + ")";
        }
    }

    private static void LogWarningSafe(string message)
    {
        try { UnityEngine.Debug.LogWarning(message); } catch { }
    }
}