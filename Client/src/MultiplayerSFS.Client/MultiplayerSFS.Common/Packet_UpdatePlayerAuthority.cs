// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePlayerAuthority : Packet
{
	public HashSet<int> RocketIds { get; set; }

	public override PacketType Type => PacketType.UpdatePlayerAuthority;

	public override void Serialize(NetOutgoingMessage msg)
	{
		RocketIds ??= new HashSet<int>();
					msg.WriteCollection((ICollection<int>)RocketIds, (Action<int>)((NetBuffer)msg).Write);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		RocketIds = msg.ReadCollection((Func<int, HashSet<int>>)((int count) => new HashSet<int>(count)), (Func<int>)((NetBuffer)msg).ReadInt32);
	}
}
