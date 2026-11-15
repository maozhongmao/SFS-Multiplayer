// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePartTemperature : Packet
{
    public int RocketId { get; set; }
    public int PartId { get; set; }
    public float Temperature { get; set; }

    public override PacketType Type => PacketType.UpdatePartTemperature;

    public override void Serialize(NetOutgoingMessage msg)
    {
        msg.Write(RocketId);
        msg.Write(PartId);
        msg.Write(Temperature);
    }

    public override void Deserialize(NetIncomingMessage msg)
    {
        RocketId = msg.ReadInt32();
        PartId = msg.ReadInt32();
        Temperature = msg.ReadFloat();
    }
}