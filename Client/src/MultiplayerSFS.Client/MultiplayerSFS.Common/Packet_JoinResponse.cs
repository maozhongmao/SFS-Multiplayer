// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using SFS.WorldBase;

namespace MultiplayerSFS.Common;

public class Packet_JoinResponse : Packet
{
	public int PlayerId { get; set; } = -1;
	public double UpdateRocketsPeriod { get; set; }
	public double ChatMessageCooldown { get; set; }
	public double WorldTime { get; set; } = double.NaN;
	public double SendTime { get; set; }
	public Difficulty.DifficultyType Difficulty { get; set; }
	public string SolarSystemName { get; set; } = "";
	public string UdpSessionToken { get; set; } = "";
	public string ResumeToken { get; set; } = "";

	public override PacketType Type => PacketType.JoinResponse;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(PlayerId);
		((NetBuffer)msg).Write(UpdateRocketsPeriod);
		((NetBuffer)msg).Write(ChatMessageCooldown);
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(SendTime);
		((NetBuffer)msg).Write((byte)Difficulty);
		((NetBuffer)msg).Write(SolarSystemName);
		((NetBuffer)msg).Write(UdpSessionToken);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		PlayerId = ((NetBuffer)msg).ReadInt32();
		UpdateRocketsPeriod = ((NetBuffer)msg).ReadDouble();
		ChatMessageCooldown = ((NetBuffer)msg).ReadDouble();
		WorldTime = ((NetBuffer)msg).ReadDouble();
		SendTime = ((NetBuffer)msg).ReadDouble();
		Difficulty = (Difficulty.DifficultyType)((NetBuffer)msg).ReadByte();
		SolarSystemName = ((NetBuffer)msg).ReadString();
		UdpSessionToken = ((NetBuffer)msg).ReadString();
	}
}
