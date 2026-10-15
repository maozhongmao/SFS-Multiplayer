// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace MultiplayerSFS.Common;

public static class RocketSyncPolicy
{
	public const int ControlledIntervalMilliseconds = 50;
	public const int UncontrolledMovingIntervalMilliseconds = 200;
	// 空闲火箭的同步间隔。回归测试（ClientRegressionTests: RocketSyncPolicyLowersIdleTraffic）
	// 的规格是 1000ms；此前被"收紧插值"改动改成 500 后没回退，导致空闲流量翻倍。
	public const int IdleIntervalMilliseconds = 1000;

	public static int GetIntervalMilliseconds(bool controlled, bool moving)
	{
		return GetIntervalMilliseconds(controlled, moving, null);
	}

	// 每枚火箭一个固定的发包相位偏移（0 .. interval），用来打散发送时刻。
	// 为什么需要：世界刚进场时所有火箭都在同一帧首次发包，之后各自按 interval 递推，
	// 相位就永久对齐了 —— 表现为"每隔一段时间所有火箭的状态包挤在同一帧发出"，
	// 接收端于是周期性突发、队列忽深忽浅，自适应档位也跟着抖。
	// 用 rocketId 做确定性映射：不需要存状态，重启/重连后相位仍然稳定。
	public static double PhaseOffsetMilliseconds(int rocketId, int intervalMilliseconds)
	{
		if (intervalMilliseconds <= 1) return 0.0;
		// 黄金分割比散列：相邻 id 的相位拉得足够开，不会退化成"每 N 枚撞一次"。
		unchecked
		{
			uint h = (uint)rocketId * 2654435761u;
			h ^= h >> 15;
			return (h % 1000u) / 1000.0 * intervalMilliseconds;
		}
	}

	public static int GetIntervalMilliseconds(bool controlled, bool moving, NetworkAdaptiveProfile profile)
	{
		if (profile == null)
		{
			if (controlled) return ControlledIntervalMilliseconds;
			return moving ? UncontrolledMovingIntervalMilliseconds : IdleIntervalMilliseconds;
		}
		if (controlled) return profile.ControlledIntervalMilliseconds;
		return moving ? profile.MovingIntervalMilliseconds : profile.IdleIntervalMilliseconds;
	}
}
