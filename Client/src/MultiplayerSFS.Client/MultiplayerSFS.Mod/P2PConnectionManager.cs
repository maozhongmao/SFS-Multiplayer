// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.IO;
using System.Linq;
using System.Threading;
using MultiplayerSFS.Common;
using MultiplayerSFS.Common.Transport.Kcp;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public static class P2PConnectionManager
{
    private static readonly Dictionary<int, Peer> peers = new Dictionary<int, Peer>();

    public static string Status { get; private set; } = "Relay Fallback";
    public static int ActivePeerCount { get; private set; }

    // 【诊断分细】以前 F8 只有一句 "Relay Fallback"，四种失败原因（服务器撤销 / 握手超时 /
    // 过渡窗口到期 / 链路丢失）全挤成同一句话，分不清卡在哪一步。这里把被拒/被撤销的原因也留下。
    public static string LastOfferRejectInfo { get; private set; } = string.Empty;
    public static string LastRevokedInfo { get; private set; } = string.Empty;

    public static void ApplyOffer(Packet_P2PPeerOffer offer)
    {
        if (offer == null || offer.PeerPlayerId < 0) return;
        if (!offer.Active)
        {
            if (peers.TryGetValue(offer.PeerPlayerId, out var oldPeer))
            {
                LastRevokedInfo = "peer " + offer.PeerPlayerId + " revoked by server";
                oldPeer.Dispose();
            }
            else
            {
                LastRevokedInfo = "revoke for unknown peer " + offer.PeerPlayerId;
            }
            peers.Remove(offer.PeerPlayerId);
            RefreshStatus();
            return;
        }
        if (!IPAddress.TryParse(offer.PeerAddress, out var address) || offer.PeerPort < 1 || offer.PeerPort > 65535 ||
            string.IsNullOrEmpty(offer.PairToken))
        {
            // 以前这里静默 return：服务器给的地址解析不了（比如发来的是域名而不是 IP）时，
            // 面板上只会永远显示 "Relay Fallback"，完全看不出是被这里丢掉的。
            LastOfferRejectInfo = "offer rejected: address=\"" + offer.PeerAddress + "\" port=" + offer.PeerPort
                + " token=" + (string.IsNullOrEmpty(offer.PairToken) ? "empty" : "present");
            DebugTrace.Note(LastOfferRejectInfo);
            return;
        }

        var endpoint = new IPEndPoint(address, offer.PeerPort);
        bool known = peers.TryGetValue(offer.PeerPlayerId, out var peer);
        bool sameToken = known && string.Equals(peer.Token, offer.PairToken, StringComparison.Ordinal);
        if (known && !sameToken)
        {
            // 新 token = 新配对周期：旧的 socket 作废（这是唯一该重建的情况）
            DebugTrace.Note("peer " + offer.PeerPlayerId + " new pair token -> socket rebuilt");
            peer.Dispose();
            known = false;
        }

        // 【死循环修复 2026-10-10】以前只要"对方端点变了"就整只重建 —— 而我们自己重建会换端口，
        // 对方看到又要重建，服务端再重发报价……双方每毫秒换一次门牌，一个探测都来不及交换。
        // 现在：对方端口变了**只改目标**，本端 socket 不动。
        if (known && !peer.Endpoint.Equals(endpoint))
        {
            DebugTrace.Note("peer " + offer.PeerPlayerId + " retarget " + peer.Endpoint + " -> " + endpoint);
            peer.Retarget(endpoint);
        }

        // 重绑只发生在"已经 Fallback 且距上次绑定够久"的时候 —— 这才是真正的动态调整（有冷却，不会抖）。
        bool rebindForRetry = known && peer.Fallback
            && (DateTime.UtcNow - peer.CreatedUtc).TotalSeconds >= 8.0;
        if (rebindForRetry)
        {
            DebugTrace.Note("peer " + offer.PeerPlayerId + " retry: rebinding local socket after fallback");
            peer.Dispose();
            known = false;
        }

        if (!known)
        {
            peer = new Peer(offer.PeerPlayerId, endpoint, offer.PairToken, Math.Max(0, offer.TransitionBufferSeconds));
            peers[offer.PeerPlayerId] = peer;
            if (LocalManager.players != null && LocalManager.players.TryGetValue(offer.PeerPlayerId, out var lp))
                peer.Username = lp.username;
            Status = "P2P Connecting";
            ToastHelper.ShowToast(Status);
            DebugTrace.Note("offer peer=" + offer.PeerPlayerId + " ep=" + endpoint + " transition=" + offer.TransitionBufferSeconds + "s");
        }
        peer.LocalRocketIds = new HashSet<int>(offer.LocalRocketIds ?? new List<int>());
        peer.PeerRocketIds = new HashSet<int>(offer.PeerRocketIds ?? new List<int>());
    }

    // 【P2P 承载数据 2026-10-10】原先这里恒 return false，接收侧也明文丢弃玩家火箭，
    // 两头焊死 -> P2P 完全空转（只握手不传数据）。
    // 现在：只把"配对时服务端点名的那些火箭"（LocalRocketIds）发给该 peer。
    // 双源冲突不再靠"禁用 P2P"解决，而靠接收侧按【来源】仲裁（见 ApplyState / HasFreshState）：
    // 中继那份的时间戳是服务端重盖的，和发送方时钟不是同一条轴，比时间戳必然来回翻。
    public static bool SendRocketState(Packet_UpdateRocketPrimary packet)
    {
        if (packet == null || peers.Count == 0) return false;
        bool sent = false;
        foreach (Peer peer in peers.Values)
        {
            if (!peer.Active || !peer.KcpInitialized || peer.KcpContext == null) continue;
            if (peer.LocalRocketIds == null || !peer.LocalRocketIds.Contains(packet.RocketId)) continue;
            try
            {
                NetPayload payload = NetPayloadCodec.Serialize(packet, includePacketType: false);
                // 帧内布局必须与接收侧 ApplyState 一致：[seq(4)][payloadCount(4)][bits(4)][payload...]
                byte[] blob = new byte[12 + payload.Data.Length];
                System.BitConverter.GetBytes(0).CopyTo(blob, 0);
                System.BitConverter.GetBytes(1).CopyTo(blob, 4);
                System.BitConverter.GetBytes(payload.BitLength).CopyTo(blob, 8);
                System.Buffer.BlockCopy(payload.Data, 0, blob, 12, payload.Data.Length);
                TcpFrame frame = new TcpFrame(TcpFrameKind.Packet, ++peer.NextStateSequence, blob, blob.Length * 8);
                peer.KcpContext.Send(TcpFrameCodec.Encode(frame));
                DebugTrace.Frame("p2p-out-state", 0, blob.Length);
                sent = true;
            }
            catch
            {
                // 单个 peer 发失败不影响其它 peer，也不该打断主循环
            }
        }
        return sent;
    }

    public static void Clear()
    {
        foreach (var peer in peers.Values)
        {
            peer.Dispose();
        }
        peers.Clear();
        p2pStateLastSeen.Clear();
        ActivePeerCount = 0;
        Status = "Relay Fallback";
    }

    public static void Update()
    {
        var now = DateTime.UtcNow;
        uint currentMs = (uint)(now.Ticks / TimeSpan.TicksPerMillisecond);

        foreach (var peer in peers.Values)
        {
            if (!peer.KcpInitialized && peer.Fallback == false)
            {
                peer.InitializeKcp();
            }

            if (peer.KcpInitialized)
            {
                peer.KcpContext.Update(currentMs);

                // 处理网络接收
                while (peer.Socket != null && peer.TryReceiveDatagram(out var datagram))
                {
                    peer.KcpContext.Input(datagram.Data, 0, datagram.Data.Length);
                }

                // 处理 KCP 接收队列
                while (peer.KcpContext.PeekSize() > 0)
                {
                    byte[] recvBuf = peer.RecvBuffer;
                    int len = peer.KcpContext.Recv(recvBuf, 0, recvBuf.Length);
                    if (len > 0)
                    {
                        HandleKcpMessage(peer, recvBuf, len);
                    }
                    else
                    {
                        break;
                    }
                }

                // 新连接的 LastProbeUtc 是 MinValue，因此首次会立即发送；后续按状态保活。
                if (!peer.Fallback && (now - peer.LastProbeUtc).TotalMilliseconds >= (peer.Active ? 1000 : 250))
                {
                    peer.LastProbeUtc = now;
                    SendProbe(peer);
                }
            }

            // 修复"双方互相看不到对方状态"（臭名昭著的老 bug）：P2P 握手成功不等于双方世界一致。
            // P2P 不承载玩家火箭状态（严格服务器单源），所以首次转 Active 且稳定窗口过后，
            // 必须向服务器请求一次权威世界快照，把双方已有火箭拉回服务器权威状态。
            // 每个连接周期只发一次（ServerSyncRequested 门控）；断线重连生成新 Peer 时会再次触发。
            if (peer.Active && now - peer.LastPacketUtc >= TimeSpan.FromSeconds(3))
            {
                peer.Active = false;
                peer.Fallback = true;
                peer.FallbackReason = "link lost: no packet for 3s";
                DebugTrace.Note("peer " + peer.PlayerId + " fallback: " + peer.FallbackReason);
            }

            // 握手超时：10 秒仍未 Active 则标记 Fallback
            if (!peer.Active && !peer.Fallback && now >= peer.HandshakeDeadlineUtc)
            {
                peer.Fallback = true;
                peer.FallbackReason = "handshake timeout: " + peer.ValidHandshakePackets + "/3 valid hellos in 10s";
                DebugTrace.Note("peer " + peer.PlayerId + " fallback: " + peer.FallbackReason + " probes=" + peer.ProbesSent + " rx=" + peer.PacketsReceived);
            }
            if (!peer.Active && !peer.Fallback && now - peer.CreatedUtc >= TimeSpan.FromSeconds(peer.TransitionBufferSeconds))
            {
                peer.Fallback = true;
                peer.FallbackReason = "transition window expired: " + peer.TransitionBufferSeconds + "s";
                DebugTrace.Note("peer " + peer.PlayerId + " fallback: " + peer.FallbackReason + " hs=" + peer.ValidHandshakePackets + "/3");
            }

            if (P2PPostConnectSyncPolicy.ShouldRequestServerSync(
                    peer.Active, peer.ServerSyncRequested, peer.ActiveSinceUtc, now))
            {
                peer.ServerSyncRequested = true;
                ClientManager.client?.RequestWorldSnapshot();
            }

            // 发送待发数据
            if (peer.KcpInitialized && peer.KcpContext.WaitSnd > 0)
            {
                peer.KcpContext.Flush();
            }
        }

        // 清理已落后的 peer
        var toRemove = new List<int>();
        foreach (var kv in peers)
        {
            if (kv.Value.Fallback && kv.Value.KcpInitialized && kv.Value.KcpContext.State == uint.MaxValue)
            {
                toRemove.Add(kv.Key);
            }
        }
        foreach (var id in toRemove)
        {
            if (peers.TryGetValue(id, out var p))
            {
                p.Dispose();
                peers.Remove(id);
            }
        }

        RefreshStatus();
    }

    private static void HandleKcpMessage(Peer peer, byte[] data, int length)
    {
        if (length <= 0) return;
        peer.LastPacketUtc = DateTime.UtcNow;

        TcpFrame frame;
        try
        {
            // P2P 与主连接使用同一种帧格式，避免发送端和接收端各自维护一套协议。
            var encoded = new byte[length];
            Buffer.BlockCopy(data, 0, encoded, 0, length);
            frame = DecodeP2PFrame(encoded);
        }
        catch
        {
            return;
        }

        // 【超级调试】P2P 通道帧级追踪（P2P 不走主传输，主传输的钩子看不到它）
        DebugTrace.Frame("p2p-in-" + frame.Kind, 0, length);
        // 【诊断分细】按帧类型 + 业务包类型分类计数（以前只留"最后一个包"，看不出构成）
        peer.PacketsReceived++;
        peer.BytesReceived += length;
        CountInto(peer.FrameKindCounts, frame.Kind.ToString());
        if (frame.Kind == TcpFrameKind.Packet && frame.Payload != null && frame.Payload.Length > 0)
        {
            // 【收尾 2026-10-13】P2P 帧 payload 的前 4 字节是【内层 seq】（恒为 0），
            // 以前把它当业务类型 -> 所有状态帧都被统计成 JoinRequest(type=0)，
            // 看起来就像"P2P 只握手不传数据"。P2P 通道只承载 UpdateRocketPrimary，按事实记。
            CountInto(peer.PacketTypeCounts, "UpdateRocketPrimary");
        }

        if (frame.Kind == TcpFrameKind.Hello)
        {
            peer.ValidHandshakePackets++;
            SendAck(peer, frame);
            if (peer.ValidHandshakePackets >= 3 && !peer.Active)
            {
                peer.Active = true;
                peer.ActiveSinceUtc = DateTime.UtcNow;
                DebugTrace.Note("peer " + peer.PlayerId + " ACTIVE rtt=" + peer.RttMs.ToString("F0") + "ms");
            }
        }
        else if (frame.Kind == TcpFrameKind.HelloAck)
        {
            UpdateRtt(peer, frame.Payload);
            peer.ValidHandshakePackets++;
            if (peer.ValidHandshakePackets >= 3 && !peer.Active)
            {
                peer.Active = true;
                peer.ActiveSinceUtc = DateTime.UtcNow;
                DebugTrace.Note("peer " + peer.PlayerId + " ACTIVE rtt=" + peer.RttMs.ToString("F0") + "ms");
            }
        }
        else if (frame.Kind == TcpFrameKind.Packet)
        {
            ApplyState(peer, frame.Payload, 0);
        }
    }

    private static void SendProbe(Peer peer)
    {
        // 时间戳由对端原样回显，避免依赖本地“最后一次探测”状态。
        byte[] payload = BitConverter.GetBytes(DateTime.UtcNow.Ticks);
        var probeFrame = new TcpFrame(TcpFrameKind.Hello, ++peer.NextStateSequence,
            payload, payload.Length * 8);
        peer.LastPingSentTicks = DateTime.UtcNow.Ticks;
        if (peer.ProbesSent == 0) DebugTrace.Note("peer " + peer.PlayerId + " first probe -> " + peer.Endpoint);
        peer.ProbesSent++;
        peer.KcpContext.Send(TcpFrameCodec.Encode(probeFrame));
    }

    private static void SendAck(Peer peer, TcpFrame hello)
    {
        var ackFrame = new TcpFrame(TcpFrameKind.HelloAck, hello.Sequence,
            hello.Payload, hello.PayloadBits);
        peer.KcpContext.Send(TcpFrameCodec.Encode(ackFrame));
    }

    private static TcpFrame DecodeP2PFrame(byte[] data)
    {
        if (data.Length < 4) throw new InvalidDataException("P2P frame is truncated.");
        int bodyLength = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
        if (bodyLength < 9 || bodyLength + 4 != data.Length)
            throw new InvalidDataException("P2P frame length is invalid.");
        var body = new byte[bodyLength];
        Buffer.BlockCopy(data, 4, body, 0, bodyLength);
        return TcpFrameCodec.DecodeBody(body);
    }

    private static void UpdateRtt(Peer peer, byte[] payload)
    {
        if (payload == null || payload.Length != sizeof(long)) return;
        long sentTicks = BitConverter.ToInt64(payload, 0);
        double sampleMs = Math.Max(0, (DateTime.UtcNow.Ticks - sentTicks) / 10000.0);
        if (sampleMs <= 0) return;
        double previous = peer.RttMs;
        peer.RttMs = previous <= 0 ? sampleMs : previous * 0.8 + sampleMs * 0.2;
        peer.JitterMs = previous <= 0
            ? 0
            : peer.JitterMs * 0.8 + Math.Abs(sampleMs - previous) * 0.2;
        peer.LastPingSentTicks = 0;
    }

    private static void ApplyState(Peer peer, byte[] data, int offset)
    {
        if (data.Length < offset + 8) return;
        var sequence = BitConverter.ToInt32(data, offset);
        var payloadCount = BitConverter.ToInt32(data, offset + 4);
        int cursor = offset + 8;
        for (int i = 0; i < payloadCount; i++)
        {
            if (data.Length < cursor + 8) return;
            var payloadBits = BitConverter.ToInt32(data, cursor);
            var payloadLength = (payloadBits + 7) / 8;
            if (payloadLength < 0 || payloadLength > data.Length - cursor - 8) return;
            var payload = new byte[payloadLength];
            Buffer.BlockCopy(data, cursor + 8, payload, 0, payloadLength);
            cursor += 8 + payloadLength;
            try
            {
                var packet = new Packet_UpdateRocketPrimary();
                packet.Deserialize(NetPayloadCodec.ToIncoming(payload, payloadBits));
                // 【按来源仲裁 2026-10-10】这里原来是"玩家火箭一律丢弃"（那时发送侧也是关的，
                // 两头焊死 => P2P 只握手不传数据）。现在接受它，并把这枚火箭标记成"正由 P2P 喂"：
                //   - 中继副本在新鲜期内到达 -> 丢弃（见 ClientManager.OnPacket_UpdateRocketPrimary）
                //   - P2P 断流超过 P2PStateFreshSeconds -> 标记过期，自动落回中继单源
                // 仲裁按【来源】而不是【时间戳】：中继那份的 WorldTime 是服务端重盖的，
                // 与发送方时钟不同轴，比大小会来回翻 —— 那正是当年"位置来回飘移"的成因。
                if (ClientManager.world != null && ClientManager.world.rockets.TryGetValue(packet.RocketId, out RocketState state))
                {
                    state.UpdateRocketPrimary(packet);
                    Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
                    // 【收尾 2026-10-13】只有本地确实有这枚火箭时才标记 fresh。以前无条件标记：
                    // 包其实没人吃（死插值器/未同步）也会标记 -> 中继副本被整批丢弃 ->
                    // P2P 一出问题两条路同时断，这正是"死插值器"症状被放大的原因。
                    if (LocalManager.syncedRockets.ContainsKey(packet.RocketId))
                    {
                        MarkP2PState(packet.RocketId);
                    }
                    continue;
                }
                // 本地 world 里没有的物体（未来非玩家邻近物体的显示体）暂不处理。
            }
            catch { }
        }
    }

    private static void RefreshStatus()
    {
        ActivePeerCount = 0;
        var connecting = false;
        foreach (var peer in peers.Values)
        {
            if (peer.Active)
            {
                if (!peer.ReportedEstablished)
                {
                    peer.ReportedEstablished = true;
                    var name = string.IsNullOrEmpty(peer.Username) ? $"player {peer.PlayerId}" : peer.Username;
                    ToastHelper.ShowToast($"P2P link established with {name}");
                }
                ActivePeerCount++;
            }
            else
            {
                peer.ReportedEstablished = false;
                if (!peer.Fallback) connecting = true;
            }
        }
        var activePeer = peers.Values.FirstOrDefault(p => p.Active);
        var next = ActivePeerCount > 0 ? $"P2P Active (RTT: {activePeer?.RttMs:F0}ms, jitter: {activePeer?.JitterMs:F0}ms)" : connecting ? "P2P Connecting" : "Relay Fallback";
        if (next != Status)
        {
            if (next == "Relay Fallback" && Status == "P2P Active")
                ToastHelper.ShowToast("P2P link lost, falling back to relay");
            Status = next;
            ToastHelper.ShowToast(Status);
        }
    }

    private static void CountInto(Dictionary<string, int> counts, string key)
    {
        if (counts.TryGetValue(key, out int existing)) counts[key] = existing + 1;
        else counts[key] = 1;
    }

    private static string FormatCounts(Dictionary<string, int> counts)
    {
        if (counts == null || counts.Count == 0) return "{}";
        var sb = new StringBuilder("{");
        bool first = true;
        foreach (var kv in counts)
        {
            if (!first) sb.Append(',');
            first = false;
            sb.Append(kv.Key).Append(':').Append(kv.Value);
        }
        return sb.Append('}').ToString();
    }

    /// <summary>
    /// F8 / 复制诊断用：把每个 peer 走到哪一步、为什么退化成中继、包都分成了哪几类，全部摊开。
    /// 纯 ASCII：这段文本可能进日志/上报，F1 控制台没有中日韩字形。
    /// </summary>
    public static string DescribePeers()
    {
        var sb = new StringBuilder();
        double active = ActivePeerCount;
        sb.Append("peers=").Append(peers.Count).Append(" active=").Append(active).Append(" status=").Append(Status);
        if (LastOfferRejectInfo.Length > 0) sb.Append("\n  ").Append(LastOfferRejectInfo);
        if (LastRevokedInfo.Length > 0) sb.Append("\n  ").Append(LastRevokedInfo);
        if (peers.Count == 0) sb.Append("\n  (no pair offered by the server yet)");

        foreach (var kv in peers)
        {
            Peer p = kv.Value;
            double elapsed = (DateTime.UtcNow - p.CreatedUtc).TotalSeconds;
            sb.Append("\n  peer ").Append(p.PlayerId)
              .Append(' ').Append(string.IsNullOrEmpty(p.Username) ? "?" : p.Username)
              .Append(' ').Append(p.Endpoint)
              .Append(" kcp=").Append(p.KcpInitialized ? "y" : "n")
              .Append(" active=").Append(p.Active ? "y" : "n")
              .Append(" fallback=").Append(p.Fallback ? "y" : "n")
              .Append(" hs=").Append(p.ValidHandshakePackets).Append("/3")
              .Append(" age=").Append(elapsed.ToString("F1")).Append("s");
            if (p.Active)
                sb.Append(" rtt=").Append(p.RttMs.ToString("F0")).Append("ms jitter=").Append(p.JitterMs.ToString("F0")).Append("ms");
            if (p.Fallback && p.FallbackReason.Length > 0)
                sb.Append(" reason=[").Append(p.FallbackReason).Append(']');
            sb.Append(" probes=").Append(p.ProbesSent)
              .Append(" rx=").Append(p.PacketsReceived).Append("pkt/").Append(p.BytesReceived).Append("B")
              .Append(" tx=").Append(p.BytesSent).Append("B")
              .Append(" kinds=").Append(FormatCounts(p.FrameKindCounts))
              .Append(" types=").Append(FormatCounts(p.PacketTypeCounts));
        }
        return sb.ToString();
    }

    // ——— 按来源仲裁（P2P vs 中继）———
    // 记录每枚火箭最近一次"被 P2P 喂"的时刻；新鲜期内中继副本一律丢弃。
    private static readonly Dictionary<int, float> p2pStateLastSeen = new Dictionary<int, float>();
    public const float P2PStateFreshSeconds = 0.5f;

    private static void MarkP2PState(int rocketId)
    {
        p2pStateLastSeen[rocketId] = Time.unscaledTime;
    }

    /// <summary>这枚火箭是否正由 P2P 单源喂状态（新鲜期内）。</summary>
    public static bool HasFreshState(int rocketId)
    {
        if (p2pStateLastSeen.Count == 0) return false;
        if (!p2pStateLastSeen.TryGetValue(rocketId, out float last)) return false;
        if (Time.unscaledTime - last <= P2PStateFreshSeconds) return true;
        p2pStateLastSeen.Remove(rocketId);
        return false;
    }

    private sealed class Peer : IDisposable
    {
        public int PlayerId { get; }
        // 对方端口会变（它也会重绑）。变的是【目标】，不是我们自己的 socket。
        public IPEndPoint Endpoint { get; private set; }

        public void Retarget(IPEndPoint endpoint)
        {
            Endpoint = endpoint;
        }
        public string Token { get; }
        public string Username { get; set; } = string.Empty;
        public int TransitionBufferSeconds { get; }
        public DateTime CreatedUtc { get; } = DateTime.UtcNow;
        public DateTime LastProbeUtc { get; set; } = DateTime.MinValue;
        public DateTime LastPacketUtc { get; set; } = DateTime.MinValue;
        public int ValidHandshakePackets { get; set; }
        public int NextStateSequence { get; set; }
        public bool Active { get; set; }
        public bool Fallback { get; set; }
        public bool ReportedEstablished { get; set; }
        // P2P 首次转 Active 的时刻；强制重同步的稳定窗口从这一刻起算。
        public DateTime ActiveSinceUtc { get; set; } = DateTime.MaxValue;
        // 本连接周期是否已向服务器请求过权威快照；防止每个连接周期重复全量同步。
        public bool ServerSyncRequested { get; set; }
        public HashSet<int> LocalRocketIds { get; set; } = new HashSet<int>();
        public HashSet<int> PeerRocketIds { get; set; } = new HashSet<int>();
        public Dictionary<int, int> LastSequences { get; } = new Dictionary<int, int>();

        // RTT 和抖动统计（用于 UI 显示和自适应参数调整）
        public double RttMs { get; set; }
        public double JitterMs { get; set; }
        public long LastPingSentTicks { get; set; }

        // 【诊断分细】失败原因 + 包分类计数（F8 / 复制诊断用）
        public string FallbackReason { get; set; } = string.Empty;
        public int ProbesSent { get; set; }
        public int PacketsReceived { get; set; }
        public long BytesSent { get; set; }
        public long BytesReceived { get; set; }
        public readonly Dictionary<string, int> FrameKindCounts = new Dictionary<string, int>();
        public readonly Dictionary<string, int> PacketTypeCounts = new Dictionary<string, int>();

        // 握手超时：CreatedUtc 后 10 秒仍未 Active 则标记 Fallback
        public DateTime HandshakeDeadlineUtc { get; set; } = DateTime.MaxValue;

        // KCP 相关
        public System.Net.Sockets.UdpClient Socket { get; private set; }
        public KcpContext KcpContext { get; private set; }
        public bool KcpInitialized { get; private set; }
        public byte[] RecvBuffer { get; } = new byte[65536];
        private System.Threading.Tasks.Task _receiveLoopTask;
        private CancellationTokenSource _receiveLoopCts;

        public Peer(int playerId, IPEndPoint endpoint, string token, int transitionBufferSeconds)
        {
            PlayerId = playerId;
            Endpoint = endpoint;
            Token = token;
            TransitionBufferSeconds = transitionBufferSeconds;
            HandshakeDeadlineUtc = DateTime.UtcNow.AddSeconds(10);
        }

        public void InitializeKcp()
                {
                    // 创建 UDP socket
                    Socket = new UdpClient(Endpoint.AddressFamily);
                    // 必须显式绑定本地端口：UdpClient(AddressFamily) 是未绑定 socket，
                    // 下面 ReceiveLoop 里的 ReceiveAsync 会抛 "在执行此操作前必须先调用 Bind 方法"，
                    // 接收循环启动即死 —— 表现就是 P2P 信令正常但永远收不到直连数据。
                    Socket.Client.Bind(new IPEndPoint(
                        Endpoint.AddressFamily == AddressFamily.InterNetworkV6 ? IPAddress.IPv6Any : IPAddress.Any,
                        0));
                    Socket.Client.ReceiveBufferSize = 256 * 1024;
                    MultiplayerSFS.Common.UdpSocketGuard.DisableConnReset(Socket);
                    Socket.Client.SendBufferSize = 256 * 1024;

                    // 两端必须得到同一个 conv。只用“对方 PlayerId”会让双方产生不同会话号。
                    uint conv = CreateConversation(ClientManager.playerId, PlayerId);
                    KcpContext = new KcpContext(conv, (data, ctx) => SendToNetwork(data));

                    // 预设集中在 Common/KcpTransportPresets（默认 balanced = 改动前的值）。
                    MultiplayerSFS.Common.KcpTransportPresets.Apply(KcpContext, MultiplayerSFS.Common.KcpTransportPresets.P2P);

                    KcpInitialized = true;
                    // 端口广告：把本端 P2P socket 真正绑定的端口告诉服务端。
                    // 服务端撮合时用的是主传输会话观察到的端点，与这个端口不同 —— 不广告必然打错门。
                    // 每次重建 socket（含失败后的动态重绑）都会走到这里，端口变化会自动同步。
                    int localPort = ((IPEndPoint)Socket.Client.LocalEndPoint).Port;
                    DebugTrace.Note("peer " + PlayerId + " p2p socket port=" + localPort + " advertising to server");
                    ClientManager.SendPacket(new Packet_P2PEndpoint { Port = localPort });

                    // 启动接收循环
                    _receiveLoopCts = new CancellationTokenSource();
                    _receiveLoopTask = ReceiveLoop(_receiveLoopCts.Token);
                }

        private void SendToNetwork(byte[] data)
        {
            try
            {
                if (Socket != null)
                {
                    Socket.Send(data, data.Length, Endpoint);
                    BytesSent += data.Length;
                }
            }
            catch { }
        }

        private static uint CreateConversation(int firstPlayerId, int secondPlayerId)
        {
            uint low = unchecked((uint)Math.Min(firstPlayerId, secondPlayerId));
            uint high = unchecked((uint)Math.Max(firstPlayerId, secondPlayerId));
            return (low * 0x9E3779B9u) ^ high ^ 0x5346534Bu;
        }

        private async System.Threading.Tasks.Task ReceiveLoop(CancellationToken cancellationToken)
        {
            try
            {
                while (Socket != null && KcpInitialized && !cancellationToken.IsCancellationRequested)
                {
                    UdpReceiveResult result;
                    try
                    {
                        result = await Socket.ReceiveAsync().ConfigureAwait(false);
                    }
                    catch (ObjectDisposedException)
                    {
                        return;
                    }
                    catch (Exception)
                    {
                        await System.Threading.Tasks.Task.Delay(10).ConfigureAwait(false);
                        continue;
                    }

                    lock (_datagramLock)
                    {
                        if (_pendingDatagrams.Count < 512)
                        {
                            _pendingDatagrams.Enqueue(new P2PRawDatagram(result.RemoteEndPoint, result.Buffer));
                        }
                    }
                }
            }
            catch { }
        }

        private readonly object _datagramLock = new object();
        private readonly Queue<P2PRawDatagram> _pendingDatagrams = new Queue<P2PRawDatagram>();

        public bool TryReceiveDatagram(out P2PRawDatagram datagram)
        {
            lock (_datagramLock)
            {
                if (_pendingDatagrams.Count > 0)
                {
                    datagram = _pendingDatagrams.Dequeue();
                    return true;
                }
            }
            datagram = default;
            return false;
        }

        public void Dispose()
        {
            KcpInitialized = false;
            _receiveLoopCts?.Cancel();
            try { _receiveLoopTask?.Wait(1000); } catch { }
            try { _receiveLoopTask?.Dispose(); } catch { }
            try { _receiveLoopCts?.Dispose(); } catch { }
            try { KcpContext?.Dispose(); } catch { }
            try { Socket?.Close(); } catch { }
            lock (_datagramLock) _pendingDatagrams.Clear();
        }
    }

    // 复用原有的数据报结构
    private readonly struct P2PRawDatagram
    {
        public readonly IPEndPoint RemoteEndPoint;
        public readonly byte[] Data;
        public P2PRawDatagram(IPEndPoint endPoint, byte[] data)
        {
            RemoteEndPoint = endPoint;
            Data = data;
        }
    }
}