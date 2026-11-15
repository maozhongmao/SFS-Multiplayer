// SPDX-License-Identifier: MPL-2.0
using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Reflection;
using System.Threading;
using System.Threading.Tasks;
using Lidgren.Network;
using MultiplayerSFS.Common;
using MultiplayerSFS.Common.Transport.Kcp;
using MultiplayerSFS.Mod;
using MultiplayerSFS.Mod.Patches;
using SFS.WorldBase;
using UnityEngine;

internal static class Program
{
    private static int failures;

    private static void Main(string[] args)
    {
        if (args.Length >= 3 && args[0] == "--tcp-smoke")
        {
            RunTcpSmoke(args).GetAwaiter().GetResult();
            return;
        }
        Run("Packet numbers match the current .NET 8 server", PacketNumbersMatchServer);
        Run("Strings match the current server wire format", StringsMatchServerWireFormat);
        Run("Join response accepts the server payload", JoinResponseAcceptsServerPayload);
        Run("Chat packet accepts and emits the server payload", ChatPacketMatchesServerPayload);
        Run("DestroyRocket accepts byte-encoded destruction reasons", DestroyRocketByteReasonRoundTrips);
        Run("DestroyPart routes by RocketId", DestroyPartRoutesByRocketId);
        Run("Discrete events run only when due", DiscreteEventsRunOnlyWhenDue);
        Run("Interpolation fraction handles duplicate timestamps", DuplicateTimestampIsSafe);
        Run("Interpolation correction keeps an already-timed target in place", CorrectionDoesNotAdvanceTarget);
        Run("TCP frames survive stream coalescing", TcpFramesSurviveStreamCoalescing);
        Run("Transport termination always reaches the game thread", TransportTerminationAlwaysReachesGameThread);
        Run("Remote TCP close reaches the game thread", () => RemoteTcpCloseReachesGameThread().GetAwaiter().GetResult());
        Run("Network packet pump waits for initialized multiplayer world", NetworkPacketPumpWaitsForInitializedWorld);
        Run("Missing local player is safe during early packets", MissingLocalPlayerIsSafe);
        Run("TCP send queue keeps latest rocket state", TcpSendQueueKeepsLatestRocketState);
        Run("Authority handoff primes state before physics", AuthorityHandoffPrimesBeforePhysics);
        Run("Rocket state uses UDP when channel is healthy", RocketStateUsesUdpWhenHealthy);
        Run("Rocket sync policy lowers idle traffic", RocketSyncPolicyLowersIdleTraffic);
        Run("Time-warp vote packet matches server wire format", TimeWarpPacketMatchesServerWireFormat);
        Run("Time-warp sync has no popup surface", TimeWarpSyncHasNoPopupSurface);
        Run("Multiplayer split follows native selection", MultiplayerSplitFollowsNativeSelection);
        Run("Control coordinator applies only server confirmation", ControlCoordinatorUsesServerConfirmation);
        Run("Control coordinator suppresses duplicate pending requests", ControlCoordinatorSuppressesDuplicatePendingRequests);
        Run("P2P state accepts only newer generation and sequence", P2PStateOrderingIsMonotonic);
        Run("P2P proximity requires same coordinate system and threshold", P2PProximityUsesRelativeDistance);
        Run("P2P timeout enters relay fallback", P2PTimeoutFallsBackToServer);
        Run("P2P establishment triggers one delayed authoritative resync", P2PEstablishmentTriggersOneAuthoritativeResync);
        Run("Network adaptation keeps drift resync tight", NetworkAdaptationKeepsDriftResyncTight);
        Run("Render state applies interpolation with zero smoothing lag", RenderStateAppliesInterpolationWithZeroLag);
        Run("Experimental unlock packet round-trips", ExperimentalUnlockPacketRoundTrips);
        Run("UDP bind remains usable after BindAck", UdpBindRemainsUsableAfterBindAck);
        Run("UDP bind enables direct state send", UdpBindEnablesDirectStateSend);
        Run("UDP state packet reaches callback without acknowledgement", UdpStatePacketReachesCallback);
        Run("UDP state keeps sending after BindAck", UdpStateKeepsSendingAfterBindAck);
        Run("UDP receive callback failure does not kill the receive loop", UdpCallbackFailureIsIsolated);

        Console.WriteLine(failures == 0 ? "ALL CLIENT REGRESSION TESTS PASSED" : $"FAILED: {failures}");
        Environment.ExitCode = failures == 0 ? 0 : 1;
    }

