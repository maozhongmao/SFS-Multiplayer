// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Security.Cryptography;
using System.Text;
using Lidgren.Network;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Server;

public sealed partial class MultiplayerServer : IAsyncDisposable
{
    public const string ApplicationId = "multiplayersfs";

    private readonly ServerSettings _settings;
    private readonly WorldSnapshot _world;
    private readonly NetServer _server;
    private readonly Stopwatch _worldClock = Stopwatch.StartNew();
    private readonly Stopwatch _saveClock = Stopwatch.StartNew();
    private readonly ConcurrentDictionary<NetConnection, ConnectedPlayer> _players = new();
    private int _nextPlayerId;
    private bool _started;

    public int Port => _server.Port;
    public int PlayerCount => _players.Count;
    public double WorldTime => _world.WorldTime + _worldClock.Elapsed.TotalSeconds;

    public MultiplayerServer(ServerSettings settings, WorldSnapshot world)
    {
        settings.Validate(allowEphemeralPort: true);
        _settings = settings;
        _world = world ?? throw new ArgumentNullException(nameof(world));
        var configuration = new NetPeerConfiguration(ApplicationId)
        {
            Port = settings.Port,
            MaximumConnections = settings.MaxConnections,
        };
        configuration.EnableMessageType(NetIncomingMessageType.StatusChanged);
        configuration.EnableMessageType(NetIncomingMessageType.ConnectionApproval);
        configuration.EnableMessageType(NetIncomingMessageType.ConnectionLatencyUpdated);
        _server = new NetServer(configuration);
    }

