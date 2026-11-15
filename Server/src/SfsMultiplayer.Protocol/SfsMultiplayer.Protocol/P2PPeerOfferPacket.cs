// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public sealed class P2PPeerOfferPacket : INetData
{
    public bool Active { get; set; }
    public int PeerPlayerId { get; set; } = -1;
    public string PeerAddress { get; set; } = string.Empty;
    public int PeerPort { get; set; }
    public string PairToken { get; set; } = string.Empty;
    public int TransitionBufferSeconds { get; set; } = 10;
    public List<int> LocalRocketIds { get; set; } = new();
    public List<int> PeerRocketIds { get; set; } = new();

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(Active);
        message.Write(PeerPlayerId);
        message.Write(PeerAddress ?? string.Empty);
        message.Write(PeerPort);
        message.Write(PairToken ?? string.Empty);
        message.Write(TransitionBufferSeconds);
        message.Write(LocalRocketIds.Count);
        foreach (var id in LocalRocketIds) message.Write(id);
        message.Write(PeerRocketIds.Count);
        foreach (var id in PeerRocketIds) message.Write(id);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        Active = message.ReadBoolean();
        PeerPlayerId = message.ReadInt32();
        PeerAddress = message.ReadStringBounded();
        PeerPort = message.ReadInt32();
        PairToken = message.ReadStringBounded();
        TransitionBufferSeconds = message.ReadInt32();
        int localCount = message.ReadInt32();
        if (localCount < 0 || localCount > 256) throw new InvalidDataException("Invalid local P2P rocket count.");
        LocalRocketIds = new List<int>(localCount);
        for (var i = 0; i < localCount; i++) LocalRocketIds.Add(message.ReadInt32());
        int peerCount = message.ReadInt32();
        if (peerCount < 0 || peerCount > 256) throw new InvalidDataException("Invalid peer P2P rocket count.");
        PeerRocketIds = new List<int>(peerCount);
        for (var i = 0; i < peerCount; i++) PeerRocketIds.Add(message.ReadInt32());
    }
}