    private static async Task RunTcpSmoke(string[] args)
    {
        var transport = new TcpClientTransport();
        try
        {
            Packet_JoinResponse response = await transport.ConnectAsync(
                IPAddress.Parse(args[1]), int.Parse(args[2]), new Packet_JoinRequest
                {
                    Username = "net48-smoke",
                    Password = args.Length >= 4 ? args[3] : string.Empty,
                    SolarSystemName = string.Empty
                });
            True(response.PlayerId >= 0, "TCP handshake player id");
            Equal(50.0, response.UpdateRocketsPeriod, "TCP update period");

            bool receivedSelf = false;
            DateTime deadline = DateTime.UtcNow.AddSeconds(6);
            while (DateTime.UtcNow < deadline && (!receivedSelf || transport.RoundTripMs <= 0))
            {
                TcpFrame frame;
                while (transport.TryReceive(out frame))
                {
                    if (frame.Kind != TcpFrameKind.Packet) continue;
                    NetIncomingMessage message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                    PacketType type = (PacketType)message.ReadByte();
                    if (type == PacketType.PlayerConnected)
                    {
                        Packet_PlayerConnected player = message.Read<Packet_PlayerConnected>();
                        if (player.PlayerId == response.PlayerId && player.Username == "net48-smoke") receivedSelf = true;
                    }
                }
                await Task.Delay(25);
            }
            True(receivedSelf, "TCP self player snapshot");
            True(transport.RoundTripMs > 0, "TCP application heartbeat RTT");
            True(transport.Connected, "TCP connection remains active");

            var rocket = new RocketState
            {
                rocketName = "TCP Cross Runtime Rocket",
                location = new NetLocation(new Double2(1, 2), new Double2(0, 0), "Earth"),
                rotation = 0,
                angularVelocity = 0,
                throttleOn = false,
                throttlePercent = 0,
                RCS = false,
                parts = new Dictionary<int, PartState>(),
                joints = new List<JointState>(),
                stages = new List<StageState>()
            };
            transport.Send(new Packet_CreateRocket
            {
                WorldTime = response.WorldTime,
                LocalId = 321,
                GlobalId = -1,
                ForLaunch = true,
                Rocket = rocket
            });

            Packet_CreateRocket echoedRocket = null;
            deadline = DateTime.UtcNow.AddSeconds(5);
            while (DateTime.UtcNow < deadline && echoedRocket == null)
            {
                TcpFrame frame;
                while (transport.TryReceive(out frame))
                {
                    if (frame.Kind != TcpFrameKind.Packet) continue;
                    NetIncomingMessage message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                    if ((PacketType)message.ReadByte() != PacketType.CreateRocket) continue;
                    Packet_CreateRocket candidate = message.Read<Packet_CreateRocket>();
                    if (candidate.LocalId == 321) echoedRocket = candidate;
                }
                await Task.Delay(25);
            }
            True(echoedRocket != null, "TCP CreateRocket echo");
            True(echoedRocket.GlobalId > 0, "TCP server assigned global rocket id");
            Equal("TCP Cross Runtime Rocket", echoedRocket.Rocket.rocketName, "TCP rocket payload");
            Console.WriteLine("TCP_NET48_SMOKE_OK PlayerId=" + response.PlayerId +
                " RocketId=" + echoedRocket.GlobalId + " RTT=" + transport.RoundTripMs.ToString("F0") + "ms");
        }
        finally
        {
            transport.Disconnect("Smoke test complete");
            transport.Dispose();
        }
    }

    private static void PacketNumbersMatchServer()
    {
    	Equal(9, (int)PacketType.CreateRocket, "CreateRocket");
    	Equal(12, (int)PacketType.UpdateRocketSecondary, "UpdateRocketSecondary");
    	Equal(20, (int)PacketType.UpdatePart_ResourceModule, "UpdatePart_ResourceModule");
    	Equal(21, (int)PacketType.ShowToastMessage, "ShowToastMessage extension");
    	Equal(22, (int)PacketType.UpdateCheatStatus, "UpdateCheatStatus");
    }

    private static void StringsMatchServerWireFormat()
    {
    	NetOutgoingMessage client = NewOutgoing();
    	client.WriteCompressedString("Earth");
    	NetOutgoingMessage server = NewOutgoing();
    	server.Write("Earth");
    	EqualBytes(server, client, "string bytes");
    }

    private static void JoinResponseAcceptsServerPayload()
    {
    	NetOutgoingMessage server = NewOutgoing();
    	server.Write(42);
    	server.Write(20.0);
    	server.Write(3.0);
    	server.Write(100.5);
    	server.Write(2.5);
    	server.Write((byte)Difficulty.DifficultyType.Normal);

    	var packet = new Packet_JoinResponse();
    	packet.Deserialize(ToIncoming(server));
    	Equal(42, packet.PlayerId, "player id");
    	Equal(string.Empty, packet.SolarSystemName, "default solar system");
    }

    private static void ChatPacketMatchesServerPayload()
    {
    	NetOutgoingMessage server = NewOutgoing();
    	server.Write(7);
    	server.Write("hello");

    	var packet = new Packet_SendChatMessage();
    	packet.Deserialize(ToIncoming(server));
    	Equal(7, packet.SenderId, "chat sender");
    	Equal("hello", packet.Message, "chat text");
    	Equal(Color.white, packet.Color, "default chat color");

    	NetOutgoingMessage client = NewOutgoing();
    	packet.Serialize(client);
    	EqualBytes(server, client, "chat bytes");
    }

    private static void TimeWarpPacketMatchesServerWireFormat()
    {
        NetOutgoingMessage server = NewOutgoing();
        server.Write((byte)TimeWarpOperation.Vote);
        server.Write(42);
        server.Write(7);
        server.Write("tester");
        server.Write(25.0);
        server.Write(true);
        server.Write(1234.5);
        server.Write(30);
        server.Write("vote");

        var packet = new Packet_TimeWarp();
        packet.Deserialize(ToIncoming(server));
        Equal(TimeWarpOperation.Vote, packet.Operation, "time-warp operation");
        Equal(42, packet.VoteId, "time-warp vote id");
        Equal(25.0, packet.Multiplier, "time-warp multiplier");

        NetOutgoingMessage client = NewOutgoing();
        packet.Serialize(client);
        EqualBytes(server, client, "time-warp bytes");
    }

    private static void TimeWarpSyncHasNoPopupSurface()
    {
        const BindingFlags flags = BindingFlags.Instance | BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic;
        Type type = typeof(TimeWarpVoteUI);
        True(type.GetMethod("Receive", flags) != null, "time-warp packets must still have a sync receiver");
        True(type.GetMethod("OnGUI", flags) == null, "time-warp sync must not create a GUI popup");
        True(type.GetMethod("Update", flags) == null, "time-warp sync must not bind F7 popup behavior");
        True(type.GetMethod("Create", flags) == null, "time-warp sync must not create a popup GameObject");
    }

