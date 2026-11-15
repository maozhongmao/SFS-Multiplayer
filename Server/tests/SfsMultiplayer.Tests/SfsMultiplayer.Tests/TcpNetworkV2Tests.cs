// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net.Sockets;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class TcpNetworkV2Tests
{
    [Fact(Timeout = 15_000)]
    public async Task ServerInfoCanBeQueriedBeforeLoginWithoutCreatingPlayer()
    {
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var client = new TcpClient { NoDelay = true };
            await client.ConnectAsync("127.0.0.1", server.Port);
            using NetworkStream stream = client.GetStream();
            await TcpFrameCodec.WriteAsync(stream,
                new TcpFrame(TcpFrameKind.ServerInfoRequest, 0, Array.Empty<byte>(), 0),
                CancellationToken.None);

            var reply = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
            Assert.Equal(TcpFrameKind.ServerInfoResponse, reply.Kind);
            Assert.Equal(8, reply.Payload.Length);
            Assert.Equal(0, BitConverter.ToInt32(reply.Payload, 0));
            Assert.Equal(4, BitConverter.ToInt32(reply.Payload, 4));
            Assert.Equal(0, server.PlayerCount);
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 15_000)]
    public async Task ServerVersionCanBeQueriedBeforeLoginWithoutCreatingPlayer()
    {
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var client = new TcpClient { NoDelay = true };
            await client.ConnectAsync("127.0.0.1", server.Port);
            using NetworkStream stream = client.GetStream();
            await TcpFrameCodec.WriteAsync(stream,
                new TcpFrame(TcpFrameKind.ServerVersionRequest, 7, Array.Empty<byte>(), 0),
                CancellationToken.None);

            var reply = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
            Assert.Equal(TcpFrameKind.ServerVersionResponse, reply.Kind);
            Assert.Equal(7, reply.Sequence);

            var version = ServerVersionWire.DecodeResponse(reply.Payload);
            // 握手版本必须和 Hello 硬校验的是同一个数，否则登录页说"匹配"而 Hello 被拒
            Assert.Equal(SessionHandshakeCodec.Version, version.HandshakeVersion);
            Assert.Equal(TcpFrameCodec.ProtocolVersion, version.ProtocolVersion);
            Assert.Equal(TcpMultiplayerServer.ServerVersionString, version.ServerVersion);
            Assert.Equal(0, server.PlayerCount);
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact]
    public void ServerVersionPayloadLayoutMatchesClientCodec()
    {
        var payload = ServerVersionWire.EncodeResponse(2, 32, "1.2.3");
        Assert.Equal(new byte[]
        {
            2, 0, 0, 0,
            32, 0, 0, 0,
            5, 0, 0, 0,
            (byte)'1', (byte)'.', (byte)'2', (byte)'.', (byte)'3',
        }, payload);

        var decoded = ServerVersionWire.DecodeResponse(payload);
        Assert.Equal(2, decoded.HandshakeVersion);
        Assert.Equal(32, decoded.ProtocolVersion);
        Assert.Equal("1.2.3", decoded.ServerVersion);

        // 长度前缀超出实际载荷：判失败，不能越界读
        Assert.Throws<InvalidDataException>(() =>
            ServerVersionWire.DecodeResponse(payload[..14]));
    }

    [Fact(Timeout = 15_000)]
    public async Task FixedHandshakeClientCanJoinCurrentServer()
    {
        var settings = new ServerSettings { Port = 0, MaxConnections = 2 };
        await using var server = new TcpMultiplayerServer(settings, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync("127.0.0.1", server.Port);
        using NetworkStream stream = client.GetStream();
        var helloPayload = SfsMultiplayer.Protocol.SessionHandshakeCodec.EncodeHello(new JoinRequestPacket
        {
            Username = "fixed-handshake-client",
        });
        await TcpFrameCodec.WriteAsync(stream,
            new TcpFrame(TcpFrameKind.Hello, SfsMultiplayer.Protocol.SessionHandshakeCodec.Version,
                helloPayload, helloPayload.Length * 8),
            CancellationToken.None);

        var reply = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
        try
        {
            Assert.Equal(TcpFrameKind.HelloAck, reply.Kind);
            Assert.Equal(SfsMultiplayer.Protocol.SessionHandshakeCodec.Version, reply.Sequence);
            Assert.NotEqual(-1, SfsMultiplayer.Protocol.SessionHandshakeCodec.DecodeAck(reply.Payload).PlayerId);
            Assert.False(string.IsNullOrEmpty(SfsMultiplayer.Protocol.SessionHandshakeCodec.DecodeAck(reply.Payload).ResumeToken));
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 15_000)]
    public async Task MaintenanceFailureDoesNotStopHeartbeats()
    {
        var world = new WorldSnapshot { WorldTime = double.NaN };
        var settings = new ServerSettings
        {
            Port = 0,
            MaxConnections = 2,
            AutoSaveSeconds = 1,
            StatePath = Path.Combine(AppContext.BaseDirectory, "maintenance-fault-state.json"),
        };
        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync("127.0.0.1", server.Port);
        using NetworkStream stream = client.GetStream();
        var helloPayload = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket
        {
            Username = "maintenance-heartbeat-client",
        });
        await TcpFrameCodec.WriteAsync(stream,
            new TcpFrame(TcpFrameKind.Hello, SessionHandshakeCodec.Version,
                helloPayload, helloPayload.Length * 8), CancellationToken.None);
        var helloAck = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
        Assert.Equal(TcpFrameKind.HelloAck, helloAck.Kind);

        var heartbeatSeen = false;
        using var readTimeout = new CancellationTokenSource(TimeSpan.FromSeconds(4));
        try
        {
            while (!readTimeout.IsCancellationRequested)
            {
                var frame = await TcpFrameCodec.ReadAsync(stream, readTimeout.Token);
                if (frame.Kind == TcpFrameKind.Ping)
                {
                    heartbeatSeen = true;
                    break;
                }
            }
        }
        catch (OperationCanceledException) when (readTimeout.IsCancellationRequested)
        {
        }
        finally
        {
            // 只让自动保存阶段触发一次故障；退出清理不应把同一故障再次抛出。
            settings.StatePath = string.Empty;
            stop.Cancel();
            await serverTask;
        }

        Assert.True(heartbeatSeen,
            "自动保存异常不能让维护循环退出；服务器仍应继续发送 TCP 心跳。");
    }

    [Fact(Timeout = 15_000)]
    public async Task TcpClientCanJoinReceiveSnapshotAndHeartbeat()
    {
        var world = new WorldSnapshot { WorldTime = 4321, Difficulty = DifficultyType.Normal };
        world.Rockets.Add(77, new RocketState
        {
            RocketName = "TCP Test",
            Location = new NetLocation(1, 2, 3, 4, "Earth")
        });
        var settings = new ServerSettings
        {
            Port = 0,
            Password = "secret",
            MaxConnections = 4,
        };

        await using var server = new TcpMultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        using var client = new TcpClient { NoDelay = true };
        await client.ConnectAsync("127.0.0.1", server.Port);
        using NetworkStream stream = client.GetStream();

        var helloPayload = SfsMultiplayer.Protocol.SessionHandshakeCodec.EncodeHello(new JoinRequestPacket
        {
            Username = "tcp-tester",
            Password = "secret",
        });
        await TcpFrameCodec.WriteAsync(stream,
            new TcpFrame(TcpFrameKind.Hello, SfsMultiplayer.Protocol.SessionHandshakeCodec.Version,
                helloPayload, helloPayload.Length * 8), CancellationToken.None);

        var helloAck = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
        Assert.Equal(TcpFrameKind.HelloAck, helloAck.Kind);
        Assert.Equal(SfsMultiplayer.Protocol.SessionHandshakeCodec.Version, helloAck.Sequence);
        var response = SfsMultiplayer.Protocol.SessionHandshakeCodec.DecodeAck(helloAck.Payload);
        Assert.NotEqual(-1, response.PlayerId);

        CreateRocketPacket? snapshot = null;
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (DateTime.UtcNow < deadline && snapshot is null)
        {
            var frame = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
            if (frame.Kind != TcpFrameKind.Packet) continue;
            var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
            if ((PacketType)message.ReadByte() != PacketType.CreateRocket) continue;
            snapshot = new CreateRocketPacket();
            snapshot.Deserialize(message);
        }
        Assert.NotNull(snapshot);
        Assert.Equal(77, snapshot!.GlobalId);
        Assert.Equal("TCP Test", snapshot.Rocket.RocketName);

        byte[] ping = BitConverter.GetBytes(DateTime.UtcNow.Ticks);
        await TcpFrameCodec.WriteAsync(stream,
            new TcpFrame(TcpFrameKind.Ping, 99, ping, ping.Length * 8), CancellationToken.None);
        TcpFrame pong;
        do
        {
            pong = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
        } while (pong.Kind != TcpFrameKind.Pong);
        Assert.Equal(99, pong.Sequence);
        Assert.Equal(ping, pong.Payload);

        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 15_000)]
    public async Task CreateRocketPreservesLocalIdOnlyForCreator()
    {
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var creator = await TcpTestPeer.ConnectAsync(server.Port, "creator");
            using var observer = await TcpTestPeer.ConnectAsync(server.Port, "observer");

            await creator.SendAsync(PacketType.CreateRocket, new CreateRocketPacket
            {
                WorldTime = 1,
                LocalId = 41,
                GlobalId = -1,
                ForLaunch = true,
                Rocket = new RocketState
                {
                    RocketName = "Creator local-id test",
                    Location = new NetLocation(1, 2, 3, 4, "Earth"),
                },
            });

            var creatorEcho = await creator.WaitForPacketAsync<CreateRocketPacket>(
                PacketType.CreateRocket,
                packet => packet.LocalId == 41 && packet.GlobalId > 0,
                TimeSpan.FromSeconds(3));
            var observerCopy = await observer.WaitForPacketAsync<CreateRocketPacket>(
                PacketType.CreateRocket,
                packet => packet.GlobalId == creatorEcho.GlobalId,
                TimeSpan.FromSeconds(3));

            Assert.Equal(-1, observerCopy.LocalId);
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 15_000)]
    public async Task DestroyingControlledRocketBroadcastsControlClear()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(77, new RocketState
        {
            RocketName = "Control clear test",
            Location = new NetLocation(1, 2, 3, 4, "Earth"),
        });
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var controller = await TcpTestPeer.ConnectAsync(server.Port, "controller");
            using var observer = await TcpTestPeer.ConnectAsync(server.Port, "observer");

            await controller.SendAsync(PacketType.UpdatePlayerControl,
                new UpdatePlayerControlPacket { RocketId = 77 });
            _ = await observer.WaitForPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == 77,
                TimeSpan.FromSeconds(3));

            await controller.SendAsync(PacketType.DestroyRocket,
                new DestroyRocketPacket { RocketId = 77, Reason = 0 });
            var clear = await observer.WaitForPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == -1,
                TimeSpan.FromSeconds(3));
            Assert.Equal(-1, clear.RocketId);
            var destroyed = await observer.WaitForPacketAsync<DestroyRocketPacket>(
                PacketType.DestroyRocket,
                packet => packet.RocketId == 77,
                TimeSpan.FromSeconds(3));
            Assert.Equal(77, destroyed.RocketId);
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 15_000)]
    public async Task RepeatedControlRequestAcknowledgesCallerWithoutBroadcastingAuthorityChurn()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(91, new RocketState
        {
            RocketName = "Idempotent control request test",
            Location = new NetLocation(1, 2, 3, 4, "Earth"),
        });
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var controller = await TcpTestPeer.ConnectAsync(server.Port, "control-owner");
            using var observer = await TcpTestPeer.ConnectAsync(server.Port, "control-observer");

            await controller.SendAsync(PacketType.UpdatePlayerControl,
                new UpdatePlayerControlPacket { RocketId = 91 });
            _ = await controller.WaitForPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == 91,
                TimeSpan.FromSeconds(3));
            _ = await observer.WaitForPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == 91,
                TimeSpan.FromSeconds(3));

            await controller.SendAsync(PacketType.UpdatePlayerControl,
                new UpdatePlayerControlPacket { RocketId = 91 });
            var confirmation = await controller.WaitForPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == 91,
                TimeSpan.FromSeconds(3));

            Assert.Equal(91, confirmation.RocketId);
            Assert.False(await observer.SeenPacketAsync<UpdatePlayerAuthorityPacket>(
                PacketType.UpdatePlayerAuthority, _ => true, TimeSpan.FromMilliseconds(750)),
                "重复控制请求只应确认请求者，不能对旁观者重复广播权威刷新。");
            Assert.False(await observer.SeenPacketAsync<UpdatePlayerControlPacket>(
                PacketType.UpdatePlayerControl,
                packet => packet.PlayerId == controller.PlayerId && packet.RocketId == 91,
                TimeSpan.FromMilliseconds(750)),
                "重复控制请求不能对旁观者重复广播相同控制权。");
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    [Fact(Timeout = 15_000)]
    public async Task MultiplayerTimeWarpAppliesPersonallyWithoutBroadcastingVote()
    {
        var settings = new ServerSettings { Port = 0, MaxConnections = 4, AutoSaveSeconds = 0 };
        await using var server = new TcpMultiplayerServer(settings, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        try
        {
            using var alice = await TcpTestPeer.ConnectAsync(server.Port, "timewarp-alice");
            using var bob = await TcpTestPeer.ConnectAsync(server.Port, "timewarp-bob");

            await alice.SendAsync(PacketType.TimeWarp, new TimeWarpPacket
            {
                Operation = TimeWarpOperation.Request,
                Multiplier = 3,
            });

            var applied = await alice.WaitForPacketAsync<TimeWarpPacket>(PacketType.TimeWarp,
                packet => packet.Operation == TimeWarpOperation.Applied && packet.Multiplier == 3,
                TimeSpan.FromSeconds(3));

            Assert.True(applied.Approved);
            Assert.Equal(1, server.TimeScale);
            Assert.False(await bob.SeenPacketAsync<TimeWarpPacket>(PacketType.TimeWarp,
                packet => packet.Operation == TimeWarpOperation.Vote, TimeSpan.FromSeconds(1)),
                "多人个人倍率不得向其他玩家发起投票。");
        }
        finally
        {
            stop.Cancel();
            await serverTask;
        }
    }

    private sealed class TcpTestPeer : IDisposable
    {
        private readonly TcpClient _client;
        private readonly NetworkStream _stream;

        public int PlayerId { get; }

        private TcpTestPeer(TcpClient client, NetworkStream stream, int playerId)
        {
            _client = client;
            _stream = stream;
            PlayerId = playerId;
        }

        public static async Task<TcpTestPeer> ConnectAsync(int port, string username)
        {
            var client = new TcpClient { NoDelay = true };
            await client.ConnectAsync("127.0.0.1", port);
            var stream = client.GetStream();
            var hello = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket { Username = username });
            await TcpFrameCodec.WriteAsync(stream,
                new TcpFrame(TcpFrameKind.Hello, SessionHandshakeCodec.Version, hello, hello.Length * 8),
                CancellationToken.None);
            var reply = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
            Assert.Equal(TcpFrameKind.HelloAck, reply.Kind);
            var response = SessionHandshakeCodec.DecodeAck(reply.Payload);
            return new TcpTestPeer(client, stream, response.PlayerId);
        }

        public async Task SendAsync(PacketType type, INetData packet)
        {
            var payload = NetPayloadCodec.Serialize(type, packet);
            await TcpFrameCodec.WriteAsync(_stream,
                new TcpFrame(TcpFrameKind.Packet, 0, payload.Data, payload.BitLength),
                CancellationToken.None);
        }

        public async Task<T> WaitForPacketAsync<T>(PacketType expected, Func<T, bool> predicate, TimeSpan timeout)
            where T : INetData, new()
        {
            using var cancellation = new CancellationTokenSource(timeout);
            while (true)
            {
                var frame = await TcpFrameCodec.ReadAsync(_stream, cancellation.Token);
                if (frame.Kind == TcpFrameKind.Ping)
                {
                    await TcpFrameCodec.WriteAsync(_stream,
                        new TcpFrame(TcpFrameKind.Pong, frame.Sequence, frame.Payload, frame.PayloadBits),
                        cancellation.Token);
                    continue;
                }
                if (frame.Kind != TcpFrameKind.Packet) continue;
                var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                if ((PacketType)message.ReadByte() != expected) continue;
                var packet = new T();
                packet.Deserialize(message);
                if (predicate(packet)) return packet;
            }
        }

        public async Task<bool> SeenPacketAsync<T>(PacketType expected, Func<T, bool> predicate, TimeSpan timeout)
            where T : INetData, new()
        {
            try
            {
                using var cancellation = new CancellationTokenSource(timeout);
                while (true)
                {
                    var frame = await TcpFrameCodec.ReadAsync(_stream, cancellation.Token);
                    if (frame.Kind == TcpFrameKind.Ping)
                    {
                        await TcpFrameCodec.WriteAsync(_stream,
                            new TcpFrame(TcpFrameKind.Pong, frame.Sequence, frame.Payload, frame.PayloadBits),
                            cancellation.Token);
                        continue;
                    }
                    if (frame.Kind != TcpFrameKind.Packet) continue;
                    var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                    if ((PacketType)message.ReadByte() != expected) continue;
                    var packet = new T();
                    packet.Deserialize(message);
                    if (predicate(packet)) return true;
                }
            }
            catch (OperationCanceledException)
            {
                return false;
            }
        }

        public void Dispose()
        {
            try { _client.Close(); } catch { }
        }
    }
}
