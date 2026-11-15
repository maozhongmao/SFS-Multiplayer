// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Concurrent;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Text;
using Lidgren.Network;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Server;

public sealed class TcpMultiplayerServer : IAsyncDisposable
{
    private readonly ServerSettings _settings;
    private readonly WorldSnapshot _world;
    private readonly object _worldLock = new();
    private readonly TcpListener _listener;
    private readonly UdpStateTransport _udp;
    private readonly ConcurrentDictionary<int, TcpSession> _players = new();
    private readonly Dictionary<(int First, int Second), string> _p2pPairTokens = new();
    private readonly Stopwatch _p2pClock = Stopwatch.StartNew();
    private readonly Dictionary<(int KeepRocket, int RemoveRocket, int KeepPart, int RemovePart), PendingDock> _pendingDocks = new();
	private readonly List<Task> _clientTasks = new();
	private readonly object _clientTasksLock = new();
    private readonly Stopwatch _worldClock = Stopwatch.StartNew();
    private readonly Stopwatch _saveClock = Stopwatch.StartNew();
    private readonly Stopwatch _debugClock = Stopwatch.StartNew();
    private readonly Stopwatch _heartbeatClock = Stopwatch.StartNew();

    private double _timeScale = 1;
    private int _nextPlayerId;
    private int _sequence;
    private bool _started;
    private bool _autoClearDebris;
    private const int AutoClearDebrisMaxParts = DebrisControlRules.DefaultAutoRemoveMaxParts;

    // 版本探测回显的号：取自身程序集，别写死——csproj 才是版本真源，写死后登录页会显示过期号。
    public static string ServerVersionString { get; } = ResolveServerVersion();

    private static string ResolveServerVersion()
    {
        var version = typeof(TcpMultiplayerServer).Assembly.GetName().Version;
        return version is null ? "unknown" : $"{version.Major}.{version.Minor}.{version.Build}";
    }

    public int Port => ((IPEndPoint)_listener.LocalEndpoint).Port;
    public int PlayerCount => _players.Count;
    public double TimeScale { get { lock (_worldLock) return _timeScale; } }
    public double WorldTime
    {
        get { lock (_worldLock) return _world.WorldTime + _worldClock.Elapsed.TotalSeconds * _timeScale; }
    }

    // 暴露当前生效配置给补丁上下文（只读访问）。
    public ServerSettings Settings => _settings;

    public TcpMultiplayerServer(ServerSettings settings, WorldSnapshot world)
    {
        settings.Validate(allowEphemeralPort: true);
        _settings = settings;
        _world = world ?? throw new ArgumentNullException(nameof(world));
        _listener = new TcpListener(settings.BindIpAddress, settings.Port);
        _udp = new UdpStateTransport(settings.BindIpAddress, settings.Port, HandleUdpDatagram);
    }

    public void Start()
    {
        if (_started) throw new InvalidOperationException("Server is already started.");
        _listener.Start(_settings.MaxConnections);
        _udp.Start();
        _started = true;
    }