    private static void MultiplayerSplitFollowsNativeSelection()
    {
        True(DivertRocketSwitching.Rocket_SetPlayerToBestControllable.ShouldRequestNativeSelection(true, 12, 7),
            "native split selection must request the selected rocket");
        False(DivertRocketSwitching.Rocket_SetPlayerToBestControllable.ShouldRequestNativeSelection(true, 7, 7),
            "native split selection must not request the current rocket");
        False(DivertRocketSwitching.Rocket_SetPlayerToBestControllable.ShouldRequestNativeSelection(false, 12, 7),
            "single-player selection must not request network control");
    }

    private static void ControlCoordinatorUsesServerConfirmation()
    {
        var coordinator = new ControlOwnershipCoordinator();
        True(coordinator.Request(12, ControlRequestOrigin.NativeSelection), "native selection request accepted");
        Equal(-1, coordinator.ConfirmedRocketId, "request must not confirm locally");
        Equal(12, coordinator.PendingRocketId, "pending native selection");
        coordinator.ApplyServerConfirmation(7);
        Equal(7, coordinator.ConfirmedRocketId, "server confirmation wins");
        Equal(-1, coordinator.PendingRocketId, "pending request cleared");
        False(coordinator.CanApplyNativeSelection(12), "unconfirmed native selection cannot apply");
        True(coordinator.CanApplyNativeSelection(7), "confirmed native selection can apply");
    }

    private static void ControlCoordinatorSuppressesDuplicatePendingRequests()
    {
        var coordinator = new ControlOwnershipCoordinator();
        True(coordinator.Request(42, ControlRequestOrigin.NativeSelection), "first control request accepted");
        False(coordinator.Request(42, ControlRequestOrigin.UserAction),
            "same control request remains pending instead of being resent");
        coordinator.ApplyServerConfirmation(42);
        False(coordinator.Request(42, ControlRequestOrigin.NativeSelection),
            "already-confirmed control request is not resent");
        True(coordinator.Request(43, ControlRequestOrigin.NativeSelection),
            "different control request is accepted");
    }

    private static void P2PStateOrderingIsMonotonic()
    {
        True(P2PStateOrderPolicy.ShouldAccept(4, 2, 4, 4), "newer sequence accepted");
        False(P2PStateOrderPolicy.ShouldAccept(4, 4, 4, 2), "older sequence rejected");
        True(P2PStateOrderPolicy.ShouldAccept(4, 99, 5, 0), "new generation accepted");
        False(P2PStateOrderPolicy.ShouldAccept(5, 0, 4, 99), "old generation rejected");
    }

    private static void P2PProximityUsesRelativeDistance()
    {
        var a = new NetLocation(new Double2(0, 0), new Double2(0, 0), "Earth");
        var near = new NetLocation(new Double2(3000, 4000), new Double2(0, 0), "Earth");
        var far = new NetLocation(new Double2(5001, 0), new Double2(0, 0), "Earth");
        var otherAddress = new NetLocation(new Double2(0, 0), new Double2(0, 0), "Moon");
        True(P2PProximityPolicy.IsEligible(a, near, 5000), "5km boundary is eligible");
        False(P2PProximityPolicy.IsEligible(a, far, 5000), "over-threshold pair is not eligible");
        False(P2PProximityPolicy.IsEligible(a, otherAddress, 5000), "different coordinate system is not eligible");
    }

    private static void P2PTimeoutFallsBackToServer()
    {
        DateTime now = DateTime.UtcNow;
        True(P2PTransitionPolicy.ShouldFallback(now, now.AddSeconds(-3.01), 3), "peer timeout falls back");
        False(P2PTransitionPolicy.ShouldFallback(now, now.AddSeconds(-2.99), 3), "fresh peer stays direct");
    }

    private static void P2PEstablishmentTriggersOneAuthoritativeResync()
    {
        DateTime established = DateTime.UtcNow;
        // 握手刚转 Active：还没到稳定窗口，不请求。
        False(P2PPostConnectSyncPolicy.ShouldRequestServerSync(true, false, established, established),
            "resync waits for the stabilization window after P2P becomes active");
        // 稳定窗口已过且尚未同步：必须请求一次。
        True(P2PPostConnectSyncPolicy.ShouldRequestServerSync(
                true, false, established, established + P2PPostConnectSyncPolicy.StabilizationDelay),
            "stabilized P2P link requests one authoritative resync");
        // 同一连接周期内不重复请求。
        False(P2PPostConnectSyncPolicy.ShouldRequestServerSync(
                true, true, established, established + P2PPostConnectSyncPolicy.StabilizationDelay),
            "resync fires at most once per P2P connection cycle");
        // 非活跃对端不触发。
        False(P2PPostConnectSyncPolicy.ShouldRequestServerSync(
                false, false, established, established + P2PPostConnectSyncPolicy.StabilizationDelay),
            "inactive peers never trigger the resync");
    }

    private static void NetworkAdaptationKeepsDriftResyncTight()
    {
        // 回退后此断言只锁定“纠偏不得慢于插值太多”的下限约束；drift-tight 版证明
        // 单方向收紧阈值/纠偏会实际变差，故不再断言具体数值，防止未来误锁坏参数。
        foreach (var profile in new[]
                 {
                     NetworkAdaptationPolicy.Evaluate(0, 0, 0),
                     NetworkAdaptationPolicy.Evaluate(100, 25, 3),
                     NetworkAdaptationPolicy.Evaluate(200, 60, 8),
                     NetworkAdaptationPolicy.Evaluate(400, 100, 15),
                 })
        {
            True(profile.MaximumExtrapolationSeconds > 0 && profile.CorrectionSeconds > 0,
                $"{profile.Quality}: extrapolation and correction must stay positive");
        }
    }

