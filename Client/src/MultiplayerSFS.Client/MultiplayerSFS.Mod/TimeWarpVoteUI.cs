// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using MultiplayerSFS.Common;
using SFS.UI;
using SFS.World;

namespace MultiplayerSFS.Mod;

// 时间加速只负责接收服务端确认并同步本地世界状态；不创建 F7 弹窗或额外按钮。
//
// 【两种模式 —— 服务端按在线人数决定，客户端必须用同一条规则判断】
//   多人（在线 > 1）：个人加速 1~5x。服务端**不动自己的时钟**，只回给请求者本人。
//                     客户端因此只改**本地时钟的速率**，同步时钟保持 1x。
//   单人（在线 = 1）：全局加速。服务端时钟跟着变，客户端把两个时钟一起对齐。
//
// 【策略 ①：会话内不拉回】个人模式下**绝不写 WorldTime.main.worldTime**，只改速率。
// 否则退出加速那一刻会把本地时间重置回服务端（1x）的值，
// 玩家辛苦快进到火星的飞行会被拉回起点白飞 —— 这正是当初否掉"②退出即拉回"的原因。
//
// 这套语义来自老 C# 服务端（TimeWarpControlRules + TcpMultiplayerServer.HandleTimeWarp），
// 那版多人时就是"只回请求者、服务端时钟不动"，C++ 移植时丢了，这里恢复。
public static class TimeWarpVoteUI
{
	// 与服务端 HandleTimeWarp 的判据保持一致：在线人数 > 1 走个人模式。
	private static bool IsPersonalMode()
	{
		return LocalManager.players != null && LocalManager.players.Count > 1;
	}

	public static void Receive(Packet_TimeWarp packet)
	{
		if (packet.Operation == TimeWarpOperation.Applied)
			ApplyTimeScale(packet.Multiplier, packet.WorldTime);
		if (!string.IsNullOrEmpty(packet.Message) && MsgDrawer.main != null)
			MsgDrawer.main.Log(packet.Message);
	}

	private static void ApplyTimeScale(double multiplier, double worldTime)
	{
		bool personal = IsPersonalMode();

		if (ClientManager.client != null)
			worldTime += ClientManager.client.RoundTripMs / 2000.0 * multiplier;

		// 只有全局加速才让"同步时钟"跟着跑。个人加速时服务端时钟根本没动，
		// 同步时钟必须保持 1x，否则它会和服务端 world_time() 盖的章互相拉。
		if (!personal && ClientManager.world != null)
			ClientManager.world.SetTimeScale(multiplier, worldTime);

		if (WorldTime.main == null) return;

		// 全局：就地重置（服务端时钟一起变了）。个人：**绝不动 worldTime**，见策略 ①。
		if (!personal)
			WorldTime.main.worldTime = worldTime;

		WorldTime.main.SetState(multiplier, multiplier <= 5, false);
	}
}
