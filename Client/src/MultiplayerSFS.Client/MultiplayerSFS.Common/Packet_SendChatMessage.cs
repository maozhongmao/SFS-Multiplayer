// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using UnityEngine;

namespace MultiplayerSFS.Common;

public class Packet_SendChatMessage : Packet
{
	public int SenderId { get; set; } = -1;

	public string Message { get; set; }

	public Color Color { get; set; } = Color.white;

	public override PacketType Type => PacketType.SendChatMessage;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(SenderId);
		msg.WriteCompressedString(Message ?? string.Empty);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		SenderId = ((NetBuffer)msg).ReadInt32();
		Message = ((NetBuffer)msg).ReadString();
		Color = Color.white;
		if (msg.LengthBits - msg.Position >= 128)
		{
			float r = ((NetBuffer)msg).ReadFloat();
			float g = ((NetBuffer)msg).ReadFloat();
			float b = ((NetBuffer)msg).ReadFloat();
			float a = ((NetBuffer)msg).ReadFloat();
			Color = new Color(r, g, b, a);
		}
	}
}