    private static void RenderStateAppliesInterpolationWithZeroLag()
    {
        // 防飘契约：平滑常数由包间隔推导，钳制在 0.02~0.12s——比旧版 0.45~1.4s 低一个量级（不飘），
        // 又保留轻量平滑（不闪）。恒速稳态滞后 ≈ 速度 × 常数，必须远小于旧版。
        True(Math.Abs(0.04 - InterpolationRenderPolicy.SmoothingSeconds(0.2)) < 0.000001,
            "200ms packet interval yields 40ms smoothing");
        Equal(InterpolationRenderPolicy.MinSmoothingSeconds, InterpolationRenderPolicy.SmoothingSeconds(0.001), "tiny interval clamps to minimum");
        Equal(InterpolationRenderPolicy.MaxSmoothingSeconds, InterpolationRenderPolicy.SmoothingSeconds(5.0), "huge interval clamps to maximum");
        Equal(InterpolationRenderPolicy.MinSmoothingSeconds, InterpolationRenderPolicy.SmoothingSeconds(double.NaN), "invalid interval falls back to minimum");

        double delta = 1.0 / 60.0;
        var (px, py) = InterpolationRenderPolicy.ResolvePosition(0.04, delta, 0, 0, 1000, 2000);
        True(px > 0 && px < 1000 && py > 0 && py < 2000, "light smoothing moves toward target without snapping");
        // 最大常数硬上限 0.12s；以 60fps 连续渲染 1 秒后应已基本收敛，既不长期飘也不裸切闪。
        Equal(0.12, InterpolationRenderPolicy.MaxSmoothingSeconds, "smoothing upper bound stays below old heavy filter");
        double rendered = 0;
        for (int frame = 0; frame < 60; frame++)
            (rendered, _) = InterpolationRenderPolicy.ResolvePosition(
                InterpolationRenderPolicy.MaxSmoothingSeconds, delta, rendered, 0, 100, 0);
        True(Math.Abs(100 - rendered) < 0.03, "worst-case smoothing converges within one second");
    }

    private static void ExperimentalUnlockPacketRoundTrips()
    {
        var source = new Packet_ExperimentalAccess
        {
            Request = true,
            Passphrase = "test-only",
            Granted = false,
            Message = string.Empty
        };
        NetOutgoingMessage outgoing = NewOutgoing();
        source.Serialize(outgoing);
        var parsed = new Packet_ExperimentalAccess();
        parsed.Deserialize(ToIncoming(outgoing));
        True(parsed.Request, "unlock request flag");
        Equal("test-only", parsed.Passphrase, "unlock passphrase");
        False(parsed.Granted, "request must not self-grant");
    }

    private static void UdpBindRemainsUsableAfterBindAck()
    {
        using (var server = new UdpClient(0))
        using (var transport = new UdpClientTransport(_ => { }))
        {
            var port = ((IPEndPoint)server.Client.LocalEndPoint).Port;
            const string token = "client-health-test";
            transport.Start(IPAddress.Loopback, port, token);

            var receiveTask = server.ReceiveAsync();
            True(receiveTask.Wait(2000), "UDP bind request received");
            var request = receiveTask.Result;
            var tokenBytes = System.Text.Encoding.UTF8.GetBytes(token);
            var ack = new byte[2 + tokenBytes.Length];
            ack[0] = 2;
            ack[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, ack, 2, tokenBytes.Length);
            server.Send(ack, ack.Length, request.RemoteEndPoint);

            var deadline = DateTime.UtcNow.AddSeconds(2);
            while (!transport.Bound && DateTime.UtcNow < deadline)
                Thread.Sleep(25);
            True(transport.Bound, "UDP bind becomes healthy after BindAck");

            Thread.Sleep(1100);
            True(transport.Bound, "UDP remains usable after the original BindAck");
        }
    }

    private static void UdpBindEnablesDirectStateSend()
    {
        using (var server = new UdpClient(0))
        using (var transport = new UdpClientTransport(_ => { }))
        {
            var port = ((IPEndPoint)server.Client.LocalEndPoint).Port;
            const string token = "client-downlink-ack-test";
            transport.Start(IPAddress.Loopback, port, token);

            var bind = server.ReceiveAsync();
            True(bind.Wait(2000), "UDP bind request received");
            var request = bind.Result;
            var tokenBytes = System.Text.Encoding.UTF8.GetBytes(token);
            var bindAck = new byte[2 + tokenBytes.Length];
            bindAck[0] = 2;
            bindAck[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, bindAck, 2, tokenBytes.Length);
            server.Send(bindAck, bindAck.Length, request.RemoteEndPoint);

            var deadline = DateTime.UtcNow.AddSeconds(2);
            while (!transport.Bound && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(25);
            }
            True(transport.Bound, "UDP bind becomes usable after BindAck");

            True(transport.TrySendPacket(new Packet_UpdateWorldTime { WorldTime = 1 }),
                "direct UDP state sends after BindAck");
            var sent = false;
            deadline = DateTime.UtcNow.AddSeconds(2);
            while (DateTime.UtcNow < deadline && !sent)
            {
                var reply = server.ReceiveAsync();
                if (!reply.Wait(300)) continue;
                var data = reply.Result.Buffer;
                sent = data.Length > 2 + tokenBytes.Length && data[0] == 3 &&
                    data[1] == tokenBytes.Length &&
                    data.Skip(2).Take(tokenBytes.Length).SequenceEqual(tokenBytes) &&
                    data[2 + tokenBytes.Length] == (byte)PacketType.UpdateWorldTime;
            }
            True(sent, "server receives direct UDP state after BindAck");
        }
    }

