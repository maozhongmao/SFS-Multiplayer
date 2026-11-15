// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class NetworkIntegrationTests
{
    [Fact(Timeout = 15_000)]
    public async Task ClientCanJoinAndReceiveWorldSnapshot()
    {
        var world = new WorldSnapshot { WorldTime = 1234.5, Difficulty = DifficultyType.Normal };
        world.Rockets.Add(7, new RocketState
        {
            RocketName = "Network Test",
            Location = new NetLocation(1, 2, 3, 4, "Earth")
        });
        var settings = new ServerSettings
        {
            Port = 0,
            Password = "secret",
            MaxConnections = 4,
            UpdateRocketsPeriod = 20,
            ChatMessageCooldown = 3,
        };

        await using var server = new MultiplayerServer(settings, world);
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        var config = new NetPeerConfiguration(MultiplayerServer.ApplicationId)
        {
            PingInterval = 0.5f,
            ConnectionTimeout = 3,
        };
        config.EnableMessageType(NetIncomingMessageType.StatusChanged);
        using var clientScope = new ClientScope(config);
        var client = clientScope.Client;
        client.Start();
        var hail = client.CreateMessage();
        hail.Write(new JoinRequestPacket { Username = "tester", Password = "secret" });
        client.Connect("127.0.0.1", server.Port, hail);

        JoinResponsePacket? response = null;
        CreateRocketPacket? snapshot = null;
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (DateTime.UtcNow < deadline && (response is null || snapshot is null))
        {
            var message = client.WaitMessage(200);
            if (message is null) continue;
            try
            {
                if (message.MessageType == NetIncomingMessageType.StatusChanged)
                {
                    var status = (NetConnectionStatus)message.ReadByte();
                    _ = message.ReadString();
                    if (status == NetConnectionStatus.Connected)
                    {
                        var remoteHail = client.ServerConnection?.RemoteHailMessage
                            ?? throw new InvalidDataException("Connected without a remote hail response.");
                        response = new JoinResponsePacket();
                        response.Deserialize(remoteHail);
                    }
                }
                else if (message.MessageType == NetIncomingMessageType.Data &&
                         (PacketType)message.ReadByte() == PacketType.CreateRocket)
                {
                    snapshot = new CreateRocketPacket();
                    snapshot.Deserialize(message);
                }
            }
            finally
            {
                client.Recycle(message);
            }
        }

        Assert.NotNull(response);
        Assert.InRange(response!.WorldTime, 1234.5, 1236.5);
        Assert.NotEqual(-1, response.PlayerId);
        Assert.NotNull(snapshot);
        Assert.Equal(7, snapshot!.GlobalId);
        Assert.Equal("Network Test", snapshot.Rocket.RocketName);

        client.Shutdown("test complete");
        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 15_000)]
    public async Task ClientCanCreateRocketWithMultiplePartVariables()
    {
        var settings = new ServerSettings { Port = 0, MaxConnections = 4 };
        await using var server = new MultiplayerServer(settings, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        var config = new NetPeerConfiguration(MultiplayerServer.ApplicationId)
        {
            PingInterval = 0.5f,
            ConnectionTimeout = 3,
        };
        config.EnableMessageType(NetIncomingMessageType.StatusChanged);
        using var clientScope = new ClientScope(config);
        var client = clientScope.Client;
        client.Start();
        var hail = client.CreateMessage();
        hail.Write(new JoinRequestPacket { Username = "rocket-tester" });
        client.Connect("127.0.0.1", server.Port, hail);

        var connected = false;
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (DateTime.UtcNow < deadline && !connected)
        {
            var message = client.WaitMessage(200);
            if (message is null) continue;
            try
            {
                if (message.MessageType != NetIncomingMessageType.StatusChanged) continue;
                connected = (NetConnectionStatus)message.ReadByte() == NetConnectionStatus.Connected;
                _ = message.ReadString();
            }
            finally
            {
                client.Recycle(message);
            }
        }
        Assert.True(connected);

        var part = new PartState { Name = "Capsule" };
        part.NumberVariables.Add("width_a", 1.0);
        part.NumberVariables.Add("width_b", 2.0);
        var rocket = new RocketState
        {
            RocketName = "Collection Test",
            Location = new NetLocation(0, 0, 0, 0, "Earth"),
        };
        rocket.Parts.Add(7, part);
        var outgoing = client.CreateMessage();
        outgoing.Write((byte)PacketType.CreateRocket);
        outgoing.Write(new CreateRocketPacket { LocalId = 123, ForLaunch = true, Rocket = rocket });
        client.SendMessage(outgoing, NetDeliveryMethod.ReliableOrdered);

        CreateRocketPacket? echoed = null;
        string? disconnectReason = null;
        deadline = DateTime.UtcNow.AddSeconds(8);
        while (DateTime.UtcNow < deadline && echoed is null && disconnectReason is null)
        {
            var message = client.WaitMessage(200);
            if (message is null) continue;
            try
            {
                if (message.MessageType == NetIncomingMessageType.Data &&
                    (PacketType)message.ReadByte() == PacketType.CreateRocket)
                {
                    echoed = new CreateRocketPacket();
                    echoed.Deserialize(message);
                }
                else if (message.MessageType == NetIncomingMessageType.StatusChanged)
                {
                    var status = (NetConnectionStatus)message.ReadByte();
                    var reason = message.ReadString();
                    if (status == NetConnectionStatus.Disconnected) disconnectReason = reason;
                }
            }
            finally
            {
                client.Recycle(message);
            }
        }

        Assert.Null(disconnectReason);
        Assert.NotNull(echoed);
        Assert.Equal(2, echoed!.Rocket.Parts[7].NumberVariables.Count);

        client.Shutdown("test complete");
        stop.Cancel();
        await serverTask;
    }

    [Fact(Timeout = 15_000)]
    public async Task WrongPasswordIsDenied()
    {
        var settings = new ServerSettings { Port = 0, Password = "correct" };
        await using var server = new MultiplayerServer(settings, new WorldSnapshot());
        server.Start();
        using var stop = new CancellationTokenSource();
        var serverTask = server.RunAsync(stop.Token);

        var config = new NetPeerConfiguration(MultiplayerServer.ApplicationId)
        {
            PingInterval = 0.5f,
            ConnectionTimeout = 3,
        };
        config.EnableMessageType(NetIncomingMessageType.StatusChanged);
        using var clientScope = new ClientScope(config);
        var client = clientScope.Client;
        client.Start();
        var hail = client.CreateMessage();
        hail.Write(new JoinRequestPacket { Username = "tester", Password = "wrong" });
        client.Connect("127.0.0.1", server.Port, hail);

        string? reason = null;
        var deadline = DateTime.UtcNow.AddSeconds(8);
        while (DateTime.UtcNow < deadline && reason is null)
        {
            var message = client.WaitMessage(200);
            if (message is null) continue;
            try
            {
                if (message.MessageType != NetIncomingMessageType.StatusChanged) continue;
                var status = (NetConnectionStatus)message.ReadByte();
                var text = message.ReadString();
                if (status == NetConnectionStatus.Disconnected) reason = text;
            }
            finally
            {
                client.Recycle(message);
            }
        }

        Assert.NotNull(reason);
        Assert.Contains("password", reason!, StringComparison.OrdinalIgnoreCase);
        stop.Cancel();
        await serverTask;
    }

    private sealed class ClientScope : IDisposable
    {
        public NetClient Client { get; }

        public ClientScope(NetPeerConfiguration configuration)
        {
            Client = new NetClient(configuration);
        }

        public void Dispose()
        {
            if (Client.Status != NetPeerStatus.NotRunning)
                Client.Shutdown("test cleanup");
        }
    }
}
