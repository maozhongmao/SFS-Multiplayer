// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;
using SFS.World;

namespace MultiplayerSFS.Common;

public class Packet_DestroyPart : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public int PartId { get; set; } = -1;

	public bool CreateExplosion { get; set; }

	public DestructionReason Reason { get; set; }

	public override PacketType Type => PacketType.DestroyPart;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		((NetBuffer)msg).Write(PartId);
		((NetBuffer)msg).Write(CreateExplosion);
		((NetBuffer)msg).Write((byte)Reason);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		PartId = ((NetBuffer)msg).ReadInt32();
		CreateExplosion = ((NetBuffer)msg).ReadBoolean();
		byte rawReason = ((NetBuffer)msg).ReadByte();
		// 必须转成枚举的基础类型（int）再交给 Enum.IsDefined：
		// 传 byte 会抛 ArgumentException("Object must be the same type as the enum")，
		// 导致每个 DestroyPart 包解析失败、部件摧毁事件全部丢失（火箭状态随之不同步）。
		Reason = Enum.IsDefined(typeof(DestructionReason), (int)rawReason) ? (DestructionReason)rawReason : DestructionReason.Intentional;
	}
}
