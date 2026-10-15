// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Security.Cryptography;
using System.Text;

namespace SfsMultiplayer.Server;

public sealed class P2PSettings
{
    // 默认关闭：直连链路尚不稳定，下个大版本再修。server.yml 里 p2p.enabled: true 可开。
    // 与 C++ 服务端 P2PSettings::enabled 保持一致（见 Server/cpp/src/server/server.hpp）。
    public bool Enabled { get; set; } = false;
    public double ProximityMeters { get; set; } = 10000;
    public int ValidationIntervalSeconds { get; set; } = 1;
    public int PeerTimeoutSeconds { get; set; } = 3;
    public int TransitionBufferSeconds { get; set; } = 10;
    public int MaxGroupMembers { get; set; } = 16;
    public int MaxDirectPeersPerClient { get; set; } = 8;
}

public sealed class ExperimentalAccessSettings
{
    public string Passphrase { get; set; } = string.Empty;

    public bool IsConfigured => !string.IsNullOrEmpty(Passphrase);

    public bool Accepts(string? candidate)
    {
        if (!IsConfigured || candidate is null) return false;
        var expected = SHA256.HashData(Encoding.UTF8.GetBytes(Passphrase));
        var supplied = SHA256.HashData(Encoding.UTF8.GetBytes(candidate));
        return CryptographicOperations.FixedTimeEquals(expected, supplied);
    }
}
