// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePart_ResourceModule : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public double ResourcePercent { get; set; }

	public HashSet<int> PartIds { get; set; }

	public override PacketType Type => PacketType.UpdatePart_ResourceModule;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		((NetBuffer)msg).Write(ResourcePercent);
		PartIds ??= new HashSet<int>();
					msg.WriteCollection((ICollection<int>)PartIds, (Action<int>)((NetBuffer)msg).Write);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		ResourcePercent = ((NetBuffer)msg).ReadDouble();
		PartIds = msg.ReadCollection((Func<int, HashSet<int>>)((int count) => new HashSet<int>(count)), (Func<int>)((NetBuffer)msg).ReadInt32);
	}
}
