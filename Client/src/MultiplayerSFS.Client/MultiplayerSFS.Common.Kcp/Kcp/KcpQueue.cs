// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。

using System;

namespace MultiplayerSFS.Common.Transport.Kcp
{
    /// <summary>
    /// 双向链表队列，模拟 C 版的 iqueue_head
    /// 用于管理 snd_queue, rcv_queue, snd_buf, rcv_buf
    /// </summary>
    public class KcpQueue
    {
        // 哨兵节点，自身指向自身表示空队列
        public KcpSegment Head { get; private set; }
        public int Count { get; internal set; }

        public KcpQueue()
        {
            Head = new KcpSegment();
            Head.Next = Head;
            Head.Prev = Head;
            Count = 0;
        }

        /// <summary>
        /// 队列是否为空
        /// </summary>
        public bool IsEmpty => Head.Next == Head;

        /// <summary>
        /// 在头部后插入节点
        /// </summary>
        public void AddAfterHead(KcpSegment node)
        {
            node.Next = Head.Next;
            node.Prev = Head;
            Head.Next.Prev = node;
            Head.Next = node;
            Count++;
        }

        /// <summary>
        /// 在尾部前插入节点
        /// </summary>
        public void AddBeforeTail(KcpSegment node)
        {
            node.Next = Head;
            node.Prev = Head.Prev;
            Head.Prev.Next = node;
            Head.Prev = node;
            Count++;
        }

        /// <summary>
        /// 移除指定节点
        /// </summary>
        public void Remove(KcpSegment node)
        {
            node.Prev.Next = node.Next;
            node.Next.Prev = node.Prev;
            node.Next = null;
            node.Prev = null;
            Count--;
        }

        /// <summary>
        /// 移除并返回头部后的第一个节点
        /// </summary>
        public KcpSegment PopFront()
        {
            if (IsEmpty) return null;
            var node = Head.Next;
            Remove(node);
            return node;
        }

        /// <summary>
        /// 移除并返回尾部前的最后一个节点
        /// </summary>
        public KcpSegment PopBack()
        {
            if (IsEmpty) return null;
            var node = Head.Prev;
            Remove(node);
            return node;
        }

        /// <summary>
        /// 获取头部后的第一个节点（不移除）
        /// </summary>
        public KcpSegment PeekFront()
        {
            return IsEmpty ? null : Head.Next;
        }

        /// <summary>
        /// 获取尾部前的最后一个节点（不移除）
        /// </summary>
        public KcpSegment PeekBack()
        {
            return IsEmpty ? null : Head.Prev;
        }

        /// <summary>
        /// 在指定节点前插入新节点
        /// </summary>
        public void InsertBefore(KcpSegment pos, KcpSegment node)
        {
            node.Next = pos;
            node.Prev = pos.Prev;
            pos.Prev.Next = node;
            pos.Prev = node;
            Count++;
        }

        /// <summary>
        /// 清空队列（不释放节点内存，由调用者处理）
        /// </summary>
        public void Clear()
        {
            Head.Next = Head;
            Head.Prev = Head;
            Count = 0;
        }
    }
}