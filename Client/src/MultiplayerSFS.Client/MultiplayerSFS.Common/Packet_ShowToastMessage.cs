// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_ShowToastMessage : Packet
{
	public string Message { get; set; }

	public override PacketType Type => PacketType.ShowToastMessage;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedString(Message ?? string.Empty);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		Message = ((NetBuffer)msg).ReadString();
	}
}
