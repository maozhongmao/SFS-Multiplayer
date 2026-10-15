// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using MultiplayerSFS.Common.Transport.Kcp;

namespace MultiplayerSFS.Common;

public sealed class UdpClientTransport : IDisposable
{
    private const byte P2PMagic = 0xA7;

    // 服务器侧 UDP 状态通道的帧类型与帧格式（对齐 C# 服务端 UdpStateTransport，权威实现）：
    //   Bind=1 客户端→服务端；BindAck=2 服务端→客户端；Data=3 双向。
    //   帧 = [kind:1][tokenLen:1][token:tokenLen][payload…]
    // 之前客户端完全没有这一层（只发裸包），所以服务端永远收不到绑定请求 =
    // ClientRegressionTests 的 7 个 UDP 用例全卡在第一步。
    private const byte UdpBind = 1;
    private const byte UdpBindAck = 2;
    private const byte UdpData = 3;
    private const int UdpBindRetryMilliseconds = 1000;
    private const int MaxPeerDatagrams = 512;

    private readonly Action<TcpFrame> receivePacket;
    private readonly object socketLock = new object();
    private readonly object peerLock = new object();
    private readonly Dictionary<IPEndPoint, KcpContext> peerContexts = new Dictionary<IPEndPoint, KcpContext>(new IPEndPointEqualityComparer());
    private readonly Queue<P2PRawDatagram> peerDatagrams = new Queue<P2PRawDatagram>();
    private UdpClient socket;
    private IPEndPoint serverEndpoint;
    private CancellationTokenSource cancellation;
    private int sequence;
    private volatile bool bound;
    private string sessionToken = string.Empty;

    public bool Bound => bound;

    public UdpClientTransport(Action<TcpFrame> receivePacket)
    {
        this.receivePacket = receivePacket;
    }

    public void Start(IPAddress address, int port, string sessionToken)
    {
        if (address == null || port < 1 || port > 65535) return;
        serverEndpoint = new IPEndPoint(address, port);
        socket = new UdpClient(address.AddressFamily);
        // 必须显式绑定本地端口：new UdpClient(AddressFamily) 建出来的是未绑定 socket，
        // 对它调用 ReceiveAsync 会抛 SocketException("在执行此操作前必须先调用 Bind 方法")，
        // 接收循环一起步就死 —— 表现就是"发得出去、永远收不到"（BindAck 收不到、状态包收不到）。
        socket.Client.Bind(new IPEndPoint(
            address.AddressFamily == System.Net.Sockets.AddressFamily.InterNetworkV6 ? IPAddress.IPv6Any : IPAddress.Any,
            0));
        UdpSocketGuard.DisableConnReset(socket);      // 同上：否则 BindAck/状态包永远收不到
        cancellation = new CancellationTokenSource();
        this.sessionToken = sessionToken ?? string.Empty;   // 注意：必须加 this.，参数名与字段同名
        bound = false;   // 必须先绑定（收到 BindAck）才允许直发状态
        var token = cancellation.Token;
        Task.Run(() =>
        {
            try { ReceiveLoop(token); }
            catch (System.Exception ex) { System.Console.Error.WriteLine("[SFS-MP] UDP receive thread exception: " + ex); }
        });
        BindLoopAsync(token);
    }

    public bool TrySendPacket(Packet packet)
    {
        if (!Bound || packet == null) return false;
        try
        {
            NetPayload payload = NetPayloadCodec.Serialize(packet, true);
            return TrySendServer(payload.Data);
        }
        catch
        {
            return false;
        }
    }

    public void SendPacket(Packet packet)
    {
        TrySendPacket(packet);
    }

    public bool TrySendPeer(IPEndPoint endpoint, byte[] data)
    {
        if (endpoint == null || data == null || data.Length == 0) return false;
        try
        {
            lock (socketLock)
            {
                if (socket == null) return false;
                socket.Send(data, data.Length, endpoint);
                return true;
            }
        }
        catch
        {
            return false;
        }
    }

    public bool TryReceivePeer(out P2PRawDatagram datagram)
    {
        lock (peerLock)
        {
            if (peerDatagrams.Count > 0)
            {
                datagram = peerDatagrams.Dequeue();
                return true;
            }
        }
        datagram = null;
        return false;
    }

