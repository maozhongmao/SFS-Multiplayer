// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace MultiplayerSFS.Common.Transport.Kcp
{
	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 这里集中定义与 ikcp.c / ikcp.h 完全一致的协议常量。数值一旦改动就会破坏与 C 版服务端的互通性，
	/// 因此每一项都标注了在 ikcp.c 中的对应名字。
	/// </summary>
	public static class KcpConstants
	{
		/// <summary>
		/// KCP 包头长度（字节，对应 IKCP_OVERHEAD）：conv(4) + cmd(1) + frg(1) + wnd(2) + ts(4) + sn(4) + una(4) + len(4)。
		/// 所有多字节字段均为小端（LSB）编码。
		/// </summary>
		public const uint Overhead = 24;

		/// <summary>nodelay 模式下的最小 RTO（毫秒），对应 IKCP_RTO_NDL。</summary>
		public const uint RtoNdl = 30;

		/// <summary>常规模式下的最小 RTO（毫秒），对应 IKCP_RTO_MIN。</summary>
		public const uint RtoMin = 100;

		/// <summary>默认 RTO（毫秒），对应 IKCP_RTO_DEF。</summary>
		public const uint RtoDef = 200;

		/// <summary>RTO 上限（毫秒），对应 IKCP_RTO_MAX。</summary>
		public const uint RtoMax = 60000;

		/// <summary>cmd：推送数据，对应 IKCP_CMD_PUSH。</summary>
		public const uint CmdPush = 81;

		/// <summary>cmd：确认，对应 IKCP_CMD_ACK。</summary>
		public const uint CmdAck = 82;

		/// <summary>cmd：窗口探测（询问对端窗口），对应 IKCP_CMD_WASK。</summary>
		public const uint CmdWask = 83;

		/// <summary>cmd：窗口通告（告知本端窗口），对应 IKCP_CMD_WINS。</summary>
		public const uint CmdWins = 84;

		/// <summary>probe 标志位：需要发送 IKCP_CMD_WASK，对应 IKCP_ASK_SEND。</summary>
		public const uint AskSend = 1;

		/// <summary>probe 标志位：需要发送 IKCP_CMD_WINS，对应 IKCP_ASK_TELL。</summary>
		public const uint AskTell = 2;

		/// <summary>默认发送窗口，对应 IKCP_WND_SND。</summary>
		public const uint WndSnd = 32;

		/// <summary>默认接收窗口（必须不小于最大分片数），对应 IKCP_WND_RCV。</summary>
		public const uint WndRcv = 128;

		/// <summary>默认 MTU，对应 IKCP_MTU_DEF。</summary>
		public const uint MtuDef = 1400;

		/// <summary>快速重传阈值常量，对应 IKCP_ACK_FAST（本 fork 的算法里未直接使用，仅保持常量一致）。</summary>
		public const uint AckFast = 3;

		/// <summary>内部刷新间隔，对应 IKCP_INTERVAL。</summary>
		public const uint Interval = 100;

		/// <summary>死链判定：累计重传次数上限，对应 IKCP_DEADLINK。</summary>
		public const uint DeadLink = 20;

		/// <summary>慢启动初始阈值，对应 IKCP_THRESH_INIT。</summary>
		public const uint ThreshInit = 2;

		/// <summary>阈值下限，对应 IKCP_THRESH_MIN。</summary>
		public const uint ThreshMin = 2;

		/// <summary>首次窗口探测间隔（毫秒），对应 IKCP_PROBE_INIT。</summary>
		public const uint ProbeInit = 5000;

		/// <summary>窗口探测间隔上限（毫秒），对应 IKCP_PROBE_LIMIT。</summary>
		public const uint ProbeLimit = 120000;

		/// <summary>快速重传次数上限的默认值，对应 IKCP_FASTACK_LIMIT。</summary>
		public const uint FastackLimit = 5;

		/// <summary>
		/// 服务端 ikcp.c 顶部定义了 <c>#define IKCP_FASTACK_CONSERVE</c>，即快速重传的 fastack 计数采用保守策略：
		/// 只有 ACK 携带的 ts 不早于当前分段 ts 时才累加计数，同时 maxack 的推进也要求 ts 更新。
		/// 本重写固定实现该策略（本常量仅作说明用，不参与运行时判断）。
		/// </summary>
		public const uint FastackConserve = 1;
	}
}
