// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。

using System;
using System.Collections.Generic;

namespace MultiplayerSFS.Common.Transport.Kcp
{
    public partial class KcpContext
    {
        private void OutputBuffer(int len)
        {
            var data = new byte[len];
            Buffer.BlockCopy(_buffer, 0, data, 0, len);
            _output(data, this);
        }

        private void UpdateAck(uint rtt)
        {
            if (RxSrtt == 0)
            {
                RxSrtt = rtt;
                RxRttval = (int)(rtt / 2);
            }
            else
            {
                int delta = (int)rtt - (int)RxSrtt;
                if (delta < 0) delta = -delta;
                RxRttval = (3 * RxRttval + delta) / 4;
                RxSrtt = (7 * RxSrtt + rtt) / 8;
                if (RxSrtt < 1) RxSrtt = 1;
            }
            uint rto = RxSrtt + KcpMath.MaxUInt(Interval, (uint)(4 * RxRttval));
            RxRto = KcpMath.Clamp(rto, RxMinrto, KcpConst.RtoMax);
        }

        private void ShrinkBuf()
        {
            if (!SndBuf.IsEmpty)
            {
                var seg = SndBuf.PeekFront();
                SndUna = seg.Sn;
            }
            else
            {
                SndUna = SndNxt;
            }
        }

        private void ParseAck(uint sn)
        {
            if ((int)(sn - SndUna) < 0 || (int)(sn - SndNxt) >= 0) return;

            var p = SndBuf.Head.Next;
            while (p != SndBuf.Head)
            {
                var seg = p;
                p = p.Next;
                if (sn == seg.Sn)
                {
                    SndBuf.Remove(seg);
                    return;
                }
                if ((int)(sn - seg.Sn) < 0) break;
            }
        }

        private void ParseUna(uint una)
        {
            var p = SndBuf.Head.Next;
            while (p != SndBuf.Head)
            {
                var seg = p;
                p = p.Next;
                if ((int)(una - seg.Sn) > 0)
                {
                    SndBuf.Remove(seg);
                }
                else break;
            }
        }

        private void ParseFastAck(uint sn, uint ts)
        {
            if ((int)(sn - SndUna) < 0 || (int)(sn - SndNxt) >= 0) return;

            var p = SndBuf.Head.Next;
            while (p != SndBuf.Head)
            {
                var seg = p;
                p = p.Next;
                if ((int)(sn - seg.Sn) < 0) break;
                if (sn != seg.Sn)
                {
                    if ((int)(ts - seg.Ts) >= 0)
                        seg.FastAck++;
                }
            }
        }

        private void AckPush(uint sn, uint ts)
        {
            _ackList.Add(sn);
            _ackTsList.Add(ts);
        }

        private void ParseData(KcpSegment seg)
        {
            uint sn = seg.Sn;

            // 超出接收窗口
            if ((int)(sn - (RcvNxt + RcvWnd)) >= 0 || (int)(sn - RcvNxt) < 0)
                return;

            // 查找插入位置（按序列号排序）
            var p = RcvBuf.Head.Prev;
            while (p != RcvBuf.Head)
            {
                if (p.Sn == sn) return; // 重复包
                if ((int)(sn - p.Sn) > 0) break;
                p = p.Prev;
            }

            // 插入到 p 之后
            var next = p.Next;
            p.Next = seg;
            seg.Prev = p;
            seg.Next = next;
            next.Prev = seg;
            RcvBuf.Count++;

            // 移动可用数据到 rcv_queue
            MoveRcvBufToQueue();
        }

        private void MoveRcvBufToQueue()
        {
            while (!RcvBuf.IsEmpty)
            {
                var seg = RcvBuf.PeekFront();
                if (seg.Sn == RcvNxt && RcvQueue.Count < RcvWnd)
                {
                    RcvBuf.Remove(seg);
                    RcvQueue.AddBeforeTail(seg);
                    RcvNxt++;
                }
                else break;
            }
        }

        public void Dispose()
        {
            // 清理队列
            ClearQueue(SndBuf);
            ClearQueue(RcvBuf);
            ClearQueue(SndQueue);
            ClearQueue(RcvQueue);
            _ackList.Clear();
            _ackTsList.Clear();
            _buffer = null;
        }

        private void ClearQueue(KcpQueue q)
        {
            while (!q.IsEmpty)
            {
                q.PopFront();
            }
        }
    }
}