// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common.Transport.Kcp
{
	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 对应 ikcp.c 的 acklist / ackcount / ackblock：待发送 ACK 的 (sn, ts) 队列。
	/// 增长策略与 C 版一致：容量不足时按 2 的幂扩容（最小 8），每个条目占 2 个 uint（sn、ts）。
	/// </summary>
	internal sealed class KcpAckList
	{
		private uint[] buffer;
		private int block;

		/// <summary>当前待发送的 ACK 条数（对应 ackcount）。</summary>
		public int Count { get; private set; }

		/// <summary>追加一个待发送的 ACK（对应 ikcp_ack_push）。</summary>
		public void Push(uint sn, uint ts)
		{
			int newsize = Count + 1;
			if (newsize > block)
			{
				int newblock = 8;
				while (newblock < newsize)
				{
					newblock <<= 1;
				}

				uint[] acklist = new uint[newblock * 2];
				if (buffer != null)
				{
					Array.Copy(buffer, acklist, Count * 2);
				}

				buffer = acklist;
				block = newblock;
			}

			buffer[Count * 2] = sn;
			buffer[Count * 2 + 1] = ts;
			Count++;
		}

		/// <summary>读取第 index 条 ACK（对应 ikcp_ack_get）。</summary>
		public void Get(int index, out uint sn, out uint ts)
		{
			sn = buffer[index * 2];
			ts = buffer[index * 2 + 1];
		}

		/// <summary>清空待发送 ACK（对应 flush 里的 ackcount = 0，容量保留）。</summary>
		public void Clear()
		{
			Count = 0;
		}
	}
}