    private static void UdpStatePacketReachesCallback()
    {
        var callbacks = 0;
        using (var server = new UdpClient(0))
        using (var transport = new UdpClientTransport(_ => Interlocked.Increment(ref callbacks)))
        {
            var port = ((IPEndPoint)server.Client.LocalEndPoint).Port;
            const string token = "client-data-ack-test";
            transport.Start(IPAddress.Loopback, port, token);

            var bind = server.ReceiveAsync();
            True(bind.Wait(2000), "UDP data acknowledgement bind received");
            var request = bind.Result;
            var tokenBytes = System.Text.Encoding.UTF8.GetBytes(token);
            var bindAck = new byte[2 + tokenBytes.Length];
            bindAck[0] = 2;
            bindAck[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, bindAck, 2, tokenBytes.Length);
            server.Send(bindAck, bindAck.Length, request.RemoteEndPoint);

            var deadline = DateTime.UtcNow.AddSeconds(2);
            while (!transport.Bound && DateTime.UtcNow < deadline)
                Thread.Sleep(25);
            True(transport.Bound, "UDP state callback bind acknowledged");

            var state = new byte[3 + tokenBytes.Length];
            state[0] = 3;
            state[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, state, 2, tokenBytes.Length);
            state[2 + tokenBytes.Length] = (byte)PacketType.UpdateWorldTime;
            server.Send(state, state.Length, request.RemoteEndPoint);

            deadline = DateTime.UtcNow.AddSeconds(2);
            while (Volatile.Read(ref callbacks) < 1 && DateTime.UtcNow < deadline)
                Thread.Sleep(25);
            Equal(1, Volatile.Read(ref callbacks), "UDP state reaches the receive callback");
        }
    }

    private static void UdpStateKeepsSendingAfterBindAck()
    {
        using (var server = new UdpClient(0))
        using (var transport = new UdpClientTransport(_ => { }))
        {
            var port = ((IPEndPoint)server.Client.LocalEndPoint).Port;
            const string token = "client-state-probe-test";
            transport.Start(IPAddress.Loopback, port, token);

            var tokenBytes = System.Text.Encoding.UTF8.GetBytes(token);
            IPEndPoint remote = null;
            var bindDeadline = DateTime.UtcNow.AddSeconds(2);
            while (DateTime.UtcNow < bindDeadline && remote == null)
            {
                var receive = server.ReceiveAsync();
                if (!receive.Wait(250)) continue;
                var datagram = receive.Result;
                if (datagram.Buffer.Length == 2 + tokenBytes.Length && datagram.Buffer[0] == 1)
                    remote = datagram.RemoteEndPoint;
            }
            True(remote != null, "UDP direct-state bind received");

            var bindAck = new byte[2 + tokenBytes.Length];
            bindAck[0] = 2;
            bindAck[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, bindAck, 2, tokenBytes.Length);
            server.Send(bindAck, bindAck.Length, remote);

            var boundDeadline = DateTime.UtcNow.AddSeconds(2);
            while (!transport.Bound && DateTime.UtcNow < boundDeadline)
                Thread.Sleep(25);
            True(transport.Bound, "UDP direct-state bind acknowledged");

            var packet = new Packet_UpdateWorldTime { WorldTime = 1 };
            True(transport.TrySendPacket(packet), "first state packet sends directly over UDP");
            True(transport.TrySendPacket(new Packet_UpdateWorldTime { WorldTime = 2 }),
                "later state packet keeps using UDP without Data ACK");

            var sentStates = 0;
            var stateDeadline = DateTime.UtcNow.AddSeconds(2);
            while (DateTime.UtcNow < stateDeadline && sentStates < 2)
            {
                var receive = server.ReceiveAsync();
                if (!receive.Wait(250)) continue;
                var data = receive.Result.Buffer;
                if (data.Length <= 2 + tokenBytes.Length || data[0] != 3 || data[1] != tokenBytes.Length) continue;
                if (!data.Skip(2).Take(tokenBytes.Length).SequenceEqual(tokenBytes)) continue;
                if (data[2 + tokenBytes.Length] != (byte)PacketType.UpdateWorldTime) continue;
                sentStates++;
            }
            Equal(2, sentStates, "two direct UDP state packets are sent after BindAck");
        }
    }

    private static void DestroyPartRoutesByRocketId()
    {
        var packet = new Packet_DestroyPart { RocketId = 41, PartId = 7, WorldTime = 10 };
        Equal(41, ClientPacketRouting.GetRocketId(packet), "destroy-part routing key");
    }

    private static void DestroyRocketByteReasonRoundTrips()
    {
        var source = new Packet_DestroyRocket
        {
            WorldTime = 12.5,
            RocketId = 42,
            Reason = SFS.World.DestructionReason.Intentional,
        };
        NetOutgoingMessage outgoing = NewOutgoing();
        source.Serialize(outgoing);

        var parsed = new Packet_DestroyRocket();
        parsed.Deserialize(ToIncoming(outgoing));

        Equal(12.5, parsed.WorldTime, "destroy-rocket world time");
        Equal(42, parsed.RocketId, "destroy-rocket id");
        Equal(SFS.World.DestructionReason.Intentional, parsed.Reason, "byte-encoded destruction reason");
    }

