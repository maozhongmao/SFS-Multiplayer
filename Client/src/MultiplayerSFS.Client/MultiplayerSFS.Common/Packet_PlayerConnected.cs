// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using UnityEngine;

namespace MultiplayerSFS.Common;

public class Packet_PlayerConnected : Packet
{
	public int PlayerId { get; set; } = -1;

	public string Username { get; set; }

	public Color IconColor { get; set; }

	public bool PrintMessage { get; set; }

	public override PacketType Type => PacketType.PlayerConnected;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedInt(PlayerId);
		msg.WriteCompressedString(Username);
		msg.WriteCompressedColor(IconColor);
		((NetBuffer)msg).Write(PrintMessage);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		PlayerId = msg.ReadCompressedInt();
		Username = msg.ReadCompressedString();
		IconColor = msg.ReadCompressedColor();
		PrintMessage = ((NetBuffer)msg).ReadBoolean();
	}
}
