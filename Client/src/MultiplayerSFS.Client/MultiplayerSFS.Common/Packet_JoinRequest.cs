// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_JoinRequest : Packet
{
	public string Username { get; set; }
	public string Password { get; set; }
	public string SolarSystemName { get; set; } = "";
	public int ResumePlayerId { get; set; } = -1;
	public string ResumeToken { get; set; } = "";

	public override PacketType Type => PacketType.JoinRequest;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedString(Username ?? string.Empty);
		msg.WriteCompressedString(Password ?? string.Empty);
		msg.WriteCompressedString(SolarSystemName ?? string.Empty);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		Username = ((NetBuffer)msg).ReadString();
		Password = ((NetBuffer)msg).ReadString();
		SolarSystemName = ((NetBuffer)msg).ReadString();
	}
}
