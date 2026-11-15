// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class RealtimeSyncTests
{
    [Fact(Timeout = 20_000)]
    public async Task ControllerUpdateIsRelayedAndOtherPlayerCannotSpoofIt()
    {
        var world = new WorldSnapshot();
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "Authority Test",
            Location = new NetLocation(0, 0, 0, 0, "Earth")
        });
        await using var server = new MultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var runTask = server.RunAsync(stop.Token);

        using var alice = TestPeer.Connect(server.Port, "Alice");
        using var bob = TestPeer.Connect(server.Port, "Bob");
        alice.Drain();
        bob.Drain();

        alice.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
        {
            PlayerId = int.MaxValue,
            RocketId = 7,
        });
        var control = bob.WaitPacket<UpdatePlayerControlPacket>(
            PacketType.UpdatePlayerControl,
            packet => packet.PlayerId == alice.PlayerId && packet.RocketId == 7,
            TimeSpan.FromSeconds(3));
        Assert.Equal(alice.PlayerId, control.PlayerId);
        Assert.Equal(7, control.RocketId);

        var expected = new NetLocation(11, 12, 13, 14, "Moon");
        alice.Send(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 1,
            RocketId = 7,
            Location = expected,
            Rotation = 15,
            AngularVelocity = 16,
        });
        var relayed = bob.WaitPacket<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary, TimeSpan.FromSeconds(3));
        Assert.Equal(expected, relayed.Location);
        Assert.Equal(15, relayed.Rotation);

        alice.Drain();
        bob.Drain();
        bob.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
        {
            PlayerId = bob.PlayerId,
            RocketId = 7,
        });
        Assert.False(alice.SeenPacket(PacketType.UpdatePlayerControl, TimeSpan.FromMilliseconds(800)));

        bob.Send(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            WorldTime = 2,
            RocketId = 7,
            Location = new NetLocation(99, 99, 99, 99, "Mars"),
        });
        Assert.False(alice.SeenPacket(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)));

        alice.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
        {
            PlayerId = alice.PlayerId,
            RocketId = -1,
        });
        _ = bob.WaitPacket<UpdatePlayerControlPacket>(
            PacketType.UpdatePlayerControl,
            packet => packet.PlayerId == alice.PlayerId && packet.RocketId == -1,
            TimeSpan.FromSeconds(3));
        alice.Drain();
        bob.Send(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
        {
            PlayerId = bob.PlayerId,
            RocketId = 7,
        });
        _ = alice.WaitPacket<UpdatePlayerControlPacket>(
            PacketType.UpdatePlayerControl,
            packet => packet.PlayerId == bob.PlayerId && packet.RocketId == 7,
            TimeSpan.FromSeconds(3));

        var takeover = new NetLocation(21, 22, 23, 24, "Earth");
        bob.Send(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            RocketId = 7,
            Location = takeover,
        });
        var takeoverRelay = alice.WaitPacket<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary,
            packet => packet.RocketId == 7 && packet.Location == takeover,
            TimeSpan.FromSeconds(3));
        Assert.Equal(takeover, takeoverRelay.Location);

        stop.Cancel();
        await runTask;
    }

    [Fact(Timeout = 20_000)]
    public async Task LaunchCreatorKeepsAuthorityUntilControlSwitchArrives()
    {
        var world = new WorldSnapshot();
        await using var server = new MultiplayerServer(
            new ServerSettings { Port = 0, MaxConnections = 4 }, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var runTask = server.RunAsync(stop.Token);

        using var alice = TestPeer.Connect(server.Port, "Alice");
        using var bob = TestPeer.Connect(server.Port, "Bob");
        alice.Drain();
        bob.Drain();

        bob.Send(PacketType.CreateRocket, new CreateRocketPacket
        {
            WorldTime = 1,
            LocalId = 42,
            GlobalId = -1,
            ForLaunch = true,
            Rocket = new RocketState
            {
                RocketName = "Bob Launch",
                Location = new NetLocation(1, 2, 3, 4, "Earth"),
            },
        });
        var created = alice.WaitPacket<CreateRocketPacket>(
            PacketType.CreateRocket,
            packet => packet.LocalId == 42 && packet.GlobalId > 0,
            TimeSpan.FromSeconds(3));

        var launchedPosition = new NetLocation(101, 202, 5, 6, "Earth");
        bob.Send(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            RocketId = created.GlobalId,
            Location = launchedPosition,
            Rotation = 7,
            AngularVelocity = 8,
        });
        var relayed = alice.WaitPacket<UpdateRocketPrimaryPacket>(
            PacketType.UpdateRocketPrimary,
            packet => packet.RocketId == created.GlobalId && packet.Location == launchedPosition,
            TimeSpan.FromSeconds(3));
        Assert.Equal(launchedPosition, relayed.Location);

        alice.Send(PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        {
            RocketId = created.GlobalId,
            Location = new NetLocation(999, 999, 0, 0, "Mars"),
        });
        Assert.False(bob.SeenPacket(PacketType.UpdateRocketPrimary, TimeSpan.FromMilliseconds(800)));

        stop.Cancel();
        await runTask;
    }

    private sealed class TestPeer : IDisposable
    {
        public NetClient Client { get; }
        public int PlayerId { get; private set; } = -1;

        private TestPeer(NetClient client) => Client = client;

        public static TestPeer Connect(int port, string username)
        {
            var config = new NetPeerConfiguration(MultiplayerServer.ApplicationId)
            {
                PingInterval = 0.5f,
                ConnectionTimeout = 3,
            };
            config.EnableMessageType(NetIncomingMessageType.StatusChanged);
            var peer = new TestPeer(new NetClient(config));
            peer.Client.Start();
            var hail = peer.Client.CreateMessage();
            hail.Write(new JoinRequestPacket { Username = username });
            peer.Client.Connect("127.0.0.1", port, hail);
            var deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline)
            {
                var message = peer.Client.WaitMessage(100);
                if (message is null) continue;
                try
                {
                    if (message.MessageType != NetIncomingMessageType.StatusChanged) continue;
                    var status = (NetConnectionStatus)message.ReadByte();
                    var reason = message.ReadString();
                    if (status == NetConnectionStatus.Disconnected)
                        throw new InvalidOperationException("Connection denied: " + reason);
                    if (status != NetConnectionStatus.Connected) continue;
                    var remoteHail = peer.Client.ServerConnection?.RemoteHailMessage
                        ?? throw new InvalidDataException("Connected without a remote hail response.");
                    var response = new JoinResponsePacket();
                    response.Deserialize(remoteHail);
                    peer.PlayerId = response.PlayerId;
                    return peer;
                }
                finally { peer.Client.Recycle(message); }
            }
            peer.Dispose();
            throw new TimeoutException("Test client did not connect.");
        }

        public void Send(PacketType type, INetData packet)
        {
            var message = Client.CreateMessage();
            message.Write((byte)type);
            message.Write(packet);
            Client.SendMessage(message, NetDeliveryMethod.ReliableOrdered);
        }

        public T WaitPacket<T>(PacketType expected, TimeSpan timeout) where T : INetData, new() =>
            WaitPacket<T>(expected, _ => true, timeout);

        public T WaitPacket<T>(PacketType expected, Func<T, bool> predicate, TimeSpan timeout)
            where T : INetData, new()
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                var message = Client.WaitMessage(100);
                if (message is null) continue;
                try
                {
                    if (message.MessageType != NetIncomingMessageType.Data) continue;
                    var type = (PacketType)message.ReadByte();
                    if (type != expected) continue;
                    var packet = new T();
                    packet.Deserialize(message);
                    if (predicate(packet)) return packet;
                }
                finally { Client.Recycle(message); }
            }
            throw new TimeoutException($"Packet {expected} matching the predicate was not received.");
        }

        public bool SeenPacket(PacketType expected, TimeSpan timeout)
        {
            var deadline = DateTime.UtcNow + timeout;
            while (DateTime.UtcNow < deadline)
            {
                var message = Client.WaitMessage(50);
                if (message is null) continue;
                try
                {
                    if (message.MessageType == NetIncomingMessageType.Data &&
                        (PacketType)message.ReadByte() == expected) return true;
                }
                finally { Client.Recycle(message); }
            }
            return false;
        }

        public void Drain()
        {
            NetIncomingMessage? message;
            while ((message = Client.ReadMessage()) is not null) Client.Recycle(message);
        }

        public void Dispose()
        {
            if (Client.Status != NetPeerStatus.NotRunning) Client.Shutdown("test cleanup");
        }
    }
}
