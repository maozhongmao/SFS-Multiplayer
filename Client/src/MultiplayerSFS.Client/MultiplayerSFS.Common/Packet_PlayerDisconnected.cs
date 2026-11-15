// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_PlayerDisconnected : Packet
{
	public int PlayerId { get; set; } = -1;

	public override PacketType Type => PacketType.PlayerDisconnected;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(PlayerId);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		PlayerId = ((NetBuffer)msg).ReadInt32();
	}
}
