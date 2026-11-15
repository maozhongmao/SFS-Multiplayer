// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using UnityEngine;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePlayerColor : Packet
{
	public int PlayerId { get; set; } = -1;

	public Color Color { get; set; }

	public override PacketType Type => PacketType.UpdatePlayerColor;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedInt(PlayerId);
		msg.WriteCompressedColor(Color);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		PlayerId = msg.ReadCompressedInt();
		Color = msg.ReadCompressedColor();
	}
}
