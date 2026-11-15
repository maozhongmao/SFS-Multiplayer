// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public enum DockTransactionOperation : byte
{
	Dock = 0,
	Undock = 1
}

public sealed class Packet_DockTransaction : Packet
{
	public int TransactionId { get; set; }
	public DockTransactionOperation Operation { get; set; }
	public bool Committed { get; set; }
	public int KeepRocketId { get; set; } = -1;
	public int RemoveRocketId { get; set; } = -1;
	public int KeepPartId { get; set; } = -1;
	public int RemovePartId { get; set; } = -1;
	public double WorldTime { get; set; }
	public RocketState MergedRocket { get; set; }
	public int SecondRocketId { get; set; } = -1;
	public RocketState SecondRocket { get; set; }

	public override PacketType Type => PacketType.DockTransaction;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedInt(TransactionId);
		msg.Write((byte)Operation);
		((NetBuffer)msg).Write(Committed);
		msg.WriteCompressedInt(KeepRocketId);
		msg.WriteCompressedInt(RemoveRocketId);
		msg.WriteCompressedInt(KeepPartId);
		msg.WriteCompressedInt(RemovePartId);
		msg.WriteCompressedDouble(WorldTime);
		if (Committed)
		{
			msg.Write(MergedRocket);
			msg.WriteCompressedInt(SecondRocketId);
			msg.Write(SecondRocket != null);
			if (SecondRocket != null) msg.Write(SecondRocket);
		}
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		TransactionId = msg.ReadCompressedInt();
		Operation = (DockTransactionOperation)msg.ReadByte();
		Committed = ((NetBuffer)msg).ReadBoolean();
		KeepRocketId = msg.ReadCompressedInt();
		RemoveRocketId = msg.ReadCompressedInt();
		KeepPartId = msg.ReadCompressedInt();
		RemovePartId = msg.ReadCompressedInt();
		WorldTime = msg.ReadCompressedDouble();
		if (Committed)
		{
			MergedRocket = msg.Read<RocketState>();
			SecondRocketId = msg.ReadCompressedInt();
			if (msg.ReadBoolean()) SecondRocket = msg.Read<RocketState>();
		}
	}
}
