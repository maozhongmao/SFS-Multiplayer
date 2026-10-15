// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common;

public enum NetworkQuality
{
	Excellent,
	Good,
	Fair,
	Poor
}

public sealed class NetworkAdaptiveProfile
{
	public NetworkQuality Quality { get; set; }
	public int ControlledIntervalMilliseconds { get; set; }
	public int MovingIntervalMilliseconds { get; set; }
	public int IdleIntervalMilliseconds { get; set; }
	public int ValidationIntervalMilliseconds { get; set; }
	public double InterpolationDelaySeconds { get; set; }
	public double MaximumExtrapolationSeconds { get; set; }
	public double CorrectionSeconds { get; set; }
}

public static class NetworkAdaptationPolicy
{
	// 【2026-10-01 修正：不再用 queueCount 判链路质量】
	// queueCount 来自 TcpTransportCore.QueueCount = KCP 的 WaitSnd，也就是【本端还有多少数据
	// 排队等着发/等确认】—— 它量的是我们自己的发送量，不是链路质量。
	// 后果：场上火箭越多，每个 tick 要发的状态包越多，WaitSnd 越大，档位就被自己顶到 Poor，
	// 插值延迟从 0.12s 自己涨到 0.48s、发包间隔从 50ms 涨到 200ms。
	// 现场表现就是"一开始同步各枚火箭的位置，延迟就明显变大"；而且是正反馈：
	// 间隔翻倍 -> 发送更突发 -> 积压更深 -> 档位更差。
	// 真正反映链路的是 RTT 与抖动（丢包会通过 KCP 重传体现为 RTT 上升），所以只用这两个。
	// queueCount 仍然保留在参数里并显示在 F8 面板上，但不再参与判档。
	public static NetworkAdaptiveProfile Evaluate(double roundTripMs, double jitterMs, int queueCount)
	{
		_ = queueCount;   // 仅诊断用，见上方说明
		NetworkQuality quality;
		if (roundTripMs >= 300 || jitterMs >= 80) quality = NetworkQuality.Poor;
		else if (roundTripMs >= 180 || jitterMs >= 50) quality = NetworkQuality.Fair;
		else if (roundTripMs >= 90 || jitterMs >= 20) quality = NetworkQuality.Good;
		else quality = NetworkQuality.Excellent;

		switch (quality)
		{
		// 【2026-10-01 提频】用户明确要求"提高一些更新频率"，所以把好链路的发包间隔抬上去。
		// 只动 Excellent / Good 两档：Fair / Poor 是链路真的差，加频只会更堵。
		// 插值延迟(delay)与纠偏(correction)保持原值 —— 这次只动频率，不碰平滑手感。
		// 回退记录：drift-tight 版（阈值0.45/纠偏0.15）实测比松参数更差，已回退到 p2p-resync 版参数。
		// 若实机出现抖动/闪烁，先把 controlled 回到 50 再谈别的。
		case NetworkQuality.Excellent:
			return Create(quality, 33, 100, 500, 750, 0.12, 1.2, 0.45);
		case NetworkQuality.Good:
			return Create(quality, 50, 150, 750, 1200, 0.20, 2.0, 0.70);
		case NetworkQuality.Fair:
			return Create(quality, 100, 350, 2000, 2000, 0.32, 3.0, 1.00);
		default:
			return Create(quality, 200, 500, 3000, 3500, 0.48, 4.0, 1.40);
		}
	}

	private static NetworkAdaptiveProfile Create(NetworkQuality quality, int controlled, int moving,
		int idle, int validation, double delay, double extrapolation, double correction)
	{
		return new NetworkAdaptiveProfile
		{
			Quality = quality,
			ControlledIntervalMilliseconds = controlled,
			MovingIntervalMilliseconds = moving,
			IdleIntervalMilliseconds = idle,
			ValidationIntervalMilliseconds = validation,
			InterpolationDelaySeconds = delay,
			MaximumExtrapolationSeconds = extrapolation,
			CorrectionSeconds = correction
		};
	}
}
