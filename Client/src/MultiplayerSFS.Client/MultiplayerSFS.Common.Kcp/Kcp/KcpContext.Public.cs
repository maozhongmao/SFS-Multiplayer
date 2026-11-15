// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。

using System;

namespace MultiplayerSFS.Common.Transport.Kcp
{
    public partial class KcpContext
    {
        /// <summary>
        /// 发送用户数据
        /// </summary>
        public int Send(byte[] buffer, int offset, int len)
        {
            if (len < 0) return -1;
            if (Mss == 0) return -2;

            int sent = 0;

            // 流模式：尝试追加到最后一个段
            if (Stream != 0 && !SndQueue.IsEmpty)
            {
                var lastSeg = SndQueue.PeekBack();
                if (lastSeg.Len < Mss)
                {
                    int capacity = (int)(Mss - lastSeg.Len);
                    int extend = Math.Min(len, capacity);
                    var newSeg = new KcpSegment((int)(lastSeg.Len + extend));
                    Buffer.BlockCopy(lastSeg.Data, 0, newSeg.Data, 0, (int)lastSeg.Len);
                    if (extend > 0)
                        Buffer.BlockCopy(buffer, offset, newSeg.Data, (int)lastSeg.Len, extend);
                    newSeg.Len = (uint)(lastSeg.Len + extend);
                    newSeg.Frg = 0;
                    SndQueue.Remove(lastSeg);
                    SndQueue.AddBeforeTail(newSeg);
                    sent = extend;
                    offset += extend;
                    len -= extend;
                }
            }

            if (len <= 0) return sent;

            int count = len <= (int)Mss ? 1 : (len + (int)Mss - 1) / (int)Mss;
            if (count >= KcpConst.WndRcv) return -2;
            if (count == 0) count = 1;

            // 分片发送
            for (int i = 0; i < count; i++)
            {
                int size = len > Mss ? (int)Mss : len;
                var seg = new KcpSegment(size);
                if (size > 0)
                    Buffer.BlockCopy(buffer, offset, seg.Data, 0, size);
                seg.Len = (uint)size;
                seg.Frg = Stream == 0 ? (uint)(count - i - 1) : 0;
                SndQueue.AddBeforeTail(seg);
                offset += size;
                len -= size;
                sent += size;
            }

            return sent;
        }

        /// <summary>
        /// 发送用户数据（简化重载）
        /// </summary>
        public int Send(byte[] data) => Send(data, 0, data.Length);

        /// <summary>
        /// 接收用户数据
        /// </summary>
        public int Recv(byte[] buffer, int offset, int len)
        {
            if (RcvQueue.IsEmpty) return -1;

            int peekSize = PeekSize();
            if (peekSize < 0) return -2;
            if (peekSize > len) return -3;

            bool recover = RcvQueue.Count >= RcvWnd;
            int totalLen = 0;

            // 合并分片
            while (!RcvQueue.IsEmpty)
            {
                var seg = RcvQueue.PeekFront();
                int copyLen = (int)Math.Min(seg.Len, len - totalLen);
                Buffer.BlockCopy(seg.Data, 0, buffer, offset + totalLen, copyLen);
                totalLen += copyLen;

                bool isLast = seg.Frg == 0;
                RcvQueue.Remove(seg);

                if (isLast) break;
            }

            // 从 rcv_buf 移动可用数据到 rcv_queue
            MoveRcvBufToQueue();

            // 快速恢复：通知对端窗口大小
            if (recover && RcvQueue.Count < RcvWnd)
                Probe |= KcpConst.AskTell;

            return totalLen;
        }

        /// <summary>
        /// 查看下一个消息的大小
        /// </summary>
        public int PeekSize()
        {
            if (RcvQueue.IsEmpty) return -1;
            var seg = RcvQueue.PeekFront();
            if (seg.Frg == 0) return (int)seg.Len;
            if (RcvQueue.Count < seg.Frg + 1) return -1;

            int len = 0;
            var cur = RcvQueue.Head.Next;
            while (cur != RcvQueue.Head)
            {
                len += (int)cur.Len;
                if (cur.Frg == 0) break;
                cur = cur.Next;
            }
            return len;
        }

        /// <summary>
        /// 获取待发送数据包数量
        /// </summary>
        public int WaitSnd => SndBuf.Count + SndQueue.Count;
    }
}