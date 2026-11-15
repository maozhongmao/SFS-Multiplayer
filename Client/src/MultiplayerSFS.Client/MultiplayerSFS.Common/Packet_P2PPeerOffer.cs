// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.IO;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public sealed class Packet_P2PPeerOffer : Packet
{
    public bool Active { get; set; }
    public int PeerPlayerId { get; set; } = -1;
    public string PeerAddress { get; set; } = string.Empty;
    public int PeerPort { get; set; }
    public string PairToken { get; set; } = string.Empty;
    public int TransitionBufferSeconds { get; set; } = 10;
    public List<int> LocalRocketIds { get; set; } = new List<int>();
    public List<int> PeerRocketIds { get; set; } = new List<int>();

    public override PacketType Type => PacketType.P2PPeerOffer;

    public override void Serialize(NetOutgoingMessage msg)
    {
        msg.Write(Active);
        msg.Write(PeerPlayerId);
        msg.Write(PeerAddress ?? string.Empty);
        msg.Write(PeerPort);
        msg.Write(PairToken ?? string.Empty);
        msg.Write(TransitionBufferSeconds);
        msg.Write(LocalRocketIds.Count);
        foreach (int id in LocalRocketIds) msg.Write(id);
        msg.Write(PeerRocketIds.Count);
        foreach (int id in PeerRocketIds) msg.Write(id);
    }

    public override void Deserialize(NetIncomingMessage msg)
    {
        Active = msg.ReadBoolean();
        PeerPlayerId = msg.ReadInt32();
        PeerAddress = msg.ReadString() ?? string.Empty;
        PeerPort = msg.ReadInt32();
        PairToken = msg.ReadString() ?? string.Empty;
        TransitionBufferSeconds = msg.ReadInt32();
        int localCount = msg.ReadInt32();
        if (localCount < 0 || localCount > 256) throw new InvalidDataException("Invalid local P2P rocket count.");
        LocalRocketIds = new List<int>(localCount);
        for (int i = 0; i < localCount; i++) LocalRocketIds.Add(msg.ReadInt32());
        int peerCount = msg.ReadInt32();
        if (peerCount < 0 || peerCount > 256) throw new InvalidDataException("Invalid peer P2P rocket count.");
        PeerRocketIds = new List<int>(peerCount);
        for (int i = 0; i < peerCount; i++) PeerRocketIds.Add(msg.ReadInt32());
    }
}
