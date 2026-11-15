// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace MultiplayerSFS.Common.Transport.Kcp
{
	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 对应 C 版 ikcp.c 里的 IQUEUEHEAD 侵入式双向链表（snd_queue / rcv_queue / snd_buf / rcv_buf）。
	/// 语义逐条对齐：
	/// <list type="bullet">
	/// <item>AddTail 对应 iqueue_add_tail；</item>
	/// <item>AddFirst 对应在队首哨兵后插入，即 C 版 <c>iqueue_add(node, head)</c>；</item>
	/// <item>InsertAfter 对应 ikcp_parse_data 里 <c>iqueue_add(&amp;seg-&gt;node, p)</c> 的"插到 p 之后"；</item>
	/// <item>Count 取代 C 版的 nrcv_buf / nsnd_buf / nrcv_que / nsnd_que 四个手工维护的计数器。</item>
	/// </list>
	/// 链表本身不包含任何定时逻辑，遍历时请像 C 版一样在删除前先取出下一个节点。
	/// </summary>
	internal sealed class KcpSegmentList
	{
		/// <summary>队首（空队列时为 null）。</summary>
		public KcpSegment First { get; private set; }

		/// <summary>队尾（空队列时为 null）。</summary>
		public KcpSegment Last { get; private set; }

		/// <summary>节点个数。</summary>
		public int Count { get; private set; }

		/// <summary>是否为空。</summary>
		public bool IsEmpty
		{
			get { return First == null; }
		}

		/// <summary>把分段插到队首。</summary>
		public void AddFirst(KcpSegment seg)
		{
			seg.Prev = null;
			seg.Next = First;
			if (First != null)
			{
				First.Prev = seg;
			}
			else
			{
				Last = seg;
			}
			First = seg;
			Count++;
		}

		/// <summary>把分段追加到队尾（对应 iqueue_add_tail）。</summary>
		public void AddTail(KcpSegment seg)
		{
			seg.Next = null;
			seg.Prev = Last;
			if (Last != null)
			{
				Last.Next = seg;
			}
			else
			{
				First = seg;
			}
			Last = seg;
			Count++;
		}

		/// <summary>把分段插到指定节点之后（pos 必须属于本链表）。</summary>
		public void InsertAfter(KcpSegment pos, KcpSegment seg)
		{
			seg.Prev = pos;
			seg.Next = pos.Next;
			if (pos.Next != null)
			{
				pos.Next.Prev = seg;
			}
			else
			{
				Last = seg;
			}
			pos.Next = seg;
			Count++;
		}

		/// <summary>摘除节点（对应 iqueue_del；节点自身仍可用，只是不再挂在链表上）。</summary>
		public void Remove(KcpSegment seg)
		{
			if (seg.Prev != null)
			{
				seg.Prev.Next = seg.Next;
			}
			else
			{
				First = seg.Next;
			}

			if (seg.Next != null)
			{
				seg.Next.Prev = seg.Prev;
			}
			else
			{
				Last = seg.Prev;
			}

			seg.Prev = null;
			seg.Next = null;
			Count--;
		}

		/// <summary>清空链表（等价于 C 版 release 时逐个摘除）。</summary>
		public void Clear()
		{
			First = null;
			Last = null;
			Count = 0;
		}
	}
}
