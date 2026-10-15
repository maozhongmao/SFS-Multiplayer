// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.Linq;
using MultiplayerSFS.Common;
using SFS.Parts;
using SFS.World;

namespace MultiplayerSFS.Mod;

public class LocalRocket
{
	public Rocket rocket;

	public Dictionary<int, Part> parts;

	// 最近一次对"本枚火箭参与的所有火箭对"应用忽略碰撞时的部件指纹（见 LocalManager.ApplyPairIgnore /
	// RocketColliderSignature）。指纹没变 = 零件没有增删 = 碰撞体集合没变，
	// 周期性重放可以整对跳过，不必再跑一遍 O(部件²) 次 Physics2D.IgnoreCollision。
	public long colliderSignature;

	public Interpolator interpolator;

	public LocalRocket(Rocket rocket)
	{
		this.rocket = rocket;
		parts = new Dictionary<int, Part>(rocket.partHolder.partsSet.Count);
		foreach (Part item in rocket.partHolder.partsSet)
		{
			// 【确定性 id】必须与 RocketState 那条路算出同一个号，否则本地又出现两套编号
			int id = IDExtensions.DeterministicPartID(new PartSave(item));
			while (parts.ContainsKey(id)) id = id == int.MaxValue ? 1 : id + 1;
			parts.Add(id, item);
		}
		interpolator = rocket.gameObject.AddComponent<Interpolator>();
		interpolator.rocket = this;
	}

	public LocalRocket(Rocket rocket, Dictionary<int, Part> parts)
	{
		this.rocket = rocket;
		this.parts = parts;
		interpolator = rocket.gameObject.AddComponent<Interpolator>();
		interpolator.rocket = this;
	}

	public RocketState ToState()
	{
		return new RocketState
		{
			rocketName = rocket.rocketName,
			location = rocket.location.Value.ToNetLocation(),
			rotation = rocket.rb2d.transform.eulerAngles.z,
			angularVelocity = rocket.rb2d.angularVelocity,
			throttleOn = rocket.throttle.throttleOn,
			throttlePercent = rocket.throttle.throttlePercent,
			RCS = rocket.arrowkeys.rcs,
			input_Turn = rocket.arrowkeys.turnAxis,
			input_Raw = rocket.arrowkeys.rawArrowkeysAxis,
			input_Horizontal = rocket.arrowkeys.horizontalAxis,
			input_Vertical = rocket.arrowkeys.verticalAxis,
			parts = parts.Where((KeyValuePair<int, Part> kvp) => kvp.Value != null).ToDictionary((KeyValuePair<int, Part> kvp) => kvp.Key, (KeyValuePair<int, Part> kvp) => new PartState(new PartSave(kvp.Value))),
			joints = (from pj in rocket.jointsGroup.joints
				where pj.a != null && pj.b != null
				select new JointState(GetPartID(pj.a), GetPartID(pj.b))).ToList(),
			stages = rocket.staging.stages.Select((Stage s) => new StageState(s.stageId, s.parts.Where((Part p) => p != null).Select(GetPartID).ToList())).ToList()
		};
	}

	// 【结构指纹修正 2026-10-09】parts 是"活体 GameObject 字典"，零件被摧毁后条目会留成 null。
	// ToState() 外发时会过滤 null，所以发给服务端的状态一直是对的；但 parts.Count 会把 null 也算进去，
	// 拿它跟服务端的零件数比就永远"对不上" —— 结果是每 8 秒白重建一次（75 部件 Instantiate、
	// 火箭瞬间"变空气"、插值被重置）。结构指纹必须只数活体。
	public int LivePartCount()
	{
		int count = 0;
		foreach (KeyValuePair<int, Part> kvp in parts)
		{
			if (kvp.Value != null) count++;
		}
		return count;
	}

	// 已摧毁但仍在字典里留下空条目的数量。对外发状态无影响（ToState() 会过滤），
	// 但它是"结构指纹被骗"的直接证据，所以要能单独量出来。
	public int DeadPartCount()
	{
		int count = 0;
		foreach (KeyValuePair<int, Part> kvp in parts)
		{
			if (kvp.Value == null) count++;
		}
		return count;
	}

	// 清掉已摧毁留下的空条目（只是卫生，不影响外发：ToState() 本来就过滤）。
	// 【任务 #9】返回值 = 本次真正清掉的空条目数（0 = 没有任何变化），调用方据此决定是否打日志。
	// 只删 null：取到 key 后删除前再确认一次当前值仍是 null，保证绝不误删活体零件 ——
	// 因此 LivePartCount() / idSetMismatch / ToState() 的结果都不受影响。
	public int PruneDestroyedParts()
	{
		if (parts == null || parts.Count == 0) return 0;
		List<int> dead = null;
		foreach (KeyValuePair<int, Part> kvp in parts)
		{
			if (kvp.Value == null)
			{
				if (dead == null) dead = new List<int>();
				dead.Add(kvp.Key);
			}
		}
		if (dead == null) return 0;
		int pruned = 0;
		for (int i = 0; i < dead.Count; i++)
		{
			// 删除前复核：只有仍然是 null 才删（防止同一 key 在这一瞬间又被填成活体）
			if (parts.TryGetValue(dead[i], out Part current) && current == null)
			{
				parts.Remove(dead[i]);
				pruned++;
			}
		}
		return pruned;
	}

	public int GetPartID(Part part)
	{
		KeyValuePair<int, Part> keyValuePair = parts.FirstOrDefault((KeyValuePair<int, Part> p) => p.Value == part);
		if (keyValuePair.Value != null)
		{
			return keyValuePair.Key;
		}
		return -1;
	}
}
