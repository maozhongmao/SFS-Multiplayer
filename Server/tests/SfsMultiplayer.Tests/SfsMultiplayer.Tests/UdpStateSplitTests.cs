// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net;
using System.Net.Sockets;
using System.Text;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

// 真实 TcpMultiplayerServer 的 C-S 状态传输验证。
// 客户端完成 Bind 后，主状态直接走 UDP；尚未绑定的接收端才走 TCP 最新状态队列。
public sealed class UdpStateSplitTests
{
    private const byte UdpKindBind = 1;
    private const byte UdpKindBindAck = 2;
    private const byte UdpKindData = 3;


    [Fact(Timeout = 20_000)]
    public async Task UpdateRocketPrimaryUsesUdpAfterRecipientBinds()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Bind Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9880, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var alice = await ConnectWithUdp(server.Port, "AckAlice");
        using var bob = await ConnectWithUdp(server.Port, "BindBob");
        alice.DrainTcp();
        bob.DrainTcp();
        await Task.Delay(100);

        var expected = new NetLocation(123, 456, 7, 8, "Moon");
        await alice.SendTcpAsync(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 1,
            RocketId = 7,
            Location = expected,
            Rotation = 9,
            AngularVelocity = 10,
        });

        var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(expected, relayed.Location);

        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task UpdateRocketPrimaryUsesDirectUdpAfterBindWithoutTcpFallback()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Direct Route Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9882, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var alice = await ConnectWithUdp(server.Port, "DirectAlice");
            using var bob = await ConnectWithUdp(server.Port, "DirectBob");
            alice.DrainTcp();
            bob.DrainTcp();
            await Task.Delay(100);

            var expected = new NetLocation(611, -722, 3, 4, "Moon");
            await alice.SendTcpAsync(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
            {
                WorldTime = 1,
                RocketId = 7,
                Location = expected,
                Rotation = 12,
                AngularVelocity = 13,
            });

            var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
                PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
            Assert.Equal(expected, relayed.Location);
            Assert.False(bob.SeenTcp(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)),
                "完成 Bind 后，1.1.4.6 的主状态分流不得再重复走 TCP。");
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 20_000)]
    public async Task UpdateRocketPrimaryRemainsUdpAfterBindWithoutDataAcknowledgement()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Direct Continuity Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9881, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var alice = await ConnectWithUdp(server.Port, "DirectAlice");
        using var bob = await ConnectWithUdp(server.Port, "DirectBob");
        alice.DrainTcp();
        bob.DrainTcp();
        await Task.Delay(100);

        await alice.SendTcpAsync(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 1,
            RocketId = 7,
            Location = new NetLocation(100, 200, 1, 2, "Moon"),
            Rotation = 3,
            AngularVelocity = 4,
        });
        _ = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));

        await Task.Delay(900);
        await alice.SendTcpAsync(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 2,
            RocketId = 7,
            Location = new NetLocation(300, 400, 5, 6, "Moon"),
            Rotation = 7,
            AngularVelocity = 8,
        });

        var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(new NetLocation(300, 400, 5, 6, "Moon"), relayed.Location);
        Assert.False(bob.SeenTcp(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)),
            "Bind 后不应因缺少 Data ACK 改回 TCP 状态中继。");

        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task UpdateRocketPrimaryUsesUdpWhenRecipientOnlyBound()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Split Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9877, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        // 两个客户端都完成 TCP 握手 + UDP Bind，拿到各自的 UdpEndpoint。
        using var alice = await ConnectWithUdp(server.Port, "Alice");
        using var bob = await ConnectWithUdp(server.Port, "Bob");
        // 排空初始快照，避免后续 TCP 断言误判。
        alice.DrainTcp();
        bob.DrainTcp();

        // 1.1.4.6 的直接分流以 Bind 建立接收端点后即走 UDP。
        var expected = new NetLocation(321.25, -654.5, 1, 2, "Moon");
        var primary = new UpdateRocketPrimaryPacket
        {
            WorldTime = 1,
            RocketId = 7,
            Location = expected,
            Rotation = 15,
            AngularVelocity = 16,
        };
        await alice.SendTcpAsync(PacketType.UpdateRocketPrimary, primary);

        var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(expected, relayed.Location);
        Assert.False(bob.SeenTcp(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)),
            "只完成 Bind 时，UpdateRocketPrimary 仍应直接通过 UDP 投递。");

        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task UdpIngressStateIsRelayedOverUdpWhenPeersAreBound()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Ingress Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9878, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var alice = await ConnectWithUdp(server.Port, "IngressAlice");
        using var bob = await ConnectWithUdp(server.Port, "IngressBob");
        alice.DrainTcp();
        bob.DrainTcp();

        alice.SendUdp(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 2,
            RocketId = 7,
            Location = new NetLocation(30, 40, 5, 6, "Moon"),
            Rotation = 7,
            AngularVelocity = 8,
        });

        var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(new NetLocation(30, 40, 5, 6, "Moon"), relayed.Location);
        Assert.False(bob.SeenTcp(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)),
            "UDP 上行的主状态应继续经 UDP 投递给已绑定接收端。");

        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task MalformedUdpPacketDoesNotStopLaterStateUpdates()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "UDP Resilience Target",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        });
        var settings = new ServerSettings { Port = 9879, MaxConnections = 4, Debug = true };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var alice = await ConnectWithUdp(server.Port, "ResilienceAlice");
        using var bob = await ConnectWithUdp(server.Port, "ResilienceBob");
        alice.DrainTcp();
        bob.DrainTcp();

        // 合法令牌 + 截断 payload：只能丢弃这一包，不能让整个 UDP 接收任务退出。
        alice.SendRawUdp(new byte[] { (byte)PacketType.UpdateRocketPrimary });
        await Task.Delay(100);

        alice.SendUdp(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 2,
            RocketId = 7,
            Location = new NetLocation(50, 60, 7, 8, "Moon"),
            Rotation = 9,
            AngularVelocity = 10,
        });

        var relayed = bob.ReceiveUdp<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(new NetLocation(50, 60, 7, 8, "Moon"), relayed.Location);

        stop.Cancel();
        await serverTask;
    }

    // ---- 测试脚手架：手工 TCP 握手 + UDP Bind，复用服务器协议 ----

    private sealed class UdpTestClient : IDisposable
    {
        private readonly TcpClient _tcp;
        private readonly NetworkStream _stream;
        private readonly UdpClient _udp;
        private readonly string _token;
        private IPEndPoint _serverEp;

        public int PlayerId { get; }

        public UdpTestClient(TcpClient tcp, NetworkStream stream, UdpClient udp, string token, IPEndPoint serverEp, int playerId)
        {
            _tcp = tcp;
            _stream = stream;
            _udp = udp;
            _token = token;
            _serverEp = serverEp;
            PlayerId = playerId;
        }

        public void DrainTcp()
        {
            // 读取并丢弃 TCP 上所有当前可立即获取的数据帧（不阻塞过久）。
            _stream.ReadTimeout = 100;
            var buf = new byte[65536];
            try
            {
                while (_tcp.Available > 0)
                    _stream.Read(buf, 0, buf.Length);
            }
            catch (IOException) { /* 读超时即无更多数据 */ }
        }

        public async Task SendTcpAsync(PacketType type, INetData packet)
        {
            var payload = NetPayloadCodec.Serialize(type, packet);
            await TcpFrameCodec.WriteAsync(_stream,
                new TcpFrame(TcpFrameKind.Packet, 0, payload.Data, payload.BitLength),
                CancellationToken.None);
        }

        public void SendUdp(PacketType type, INetData packet)
        {
            SendRawUdp(NetPayloadCodec.Serialize(type, packet).Data);
        }

        public void SendRawUdp(byte[] payload)
        {
            var tokenBytes = Encoding.UTF8.GetBytes(_token);
            var frame = new byte[2 + tokenBytes.Length + payload.Length];
            frame[0] = UdpKindData;
            frame[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, frame, 2, tokenBytes.Length);
            Buffer.BlockCopy(payload, 0, frame, 2 + tokenBytes.Length, payload.Length);
            _udp.Send(frame, frame.Length, _serverEp);
        }

        public T ReceiveUdp<T>(PacketType expected, TimeSpan timeout) where T : INetData, new()
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                try
                {
                    var receiveTask = _udp.ReceiveAsync();
                    if (!receiveTask.Wait(200)) continue;
                    var result = receiveTask.Result.Buffer;
                    if (result.Length < 2) continue;
                    var kind = result[0];
                    if (kind != UdpKindData) continue;
                    var tokenLen = result[1];
                    if (result.Length < 2 + tokenLen) continue;
                    var payload = new byte[result.Length - 2 - tokenLen];
                    Buffer.BlockCopy(result, 2 + tokenLen, payload, 0, payload.Length);
                    var message = NetPayloadCodec.ToIncoming(payload, payload.Length * 8);
                    if ((PacketType)message.ReadByte() != expected) continue;
                    var packet = new T();
                    packet.Deserialize(message);
                    return packet;
                }
                catch (Exception) { /* 读超时，继续轮询 */ }
            }
            throw new TimeoutException($"UDP packet {expected} was not received.");
        }

        public void StopUdp()
        {
            try { _udp.Close(); } catch { }
        }

        public bool SeenTcp(PacketType expected, TimeSpan timeout)
        {
            var deadline = DateTime.UtcNow + timeout;
            _stream.ReadTimeout = 200;
            while (DateTime.UtcNow < deadline)
            {
                try
                {
                    if (_tcp.Available == 0) { Task.Delay(50).Wait(); continue; }
                    var frame = TcpFrameCodec.ReadAsync(_stream, CancellationToken.None).AsTask().GetAwaiter().GetResult();
                    if (frame.Kind != TcpFrameKind.Packet) continue;
                    var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                    if ((PacketType)message.ReadByte() == expected) return true;
                }
                catch (IOException) { /* 读超时 */ }
            }
            return false;
        }

        public void Dispose()
        {
            try { _tcp.Close(); } catch { }
            try { _udp.Close(); } catch { }
        }
    }

    private static async Task<UdpTestClient> ConnectWithUdp(int port, string username)
    {
        var tcp = new TcpClient { NoDelay = true };
        await tcp.ConnectAsync(IPAddress.Loopback, port);
        var stream = tcp.GetStream();
        var hello = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket { Username = username });
        await TcpFrameCodec.WriteAsync(stream,
            new TcpFrame(TcpFrameKind.Hello, SessionHandshakeCodec.Version, hello, hello.Length * 8),
            CancellationToken.None);
        var ackFrame = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
        Assert.Equal(TcpFrameKind.HelloAck, ackFrame.Kind);
        var ack = SessionHandshakeCodec.DecodeAck(ackFrame.Payload);
        Assert.NotEqual(-1, ack.PlayerId);
        Assert.False(string.IsNullOrEmpty(ack.UdpSessionToken), "服务器必须下发 UDP 会话令牌。");

        var udp = new UdpClient(AddressFamily.InterNetwork);
        var serverEp = new IPEndPoint(IPAddress.Loopback, port);
        var tokenBytes = Encoding.UTF8.GetBytes(ack.UdpSessionToken);
        var bind = new byte[2 + tokenBytes.Length];
        bind[0] = UdpKindBind;
        bind[1] = (byte)tokenBytes.Length;
        Buffer.BlockCopy(tokenBytes, 0, bind, 2, tokenBytes.Length);
        udp.Send(bind, bind.Length, serverEp);

        // 等待 BindAck 确认 UDP 通道建立（不 Connect，允许接收任意源回包）。
        var gotAck = false;
        var ackDeadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < ackDeadline && !gotAck)
        {
            try
            {
                var receiveTask = udp.ReceiveAsync();
                if (receiveTask.Wait(300))
                {
                    var reply = receiveTask.Result.Buffer;
                    if (reply.Length >= 2 && reply[0] == UdpKindBindAck) gotAck = true;
                }
            }
            catch (Exception) { /* 超时或端口不可达，重试 */ }
        }
        Assert.True(gotAck, "UDP BindAck 未收到，C-S UDP 通道未建立。");


        return new UdpTestClient(tcp, stream, udp, ack.UdpSessionToken, serverEp, ack.PlayerId);
    }
}
