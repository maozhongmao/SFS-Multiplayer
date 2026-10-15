// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using MultiplayerSFS.Common.Transport.Kcp;

namespace MultiplayerSFS.Common;

/// <summary>
/// KCP transport presets.
///
/// Before: SetNoDelay / SetMtu / WndSize were hard-coded in 4 places (main session,
/// P2P peer, join-menu probe, legacy UDP transport) with slightly different values,
/// and there was no way to change them without a rebuild.
///
/// Now: one table + an optional adaptive mapping driven by the measured link quality.
///
/// Notes that must not be lost:
///  - nodelay / interval / resend / nc are LOCAL SENDER policies; they do not have to
///    match the peer. The server uses kFastResend (see Server/cpp/src/net/kcp_transport.hpp);
///    resend=0 means "timeout retransmit only", resend=2 enables fast retransmit.
///  - MTU must stay below the path MTU. IPv6's floor is 1280, so 1400 can black-hole
///    behind tunnels/relays; the ipv6safe preset drops to 1200.
///  - balanced is byte-for-byte the behaviour that was shipped before this file existed,
///    and it stays the default: choosing nothing changes nothing.
///  - KCP parameters only take effect when a connection is created, so switching a
///    preset requires a reconnect.
/// </summary>
public static class KcpTransportPresets
{
    public const string Balanced = "balanced";
    public const string Fast = "fast";
    public const string Ipv6Safe = "ipv6safe";
    public const string Adaptive = "adaptive";

    public static readonly string[] All = { Balanced, Fast, Ipv6Safe, Adaptive };

    // Two independent data paths, two independent presets:
    //   Relay = every non-P2P byte: the main session (TcpTransportCore, i.e. the server relay),
    //           the join-menu probe and the legacy UDP transport. Link goes over the WAN/board.
    //   P2P   = the direct channel (one KCP context per peer in P2PConnectionManager).
    //           Usually same-LAN or after a successful hole punch, so RTT and MTU differ.
    // Their link characteristics are not the same, so they no longer share one value.
    public const string RelayPrefsKey = "multiplayersfs.kcp.preset.relay";
    public const string P2PPrefsKey = "multiplayersfs.kcp.preset.p2p";
    private const string LegacyPrefsKey = "multiplayersfs.kcp.preset";

    private static string relay;
    private static bool relayLoaded;
    private static string p2p;
    private static bool p2pLoaded;

    /// <summary>Preset for the non-P2P data path (server relay session + probe + legacy UDP).</summary>
    public static string Relay
    {
        get
        {
            if (!relayLoaded)
            {
                relayLoaded = true;
                relay = Load(RelayPrefsKey);
            }
            return relay;
        }
        set
        {
            relayLoaded = true;
            relay = value;
            Save(RelayPrefsKey, value);
        }
    }

    /// <summary>Preset for the direct P2P channel.</summary>
    public static string P2P
    {
        get
        {
            if (!p2pLoaded)
            {
                p2pLoaded = true;
                p2p = Load(P2PPrefsKey);
            }
            return p2p;
        }
        set
        {
            p2pLoaded = true;
            p2p = value;
            Save(P2PPrefsKey, value);
        }
    }

    private static string Load(string key)
    {
        string value = null;
        try
        {
            value = UnityEngine.PlayerPrefs.GetString(key, null);
            // Migration: the first cut of this file stored one global value under the legacy key.
            if (string.IsNullOrEmpty(value)) value = UnityEngine.PlayerPrefs.GetString(LegacyPrefsKey, null);
        }
        catch
        {
            value = null;
        }
        if (string.IsNullOrEmpty(value) || System.Array.IndexOf(All, value) < 0) return Balanced;
        return value;
    }

    private static void Save(string key, string value)
    {
        try
        {
            UnityEngine.PlayerPrefs.SetString(key, value);
            UnityEngine.PlayerPrefs.Save();
        }
        catch
        {
            // PlayerPrefs is unavailable outside the game process (tests): keep the value in memory.
        }
    }

    public static string Next(string current)
    {
        int index = System.Array.IndexOf(All, current);
        return All[(index + 1) % All.Length];
    }

    /// <summary>Apply a preset (or resolve the adaptive one) to a fresh KCP context.</summary>
    public static void Apply(KcpContext context, string preset, double roundTripMs = 0, double jitterMs = 0)
    {
        if (context == null) return;
        string resolved = preset;
        if (resolved == Adaptive)
        {
            NetworkQuality quality = NetworkAdaptationPolicy.Evaluate(roundTripMs, jitterMs, 0).Quality;
            if (quality == NetworkQuality.Excellent) resolved = Fast;
            else if (quality == NetworkQuality.Good) resolved = Balanced;
            else resolved = Ipv6Safe;
        }

        switch (resolved)
        {
        case Fast:
            // interval 10 -> 5 ms and resend 0 -> 2: retransmit before the RTO expires.
            context.SetNoDelay(1, 5, 2, 1);
            context.SetMtu(1400);
            context.WndSize(256, 512);
            break;
        case Ipv6Safe:
            context.SetNoDelay(1, 20, 0, 1);
            context.SetMtu(1200);
            context.WndSize(128, 256);
            break;
        default:
            context.SetNoDelay(1, 10, 0, 1);
            context.SetMtu(1400);
            context.WndSize(256, 512);
            break;
        }
    }

    public static string DescribeFor(string preset)
    {
        switch (preset)
        {
        case Fast:
            return "fast: nodelay=1 interval=5ms resend=2 nc=1 mtu=1400 wnd=256/512 (fast retransmit on)";
        case Ipv6Safe:
            return "ipv6safe: nodelay=1 interval=20ms resend=0 nc=1 mtu=1200 wnd=128/256 (no fragmentation black hole)";
        case Adaptive:
            return "adaptive: pick fast / balanced / ipv6safe from measured RTT and jitter";
        default:
            return "balanced: nodelay=1 interval=10ms resend=0 nc=1 mtu=1400 wnd=256/512 (= pre-change values)";
        }
    }

    /// <summary>Description of the relay (non-P2P) preset.</summary>
    public static string Describe()
    {
        return DescribeFor(Relay);
    }
}
