// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Reflection;
using Lidgren.Network;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class ServerLifecycleTests
{
    [Fact(Timeout = 20_000)]
    public async Task DisposeWaitsUntilUdpPortCanBeRebound()
    {
        var first = new MultiplayerServer(
            new ServerSettings { Port = 0 }, new WorldSnapshot());
        first.Start();
        var port = first.Port;
        var peerField = typeof(MultiplayerServer).GetField("_server",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException("Server peer field was not found.");
        var firstPeer = Assert.IsAssignableFrom<NetPeer>(peerField.GetValue(first));
        await first.DisposeAsync();
        Assert.Equal(NetPeerStatus.NotRunning, firstPeer.Status);

        for (var attempt = 0; attempt < 20; attempt++)
        {
            var replacement = new MultiplayerServer(
                new ServerSettings { Port = port }, new WorldSnapshot());
            replacement.Start();
            var replacementPeer = Assert.IsAssignableFrom<NetPeer>(peerField.GetValue(replacement));
            Assert.Equal(port, replacement.Port);
            await replacement.DisposeAsync();
            Assert.Equal(NetPeerStatus.NotRunning, replacementPeer.Status);
        }
    }
}
