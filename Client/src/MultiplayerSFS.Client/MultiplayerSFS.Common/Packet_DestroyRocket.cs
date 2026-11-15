// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;
using SFS.World;

namespace MultiplayerSFS.Common;

public class Packet_DestroyRocket : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public DestructionReason Reason { get; set; }

	public override PacketType Type => PacketType.DestroyRocket;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		((NetBuffer)msg).Write((byte)Reason);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		byte rawReason = ((NetBuffer)msg).ReadByte();
		// 枚举底层类型不匹配曾让真机反序列化直接抛异常，销毁事件丢失后双方世界分叉；
		// 非法值必须收敛为 Intentional，绝不能让销毁包解码中断网络帧处理。
		Reason = Enum.IsDefined(typeof(DestructionReason), (int)rawReason)
			? (DestructionReason)rawReason
			: DestructionReason.Intentional;
	}
}
