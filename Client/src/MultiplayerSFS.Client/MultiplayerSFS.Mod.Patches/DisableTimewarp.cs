// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using HarmonyLib;
using MultiplayerSFS.Mod;
using SFS.UI;
using SFS.World;
using SFS.World.Maps;

namespace MultiplayerSFS.Mod.Patches;

public class DisableTimewarp
{
	// 多人（在线 > 1）：只允许 1~5 倍，与服务端 kPersonalTimeScaleMax 一致。
	// 单人：放开到 2500 倍（老 C# TimeWarpControlRules.MaximumMultiplier = 2500）。
	// 判据必须和服务端 HandleTimeWarp 用同一条规则（在线人数 > 1），否则会出现
	// "客户端请求 100x、服务端按个人规则拒绝"这种自相矛盾。
	private static readonly double[] Multipliers = { 1, 2, 3, 5 };
	private static readonly double[] SoloMultipliers = { 1, 2, 3, 5, 25, 100, 500, 2500 };

	private static double[] ActiveMultipliers()
	{
		return LocalManager.players != null && LocalManager.players.Count > 1
			? Multipliers
			: SoloMultipliers;
	}

	// 【必须读"本地真实倍率"，不能读 world.timeScale】
	// 个人加速模式下同步时钟（ClientManager.world）恒为 1x —— 服务端时钟本来就没动。
	// 以前这里读 world.timeScale，改成个人模式后它永远是 1，于是：
	//   按加速 -> 永远只到 2x；按减速 -> 直接回 1x，档位完全走不动。
	// WorldTime.main.timewarpSpeed 才是玩家实际在跑的倍率。
	private static double CurrentLocalSpeed()
	{
		return WorldTime.main != null ? WorldTime.main.timewarpSpeed : 1.0;
	}

	private static double NextMultiplier()
	{
		double current = CurrentLocalSpeed();
		double[] table = ActiveMultipliers();
		foreach (double multiplier in table)
			if (multiplier > current + 0.0001) return multiplier;
		return table[table.Length - 1];   // 已到顶：返回本模式的上限，而不是写死的 2500
	}

	private static double PreviousMultiplier()
	{
		double current = CurrentLocalSpeed();
		double[] table = ActiveMultipliers();
		for (int i = table.Length - 1; i >= 0; i--)
			if (table[i] < current - 0.0001) return table[i];
		return 1;
	}
	[HarmonyPatch(typeof(WorldTime), "AccelerateTime")]
	public class WorldTime_AccelerateTime
	{
		public static bool Prefix()
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ClientManager.RequestTimeScale(NextMultiplier());
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(WorldTime), "DecelerateTime")]
	public class WorldTime_DecelerateTime
	{
		public static bool Prefix()
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ClientManager.RequestTimeScale(PreviousMultiplier());
				return false;
			}
			return true;
		}
	}

	// 【2026-10-07 解锁地图上的 Timewarp Here】
	// 以前这里是直接拦掉、提示玩家用普通加减速按钮。那是"全局加速时代"的做法：
	// 任何人都能把全服倍率拉起来，而这条路的倍率会走到轨道档（25/100/2500…），所以干脆封了。
	// 现在改成每人独立 1~5x，别人不受影响，没理由再封 —— 倍率上限由下面的
	// WorldTime_SetState_Clamp 兜住（卡在 SetState 这个所有倍率变化的最终落点上）。
	//
	// 注意：**不要再加回"直接 return false 拦掉"**。要限制就限幅，不要禁用 ——
	// 拦掉会让"到点自动停"这个有用的功能一起消失，而且玩家点了没反应（用户实测就是这么反馈的）。

	// 多人时把时间加速硬限制在游戏自己的"物理档"上限内（物理档就是 {1,2,3,5}）。
	//
	// 为什么卡在 SetState 而不是 ApplyState：SetState 是**所有**倍率变化的最终落点
	//（ApplyState 内部也只是转调它），一处全覆盖 —— 原生键位、地图自动加速、我们自己的回执
	// 全都要经过这里，不会漏。
	//
	// 为什么把 realtimePhysics 一并置 true：≤5 必须走物理档才和倍率自洽
	//（轨道档的 FixedDeltaTime 语义不同，会给 rails 用）。
	[HarmonyPatch(typeof(WorldTime), "SetState")]
	public class WorldTime_SetState_Clamp
	{
		private const double MultiplayerMaxSpeed = 5.0;
		private static bool warnedClamp;

		// __0 = timewarpSpeed, __1 = realtimePhysics（用位置名，避免依赖参数名）
		public static void Prefix(ref double __0, ref bool __1)
		{
			if (!ClientManager.multiplayerEnabled.Value) return;
			if (__0 <= MultiplayerMaxSpeed) return;

			if (!warnedClamp)
			{
				warnedClamp = true;
				UnityEngine.Debug.Log("[SFS-MP] timewarp speed clamped to " + MultiplayerMaxSpeed
					+ "x (multiplayer limit).");
			}
			__0 = MultiplayerMaxSpeed;
			__1 = true;
		}
	}
}
