// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public enum TimeWarpOperation : byte
{
	Request = 0,
	Vote = 1,
	Response = 2,
	Applied = 3,
	Cancelled = 4,
	Notice = 5
}

public sealed class Packet_TimeWarp : Packet
{
	public TimeWarpOperation Operation { get; set; }
	public int VoteId { get; set; }
	public int RequesterId { get; set; } = -1;
	public string RequesterName { get; set; } = "";
	public double Multiplier { get; set; } = 1.0;
	public bool Approved { get; set; }
	public double WorldTime { get; set; }
	public int TimeoutSeconds { get; set; }
	public string Message { get; set; } = "";

	public override PacketType Type => PacketType.TimeWarp;

	public override void Serialize(NetOutgoingMessage msg)
	{
		msg.Write((byte)Operation);
		msg.Write(VoteId);
		msg.Write(RequesterId);
		msg.Write(RequesterName ?? "");
		msg.Write(Multiplier);
		msg.Write(Approved);
		msg.Write(WorldTime);
		msg.Write(TimeoutSeconds);
		msg.Write(Message ?? "");
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		byte rawOperation = msg.ReadByte();
		Operation = Enum.IsDefined(typeof(TimeWarpOperation), rawOperation) ? (TimeWarpOperation)rawOperation : TimeWarpOperation.Applied;
		VoteId = msg.ReadInt32();
		RequesterId = msg.ReadInt32();
		RequesterName = msg.ReadString();
		Multiplier = msg.ReadDouble();
		Approved = msg.ReadBoolean();
		WorldTime = msg.ReadDouble();
		TimeoutSeconds = msg.ReadInt32();
		Message = msg.ReadString();
	}
}
