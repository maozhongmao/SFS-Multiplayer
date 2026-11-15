// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using UnityEngine;

namespace MultiplayerSFS.Common;

public class Packet_UpdateRocketSecondary : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public float Input_Turn { get; set; }

	public Vector2 Input_Raw { get; set; }

	public Vector2 Input_Horizontal { get; set; }

	public Vector2 Input_Vertical { get; set; }

	public float ThrottlePercent { get; set; }

	public bool ThrottleOn { get; set; }

	public bool RCS { get; set; }

	public override PacketType Type => PacketType.UpdateRocketSecondary;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedDouble(WorldTime);
		msg.WriteCompressedInt(RocketId);
		msg.WriteCompressedFloat(Input_Turn);
		msg.WriteCompressedVector2(Input_Raw);
		msg.WriteCompressedVector2(Input_Horizontal);
		msg.WriteCompressedVector2(Input_Vertical);
		msg.WriteCompressedFloat(ThrottlePercent);
		((NetBuffer)msg).Write(ThrottleOn);
		((NetBuffer)msg).Write(RCS);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = msg.ReadCompressedDouble();
		RocketId = msg.ReadCompressedInt();
		Input_Turn = msg.ReadCompressedFloat();
		Input_Raw = msg.ReadCompressedVector2();
		Input_Horizontal = msg.ReadCompressedVector2();
		Input_Vertical = msg.ReadCompressedVector2();
		ThrottlePercent = msg.ReadCompressedFloat();
		ThrottleOn = ((NetBuffer)msg).ReadBoolean();
		RCS = ((NetBuffer)msg).ReadBoolean();
	}
}
