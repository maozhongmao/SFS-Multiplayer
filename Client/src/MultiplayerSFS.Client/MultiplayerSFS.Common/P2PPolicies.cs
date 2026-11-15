// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common;

public static class P2PStateOrderPolicy
{
    public static bool ShouldAccept(long currentGeneration, long currentSequence,
        long incomingGeneration, long incomingSequence)
    {
        if (incomingGeneration != currentGeneration)
            return incomingGeneration > currentGeneration;
        return incomingSequence > currentSequence;
    }
}

public static class P2PProximityPolicy
{
    public static bool IsEligible(NetLocation first, NetLocation second, double thresholdMeters)
    {
        if (first == null || second == null || !IsFinite(thresholdMeters) || thresholdMeters < 0)
            return false;
        if (!string.Equals(first.address, second.address, StringComparison.Ordinal))
            return false;
        var dx = second.position.x - first.position.x;
        var dy = second.position.y - first.position.y;
        if (!IsFinite(dx) || !IsFinite(dy)) return false;
        return dx * dx + dy * dy <= thresholdMeters * thresholdMeters;
    }

    private static bool IsFinite(double value)
    {
        return !double.IsNaN(value) && !double.IsInfinity(value);
    }
}

public static class P2PTransitionPolicy
{
    public static bool ShouldFallback(DateTime nowUtc, DateTime lastPacketUtc, double timeoutSeconds)
    {
        if (!IsFinite(timeoutSeconds) || timeoutSeconds < 0) return true;
        return nowUtc - lastPacketUtc >= TimeSpan.FromSeconds(timeoutSeconds);
    }

    private static bool IsFinite(double value)
    {
        return !double.IsNaN(value) && !double.IsInfinity(value);
    }
}

/// <summary>
/// P2P 建立后的“双方互相看不到对方状态”修复（臭名昭著的老 bug，勿删）：
/// P2P 握手成功时，双方此前经由服务器中继的初始状态可能已经漂移（初始包丢失/迟到，
/// 或本地世界初始化晚于首个状态包），P2P 本身又不承载玩家火箭状态（严格服务器单源），
/// 所以必须在 P2P 首次转 Active 后，由客户端向服务器请求一次权威快照强制重同步。
/// 规则：每个 P2P 连接周期只触发一次；先等待一小段稳定窗口，避免与握手尾包/首批状态包抢时序。
/// 断线重连会生成新的 Peer（新连接周期），允许再次同步一次。
/// </summary>
public static class P2PPostConnectSyncPolicy
{
    /// <summary>P2P 转为 Active 后，延迟一小段时间再向服务器请求权威快照，避免抢握手尾包的时序。</summary>
    public static readonly TimeSpan StabilizationDelay = TimeSpan.FromSeconds(1);

    /// <summary>返回是否应向服务器请求一次权威世界快照（每个 P2P 连接周期最多一次）。</summary>
    public static bool ShouldRequestServerSync(
        bool peerActive,
        bool syncAlreadyRequested,
        DateTime activeSinceUtc,
        DateTime nowUtc)
    {
        if (!peerActive || syncAlreadyRequested) return false;
        return nowUtc - activeSinceUtc >= StabilizationDelay;
    }
}
