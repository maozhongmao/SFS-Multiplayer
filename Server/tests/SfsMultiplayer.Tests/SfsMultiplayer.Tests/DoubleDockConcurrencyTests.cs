// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net.Sockets;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class DoubleDockConcurrencyTests
{
    [Fact(Timeout = 20_000)]
    public async Task DifferentDockingPortPairsCannotCrossConfirmEachOther()
    {
        var world = CreateDoublePortWorld();
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var runTask = server.RunAsync(stop.Token);
        await using var alice = await TcpDockPeer.Connect(server.Port, "Alice");
        await using var bob = await TcpDockPeer.Connect(server.Port, "Bob");
        await alice.Drain(TimeSpan.FromMilliseconds(150));
        await bob.Drain(TimeSpan.FromMilliseconds(150));

        await alice.Send(PacketType.UpdatePlayerControl,
            new UpdatePlayerControlPacket { RocketId = 100 });
        await bob.Send(PacketType.UpdatePlayerControl,
            new UpdatePlayerControlPacket { RocketId = 200 });
        await Task.Delay(150);
        await alice.Drain(TimeSpan.FromMilliseconds(100));
        await bob.Drain(TimeSpan.FromMilliseconds(100));

        await Task.WhenAll(
            alice.Send(PacketType.DockTransaction, Dock(1, 11, 21)).AsTask(),
            bob.Send(PacketType.DockTransaction, Dock(2, 12, 22)).AsTask());

        var commits = await alice.CountCommittedDockPackets(TimeSpan.FromMilliseconds(700));
        Assert.Equal(0, commits);
        Assert.Equal(2, world.Rockets.Count);
        Assert.True(await alice.Ping());
        Assert.True(await bob.Ping());

        stop.Cancel();
        await runTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task TwoPortPairsDockingAtOnceCommitsExactlyOnceWithoutDisconnectingClients()
    {
        var world = CreateDoublePortWorld();
        await using var server = new TcpMultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var runTask = server.RunAsync(stop.Token);
        await using var alice = await TcpDockPeer.Connect(server.Port, "Alice-double");
        await using var bob = await TcpDockPeer.Connect(server.Port, "Bob-double");
        await alice.Drain(TimeSpan.FromMilliseconds(150));
        await bob.Drain(TimeSpan.FromMilliseconds(150));
        await alice.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket { RocketId = 100 });
        await bob.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket { RocketId = 200 });
        await Task.Delay(150);
        await alice.Drain(TimeSpan.FromMilliseconds(100));
        await bob.Drain(TimeSpan.FromMilliseconds(100));

        await Task.WhenAll(
            SendBothDockPairs(alice, 10),
            SendBothDockPairs(bob, 20));

        var commits = await alice.CountCommittedDockPackets(TimeSpan.FromSeconds(1));
        Assert.Equal(1, commits);
        Assert.Single(world.Rockets);
        Assert.Equal(6, world.Rockets.Values.Single().Parts.Count);
        Assert.True(await alice.Ping());
        Assert.True(await bob.Ping());

        stop.Cancel();
        await runTask;
    }

    private static async Task SendBothDockPairs(TcpDockPeer peer, int transactionBase)
    {
        await peer.Send(PacketType.DockTransaction, Dock(transactionBase + 1, 11, 21));
        await peer.Send(PacketType.DockTransaction, Dock(transactionBase + 2, 12, 22));
    }

    private static DockTransactionPacket Dock(int transactionId, int keepPart, int removePart) => new()
    {
        TransactionId = transactionId,
        Operation = DockTransactionOperation.Dock,
        KeepRocketId = 100,
        RemoveRocketId = 200,
        KeepPartId = keepPart,
        RemovePartId = removePart,
    };

    private static WorldSnapshot CreateDoublePortWorld()
    {
        var world = new WorldSnapshot();
        var a = new RocketState { RocketName = "A", Location = new NetLocation(0, 0, 0, 0, "Earth") };
        a.Parts.Add(10, Part("A-body", 0, 0));
        a.Parts.Add(11, Part("A-port-1", -1, 2));
        a.Parts.Add(12, Part("A-port-2", 1, 2));
        a.Joints.Add(new JointState(10, 11));
        a.Joints.Add(new JointState(10, 12));
        var b = new RocketState { RocketName = "B", Location = new NetLocation(0, 4, 0, 0, "Earth") };
        b.Parts.Add(20, Part("B-body", 0, 0));
        b.Parts.Add(21, Part("B-port-1", -1, -2));
        b.Parts.Add(22, Part("B-port-2", 1, -2));
        b.Joints.Add(new JointState(20, 21));
        b.Joints.Add(new JointState(20, 22));
        world.Rockets.Add(100, a);
        world.Rockets.Add(200, b);
        return world;
    }

    private static PartState Part(string name, float x, float y) => new()
    {
        Name = name, X = x, Y = y,
        OrientationX = 1, OrientationY = 1, OrientationZ = 0,
    };

    private sealed class TcpDockPeer : IAsyncDisposable
    {
        private readonly TcpClient _client;
        private readonly NetworkStream _stream;
        private int _sequence;
        public bool Connected => _client.Connected;

        private TcpDockPeer(TcpClient client)
        {
            _client = client;
            _stream = client.GetStream();
        }

        public static async Task<TcpDockPeer> Connect(int port, string username)
        {
            var client = new TcpClient { NoDelay = true };
            await client.ConnectAsync("127.0.0.1", port);
            var peer = new TcpDockPeer(client);
            var hello = SfsMultiplayer.Protocol.SessionHandshakeCodec.EncodeHello(new JoinRequestPacket { Username = username });
            await TcpFrameCodec.WriteAsync(peer._stream,
                new TcpFrame(TcpFrameKind.Hello, SfsMultiplayer.Protocol.SessionHandshakeCodec.Version,
                    hello, hello.Length * 8),
                CancellationToken.None);
            var ack = await TcpFrameCodec.ReadAsync(peer._stream, CancellationToken.None);
            Assert.Equal(TcpFrameKind.HelloAck, ack.Kind);
            Assert.Equal(SfsMultiplayer.Protocol.SessionHandshakeCodec.Version, ack.Sequence);
            Assert.NotEqual(-1, SfsMultiplayer.Protocol.SessionHandshakeCodec.DecodeAck(ack.Payload).PlayerId);
            return peer;
        }

        public ValueTask Send(PacketType type, INetData packet)
        {
            var payload = NetPayloadCodec.Serialize(type, packet);
            return TcpFrameCodec.WriteAsync(_stream,
                new TcpFrame(TcpFrameKind.Packet, Interlocked.Increment(ref _sequence), payload.Data, payload.BitLength),
                CancellationToken.None);
        }

        public async Task<bool> Ping()
        {
            var sequence = Interlocked.Increment(ref _sequence);
            var payload = BitConverter.GetBytes(DateTime.UtcNow.Ticks);
            await TcpFrameCodec.WriteAsync(_stream,
                new TcpFrame(TcpFrameKind.Ping, sequence, payload, payload.Length * 8),
                CancellationToken.None);
            var deadline = DateTime.UtcNow.AddSeconds(2);
            while (DateTime.UtcNow < deadline)
            {
                var frame = await TryRead(deadline - DateTime.UtcNow);
                if (frame is null) return false;
                if (frame.Kind == TcpFrameKind.Pong && frame.Sequence == sequence) return true;
            }
            return false;
        }

        public async Task<int> CountCommittedDockPackets(TimeSpan duration)
        {
            var count = 0;
            var deadline = DateTime.UtcNow + duration;
            while (DateTime.UtcNow < deadline)
            {
                var frame = await TryRead(deadline - DateTime.UtcNow);
                if (frame is null) break;
                if (frame.Kind != TcpFrameKind.Packet) continue;
                var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                if ((PacketType)message.ReadByte() != PacketType.DockTransaction) continue;
                var packet = new DockTransactionPacket();
                packet.Deserialize(message);
                if (packet.Committed) count++;
            }
            return count;
        }

        public async Task Drain(TimeSpan duration)
        {
            var deadline = DateTime.UtcNow + duration;
            while (DateTime.UtcNow < deadline)
                if (await TryRead(deadline - DateTime.UtcNow) is null) break;
        }

        private async Task<TcpFrame?> TryRead(TimeSpan timeout)
        {
            if (timeout <= TimeSpan.Zero) return null;
            using var cancel = new CancellationTokenSource(timeout);
            try { return await TcpFrameCodec.ReadAsync(_stream, cancel.Token); }
            catch (OperationCanceledException) { return null; }
        }

        public ValueTask DisposeAsync()
        {
            _stream.Dispose();
            _client.Dispose();
            return ValueTask.CompletedTask;
        }
    }
}
