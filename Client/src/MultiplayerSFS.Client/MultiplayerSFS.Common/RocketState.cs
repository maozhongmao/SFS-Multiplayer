// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.Linq;
using Lidgren.Network;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Common;

public class RocketState : INetData
{
	public string rocketName;

	public NetLocation location;

	public float rotation;

	public float angularVelocity;

	public bool throttleOn;

	public float throttlePercent;

	public bool RCS;

	public float input_Turn;

	public Vector2 input_Raw;

	public Vector2 input_Horizontal;

	public Vector2 input_Vertical;

	public Dictionary<int, PartState> parts;

	public List<JointState> joints;

	public List<StageState> stages;

	public RocketState()
	{
	}

	public RocketState(RocketSave save)
	{
		rocketName = save.rocketName;
		location = new NetLocation(save.location.position, save.location.velocity, save.location.address);
		rotation = save.rotation;
		angularVelocity = save.angularVelocity;
		throttleOn = save.throttleOn;
		throttlePercent = save.throttlePercent;
		RCS = save.RCS;
		input_Turn = 0f;
		input_Raw = Vector2.zero;
		input_Horizontal = Vector2.zero;
		input_Vertical = Vector2.zero;
		Dictionary<int, int> partIndexToID = new Dictionary<int, int>(save.parts.Length);
		parts = new Dictionary<int, PartState>(save.parts.Length);
		for (int i = 0; i < save.parts.Length; i++)
		{
			PartState item = new PartState(save.parts[i]);
			// 【确定性 id 2026-10-10】不再用 Random.Next()：同一枚零件在任何客户端、任何一次重建上
			// 都算出同一个编号。随机号会让 DestroyPart 的 PartId 到了对面变成另一个零件或不存在
			// （实测：destroy part ... not found ×12，且结构分叉双向）。
			int value = IDExtensions.DeterministicPartID(save.parts[i]);
			while (parts.ContainsKey(value)) value = value == int.MaxValue ? 1 : value + 1;
			parts.Add(value, item);
			partIndexToID.Add(i, value);
		}
		joints = save.joints.Select((JointSave joint) => new JointState(joint, partIndexToID)).ToList();
		stages = save.stages.Select((StageSave stage) => new StageState(stage, partIndexToID)).ToList();
	}

	public void UpdateRocketPrimary(Packet_UpdateRocketPrimary packet)
	{
		location = packet.Location;
		rotation = packet.Rotation;
		angularVelocity = packet.AngularVelocity;
	}

	public void UpdateRocketSecondary(Packet_UpdateRocketSecondary packet)
	{
		input_Turn = packet.Input_Turn;
		input_Raw = packet.Input_Raw;
		input_Horizontal = packet.Input_Horizontal;
		input_Vertical = packet.Input_Vertical;
		throttlePercent = packet.ThrottlePercent;
		throttleOn = packet.ThrottleOn;
		RCS = packet.RCS;
	}

	public bool RemovePart(int id)
	{
		joints.RemoveAll((JointState j) => j.id_A == id || j.id_B == id);
		foreach (StageState stage in stages)
		{
			stage.partIDs.RemoveAll((int p) => p == id);
		}
		return parts.Remove(id);
	}

	public void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedString(rocketName);
		msg.Write(location);
		msg.WriteCompressedFloat(rotation);
		msg.WriteCompressedFloat(angularVelocity);
		((NetBuffer)msg).Write(throttleOn);
		msg.WriteCompressedFloat(throttlePercent);
		((NetBuffer)msg).Write(RCS);
		msg.WriteCollection(parts, delegate(KeyValuePair<int, PartState> kvp)
		{
			msg.WriteCompressedInt(kvp.Key);
			msg.Write(kvp.Value);
		});
		msg.WriteCollection(joints, msg.Write);
		msg.WriteCollection(stages, msg.Write);
	}

	public void Deserialize(NetIncomingMessage msg)
	{
		rocketName = msg.ReadCompressedString();
		location = msg.Read<NetLocation>();
		rotation = msg.ReadCompressedFloat();
		angularVelocity = msg.ReadCompressedFloat();
		throttleOn = ((NetBuffer)msg).ReadBoolean();
		throttlePercent = msg.ReadCompressedFloat();
		RCS = ((NetBuffer)msg).ReadBoolean();
		parts = msg.ReadCollection((int count) => new Dictionary<int, PartState>(), () => new KeyValuePair<int, PartState>(msg.ReadCompressedInt(), msg.Read<PartState>()));
		joints = msg.ReadCollection((int count) => new List<JointState>(count), () => msg.Read<JointState>());
		stages = msg.ReadCollection((int count) => new List<StageState>(count), () => msg.Read<StageState>());
	}
}