    private bool TrySendServer(byte[] payload)
    {
        if (serverEndpoint == null) return false;
        if (string.IsNullOrEmpty(sessionToken)) return false;
        var tokenBytes = System.Text.Encoding.UTF8.GetBytes(sessionToken);
        if (tokenBytes.Length > byte.MaxValue) return false;
        // [kind][tokenLen][token][payload] —— 与 C# 服务端 UdpStateTransport.Send 完全一致
        var frame = new byte[2 + tokenBytes.Length + (payload != null ? payload.Length : 0)];
        frame[0] = UdpData;
        frame[1] = (byte)tokenBytes.Length;
        System.Buffer.BlockCopy(tokenBytes, 0, frame, 2, tokenBytes.Length);
        if (payload != null && payload.Length > 0)
            System.Buffer.BlockCopy(payload, 0, frame, 2 + tokenBytes.Length, payload.Length);
        return TrySendPeer(serverEndpoint, frame);
    }

    // 绑定重发：UDP 上绑定请求本身可能丢。未绑定前每秒重发一次，绑定成功或取消即退出。
    // 用 async/await 而不是 Task.Delay(...).Wait(token)：后者会占住一个线程池线程，
    // 在连续创建/销毁 transport 的场景（回归测试）里会拖慢其它用例。
    private async void BindLoopAsync(CancellationToken token)
    {
        try
        {
            while (!token.IsCancellationRequested)
            {
                if (bound) return;
                SendBindRequest();
                await Task.Delay(UdpBindRetryMilliseconds, token).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) { }
        catch (System.Exception ex)
        {
            System.Console.Error.WriteLine("[SFS-MP] UDP rebind exception: " + ex.Message);
        }
    }

    private void SendBindRequest()
    {
        if (serverEndpoint == null || string.IsNullOrEmpty(sessionToken)) return;
        var tokenBytes = System.Text.Encoding.UTF8.GetBytes(sessionToken);
        if (tokenBytes.Length > byte.MaxValue) return;
        var frame = new byte[2 + tokenBytes.Length];
        frame[0] = UdpBind;
        frame[1] = (byte)tokenBytes.Length;
        System.Buffer.BlockCopy(tokenBytes, 0, frame, 2, tokenBytes.Length);
        bool ok = TrySendPeer(serverEndpoint, frame);
    }

    // 解析服务器侧 UDP 帧：Bind=1 / BindAck=2 / Data=3
    private void HandleServerDatagram(UdpReceiveResult result)
    {
        byte[] data = result.Buffer;
        if (data.Length < 2) return;
        byte kind = data[0];
        int tokenLength = data[1];
        if (2 + tokenLength > data.Length) return;
        string token = System.Text.Encoding.UTF8.GetString(data, 2, tokenLength);
        if (kind == UdpBindAck)
        {
            if (token != sessionToken) return;   // 不是我们的会话，忽略
            bound = true;
            return;
        }
        // kind == UdpData：服务端经 UDP 下发的状态帧 —— 载荷就是 NetPayload（与发送端一致），
        // 包成 Packet 帧交给上层回调，与 KCP 路径的下发形状保持一致。
        if (kind == UdpData)
        {
            int offset = 2 + tokenLength;
            int payloadLength = data.Length - offset;
            if (payloadLength <= 0) return;
            var payload = new byte[payloadLength];
            System.Buffer.BlockCopy(data, offset, payload, 0, payloadLength);
            receivePacket?.Invoke(new TcpFrame(TcpFrameKind.Packet, 0, payload, payloadLength * 8));
        }
    }

    private async Task ReceiveLoop(CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                UdpReceiveResult result;
                try
                {
                    result = await socket.ReceiveAsync().ConfigureAwait(false);
                }
                catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
                {
                    return;
                }
                catch (SocketException ex) when (!cancellationToken.IsCancellationRequested)
                {
                    bound = false;
                    LogWarningSafe("[SFS-MP] UDP receive failed: " + ex.Message);
                    try { await Task.Delay(100, cancellationToken).ConfigureAwait(false); }
                    catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { return; }
                    continue;
                }

                try
                {
                    HandleDatagram(result);
                }
                catch (Exception ex)
                {
                    LogWarningSafe("[SFS-MP] UDP packet rejected: " + ex.Message);
                }
            }
        }
        catch (Exception ex) when (!cancellationToken.IsCancellationRequested)
        {
            bound = false;
            LogWarningSafe("[SFS-MP] UDP receive loop stopped: " + ex.Message);
        }
    }

    private void HandleDatagram(UdpReceiveResult result)
    {
        byte[] data = result.Buffer;
        if (data.Length == 0) return;

        if (data[0] == P2PMagic)
        {
            HandleP2PDatagram(result);
            return;
        }

        if (data[0] == UdpBind || data[0] == UdpBindAck || data[0] == UdpData)
        {
            HandleServerDatagram(result);
            return;
        }

        // Server data packet (no token binding)
        if (data.Length == 0) return;
        int length = data.Length;
        if (length == 0) return;

        byte[] payload = new byte[length];
        Buffer.BlockCopy(data, 0, payload, 0, length);
        receivePacket(new TcpFrame(TcpFrameKind.Packet, Interlocked.Increment(ref sequence), payload, payload.Length * 8));
    }

    private void HandleP2PDatagram(UdpReceiveResult result)
    {
        byte[] data = result.Buffer;
        if (data.Length < 2) return; // Need at least magic + length byte

        var endpoint = result.RemoteEndPoint;
        KcpContext ctx;

        lock (peerLock)
        {
            if (!peerContexts.TryGetValue(endpoint, out ctx))
            {
                // Create new KCP context for this peer
                ctx = new KcpContext(GenerateConv(endpoint), KcpOutputCallback);
                KcpTransportPresets.Apply(ctx, KcpTransportPresets.Relay);
                peerContexts[endpoint] = ctx;
            }
        }

        // Input data to KCP
        int inputResult = ctx.Input(data, 1, data.Length - 1); // Skip magic byte
        if (inputResult < 0)
        {
            LogWarningSafe($"[SFS-MP] KCP input failed for {endpoint}: {inputResult}");
            return;
        }

        // Try to receive decoded packets from KCP
        byte[] recvBuffer = new byte[ctx.Mss];
        while (true)
        {
            int recvLen = ctx.Recv(recvBuffer, 0, recvBuffer.Length);
            if (recvLen <= 0) break;

            byte[] payload = new byte[recvLen];
            Buffer.BlockCopy(recvBuffer, 0, payload, 0, recvLen);

            lock (peerLock)
            {
                if (peerDatagrams.Count < MaxPeerDatagrams)
                {
                    peerDatagrams.Enqueue(new P2PRawDatagram(endpoint, payload));
                }
            }
        }
    }

    private void KcpOutputCallback(byte[] buffer, KcpContext kcp)
    {
        // Find the endpoint for this KCP context
        IPEndPoint targetEndpoint = null;
        lock (peerLock)
        {
            foreach (var kvp in peerContexts)
            {
                if (kvp.Value == kcp)
                {
                    targetEndpoint = kvp.Key;
                    break;
                }
            }
        }

        if (targetEndpoint != null)
        {
            // Prepend magic byte for wire format
            byte[] wireData = new byte[1 + buffer.Length];
            wireData[0] = P2PMagic;
            Buffer.BlockCopy(buffer, 0, wireData, 1, buffer.Length);
            TrySendPeer(targetEndpoint, wireData);
        }
    }

    private static uint GenerateConv(IPEndPoint endpoint)
    {
        // Generate a deterministic conversation ID from endpoint
        uint hash = 0;
        byte[] addrBytes = endpoint.Address.GetAddressBytes();
        foreach (byte b in addrBytes)
        {
            hash = hash * 31 + b;
        }
        hash = hash * 31 + (uint)endpoint.Port;
        return hash | 0x80000000u; // Ensure high bit set to distinguish from server packets
    }

    private static void LogWarningSafe(string message)
    {
        try { UnityEngine.Debug.LogWarning(message); } catch { }
    }

    public void Dispose()
    {
        bound = false;
        try { cancellation?.Cancel(); } catch { }
        try { socket?.Close(); } catch { }
        lock (peerLock)
        {
            foreach (var ctx in peerContexts.Values)
            {
                ctx.Dispose();
            }
            peerContexts.Clear();
            peerDatagrams.Clear();
        }
    }

    private sealed class IPEndPointEqualityComparer : IEqualityComparer<IPEndPoint>
    {
        public bool Equals(IPEndPoint x, IPEndPoint y)
        {
            if (x == null || y == null) return false;
            return x.Address.Equals(y.Address) && x.Port == y.Port;
        }

        public int GetHashCode(IPEndPoint obj)
        {
            return obj.Address.GetHashCode() * 31 + obj.Port;
        }
    }
}