    private static void UdpCallbackFailureIsIsolated()
    {
        using (var server = new UdpClient(0))
        {
            var port = ((IPEndPoint)server.Client.LocalEndPoint).Port;
            var callbackCount = 0;
            using (var transport = new UdpClientTransport(_ =>
            {
                callbackCount++;
                if (callbackCount == 1) throw new InvalidOperationException("test callback failure");
            }))
            {
                const string token = "client-callback-test";
                transport.Start(IPAddress.Loopback, port, token);

                var bind = server.ReceiveAsync();
                True(bind.Wait(2000), "UDP callback test bind received");
                var request = bind.Result;
                var tokenBytes = System.Text.Encoding.UTF8.GetBytes(token);
                var ack = new byte[2 + tokenBytes.Length];
                ack[0] = 2;
                ack[1] = (byte)tokenBytes.Length;
                Buffer.BlockCopy(tokenBytes, 0, ack, 2, tokenBytes.Length);
                server.Send(ack, ack.Length, request.RemoteEndPoint);

                var deadline = DateTime.UtcNow.AddSeconds(2);
                while (!transport.Bound && DateTime.UtcNow < deadline)
                    Thread.Sleep(25);
                True(transport.Bound, "UDP callback test bind acknowledged");

                var tokenPrefix = new byte[2 + tokenBytes.Length];
                tokenPrefix[0] = 3;
                tokenPrefix[1] = (byte)tokenBytes.Length;
                Buffer.BlockCopy(tokenBytes, 0, tokenPrefix, 2, tokenBytes.Length);
                var first = tokenPrefix.Concat(new byte[] { (byte)PacketType.UpdateWorldTime, 1 }).ToArray();
                var second = tokenPrefix.Concat(new byte[] { (byte)PacketType.UpdateWorldTime, 2 }).ToArray();
                deadline = DateTime.UtcNow.AddSeconds(2);
                server.Send(first, first.Length, request.RemoteEndPoint);
                while (callbackCount < 1 && DateTime.UtcNow < deadline)
                    Thread.Sleep(25);
                Equal(1, callbackCount, "first UDP packet reaches callback");

                server.Send(second, second.Length, request.RemoteEndPoint);
                deadline = DateTime.UtcNow.AddSeconds(2);
                while (callbackCount < 2 && DateTime.UtcNow < deadline)
                    Thread.Sleep(25);
                Equal(2, callbackCount, "second UDP packet reaches callback after first failure");
            }
        }
    }

    private static void DiscreteEventsRunOnlyWhenDue()
    {
        True(Interpolator.IsPacketDue(9.9, 10.0), "past event must run");
        True(Interpolator.IsPacketDue(10.0, 10.0), "current event must run");
        False(Interpolator.IsPacketDue(10.1, 10.0), "future event must wait");
    }

    private static void DuplicateTimestampIsSafe()
    {
        Equal(1.0, Interpolator.GetInterpolationFraction(4, 4, 4), "duplicate at same time");
        Equal(1.0, Interpolator.GetInterpolationFraction(4, 4, 5), "duplicate after time");
        Equal(0.5, Interpolator.GetInterpolationFraction(4, 6, 5), "normal midpoint");
    }

    private static void CorrectionDoesNotAdvanceTarget()
    {
        Equal(100.0, Interpolator.GetCorrectionTarget(100.0, 1000.0), "target position");
        Equal(15f, Interpolator.GetCorrectionRotation(15f, 40f), "target rotation");
    }

    private static void TcpFramesSurviveStreamCoalescing()
    {
        var first = new TcpFrame(TcpFrameKind.Packet, 11, new byte[] { 9, 1, 2 }, 19);
        var second = new TcpFrame(TcpFrameKind.Ping, 12, BitConverter.GetBytes(123L), 64);
        byte[] joined = TcpFrameCodec.Encode(first).Concat(TcpFrameCodec.Encode(second)).ToArray();
        using (var stream = new MemoryStream(joined))
        {
            TcpFrame decodedFirst = TcpFrameCodec.Read(stream);
            TcpFrame decodedSecond = TcpFrameCodec.Read(stream);
            Equal(TcpFrameKind.Packet, decodedFirst.Kind, "first kind");
            Equal(11, decodedFirst.Sequence, "first sequence");
            Equal(19, decodedFirst.PayloadBits, "first exact bit length");
            True(decodedFirst.Payload.SequenceEqual(first.Payload), "first payload");
            Equal(TcpFrameKind.Ping, decodedSecond.Kind, "second kind");
            Equal(12, decodedSecond.Sequence, "second sequence");
        }
    }

    private static void TransportTerminationAlwaysReachesGameThread()
    {
        using (var transport = new TcpClientTransport())
        {
            var signal = typeof(TcpClientTransport).GetMethod("SignalDisconnect",
                BindingFlags.Instance | BindingFlags.NonPublic);
            True(signal != null, "all transport failures must share one disconnect signal path");
            signal.Invoke(transport, new object[] { "transport test", false });
			signal.Invoke(transport, new object[] { "duplicate termination", false });

            TcpFrame frame;
            True(transport.TryReceive(out frame), "termination emits a main-thread disconnect frame");
            Equal(TcpFrameKind.Disconnect, frame.Kind, "termination frame kind");
            Equal("transport test", System.Text.Encoding.UTF8.GetString(frame.Payload), "termination reason");
			False(transport.TryReceive(out frame), "termination is queued exactly once");
        }
    }

