// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class Packet_UpdateRocketPrimary : Packet
{
	public double WorldTime { get; set; } = double.NaN;

	public int RocketId { get; set; } = -1;

	public NetLocation Location { get; set; }

	public float Rotation { get; set; }

	public float AngularVelocity { get; set; }

	// 【结构哈希 2026-10-10】活体零件 ID 集合的 FNV-1a 哈希（由权威方填）。
	// 观测方拿它跟自己的比对：不一致 = 结构分叉 -> 请求一次权威快照自愈。
	// 这样"任何一条路径漏了结构事件"都能在 1~2 秒内收敛，且只在真不一致时才重建。
	// 0 = 对方是旧版本，跳过检查（兼容）。
	public uint StructureHash { get; set; }

	public override PacketType Type => PacketType.UpdateRocketPrimary;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(WorldTime);
		((NetBuffer)msg).Write(RocketId);
		msg.Write(Location);
		((NetBuffer)msg).Write(Rotation);
		((NetBuffer)msg).Write(AngularVelocity);
		((NetBuffer)msg).Write(StructureHash);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		WorldTime = ((NetBuffer)msg).ReadDouble();
		RocketId = ((NetBuffer)msg).ReadInt32();
		Location = msg.Read<NetLocation>();
		Rotation = ((NetBuffer)msg).ReadFloat();
		AngularVelocity = ((NetBuffer)msg).ReadFloat();
		StructureHash = ((NetBuffer)msg).ReadUInt32();
	}
}
