// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePart_EngineModule : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public int PartId { get; set; } = -1;

	public bool EngineOn { get; set; }

	public override PacketType Type => PacketType.UpdatePart_EngineModule;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		((NetBuffer)msg).Write(PartId);
		((NetBuffer)msg).Write(EngineOn);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		PartId = ((NetBuffer)msg).ReadInt32();
		EngineOn = ((NetBuffer)msg).ReadBoolean();
	}
}