    public async Task RunAsync(CancellationToken cancellationToken)
    {
        if (!_started) throw new InvalidOperationException("Start must be called before RunAsync.");
        using var registration = cancellationToken.Register(() => _listener.Stop());
        var maintenance = MaintenanceLoopAsync(cancellationToken);
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                TcpClient client;
                try
                {
                    client = await _listener.AcceptTcpClientAsync(cancellationToken).ConfigureAwait(false);
                }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                catch (SocketException) when (cancellationToken.IsCancellationRequested)
                {
                    break;
                }
                client.NoDelay = true;
                client.ReceiveBufferSize = 256 * 1024;
                client.SendBufferSize = 256 * 1024;
                var task = HandleClientAsync(client, cancellationToken);
                lock (_clientTasksLock)
                {
                	_clientTasks.RemoveAll(t => t.IsCompleted);
                	_clientTasks.Add(task);
                }
            }
        }
        finally
        {
            try { await maintenance.ConfigureAwait(false); } catch (OperationCanceledException) { }
            foreach (var session in _players.Values) session.Close();
            Task[] tasksToWait;
            lock (_clientTasksLock) tasksToWait = _clientTasks.ToArray();
            try { await Task.WhenAll(tasksToWait).ConfigureAwait(false); } catch { }
            SaveState();
        }
    }

    private bool HandleUdpDatagram(byte kind, string token, IPEndPoint endpoint, byte[] payload)
    {
        var session = _players.Values.FirstOrDefault(player => player.UdpToken == token);
        if (session is null) return false;
        var now = DateTime.UtcNow;
        session.RecordUdpEndpoint(endpoint);
        if (kind == UdpStateTransport.Bind)
        {
            session.RecordUdpReceive(now);
            return payload.Length == 0;
        }
        if (kind != UdpStateTransport.Data) return false;
        session.RecordUdpReceive(now);
        if (payload.Length == 0) return true;
        try
        {
            lock (_worldLock) HandleData(session, new TcpFrame(TcpFrameKind.Packet, 0, payload, payload.Length * 8));
            return true;
        }
        catch (Exception ex) when (ex is InvalidDataException or ArgumentException or NetException)
        {
            if (_settings.Debug) Console.WriteLine($"[UDP拒绝] {endpoint}: {ex.Message}");
            return false;
        }
    }

    private async Task HandleClientAsync(TcpClient client, CancellationToken serverToken)
    {
        TcpSession? session = null;
        var handlerGeneration = -1;
        var endpoint = client.Client.RemoteEndPoint?.ToString() ?? "unknown";
        try
        {
            using (client)
            using (var stream = client.GetStream())
            using (var joinTimeout = CancellationTokenSource.CreateLinkedTokenSource(serverToken))
            {
                Console.WriteLine($"[MP-CONNECT] TCP_ACCEPTED {endpoint}");
                joinTimeout.CancelAfter(TimeSpan.FromSeconds(10));
                var hello = await TcpFrameCodec.ReadAsync(stream, joinTimeout.Token).ConfigureAwait(false);
                Console.WriteLine($"[MP-CONNECT] HELLO_FRAME_RECEIVED {endpoint} kind={hello.Kind} sequence={hello.Sequence} bytes={hello.Payload.Length}");
                if (hello.Kind == TcpFrameKind.ServerInfoRequest)
                {
                    if (hello.Payload.Length != 0 || hello.PayloadBits != 0)
                        throw new InvalidDataException("Server info request must not contain a payload.");

                    var infoPayload = ServerInfoWire.EncodeResponse(_players.Count, _settings.MaxConnections);
                    await TcpFrameCodec.WriteAsync(stream,
                        new TcpFrame(TcpFrameKind.ServerInfoResponse, hello.Sequence,
                            infoPayload, infoPayload.Length * 8), serverToken).ConfigureAwait(false);
                    return;
                }
                if (hello.Kind == TcpFrameKind.ServerVersionRequest)
                {
                    if (hello.Payload.Length != 0 || hello.PayloadBits != 0)
                        throw new InvalidDataException("Server version request must not contain a payload.");

                    // 握手版本用 Hello 硬校验的同一个数，所以"登录页显示匹配"和"Hello 能不能过"是同一个口径。
                    var versionPayload = ServerVersionWire.EncodeResponse(
                        SessionHandshakeCodec.Version, TcpFrameCodec.ProtocolVersion, ServerVersionString);
                    await TcpFrameCodec.WriteAsync(stream,
                        new TcpFrame(TcpFrameKind.ServerVersionResponse, hello.Sequence,
                            versionPayload, versionPayload.Length * 8), serverToken).ConfigureAwait(false);
                    return;
                }
                if (hello.Kind != TcpFrameKind.Hello)
                    throw new InvalidDataException("First TCP frame must be Hello.");
                if (hello.Sequence != SessionHandshakeCodec.Version)
                {
                    await SendDisconnectDirectAsync(stream,
                        $"Handshake mismatch: server={SessionHandshakeCodec.Version}, client={hello.Sequence}.", serverToken)
                        .ConfigureAwait(false);
                    return;
                }

                var request = SessionHandshakeCodec.DecodeHello(hello.Payload);
                var username = request.Username.Trim();
                Console.WriteLine($"[MP-CONNECT] HELLO_DECODED {endpoint} user={username}");
                var isResume = request.ResumePlayerId >= 0 &&
                    _players.TryGetValue(request.ResumePlayerId, out var resumableSession) &&
                    resumableSession.CanResume(request.ResumeToken);
                var denial = ValidateJoin(username, request.Password, isResume);
                if (denial is not null)
                {
                    await SendDisconnectDirectAsync(stream, denial, serverToken).ConfigureAwait(false);
                    return;
                }

                if (request.ResumePlayerId >= 0 &&
                    _players.TryGetValue(request.ResumePlayerId, out var existing) &&
                    existing.CanResume(request.ResumeToken))
                {
                    session = existing;
                    session.ReplaceConnection(client, stream);
                    Console.WriteLine($"[TCP恢复] {username} @ {endpoint}");
                }
                else
                {
                    session = new TcpSession(
                        Interlocked.Increment(ref _nextPlayerId), username, RandomColor(), client, stream);
                    if (!_players.TryAdd(session.Id, session))
                        throw new InvalidOperationException("Could not register TCP player.");
                }

                var responsePayload = SessionHandshakeCodec.EncodeAck(new JoinResponsePacket
                {
                    PlayerId = session.Id,
                    UpdateRocketsPeriod = 50,
                    ChatMessageCooldown = _settings.ChatMessageCooldown,
                    WorldTime = WorldTime,
                    SendTime = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds() / 1000.0,
                    Difficulty = _world.Difficulty,
                    SolarSystemName = string.Empty,
                    UdpSessionToken = session.UdpToken,
                    ResumeToken = session.ResumeToken,
                });
                await TcpFrameCodec.WriteAsync(stream,
                    new TcpFrame(TcpFrameKind.HelloAck, SessionHandshakeCodec.Version,
                        responsePayload, responsePayload.Length * 8), serverToken).ConfigureAwait(false);
                Console.WriteLine($"[MP-CONNECT] HELLO_ACK_SENT {endpoint} player={session.Id} bytes={responsePayload.Length}");

                Console.WriteLine($"[TCP连接] {username} @ {endpoint}");
                var connectionGeneration = session.ConnectionGeneration;
                handlerGeneration = connectionGeneration;
                var writer = WriterLoopAsync(session, connectionGeneration, serverToken);
                lock (_worldLock)
                {
                    SendInitialState(session);
                    RefreshAuthorities();
                }

                while (!serverToken.IsCancellationRequested && client.Connected && session.ConnectionGeneration == connectionGeneration)
                {
                    var frame = await TcpFrameCodec.ReadAsync(stream, serverToken).ConfigureAwait(false);
                    session.LastReceiveUtc = DateTime.UtcNow;
                    session.ReceivedBytes += frame.Payload.Length + 13;
                    session.ReceivedFrames++;
                    switch (frame.Kind)
                    {
                        case TcpFrameKind.Ping:
                            EnqueueCritical(session, new TcpFrame(TcpFrameKind.Pong, frame.Sequence,
                                frame.Payload, frame.PayloadBits));
                            break;
                        case TcpFrameKind.Pong:
                            HandlePong(session, frame);
                            break;
                        case TcpFrameKind.Packet:
                            lock (_worldLock) HandleData(session, frame);
                            break;
                        case TcpFrameKind.RequestWorldSnapshot:
                            lock (_worldLock) SendWorldSnapshot(session);
                            break;
                        case TcpFrameKind.RequestRocketSnapshot:
                            if (frame.Payload.Length >= 4)
                                lock (_worldLock) SendRocketSnapshot(session, BitConverter.ToInt32(frame.Payload, 0));
                            break;
                        case TcpFrameKind.Disconnect:
                            return;
                        default:
                            throw new InvalidDataException($"Unexpected TCP frame: {frame.Kind}.");
                    }
                }
                if (session.ConnectionGeneration == connectionGeneration)
                    session.Close();
                try { await writer.ConfigureAwait(false); } catch (OperationCanceledException) { }
            }
        }
        catch (OperationCanceledException) when (serverToken.IsCancellationRequested)
        {
        }
        catch (EndOfStreamException)
        {
        }
        catch (IOException ex)
        {
            if (_settings.Debug) Console.WriteLine($"[TCP网络] {endpoint}: {ex.Message}");
        }
        catch (SocketException ex)
        {
            if (_settings.Debug) Console.WriteLine($"[TCP网络] {endpoint}: {ex.Message}");
        }
        catch (Exception ex) when (ex is InvalidDataException or IndexOutOfRangeException or ArgumentException or NetException)
        {
            Console.WriteLine($"[拒绝TCP数据] {endpoint}: {ex.Message}");
            if (session is not null)
                EnqueueCritical(session, DisconnectFrame("Invalid packet."));
        }
        finally
        {
            if (session is not null && session.ConnectionGeneration == handlerGeneration)
            {
                if (session.EnterRecoveryWindow()) { }
                else if (_players.TryRemove(session.Id, out _))
                {
                    session.Close();
                    lock (_worldLock)
                    {
                        Broadcast(PacketType.PlayerDisconnected,
                            new PlayerDisconnectedPacket { PlayerId = session.Id }, session);
                        RefreshAuthorities();
                    }
                    Console.WriteLine($"[TCP断开] {session.Username}");
                }
            }
        }
    }

    private async Task WriterLoopAsync(TcpSession session, int connectionGeneration, CancellationToken serverToken)
    {
        using var linked = CancellationTokenSource.CreateLinkedTokenSource(serverToken, session.Closed.Token);
        while (!linked.IsCancellationRequested && session.ConnectionGeneration == connectionGeneration)
        {
            if (!session.Queue.TryDequeue(out var frame) || frame is null)
            {
                await session.SendSignal.WaitAsync(TimeSpan.FromMilliseconds(100), linked.Token).ConfigureAwait(false);
                continue;
            }
            await TcpFrameCodec.WriteAsync(session.Stream, frame, linked.Token).ConfigureAwait(false);
            session.SentBytes += frame.Payload.Length + 13;
            session.SentFrames++;
        }
    }

    private async Task MaintenanceLoopAsync(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                if (_heartbeatClock.Elapsed >= TimeSpan.FromSeconds(2))
                {
                    _heartbeatClock.Restart();
                    var now = DateTime.UtcNow;
                    foreach (var session in _players.Values)
                    {
                        if (now - session.LastReceiveUtc > TimeSpan.FromSeconds(10))
                        {
                            if (session.EnterRecoveryWindow())
                            {
                                continue;
                            }
                            if (_players.TryRemove(session.Id, out _))
                            {
                                session.Close();
                                lock (_worldLock)
                                {
                                    Broadcast(PacketType.PlayerDisconnected,
                                        new PlayerDisconnectedPacket { PlayerId = session.Id }, session);
                                    RefreshAuthorities();
                                }
                                Console.WriteLine($"{session.Username} 已断开。");
                            }
                            continue;
                        }
                        var ticks = now.Ticks;
                        session.RegisterHeartbeat(ticks);
                        var bytes = BitConverter.GetBytes(ticks);
                        EnqueueCritical(session, new TcpFrame(TcpFrameKind.Ping,
                            Interlocked.Increment(ref _sequence), bytes, bytes.Length * 8));
                    }
                }
                SaveIfDue();
                if (_autoClearDebris)
                {
                    lock (_worldLock)
                    {
                        var removed = ClearDebris(AutoClearDebrisMaxParts);
                        if (removed > 0)
                            Console.WriteLine($"[debris auto] 已清理 {removed} 枚太空垃圾。");
                    }
                }
                lock (_worldLock)
                {
                    if (_settings.P2P.Enabled && _p2pClock.Elapsed.TotalSeconds >= _settings.P2P.ValidationIntervalSeconds)
                    {
                        _p2pClock.Restart();
                        RefreshP2PPeers();
                    }
                }
                if (_settings.Debug && _debugClock.Elapsed >= TimeSpan.FromSeconds(5))
                {
                    _debugClock.Restart();
                    PrintDebugSummary();
                }
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
            catch (Exception ex)
            {
                Console.WriteLine($"[维护] 本轮异常，继续运行: {ex.Message}");
                if (_settings.Debug || Environment.GetEnvironmentVariable("SFS_SERVER_DEBUG") == "1")
                    Console.Error.WriteLine(ex);
            }

            try
            {
                await Task.Delay(100, cancellationToken).ConfigureAwait(false);
            }
            catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested)
            {
                break;
            }
        }
    }

    private void RefreshP2PPeers()
    {
        var eligible = _players.Values
            .Where(session => session.UdpEndpoint is not null)
            .OrderBy(session => session.Id)
            .ToArray();
        var activePairs = new HashSet<(int First, int Second)>();
        for (var i = 0; i < eligible.Length; i++)
        {
            for (var j = i + 1; j < eligible.Length; j++)
            {
                var first = eligible[i];
                var second = eligible[j];
                var firstRockets = P2PRocketIds(first);
                var secondRockets = P2PRocketIds(second);
                var matches = FindP2PMatches(firstRockets, secondRockets);
                if (matches.Count == 0) continue;
                var key = (first.Id, second.Id);
                activePairs.Add(key);
                if (!_p2pPairTokens.TryGetValue(key, out var pairToken))
                {
                    pairToken = Convert.ToHexString(RandomNumberGenerator.GetBytes(24));
                    _p2pPairTokens[key] = pairToken;
                }
                SendP2POffer(first, second, pairToken, matches);
                SendP2POffer(second, first, pairToken, matches.Select(match => (match.Second, match.First)).ToList());
            }
        }

        foreach (var pair in _p2pPairTokens.Keys.Where(pair => !activePairs.Contains(pair)).ToArray())
        {
            _p2pPairTokens.Remove(pair);
            if (_players.TryGetValue(pair.First, out var first) && _players.TryGetValue(pair.Second, out var second))
            {
                SendP2PRevoke(first, second.Id);
                SendP2PRevoke(second, first.Id);
            }
        }
    }

    private List<int> P2PRocketIds(TcpSession session)
    {
        return session.UpdateAuthority
            .Where(id => _world.Rockets.ContainsKey(id))
            .OrderBy(id => id)
            .ToList();
    }

    private List<(int First, int Second)> FindP2PMatches(List<int> firstIds, List<int> secondIds)
    {
        var result = new List<(int First, int Second)>();
        foreach (var firstId in firstIds)
        {
            foreach (var secondId in secondIds)
            {
                if (P2PProximityPolicy.IsEligible(_world.Rockets[firstId].Location,
                        _world.Rockets[secondId].Location, _settings.P2P.ProximityMeters))
                    result.Add((firstId, secondId));
            }
        }
        return result;
    }

    private void SendP2POffer(TcpSession recipient, TcpSession peer, string pairToken,
        List<(int First, int Second)> matches)
    {
        Send(recipient, PacketType.P2PPeerOffer, new P2PPeerOfferPacket
        {
            Active = true,
            PeerPlayerId = peer.Id,
            PeerAddress = peer.UdpEndpoint!.Address.ToString(),
            PeerPort = peer.UdpEndpoint.Port,
            PairToken = pairToken,
            TransitionBufferSeconds = _settings.P2P.TransitionBufferSeconds,
            LocalRocketIds = matches.Select(match => match.First).Distinct().OrderBy(id => id).ToList(),
            PeerRocketIds = matches.Select(match => match.Second).Distinct().OrderBy(id => id).ToList(),
        });
    }

    private void SendP2PRevoke(TcpSession recipient, int peerId)
    {
        Send(recipient, PacketType.P2PPeerOffer, new P2PPeerOfferPacket
        {
            Active = false,
            PeerPlayerId = peerId,
            TransitionBufferSeconds = _settings.P2P.TransitionBufferSeconds,
        });
    }
    private void HandlePong(TcpSession session, TcpFrame frame)
    {
        if (frame.Payload.Length < 8) return;
        var sentTicks = BitConverter.ToInt64(frame.Payload, 0);
        if (!session.RecordHeartbeatPong(sentTicks, out var rtt)) return;
        Send(session, PacketType.UpdateWorldTime,
            new UpdateWorldTimePacket { WorldTime = WorldTime + rtt / 2000.0 * TimeScale });
    }

    private string? ValidateJoin(string username, string password, bool isResume)
    {
        if (!isResume && _players.Count >= _settings.MaxConnections) return "Server is full.";
        if (username.Length == 0 || username.Length > _settings.MaxUsernameLength || username.Any(char.IsControl))
            return "Invalid username.";
        if (!isResume && _settings.BlockDuplicatePlayerNames && _players.Values.Any(
                player => string.Equals(player.Username, username, StringComparison.OrdinalIgnoreCase)))
            return "Username is already in use.";
        return PasswordMatches(password, _settings.Password) ? null : "Invalid password.";
    }

    private void SendInitialState(TcpSession joining)
    {
        foreach (var player in _players.Values.OrderBy(player => player.Id))
        {
            Send(joining, PacketType.PlayerConnected, new PlayerConnectedPacket
            {
                PlayerId = player.Id,
                Username = player.Username,
                IconColor = player.Color,
                PrintMessage = false,
            });
            Send(joining, PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
            {
                PlayerId = player.Id,
                RocketId = player.ControlledRocket,
            });
        }
        SendWorldSnapshot(joining);
        Send(joining, PacketType.TimeWarp, new TimeWarpPacket
        {
            Operation = TimeWarpOperation.Applied,
            Multiplier = _timeScale,
            WorldTime = WorldTime,
        });

        Broadcast(PacketType.PlayerConnected, new PlayerConnectedPacket
        {
            PlayerId = joining.Id,
            Username = joining.Username,
            IconColor = joining.Color,
            PrintMessage = true,
        }, joining);

        Broadcast(PacketType.ShowToastMessage, new PlayerEventToastPacket
        {
            Message = $"{joining.Username} entered the world",
        }, joining);
    }

    private void SendWorldSnapshot(TcpSession session)
    {
        foreach (var rocket in _world.Rockets.OrderBy(pair => pair.Key))
            SendRocketSnapshot(session, rocket.Key);
    }

    private void SendRocketSnapshot(TcpSession session, int rocketId)
    {
        if (!_world.Rockets.TryGetValue(rocketId, out var rocket)) return;
        Send(session, PacketType.CreateRocket, new CreateRocketPacket
        {
            WorldTime = WorldTime,
            GlobalId = rocketId,
            Rocket = rocket,
        });
    }

    private void HandleData(TcpSession player, TcpFrame frame)
    {
        var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
        var rawType = message.ReadByte();
        if (!Enum.IsDefined(typeof(PacketType), rawType))
            throw new InvalidDataException($"Unknown packet type: {rawType}.");
        var type = (PacketType)rawType;
        player.PacketCounts.AddOrUpdate(type, 1, (_, count) => count + 1);
        switch (type)
        {
            case PacketType.UpdatePlayerControl: HandlePlayerControl(message, player); break;
            case PacketType.UpdatePlayerColor: HandlePlayerColor(message, player); break;
            case PacketType.SendChatMessage: HandleChat(message, player); break;
            case PacketType.CreateRocket: HandleCreateRocket(message, player); break;
            case PacketType.DestroyRocket: HandleDestroyRocket(message, player); break;
            case PacketType.UpdateRocketPrimary: HandleRocketPrimary(message, player); break;
            case PacketType.UpdateRocketSecondary: HandleRocketSecondary(message, player); break;
            case PacketType.DestroyPart: HandleDestroyPart(message, player); break;
            case PacketType.UpdateStaging: HandleStaging(message, player); break;
            case PacketType.UpdatePart_EngineModule: HandleEngine(message, player); break;
            case PacketType.UpdatePart_WheelModule: HandleWheel(message, player); break;
            case PacketType.UpdatePart_BoosterModule: HandleBooster(message, player); break;
            case PacketType.UpdatePart_ParachuteModule: HandleParachute(message, player); break;
            case PacketType.UpdatePart_MoveModule: HandleMove(message, player); break;
            case PacketType.UpdatePart_ResourceModule: HandleResource(message, player); break;
            case PacketType.DockTransaction: HandleDockTransaction(message, player); break;
            case PacketType.TimeWarp: HandleTimeWarp(message, player); break;
            case PacketType.ExperimentalAccess: HandleExperimentalAccess(message, player); break;
            default: throw new InvalidDataException($"Packet {type} is server-only or invalid after joining.");
        }
    }

    private void HandleExperimentalAccess(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<ExperimentalAccessPacket>(message);
        if (!packet.Request) return;
        player.ExperimentalAccessGranted = _settings.ExperimentalAccess.Accepts(packet.Passphrase);
        Send(player, PacketType.ExperimentalAccess, new ExperimentalAccessPacket
        {
            Request = false,
            Granted = player.ExperimentalAccessGranted,
            Message = player.ExperimentalAccessGranted ? "Experimental access granted." : "Experimental access denied."
        });
    }

    private void HandleTimeWarp(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<TimeWarpPacket>(message);
        if (packet.Operation != TimeWarpOperation.Request)
            throw new InvalidDataException("Invalid client time-warp operation.");

        if (_players.Count > 1)
        {
            if (!TimeWarpControlRules.CanSetPersonal(_players.Count, packet.Multiplier))
            {
                SendTimeWarpNotice(player, "Personal time scale in multiplayer must be between 1x and 5x.");
                return;
            }

            Send(player, PacketType.TimeWarp, new TimeWarpPacket
            {
                Operation = TimeWarpOperation.Applied,
                Multiplier = packet.Multiplier,
                WorldTime = WorldTime,
                Approved = true,
                Message = $"Personal time scale set to {packet.Multiplier:0.##}x."
            });
            return;
        }

        var controllingPlayers = _players.Values.Count(session => session.ControlledRocket != -1);
        if (!TimeWarpControlRules.CanSet(controllingPlayers, packet.Multiplier))
        {
            SendTimeWarpNotice(player, "The time scale cannot be changed right now.");
            return;
        }

        SetTimeScale(packet.Multiplier, string.Empty);
    }

    private void SendTimeWarpNotice(TcpSession player, string message)
    {
        Send(player, PacketType.TimeWarp, new TimeWarpPacket
        {
            Operation = TimeWarpOperation.Notice,
            Multiplier = _timeScale,
            WorldTime = WorldTime,
            Message = message,
        });
    }

    private void HandlePlayerControl(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePlayerControlPacket>(message);
        if (packet.RocketId != -1)
        {
            if (!_world.Rockets.ContainsKey(packet.RocketId) ||
                _players.Values.Any(other => other.Id != player.Id && other.ControlledRocket == packet.RocketId))
            {
                Send(player, PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
                {
                    PlayerId = player.Id,
                    RocketId = player.ControlledRocket,
                });
                return;
            }
        }
        if (packet.RocketId == player.ControlledRocket)
        {
            // 幂等回执（勿删）：客户端在 P2P 重建/原生选择/销毁等路径会重复请求同一火箭的控制权。
            // 曾因每次重复都 RefreshAuthorities+Broadcast，把偶发的控制交接竞态放大成
            // 数万次 UpdatePlayerControl/UpdatePlayerAuthority 风暴（真机日志 37820 次发送）。
            // 相同目标只单播确认给请求者，不刷新权威、不广播。
            Send(player, PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
            {
                PlayerId = player.Id,
                RocketId = player.ControlledRocket,
            });
            return;
        }
        packet.PlayerId = player.Id;
        player.ControlledRocket = packet.RocketId;
        RefreshAuthorities();
        EnforceTimeScaleControlRule();
        Broadcast(PacketType.UpdatePlayerControl, packet);
    }

    private void HandlePlayerColor(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePlayerColorPacket>(message);
        packet.PlayerId = player.Id;
        packet.Color = ClampColor(packet.Color);
        player.Color = packet.Color;
        Broadcast(PacketType.UpdatePlayerColor, packet, player);
    }

    private void HandleChat(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<SendChatMessagePacket>(message);
        var text = packet.Message.Trim();
        if (text.Length == 0 || text.Length > _settings.MaxChatMessageLength || text.Any(char.IsControl)) return;
        var now = DateTime.UtcNow;
        if ((now - player.LastChatUtc).TotalSeconds < _settings.ChatMessageCooldown) return;
        player.LastChatUtc = now;
        packet.SenderId = player.Id;
        packet.Message = text;
        Broadcast(PacketType.SendChatMessage, packet, player);
    }

    private void HandleCreateRocket(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<CreateRocketPacket>(message);
        ValidateRocket(packet.Rocket);
        if (packet.GlobalId >= 0 && _world.Rockets.ContainsKey(packet.GlobalId))
        {
            if (!CanUpdate(player, packet.GlobalId)) return;
            _world.Rockets[packet.GlobalId] = packet.Rocket;
            packet.WorldTime = WorldTime;
            packet.LocalId = -1;
            Broadcast(PacketType.CreateRocket, packet, player);
            return;
        }
        packet.GlobalId = NextRocketId();
        packet.WorldTime = WorldTime;
        _world.Rockets.Add(packet.GlobalId, packet.Rocket);
        // 只有创建者获得写权；旁观者的 CreateRocket 回执里 LocalId 必须清成 -1，
        // 否则不同客户端的本地临时编号碰撞会把对方火箭误认成自己的回显（错绑/错申请控制）。
        player.UpdateAuthority.Add(packet.GlobalId);
        Send(player, PacketType.CreateRocket, packet);
        packet.LocalId = -1;
        Broadcast(PacketType.CreateRocket, packet, player);
        if (!packet.ForLaunch) RefreshAuthorities();
    }

    private void HandleDestroyRocket(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<DestroyRocketPacket>(message);
        if (!CanUpdate(player, packet.RocketId) || !_world.Rockets.Remove(packet.RocketId)) return;
        packet.WorldTime = WorldTime;
        foreach (var connected in _players.Values)
        {
            if (connected.ControlledRocket == packet.RocketId)
            {
                connected.ControlledRocket = -1;
                Broadcast(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
                {
                    PlayerId = connected.Id,
                    RocketId = -1,
                });
            }
            connected.UpdateAuthority.Remove(packet.RocketId);
        }
        Broadcast(PacketType.DestroyRocket, packet, player);
        RefreshAuthorities();
    }

    private void HandleRocketPrimary(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdateRocketPrimaryPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        ValidateFinite(packet.Location, packet.Rotation, packet.AngularVelocity);
        RocketLatencyCompensation.Advance(packet, player.RoundTripMs);
        packet.WorldTime = WorldTime;
        rocket.Apply(packet);
        BroadcastLatest(PacketType.UpdateRocketPrimary, packet, packet.RocketId, player);
    }

    private void HandleRocketSecondary(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdateRocketSecondaryPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        if (!AllFinite(packet.InputTurn, packet.RawX, packet.RawY, packet.HorizontalX,
                packet.HorizontalY, packet.VerticalX, packet.VerticalY, packet.ThrottlePercent))
            throw new InvalidDataException("Rocket input contains a non-finite value.");
        packet.ThrottlePercent = Math.Clamp(packet.ThrottlePercent, 0, 1);
        packet.WorldTime = WorldTime;
        rocket.Apply(packet);
        Broadcast(PacketType.UpdateRocketSecondary, packet, player);
    }

    private void HandleDestroyPart(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<DestroyPartPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket) || !rocket.RemovePart(packet.PartId)) return;
        packet.WorldTime = WorldTime;
        Broadcast(PacketType.DestroyPart, packet, player);
    }

    private void HandleStaging(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdateStagingPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        ValidateStages(packet.Stages, rocket);
        packet.WorldTime = WorldTime;
        rocket.Stages = packet.Stages;
        Broadcast(PacketType.UpdateStaging, packet, player);
    }

    private void HandleDockTransaction(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<DockTransactionPacket>(message);
        if (packet.Committed || packet.KeepRocketId == packet.RemoveRocketId) return;
        if (packet.Operation == DockTransactionOperation.Undock)
        {
            HandleUndockTransaction(packet, player);
            return;
        }
        if (packet.KeepRocketId > packet.RemoveRocketId)
        {
            (packet.KeepRocketId, packet.RemoveRocketId) = (packet.RemoveRocketId, packet.KeepRocketId);
            (packet.KeepPartId, packet.RemovePartId) = (packet.RemovePartId, packet.KeepPartId);
        }
        if (!_world.Rockets.TryGetValue(packet.KeepRocketId, out var keep) ||
            !_world.Rockets.TryGetValue(packet.RemoveRocketId, out var remove) ||
            !keep.Parts.ContainsKey(packet.KeepPartId) || !remove.Parts.ContainsKey(packet.RemovePartId)) return;

        var keepController = _players.Values.FirstOrDefault(value => value.ControlledRocket == packet.KeepRocketId);
        var removeController = _players.Values.FirstOrDefault(value => value.ControlledRocket == packet.RemoveRocketId);
        if (player != keepController && player != removeController && !CanUpdate(player, packet.KeepRocketId)) return;

        var key = (packet.KeepRocketId, packet.RemoveRocketId, packet.KeepPartId, packet.RemovePartId);
        if (!_pendingDocks.TryGetValue(key, out var pending) ||
            DateTime.UtcNow - pending.CreatedUtc > TimeSpan.FromSeconds(5))
        {
            pending = new PendingDock(packet, DateTime.UtcNow);
            _pendingDocks[key] = pending;
        }
        pending.Confirmations.Add(player.Id);

        if (keepController is not null && removeController is not null && keepController.Id != removeController.Id &&
            (!pending.Confirmations.Contains(keepController.Id) || !pending.Confirmations.Contains(removeController.Id))) return;

        var merged = MergeDockedRockets(keep, remove, packet.KeepPartId, packet.RemovePartId);
        _world.Rockets[packet.KeepRocketId] = merged;
        _world.Rockets.Remove(packet.RemoveRocketId);
        _pendingDocks.Remove(key);

        foreach (var connected in _players.Values)
        {
            if (connected.ControlledRocket != packet.KeepRocketId && connected.ControlledRocket != packet.RemoveRocketId) continue;
            connected.ControlledRocket = -1;
            Broadcast(PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket
            {
                PlayerId = connected.Id,
                RocketId = -1,
            });
        }

        packet.Committed = true;
        packet.WorldTime = WorldTime;
        packet.MergedRocket = merged;
        Broadcast(PacketType.DockTransaction, packet);

        if (keepController is not null && removeController is not null && keepController.Id != removeController.Id)
        {
            Broadcast(PacketType.ShowToastMessage, new PlayerEventToastPacket
            {
                Message = $"{keepController.Username} docked with {removeController.Username}",
            });
        }
        RefreshAuthorities();
    }

    private static RocketState MergeDockedRockets(RocketState keep, RocketState remove, int keepPartId, int removePartId)
    {
        var keepPort = keep.Parts[keepPartId];
        var removePort = remove.Parts[removePartId];
        var keepPivotX = keepPort.X;
        var keepPivotY = keepPort.Y;
        var removePivotX = removePort.X;
        var removePivotY = removePort.Y;
        var keepDirection = PortDirectionDegrees(keepPort);
        var removeDirection = PortDirectionDegrees(removePort);
        var alignWorld = MathF.Round((keepDirection - removeDirection + 180f) / 90f) * 90f;
        var relativeRotation = alignWorld;

        var idMap = new Dictionary<int, int>();
        var nextId = keep.Parts.Count == 0 ? 1 : keep.Parts.Keys.Max() + 1;
        foreach (var pair in remove.Parts.OrderBy(pair => pair.Key))
        {
            while (keep.Parts.ContainsKey(nextId)) nextId++;
            var id = keep.Parts.ContainsKey(pair.Key) ? nextId++ : pair.Key;
            idMap[pair.Key] = id;
            var relativeX = pair.Value.X - removePivotX;
            var relativeY = pair.Value.Y - removePivotY;
            Rotate(relativeX, relativeY, relativeRotation, out var rotatedX, out var rotatedY);
            pair.Value.X = keepPivotX + rotatedX;
            pair.Value.Y = keepPivotY + rotatedY;
            pair.Value.OrientationZ += relativeRotation;
            keep.Parts[id] = pair.Value;
        }
        foreach (var joint in remove.Joints)
            keep.Joints.Add(new JointState(idMap[joint.PartA], idMap[joint.PartB]));
        keep.Joints.Add(new JointState(keepPartId, idMap[removePartId]));
        foreach (var stage in remove.Stages)
            keep.Stages.Add(new StageState(stage.StageId, stage.PartIds.Select(id => idMap[id])));
        keep.RocketName = string.IsNullOrWhiteSpace(keep.RocketName) ? remove.RocketName : keep.RocketName;
        return keep;
    }

    private void HandleUndockTransaction(DockTransactionPacket packet, TcpSession player)
    {
        if (!_world.Rockets.TryGetValue(packet.KeepRocketId, out var source) ||
            !CanUpdate(player, packet.KeepRocketId)) return;
        var bridge = source.Joints.FirstOrDefault(joint =>
            (joint.PartA == packet.KeepPartId && joint.PartB == packet.RemovePartId) ||
            (joint.PartA == packet.RemovePartId && joint.PartB == packet.KeepPartId));
        if (bridge is null) return;

        source.Joints.Remove(bridge);
        var groups = ConnectedPartGroups(source);
        if (groups.Count != 2) return;
        var keepGroup = groups.FirstOrDefault(group => group.Contains(packet.KeepPartId)) ?? groups[0];
        var secondGroup = groups.First(group => group != keepGroup);
        var firstRocket = ExtractRocket(source, keepGroup);
        var secondRocket = ExtractRocket(source, secondGroup);
        var secondId = NextRocketId();
        _world.Rockets[packet.KeepRocketId] = firstRocket;
        _world.Rockets[secondId] = secondRocket;
        ClearInvalidControlAssignments();

        packet.Committed = true;
        packet.WorldTime = WorldTime;
        packet.MergedRocket = firstRocket;
        packet.SecondRocketId = secondId;
        packet.SecondRocket = secondRocket;
        Broadcast(PacketType.DockTransaction, packet);
        RefreshAuthorities();
    }

    private static List<HashSet<int>> ConnectedPartGroups(RocketState rocket)
    {
        var neighbours = rocket.Parts.Keys.ToDictionary(id => id, _ => new List<int>());
        foreach (var joint in rocket.Joints)
        {
            neighbours[joint.PartA].Add(joint.PartB);
            neighbours[joint.PartB].Add(joint.PartA);
        }
        var remaining = new HashSet<int>(rocket.Parts.Keys);
        var groups = new List<HashSet<int>>();
        while (remaining.Count > 0)
        {
            var group = new HashSet<int>();
            var stack = new Stack<int>();
            stack.Push(remaining.First());
            while (stack.Count > 0)
            {
                var id = stack.Pop();
                if (!remaining.Remove(id)) continue;
                group.Add(id);
                foreach (var neighbour in neighbours[id]) stack.Push(neighbour);
            }
            groups.Add(group);
        }
        return groups;
    }

    private static RocketState ExtractRocket(RocketState source, HashSet<int> ids)
    {
        var result = new RocketState
        {
            RocketName = source.RocketName,
            Location = source.Location,
            Rotation = source.Rotation,
            AngularVelocity = source.AngularVelocity,
            ThrottleOn = source.ThrottleOn,
            ThrottlePercent = source.ThrottlePercent,
            Rcs = source.Rcs,
            Joints = source.Joints.Where(joint => ids.Contains(joint.PartA) && ids.Contains(joint.PartB)).ToList(),
            Stages = source.Stages.Select(stage => new StageState(stage.StageId, stage.PartIds.Where(ids.Contains))).Where(stage => stage.PartIds.Count > 0).ToList(),
        };
        foreach (var id in ids) result.Parts[id] = source.Parts[id];
        return result;
    }

    private static float PortDirectionDegrees(PartState part) => part.OrientationZ + (part.OrientationY < 0 ? -90f : 90f);

    private static void Rotate(float x, float y, float degrees, out float rotatedX, out float rotatedY)
    {
        var radians = degrees * MathF.PI / 180f;
        var cos = MathF.Cos(radians);
        var sin = MathF.Sin(radians);
        rotatedX = x * cos - y * sin;
        rotatedY = x * sin + y * cos;
    }

    private void HandleEngine(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartEnginePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        packet.WorldTime = WorldTime; part.ToggleVariables["engine_on"] = packet.EngineOn;
        Broadcast(PacketType.UpdatePart_EngineModule, packet, player);
    }

    private void HandleWheel(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartWheelPacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        packet.WorldTime = WorldTime; part.ToggleVariables["wheel_on"] = packet.WheelOn;
        Broadcast(PacketType.UpdatePart_WheelModule, packet, player);
    }

    private void HandleBooster(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartBoosterPacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.Throttle, packet.FuelPercent)) throw new InvalidDataException("Booster state is non-finite.");
        packet.Throttle = Math.Clamp(packet.Throttle, 0, 1); packet.FuelPercent = Math.Clamp(packet.FuelPercent, 0, 1);
        packet.WorldTime = WorldTime; part.NumberVariables["fuel_percent"] = packet.FuelPercent;
        Broadcast(PacketType.UpdatePart_BoosterModule, packet, player);
    }

    private void HandleParachute(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartParachutePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.State, packet.TargetState)) throw new InvalidDataException("Parachute state is non-finite.");
        packet.WorldTime = WorldTime; part.NumberVariables["animation_state"] = packet.State;
        part.NumberVariables["deploy_state"] = packet.TargetState;
        Broadcast(PacketType.UpdatePart_ParachuteModule, packet, player);
    }

    private void HandleMove(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartMovePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.Time, packet.TargetTime)) throw new InvalidDataException("Move state is non-finite.");
        packet.WorldTime = WorldTime; part.NumberVariables["state"] = packet.Time;
        part.NumberVariables["state_target"] = packet.TargetTime;
        Broadcast(PacketType.UpdatePart_MoveModule, packet, player);
    }

    private void HandleResource(NetIncomingMessage message, TcpSession player)
    {
        var packet = Read<UpdatePartResourcePacket>(message);
        if (!CanUpdate(player, packet.RocketId) || !double.IsFinite(packet.ResourcePercent)) return;
        if (!_world.Rockets.TryGetValue(packet.RocketId, out var rocket)) return;
        packet.ResourcePercent = Math.Clamp(packet.ResourcePercent, 0, 1);
        var found = false;
        foreach (var id in packet.PartIds)
            if (rocket.Parts.TryGetValue(id, out var part))
            { part.NumberVariables["fuel_percent"] = packet.ResourcePercent; found = true; }
        if (!found) return;
        packet.WorldTime = WorldTime;
        Broadcast(PacketType.UpdatePart_ResourceModule, packet, player);
    }

    private void Send(TcpSession session, PacketType type, INetData packet)
    {
        var payload = NetPayloadCodec.Serialize(type, packet);
        EnqueueCritical(session, new TcpFrame(TcpFrameKind.Packet,
            Interlocked.Increment(ref _sequence), payload.Data, payload.BitLength));
    }

    private void Broadcast(PacketType type, INetData packet, TcpSession? except = null)
    {
        var payload = NetPayloadCodec.Serialize(type, packet);
        foreach (var session in _players.Values)
        {
            if (session == except) continue;
            EnqueueCritical(session, new TcpFrame(TcpFrameKind.Packet,
                Interlocked.Increment(ref _sequence), payload.Data, payload.BitLength));
        }
    }

    private void BroadcastLatest(PacketType type, INetData packet, int rocketId, TcpSession? except = null)
    {
        var payload = NetPayloadCodec.Serialize(type, packet);
        var key = ((long)(byte)type << 32) | (uint)rocketId;
        foreach (var session in _players.Values)
        {
            if (session == except) continue;
            var udpEndpoint = session.UdpEndpoint;
            if (udpEndpoint is not null)
            {
                _udp.SendState(udpEndpoint, session.UdpToken, payload.Data);
            }
            else
            {
                session.Queue.EnqueueLatest(key, new TcpFrame(TcpFrameKind.Packet,
                    Interlocked.Increment(ref _sequence), payload.Data, payload.BitLength));
                session.Signal();
            }
        }
    }

    private static void EnqueueCritical(TcpSession session, TcpFrame frame)
    {
        session.Queue.EnqueueCritical(frame);
        session.Signal();
    }

    private void ClearInvalidControlAssignments()
    {
        foreach (var player in _players.Values)
        {
            if (player.ControlledRocket >= 0 && !_world.Rockets.ContainsKey(player.ControlledRocket))
                player.ControlledRocket = -1;
        }
    }

    private void RefreshAuthorities()
    {
        foreach (var player in _players.Values) player.UpdateAuthority.Clear();
        var participants = _players.Values
            .Select(player => new AuthorityParticipant(player.Id, player.ControlledRocket))
            .ToArray();
        var assignments = AuthorityAllocationPolicy.Allocate(_world.Rockets.Keys, participants);
        foreach (var assignment in assignments)
        {
            if (_players.TryGetValue(assignment.Value, out var owner))
                owner.UpdateAuthority.Add(assignment.Key);
        }
        foreach (var player in _players.Values)
            Send(player, PacketType.UpdatePlayerAuthority,
                new UpdatePlayerAuthorityPacket { RocketIds = new HashSet<int>(player.UpdateAuthority) });
        EnforceTimeScaleControlRule();
    }

    private bool CanUpdate(TcpSession player, int rocketId) =>
        player.ControlledRocket == rocketId || player.UpdateAuthority.Contains(rocketId);

    private bool TryAuthorizedRocket(TcpSession player, int rocketId, out RocketState rocket)
    {
        if (CanUpdate(player, rocketId) && _world.Rockets.TryGetValue(rocketId, out rocket!)) return true;
        rocket = null!; return false;
    }

    private bool TryAuthorizedPart(TcpSession player, int rocketId, int partId, out PartState part)
    {
        if (TryAuthorizedRocket(player, rocketId, out var rocket) && rocket.Parts.TryGetValue(partId, out part!))
            return true;
        part = null!; return false;
    }

    private int NextRocketId()
    {
        int id;
        do id = RandomNumberGenerator.GetInt32(1, int.MaxValue);
        while (_world.Rockets.ContainsKey(id));
        return id;
    }

    private static T Read<T>(NetIncomingMessage message) where T : INetData, new()
    { var packet = new T(); packet.Deserialize(message); return packet; }

    private static Color3 ClampColor(Color3 color)
    {
        if (!AllFinite(color.R, color.G, color.B)) throw new InvalidDataException("Color is non-finite.");
        return new Color3(Math.Clamp(color.R, 0, 1), Math.Clamp(color.G, 0, 1), Math.Clamp(color.B, 0, 1));
    }

    private static void ValidateFinite(NetLocation location, float rotation, float angularVelocity)
    {
        if (!double.IsFinite(location.X) || !double.IsFinite(location.Y) ||
            !double.IsFinite(location.Vx) || !double.IsFinite(location.Vy) ||
            !AllFinite(rotation, angularVelocity) || string.IsNullOrWhiteSpace(location.Address) ||
            location.Address.Length > 256 || location.Address.Any(char.IsControl))
            throw new InvalidDataException("Rocket location contains invalid values.");
    }

    private static bool AllFinite(params float[] values) => values.All(float.IsFinite);

    private static void ValidateRocket(RocketState rocket)
    {
        if (rocket.RocketName.Length > 256 || rocket.RocketName.Any(char.IsControl))
            throw new InvalidDataException("Rocket name is invalid.");
        if (rocket.Parts.Count > NetMessageExtensions.MaxCollectionCount ||
            rocket.Joints.Count > NetMessageExtensions.MaxCollectionCount ||
            rocket.Stages.Count > NetMessageExtensions.MaxCollectionCount)
            throw new InvalidDataException("Rocket collections are too large.");
        ValidateFinite(rocket.Location, rocket.Rotation, rocket.AngularVelocity);
        if (!float.IsFinite(rocket.ThrottlePercent)) throw new InvalidDataException("Rocket throttle is non-finite.");
        ValidateStages(rocket.Stages, rocket);
    }

    private static void ValidateStages(IEnumerable<StageState> stages, RocketState rocket)
    {
        foreach (var stage in stages)
        {
            if (stage.PartIds.Count > NetMessageExtensions.MaxCollectionCount)
                throw new InvalidDataException("Stage contains too many part IDs.");
            if (stage.PartIds.Any(id => !rocket.Parts.ContainsKey(id)))
                throw new InvalidDataException("Stage references an unknown part.");
        }
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
            0 => new Color3(1, x, 0), 1 => new Color3(x, 1, 0),
            2 => new Color3(0, 1, x), 3 => new Color3(0, x, 1),
            4 => new Color3(x, 0, 1), _ => new Color3(1, 0, x),
        };
    }

    private static TcpFrame DisconnectFrame(string reason)
    {
        var bytes = Encoding.UTF8.GetBytes(reason);
        return new TcpFrame(TcpFrameKind.Disconnect, 0, bytes, bytes.Length * 8);
    }

    private static ValueTask SendDisconnectDirectAsync(Stream stream, string reason, CancellationToken cancellationToken) =>
        TcpFrameCodec.WriteAsync(stream, DisconnectFrame(reason), cancellationToken);

    private void SaveIfDue()
    {
        if (_settings.AutoSaveSeconds <= 0 || string.IsNullOrWhiteSpace(_settings.StatePath)) return;
        if (_saveClock.Elapsed.TotalSeconds < _settings.AutoSaveSeconds) return;
        SaveState(); _saveClock.Restart();
    }

    private void SaveState()
    {
        if (string.IsNullOrWhiteSpace(_settings.StatePath)) return;
        lock (_worldLock)
        {
            _world.WorldTime += _worldClock.Elapsed.TotalSeconds * _timeScale;
            _worldClock.Restart();
            ServerStateStore.Save(_settings.StatePath, _world);
        }
    }

    public ServerCommandResult ExecuteCommand(string? commandLine)
    {
        var line = (commandLine ?? string.Empty).Trim();
        if (line.Length == 0) return new ServerCommandResult(false, string.Empty);
        var parts = line.Split(new[] { ' ' }, StringSplitOptions.RemoveEmptyEntries);
        var command = parts[0].ToLowerInvariant();
        switch (command)
        {
            case "help":
                return new ServerCommandResult(false,
                    "常用: status, players, network, say <消息>, time <倍率|off>, debris [auto|最大部件数], world <save|sync>, kick <ID|名字>, reload（热重载补丁）, stop。");
            case "status":
                return new ServerCommandResult(false,
                    $"玩家={PlayerCount} 火箭={RocketCount()} 世界时间={WorldTime:F1} 倍率={TimeScale:0.##}x");
            case "players":
                return new ServerCommandResult(false, PlayerList());
            case "network":
                return new ServerCommandResult(false, NetworkList());
            case "say":
                if (parts.Length < 2) return new ServerCommandResult(false, "用法: say <消息>");
                var text = line.Substring(line.IndexOf(' ') + 1).Trim();
                if (text.Length == 0) return new ServerCommandResult(false, "用法: say <消息>");
                Broadcast(PacketType.SendChatMessage, new SendChatMessagePacket { SenderId = -1, Message = "[Server] " + text });
                return new ServerCommandResult(false, "广播已发送。");
            case "time":
                if (parts.Length == 2 && string.Equals(parts[1], "off", StringComparison.OrdinalIgnoreCase))
                {
                    SetTimeScale(1, "服务器结束时间加速");
                    return new ServerCommandResult(false, "时间倍率已恢复为 1x。");
                }
                if (parts.Length != 2 || !double.TryParse(parts[1], System.Globalization.NumberStyles.Float,
                        System.Globalization.CultureInfo.InvariantCulture, out var scale) || !IsAllowedTimeScale(scale))
                    return new ServerCommandResult(false, "允许的时间倍率: 1 到 2500");
                SetTimeScale(scale, "服务器强制设置");
                return new ServerCommandResult(false, $"时间倍率已强制设置为 {scale:0.##}x。");
            case "debris":
                var maxParts = 3;
                if (parts.Length == 2 && string.Equals(parts[1], "auto", StringComparison.OrdinalIgnoreCase))
                {
                    _autoClearDebris = !_autoClearDebris;
                    return new ServerCommandResult(false, $"debris auto 已{(_autoClearDebris ? "开启" : "关闭")}，自动阈值 {AutoClearDebrisMaxParts} 个部件。");
                }
                if (parts.Length > 2 || (parts.Length == 2 && (!int.TryParse(parts[1], out maxParts) || maxParts < 0)))
                    return new ServerCommandResult(false, "用法: debris [auto|最大部件数]（默认 3）");
                var removed = ClearDebris(maxParts);
                return new ServerCommandResult(false, $"已清理 {removed} 枚无人控制且部件数不超过 {maxParts} 的太空垃圾。");
            case "world":
                if (parts.Length != 2)
                    return new ServerCommandResult(false, "用法: world <save|sync>");
                if (string.Equals(parts[1], "save", StringComparison.OrdinalIgnoreCase))
                {
                    SaveState();
                    return new ServerCommandResult(false, "世界状态已保存。");
                }
                if (string.Equals(parts[1], "sync", StringComparison.OrdinalIgnoreCase))
                {
                    lock (_worldLock)
                        foreach (var session in _players.Values) SendWorldSnapshot(session);
                    return new ServerCommandResult(false, "已向所有玩家发送世界快照。");
                }
                return new ServerCommandResult(false, "用法: world <save|sync>");
            case "kick":
                if (parts.Length != 2) return new ServerCommandResult(false, "用法: kick <玩家ID|名字>");
                var target = FindPlayer(parts[1]);
                if (target is null) return new ServerCommandResult(false, "未找到玩家。");
                if (_players.TryRemove(target.Id, out _))
                {
                	target.Close();
                	lock (_worldLock)
                	{
                		Broadcast(PacketType.PlayerDisconnected, new PlayerDisconnectedPacket { PlayerId = target.Id }, target);
                		RefreshAuthorities();
                	}
                }
                return new ServerCommandResult(false, $"已踢出 {target.Username} (ID {target.Id})。");
            case "reload":
                return ReloadPatches();
            case "stop":
                return new ServerCommandResult(true, "正在安全保存并停止服务端。");
            default:
                return new ServerCommandResult(false, "未知命令。输入 help 查看命令。");
        }
    }

    private int RocketCount() { lock (_worldLock) return _world.Rockets.Count; }

    private string PlayerList()
    {
        var players = _players.Values.OrderBy(p => p.Id).Select(p =>
            $"{p.Id}: {p.Username} 控制={p.ControlledRocket} RTT={p.RoundTripMs:F0}ms").ToArray();
        return players.Length == 0 ? "当前没有在线玩家。" : string.Join(Environment.NewLine, players);
    }

    private string NetworkList()
    {
        var players = _players.Values.OrderBy(player => player.Id).ToArray();
        if (players.Length == 0) return "当前没有在线玩家。";
        var lines = new List<string> { "玩家名\t丢包率\t延迟" };
        lines.AddRange(players.Select(player =>
        {
            var loss = player.HeartbeatSamples == 0 ? "--" : $"{player.PacketLossRate:F1}%";
            var latency = player.RoundTripMs <= 0 ? "--" : $"{player.RoundTripMs:F0}ms";
            return $"{player.Username}\t{loss}\t{latency}";
        }));
        return string.Join(Environment.NewLine, lines);
    }

    private TcpSession? FindPlayer(string value)
    {
        if (int.TryParse(value, out var id) && _players.TryGetValue(id, out var byId)) return byId;
        return _players.Values.FirstOrDefault(p => string.Equals(p.Username, value, StringComparison.OrdinalIgnoreCase));
    }

    private static bool IsAllowedTimeScale(double value) =>
        double.IsFinite(value) && value >= TimeWarpControlRules.MinimumMultiplier &&
        value <= TimeWarpControlRules.MaximumMultiplier;

    private void EnforceTimeScaleControlRule()
    {
        if (_timeScale == 1) return;
        var controllingPlayers = _players.Values.Count(session => session.ControlledRocket != -1);
        if (controllingPlayers != 1) SetTimeScale(1, string.Empty);
    }

    private void SetTimeScale(double scale, string reason)
    {
        lock (_worldLock)
        {
            _world.WorldTime += _worldClock.Elapsed.TotalSeconds * _timeScale;
            _worldClock.Restart();
            _timeScale = scale;
            Broadcast(PacketType.TimeWarp, new TimeWarpPacket
            {
                Operation = TimeWarpOperation.Applied,
                Multiplier = scale,
                WorldTime = _world.WorldTime,
                Approved = true,
                Message = reason,
            });
        }
    }

    private int ClearDebris(int maxParts)
    {
        lock (_worldLock)
        {
            var controlled = _players.Values.Select(player => player.ControlledRocket).Where(id => id >= 0).ToHashSet();
            var ids = _world.Rockets.Where(pair => !controlled.Contains(pair.Key) && pair.Value.Parts.Count <= maxParts)
                .Select(pair => pair.Key).ToArray();
            foreach (var id in ids)
            {
                _world.Rockets.Remove(id);
                Broadcast(PacketType.DestroyRocket, new DestroyRocketPacket
                {
                    WorldTime = WorldTime,
                    RocketId = id,
                    Reason = 0,
                });
            }
            if (ids.Length > 0) RefreshAuthorities();
            return ids.Length;
        }
    }

    private void PrintDebugSummary()
    {
        var sent = _players.Values.Sum(player => player.SentBytes);
        var received = _players.Values.Sum(player => player.ReceivedBytes);
        var overwritten = _players.Values.Sum(player => player.Queue.OverwrittenStates);
        Console.WriteLine($"[TCP调试] 玩家={_players.Count} 上行={received / 1024.0:F1}KB 下行={sent / 1024.0:F1}KB 状态覆盖={overwritten}");
        foreach (var player in _players.Values.OrderBy(player => player.Id))
            Console.WriteLine($"[TCP玩家] {player.Username} RTT={player.RoundTripMs:F0}ms 抖动={player.JitterMs:F0}ms 队列={player.Queue.Count} 收={player.ReceivedFrames} 发={player.SentFrames}");
    }

    public ValueTask DisposeAsync()
    {
        if (_started)
        {
            _listener.Stop();
            _udp.Dispose();
            foreach (var session in _players.Values) session.Close();
            _started = false;
        }
        // 退出前卸载全部补丁，触发其 OnUnload 释放资源。
        _patchLoader?.Dispose();
        return ValueTask.CompletedTask;
    }

    // DLL 补丁加载器（MC 插件范式：启动扫描 plugins/ 加载，reload 命令热重载）。
    private ServerPatch.PatchLoader? _patchLoader;

    // 启动加载 plugins/ 目录下的补丁 DLL。失败不影响服务器本体运行。
    public void LoadPatches()
    {
        try
        {
            _patchLoader = new ServerPatch.PatchLoader(this);
            _patchLoader.LoadAll();
        }
        catch (Exception ex)
        {
            Console.WriteLine($"[补丁] 加载器初始化失败: {ex.Message}");
            if (Environment.GetEnvironmentVariable("SFS_SERVER_DEBUG") == "1")
                Console.Error.WriteLine(ex);
        }
    }

    // 控制台 reload 命令调用：热重载全部补丁，不重启服务器。
    public ServerCommandResult ReloadPatches()
    {
        if (!_settings.ExperimentalPatches)
            return new ServerCommandResult(false, "实验性功能未开启（server.yml 的 experimental_patches: false），无补丁可重载。");
        if (_patchLoader is null)
        {
            LoadPatches();
            return new ServerCommandResult(false, "补丁加载器未初始化，已重新初始化并加载。");
        }
        try
        {
            _patchLoader.Reload();
            return new ServerCommandResult(false, "已热重载全部补丁。");
        }
        catch (Exception ex)
        {
            return new ServerCommandResult(false, $"热重载失败: {ex.Message}");
        }
    }

    private sealed class PendingDock
    {
        public DockTransactionPacket Request { get; }
        public DateTime CreatedUtc { get; }
        public HashSet<int> Confirmations { get; } = new();

        public PendingDock(DockTransactionPacket request, DateTime createdUtc)
        {
            Request = request;
            CreatedUtc = createdUtc;
        }
    }


    private sealed class TcpSession
    {
        public int Id { get; }
        public string Username { get; }
        public Color3 Color { get; set; }
        public TcpClient Client { get; private set; }
        public NetworkStream Stream { get; private set; }
        public int ConnectionGeneration { get; private set; } = 1;
        public TcpSendQueue Queue { get; } = new();
        public SemaphoreSlim SendSignal { get; } = new(0, 1);
		public CancellationTokenSource Closed { get; private set; } = new();
        public HashSet<int> UpdateAuthority { get; } = new();
        public ConcurrentDictionary<PacketType, long> PacketCounts { get; } = new();
        public string UdpToken { get; } = Convert.ToHexString(RandomNumberGenerator.GetBytes(24));
        public string ResumeToken { get; } = Convert.ToHexString(RandomNumberGenerator.GetBytes(24));
		private readonly object _udpHealthLock = new();
		private IPEndPoint? _udpEndpoint;
		private DateTime _lastUdpReceiveUtc = DateTime.UtcNow;
        public DateTime RecoveryExpiresUtc { get; private set; } = DateTime.MinValue;
        public int ControlledRocket { get; set; } = -1;
        public bool ExperimentalAccessGranted { get; set; }
        public DateTime LastChatUtc { get; set; } = DateTime.MinValue;
        public DateTime LastReceiveUtc { get; set; } = DateTime.UtcNow;
        private readonly object _heartbeatLock = new();
        private long _lastPingTicks;
        private long _lastPongTicks;
        private long _heartbeatSamples;
        private long _heartbeatLost;
        public long LastPingTicks { get { lock (_heartbeatLock) return _lastPingTicks; } }
        public long HeartbeatSamples { get { lock (_heartbeatLock) return _heartbeatSamples; } }
        public double PacketLossRate
        {
            get
            {
                lock (_heartbeatLock)
                    return _heartbeatSamples == 0 ? 0 : _heartbeatLost * 100.0 / _heartbeatSamples;
            }
        }
        public double RoundTripMs { get; set; }
        public double JitterMs { get; set; }
        public long SentBytes { get; set; }
        public long ReceivedBytes { get; set; }
        public long SentFrames { get; set; }
        public long ReceivedFrames { get; set; }

        public TcpSession(int id, string username, Color3 color, TcpClient client, NetworkStream stream)
        { Id = id; Username = username; Color = color; Client = client; Stream = stream; }

		public IPEndPoint? UdpEndpoint
		{
			get { lock (_udpHealthLock) return _udpEndpoint; }
		}

		public void RecordUdpEndpoint(IPEndPoint endpoint)
		{
			lock (_udpHealthLock) _udpEndpoint = endpoint;
		}

		public void RecordUdpReceive(DateTime now)
		{
			lock (_udpHealthLock) _lastUdpReceiveUtc = now;
		}


        public bool CanResume(string token) =>
            RecoveryExpiresUtc >= DateTime.UtcNow &&
            !string.IsNullOrEmpty(token) &&
            CryptographicOperations.FixedTimeEquals(
                Encoding.UTF8.GetBytes(ResumeToken), Encoding.UTF8.GetBytes(token));

        public void RegisterHeartbeat(long ticks)
        {
            lock (_heartbeatLock)
            {
                if (_lastPingTicks != 0 && _lastPongTicks != _lastPingTicks)
                    _heartbeatLost++;
                _heartbeatSamples++;
                _lastPingTicks = ticks;
                _lastPongTicks = 0;
            }
        }

        public bool RecordHeartbeatPong(long sentTicks, out double rtt)
        {
            lock (_heartbeatLock)
            {
                if (_lastPingTicks == 0 || sentTicks != _lastPingTicks || _lastPongTicks == sentTicks)
                {
                    rtt = 0;
                    return false;
                }
                _lastPongTicks = sentTicks;
                rtt = TimeSpan.FromTicks(Math.Max(0, DateTime.UtcNow.Ticks - sentTicks)).TotalMilliseconds;
                JitterMs = RoundTripMs <= 0 ? 0 : JitterMs * 0.8 + Math.Abs(rtt - RoundTripMs) * 0.2;
                RoundTripMs = rtt;
                return true;
            }
        }

        public void ReplaceConnection(TcpClient client, NetworkStream stream)
        {
            var oldClient = Client;
            Client = client;
            Stream = stream;
            ConnectionGeneration++;
            ExperimentalAccessGranted = false;
            RecoveryExpiresUtc = DateTime.MinValue;
            LastReceiveUtc = DateTime.UtcNow;
            try { oldClient.Close(); } catch { }
            Closed = new CancellationTokenSource(); // 重置连接取消源，使恢复连接后的写入循环不被旧取消状态影响
        }

        public bool EnterRecoveryWindow()
        {
            lock (_udpHealthLock)
                if (DateTime.UtcNow - _lastUdpReceiveUtc > TimeSpan.FromSeconds(5)) return false;
            RecoveryExpiresUtc = DateTime.UtcNow.AddSeconds(20);
            return true;
        }

        public void Signal()
        {
            try { SendSignal.Release(); } catch (SemaphoreFullException) { }
        }

        public void Close()
        {
            if (!Closed.IsCancellationRequested) Closed.Cancel();
            try { Client.Close(); } catch { }
            Signal();
        }
    }
}