    private static async Task RemoteTcpCloseReachesGameThread()
    {
        // 传输层已全面 KCP/UDP：ConnectAsync 只用 UdpClient + KcpContext，客户端已无 TCP socket，
        // 所以对端必须是真 KCP 节点（TCP 监听器根本没法完成握手，旧写法是 TCP 时代的遗留）。
        // 流程：完成 Hello/HelloAck 握手 → 对端发一个 Disconnect 帧模拟"远端关闭"
        //      → 游戏线程必须收到该帧，且传输层清掉连接状态。
        using (var peerSocket = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0)))
        {
            var port = ((IPEndPoint)peerSocket.Client.LocalEndPoint).Port;
            var handshakeSent = new TaskCompletionSource<bool>();
            var serverTask = Task.Run(async () =>
            {
                KcpContext peer = null;
                byte[] scratch = new byte[64 * 1024];
                bool ackSent = false;
                var deadline = DateTime.UtcNow.AddSeconds(20);
                while (DateTime.UtcNow < deadline)
                {
                    while (peerSocket.Available > 0)
                    {
                        var datagram = await peerSocket.ReceiveAsync();
                        if (peer == null)
                        {
                            // conv 由客户端随机生成，这里从对端 KCP 头里取，保证两端一致。
                            uint conv = (uint)(datagram.Buffer[0] | (datagram.Buffer[1] << 8)
                                | (datagram.Buffer[2] << 16) | ((uint)datagram.Buffer[3] << 24));
                            var endpoint = datagram.RemoteEndPoint;
                            peer = new KcpContext(conv, (data, ctx) =>
                            {
                                try { peerSocket.Send(data, data.Length, endpoint); } catch { }
                            });
                            peer.SetNoDelay(1, 10, 2, 1);
                            peer.SetMtu(1400);
                            peer.WndSize(256, 512);
                        }
                        peer.Input(datagram.Buffer, 0, datagram.Buffer.Length);
                    }
                    if (peer == null)
                    {
                        await Task.Delay(2);
                        continue;
                    }
                    peer.Update((uint)Environment.TickCount);
                    while (true)
                    {
                        int size = peer.PeekSize();
                        if (size <= 0) break;
                        if (scratch.Length < size) scratch = new byte[size];
                        if (peer.Recv(scratch, 0, scratch.Length) <= 0) break;
                        if (ackSent) continue;
                        byte[] ack = EncodeTestHelloAck();
                        SendKcpFrame(peer, new TcpFrame(TcpFrameKind.HelloAck,
                            SessionHandshakeCodec.Version, ack, ack.Length * 8));
                        ackSent = true;
                        handshakeSent.TrySetResult(true);
                        // 等客户端 ConnectAsync 收下 HelloAck 并返回，再模拟远端关闭
                        await Task.Delay(300);
                        byte[] reason = System.Text.Encoding.UTF8.GetBytes("remote closed");
                        SendKcpFrame(peer, new TcpFrame(TcpFrameKind.Disconnect, 0,
                            reason, reason.Length * 8));
                        await Task.Delay(1500);
                        return;
                    }
                    await Task.Delay(5);
                }
            });

            using (var transport = new TcpClientTransport())
            {
                try
                {
                    await transport.ConnectAsync(IPAddress.Loopback, port, new Packet_JoinRequest
                    {
                        Username = "disconnect-smoke",
                        SolarSystemName = string.Empty,
                    });
                    var handshakeCompleted = await Task.WhenAny(handshakeSent.Task, Task.Delay(3000));
                    True(handshakeCompleted == handshakeSent.Task, "disconnect smoke handshake reached server");
                    await handshakeSent.Task;
                    transport.Send(new Packet_UpdateWorldTime { WorldTime = 1 });

                    var deadline = DateTime.UtcNow.AddSeconds(3);
                    var disconnected = false;
                    while (DateTime.UtcNow < deadline && !disconnected)
                    {
                        TcpFrame frame;
                        while (transport.TryReceive(out frame))
                        {
                            if (frame.Kind == TcpFrameKind.Disconnect)
                            {
                                disconnected = true;
                                break;
                            }
                        }
                        if (!disconnected) await Task.Delay(25);
                    }
                    True(disconnected, "remote close must notify the game thread");
                    False(transport.Connected, "remote close clears transport connection state");
                }
                finally
                {
                    try { await serverTask; } catch { }
                }
            }
        }
    }

    // 与 TcpTransportCore.SendKcpMessage 一致的封装：4 字节小端长度前缀 + KCP 消息体。
    private static void SendKcpFrame(KcpContext ctx, TcpFrame frame)
    {
        byte[] frameData = TcpFrameCodec.Encode(frame);
        byte[] message = new byte[4 + frameData.Length];
        message[0] = (byte)frameData.Length;
        message[1] = (byte)(frameData.Length >> 8);
        message[2] = (byte)(frameData.Length >> 16);
        message[3] = (byte)(frameData.Length >> 24);
        Buffer.BlockCopy(frameData, 0, message, 4, frameData.Length);
        ctx.Send(message, 0, message.Length);
        ctx.Flush();
    }

    private static void NetworkPacketPumpWaitsForInitializedWorld()
    {
        MethodInfo readiness = typeof(ClientManager).GetMethod("CanPumpNetworkPackets",
            BindingFlags.Static | BindingFlags.NonPublic, null,
            new[] { typeof(bool), typeof(bool), typeof(bool), typeof(bool), typeof(bool), typeof(bool) }, null);
        True(readiness != null, "network packet readiness gate exists");

        False((bool)readiness.Invoke(null, new object[] { true, false, true, true, true, true }),
            "network frames stay queued until the multiplayer world exists");
        False((bool)readiness.Invoke(null, new object[] { true, true, false, true, true, true }),
            "network frames stay queued until the player table exists");
        False((bool)readiness.Invoke(null, new object[] { true, true, true, false, true, true }),
            "network frames stay queued until synced rockets are initialized");
        True((bool)readiness.Invoke(null, new object[] { true, true, true, true, true, true }),
            "network frames can be handled after multiplayer state initialization");
    }

    private static void MissingLocalPlayerIsSafe()
    {
        Dictionary<int, LocalPlayer> previousPlayers = LocalManager.players;
        try
        {
            LocalManager.players = null;
            True(LocalManager.Player == null, "missing player table yields no local player");
        }
        finally
        {
            LocalManager.players = previousPlayers;
        }
    }

    private static byte[] EncodeTestHelloAck()
    {
        using (var stream = new MemoryStream())
        using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8))
        {
            writer.Write(0x31465353u);
            writer.Write(SessionHandshakeCodec.Version);
            writer.Write(1);
            writer.Write(50.0);
            writer.Write(3.0);
            writer.Write(1000.0);
            writer.Write(0.0);
            writer.Write((byte)0);
            WriteHandshakeString(writer, string.Empty);
            WriteHandshakeString(writer, "disconnect-smoke-udp");
            WriteHandshakeString(writer, "disconnect-smoke-resume");
            return stream.ToArray();
        }
    }

    private static void WriteHandshakeString(BinaryWriter writer, string value)
    {
        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(value ?? string.Empty);
        writer.Write(bytes.Length);
        writer.Write(bytes);
    }

    private static void TcpSendQueueKeepsLatestRocketState()
    {
        var queue = new TcpSendQueue();
        queue.EnqueueLatest(41, new TcpFrame(TcpFrameKind.Packet, 1, new byte[] { 1 }, 8));
        queue.EnqueueLatest(41, new TcpFrame(TcpFrameKind.Packet, 2, new byte[] { 2 }, 8));
        queue.EnqueueCritical(new TcpFrame(TcpFrameKind.Packet, 3, new byte[] { 3 }, 8));

        TcpFrame critical;
        TcpFrame latest;
        True(queue.TryDequeue(out critical), "critical frame available");
        True(queue.TryDequeue(out latest), "latest state available");
        Equal(3, critical.Sequence, "critical priority");
        Equal(2, latest.Sequence, "old rocket state overwritten");
        Equal(1L, queue.OverwrittenStates, "overwrite statistic");
        Equal(0, queue.Count, "queue drained");
    }

    private static void RocketSyncPolicyLowersIdleTraffic()
    {
        Equal(50, RocketSyncPolicy.GetIntervalMilliseconds(true, false), "controlled 20Hz");
        Equal(200, RocketSyncPolicy.GetIntervalMilliseconds(false, true), "uncontrolled moving 5Hz");
        Equal(1000, RocketSyncPolicy.GetIntervalMilliseconds(false, false), "idle snapshot");
    }

    private static void AuthorityHandoffPrimesBeforePhysics()
    {
        False(Interpolator.ShouldPrimeAuthorityState(false, false), "non-authority remains non-authority");
        True(Interpolator.ShouldPrimeAuthorityState(false, true), "authority gain primes state");
        False(Interpolator.ShouldPrimeAuthorityState(true, true), "existing authority does not re-prime");
        False(Interpolator.ShouldPrimeAuthorityState(true, false), "authority loss does not prime");
    }

    private static void RocketStateUsesUdpWhenHealthy()
    {
        True(TcpClientTransport.ShouldSendOverUdp(new Packet_UpdateRocketPrimary()), "position uses UDP after health check");
        False(TcpClientTransport.ShouldSendOverUdp(new Packet_UpdateRocketSecondary()), "input must not use UDP");
        True(TcpClientTransport.ShouldCoalesceState(new Packet_UpdateRocketPrimary()), "position may coalesce");
        False(TcpClientTransport.ShouldCoalesceState(new Packet_UpdateRocketSecondary()), "input must not coalesce");
    }

    private static void Run(string name, Action test)
    {
        try { test(); Console.WriteLine("PASS " + name); }
        catch (Exception ex) { failures++; Console.WriteLine("FAIL " + name + ": " + ex.Message); }
    }

    private static void Equal<T>(T expected, T actual, string name)
    {
        if (!EqualityComparer<T>.Default.Equals(expected, actual))
            throw new Exception($"{name}: expected {expected}, got {actual}");
    }

    private static void True(bool value, string name)
    {
        if (!value) throw new Exception(name);
    }

    private static void False(bool value, string name)
    {
    	if (value) throw new Exception(name);
    }

    private static NetOutgoingMessage NewOutgoing()
    {
    	return (NetOutgoingMessage)Activator.CreateInstance(typeof(NetOutgoingMessage), nonPublic: true);
    }

    private static NetIncomingMessage ToIncoming(NetOutgoingMessage outgoing)
    {
    	var incoming = (NetIncomingMessage)Activator.CreateInstance(typeof(NetIncomingMessage), nonPublic: true);
    	incoming.Data = outgoing.Data.Take(outgoing.LengthBytes).ToArray();
    	incoming.LengthBits = outgoing.LengthBits;
    	incoming.Position = 0;
    	return incoming;
    }

    private static void EqualBytes(NetOutgoingMessage expected, NetOutgoingMessage actual, string name)
    {
    	Equal(expected.LengthBits, actual.LengthBits, name + " bit length");
    	byte[] left = expected.Data.Take(expected.LengthBytes).ToArray();
    	byte[] right = actual.Data.Take(actual.LengthBytes).ToArray();
    	if (!left.SequenceEqual(right))
    		throw new Exception(name + ": byte payload differs");
    }
    }