    public void Start()
    {
        if (_started) throw new InvalidOperationException("Server is already started.");
        _server.Start();
        _started = true;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        if (!_started) throw new InvalidOperationException("Start must be called before RunAsync.");
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                ProcessAvailableMessages();
                SaveIfDue();
                await Task.Delay(2, cancellationToken).ConfigureAwait(false);
            }
        }
        catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
        {
        }
        finally
        {
            ProcessAvailableMessages();
            SaveState();
        }
    }

    private void ProcessAvailableMessages()
    {
        NetIncomingMessage? message;
        while ((message = _server.ReadMessage()) is not null)
        {
            try
            {
                switch (message.MessageType)
                {
                    case NetIncomingMessageType.ConnectionApproval:
                        ApproveOrDeny(message);
                        break;
                    case NetIncomingMessageType.StatusChanged:
                        HandleStatus(message);
                        break;
                    case NetIncomingMessageType.ConnectionLatencyUpdated:
                        HandleLatency(message);
                        break;
                    case NetIncomingMessageType.Data:
                        HandleData(message);
                        break;
                    case NetIncomingMessageType.WarningMessage:
                    case NetIncomingMessageType.ErrorMessage:
                    case NetIncomingMessageType.DebugMessage:
                    case NetIncomingMessageType.VerboseDebugMessage:
                        Console.WriteLine($"[Lidgren] {message.ReadString()}");
                        break;
                }
            }
            catch (Exception ex) when (ex is InvalidDataException or IndexOutOfRangeException or ArgumentException or NetException)
            {
                Console.WriteLine($"[拒绝数据包] {message.SenderEndPoint}: {ex.Message}");
                message.SenderConnection?.Disconnect("Invalid packet.");
            }
            finally
            {
                _server.Recycle(message);
            }
        }
    }

    private void ApproveOrDeny(NetIncomingMessage message)
    {
        var connection = message.SenderConnection
            ?? throw new InvalidDataException("Connection approval message has no sender connection.");
        var hail = connection.RemoteHailMessage;
        if (hail is null)
        {
            connection.Deny("Missing join request.");
            return;
        }
        var request = new JoinRequestPacket();
        request.Deserialize(hail);
        var username = request.Username.Trim();

        if (_players.Count >= _settings.MaxConnections)
        {
            connection.Deny("Server is full.");
            return;
        }
        if (username.Length == 0 || username.Length > _settings.MaxUsernameLength || username.Any(char.IsControl))
        {
            connection.Deny("Invalid username.");
            return;
        }
        if (_settings.BlockDuplicatePlayerNames && _players.Values.Any(
                player => string.Equals(player.Username, username, StringComparison.OrdinalIgnoreCase)))
        {
            connection.Deny("Username is already in use.");
            return;
        }
        if (!PasswordMatches(request.Password, _settings.Password))
        {
            connection.Deny("Invalid password.");
            return;
        }

        var player = new ConnectedPlayer(Interlocked.Increment(ref _nextPlayerId), username, RandomColor());
        if (!_players.TryAdd(connection, player))
        {
            connection.Deny("Duplicate connection.");
            return;
        }

        var response = _server.CreateMessage();
        response.Write(new JoinResponsePacket
        {
            PlayerId = player.Id,
            UpdateRocketsPeriod = _settings.UpdateRocketsPeriod,
            ChatMessageCooldown = _settings.ChatMessageCooldown,
            WorldTime = WorldTime,
            SendTime = NetTime.Now,
            Difficulty = _world.Difficulty,
        });
        connection.Approve(response);
        Console.WriteLine($"[连接审批] {username} @ {message.SenderEndPoint}");
    }

    private void HandleStatus(NetIncomingMessage message)
    {
        var connection = message.SenderConnection
            ?? throw new InvalidDataException("Status message has no sender connection.");
        var status = (NetConnectionStatus)message.ReadByte();
        var reason = message.ReadString();
        if (status == NetConnectionStatus.Connected)
        {
            SendInitialState(connection);
            RefreshAuthorities();
        }
        else if (status == NetConnectionStatus.Disconnected &&
                 _players.TryRemove(connection, out var player))
        {
            Broadcast(PacketType.PlayerDisconnected,
                new PlayerDisconnectedPacket { PlayerId = player.Id }, connection);
            RefreshAuthorities();
            Console.WriteLine($"[断开] {player.Username}: {reason}");
        }
    }

    private void SendInitialState(NetConnection connection)
    {
        if (!_players.TryGetValue(connection, out var joining)) return;
        Broadcast(PacketType.PlayerConnected, new PlayerConnectedPacket
        {
            PlayerId = joining.Id,
            Username = joining.Username,
            IconColor = joining.Color,
            PrintMessage = true,
        }, connection);

        foreach (var rocket in _world.Rockets.OrderBy(pair => pair.Key))
        {
            Send(connection, PacketType.CreateRocket, new CreateRocketPacket
            {
                WorldTime = WorldTime,
                GlobalId = rocket.Key,
                Rocket = rocket.Value,
            });
        }
        foreach (var player in _players.Values.OrderBy(player => player.Id))
        {
            Send(connection, PacketType.PlayerConnected, new PlayerConnectedPacket
            {
                PlayerId = player.Id,
                Username = player.Username,
                IconColor = player.Color,
                PrintMessage = false,
            });
            Send(connection, PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
            {
                PlayerId = player.Id,
                RocketId = player.ControlledRocket,
            });
        }
    }

    private void HandleLatency(NetIncomingMessage message)
    {
        var connection = message.SenderConnection
            ?? throw new InvalidDataException("Latency message has no sender connection.");
        if (!_players.TryGetValue(connection, out var player)) return;
        player.RoundTripSeconds = connection.AverageRoundtripTime;
        Send(connection, PacketType.UpdateWorldTime,
            new UpdateWorldTimePacket { WorldTime = WorldTime + player.RoundTripSeconds / 2 });
    }

    private void HandleData(NetIncomingMessage message)
    {
        var connection = message.SenderConnection
            ?? throw new InvalidDataException("Data message has no sender connection.");
        if (!_players.TryGetValue(connection, out var player))
            throw new InvalidDataException("Data received before connection approval.");
        var rawType = message.ReadByte();
        if (!Enum.IsDefined(typeof(PacketType), rawType))
            throw new InvalidDataException($"Unknown packet type: {rawType}.");
        HandlePacket((PacketType)rawType, message, player);
    }

    private void Send(NetConnection connection, PacketType type, INetData packet,
        NetDeliveryMethod delivery = NetDeliveryMethod.ReliableOrdered)
    {
        var message = _server.CreateMessage();
        message.Write((byte)type);
        message.Write(packet);
        _server.SendMessage(message, connection, delivery);
    }

    private void Broadcast(PacketType type, INetData packet, NetConnection? except = null,
        NetDeliveryMethod delivery = NetDeliveryMethod.ReliableOrdered)
    {
        var recipients = _server.Connections
            .Where(connection => connection.Status == NetConnectionStatus.Connected && connection != except)
            .ToList();
        if (recipients.Count == 0) return;
        var message = _server.CreateMessage();
        message.Write((byte)type);
        message.Write(packet);
        _server.SendMessage(message, recipients, delivery, 0);
    }

    private static bool PasswordMatches(string supplied, string expected)
    {
        if (expected.Length == 0) return true;
        var left = SHA256.HashData(Encoding.UTF8.GetBytes(supplied ?? string.Empty));
        var right = SHA256.HashData(Encoding.UTF8.GetBytes(expected));
        return CryptographicOperations.FixedTimeEquals(left, right);
    }

    private static Color3 RandomColor()
    {
        var hue = RandomNumberGenerator.GetInt32(0, 360) / 360f;
        var section = hue * 6f;
        var x = 1f - MathF.Abs(section % 2f - 1f);
        return (int)section switch
        {
            0 => new Color3(1, x, 0),
            1 => new Color3(x, 1, 0),
            2 => new Color3(0, 1, x),
            3 => new Color3(0, x, 1),
            4 => new Color3(x, 0, 1),
            _ => new Color3(1, 0, x),
        };
    }

    private void SaveIfDue()
    {
        if (_settings.AutoSaveSeconds <= 0 || string.IsNullOrWhiteSpace(_settings.StatePath)) return;
        if (_saveClock.Elapsed.TotalSeconds < _settings.AutoSaveSeconds) return;
        SaveState();
        _saveClock.Restart();
    }

    private void SaveState()
    {
        if (string.IsNullOrWhiteSpace(_settings.StatePath)) return;
        _world.WorldTime = WorldTime;
        _worldClock.Restart();
        ServerStateStore.Save(_settings.StatePath, _world);
    }

    public async ValueTask DisposeAsync()
    {
        if (!_started) return;

        _server.Shutdown("Server stopping.");
        var deadline = Stopwatch.StartNew();
        while (_server.Status != NetPeerStatus.NotRunning && deadline.Elapsed < TimeSpan.FromSeconds(5))
            await Task.Delay(10).ConfigureAwait(false);

        _started = false;
        if (_server.Status != NetPeerStatus.NotRunning)
            throw new TimeoutException("Lidgren network thread did not stop within five seconds.");
    }

    internal sealed class ConnectedPlayer(int id, string username, Color3 color)
    {
        public int Id { get; } = id;
        public string Username { get; } = username;
        public Color3 Color { get; set; } = color;
        public float RoundTripSeconds { get; set; }
        public int ControlledRocket { get; set; } = -1;
        public HashSet<int> UpdateAuthority { get; } = new();
        public DateTime LastChatUtc { get; set; } = DateTime.MinValue;
    }
}
