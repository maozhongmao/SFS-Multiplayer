// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdatePart_BoosterModule : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public int PartId { get; set; } = -1;

	public bool Primed { get; set; }

	public float Throttle { get; set; }

	public float FuelPercent { get; set; }

	public override PacketType Type => PacketType.UpdatePart_BoosterModule;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		((NetBuffer)msg).Write(PartId);
		((NetBuffer)msg).Write(Primed);
		((NetBuffer)msg).Write(Throttle);
		((NetBuffer)msg).Write(FuelPercent);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		PartId = ((NetBuffer)msg).ReadInt32();
		Primed = ((NetBuffer)msg).ReadBoolean();
		Throttle = ((NetBuffer)msg).ReadFloat();
		FuelPercent = ((NetBuffer)msg).ReadFloat();
	}
}
