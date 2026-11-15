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
        /// <summary>
        /// 刷新待发送数据
        /// </summary>
        public void Flush()
        {
            int ptr = 0;
            uint cwnd = Math.Min(SndWnd, RmtWnd);
            if (NoCwnd == 0) cwnd = Math.Min(Cwnd, cwnd);

            // 发送 ACK
            for (int i = 0; i < _ackList.Count; i++)
            {
                if (ptr + KcpConst.Overhead > Mtu)
                {
                    OutputBuffer(ptr);
                    ptr = 0;
                }

                var ackSeg = new KcpSegment();
                ackSeg.Conv = Conv;
                ackSeg.Cmd = KcpCmd.Ack;
                ackSeg.Frg = 0;
                ackSeg.Wnd = (uint)Math.Max(0, (int)RcvWnd - RcvQueue.Count);
                ackSeg.Una = RcvNxt;
                ackSeg.Len = 0;
                ackSeg.Sn = _ackList[i];
                ackSeg.Ts = _ackTsList[i];

                ptr = KcpEncode.EncodeSegment(_buffer, ptr, ackSeg);
            }
            _ackList.Clear();
            _ackTsList.Clear();

            // 窗口探测
            if (RmtWnd == 0)
            {
                if (ProbeWait == 0)
                {
                    ProbeWait = KcpConst.ProbeInit;
                    TsProbe = Current + ProbeWait;
                }
                else if ((int)(Current - TsProbe) >= 0)
                {
                    if (ProbeWait < KcpConst.ProbeInit) ProbeWait = KcpConst.ProbeInit;
                    ProbeWait += ProbeWait / 2;
                    if (ProbeWait > KcpConst.ProbeLimit) ProbeWait = KcpConst.ProbeLimit;
                    TsProbe = Current + ProbeWait;
                    Probe |= KcpConst.AskSend;
                }
            }
            else
            {
                TsProbe = 0;
                ProbeWait = 0;
            }

            if ((Probe & KcpConst.AskSend) != 0)
            {
                if (ptr + KcpConst.Overhead > Mtu) { OutputBuffer(ptr); ptr = 0; }
                var probeSeg = new KcpSegment { Conv = Conv, Cmd = KcpCmd.Wask, Frg = 0, Wnd = 0, Una = RcvNxt, Len = 0 };
                ptr = KcpEncode.EncodeSegment(_buffer, ptr, probeSeg);
            }

            if ((Probe & KcpConst.AskTell) != 0)
            {
                if (ptr + KcpConst.Overhead > Mtu) { OutputBuffer(ptr); ptr = 0; }
                var probeSeg = new KcpSegment { Conv = Conv, Cmd = KcpCmd.Wins, Frg = 0, Wnd = 0, Una = RcvNxt, Len = 0 };
                ptr = KcpEncode.EncodeSegment(_buffer, ptr, probeSeg);
            }
            Probe = 0;

            // 移动数据从 snd_queue 到 snd_buf
            while ((int)(SndNxt - (SndUna + cwnd)) < 0)
            {
                if (SndQueue.IsEmpty) break;
                var newSeg = SndQueue.PopFront();
                SndBuf.AddBeforeTail(newSeg);

                newSeg.Conv = Conv;
                newSeg.Cmd = KcpCmd.Push;
                newSeg.Wnd = (uint)Math.Max(0, (int)RcvWnd - RcvQueue.Count);
                newSeg.Ts = Current;
                newSeg.Sn = SndNxt++;
                newSeg.Una = RcvNxt;
                newSeg.ResendTs = Current;
                newSeg.Rto = RxRto;
                newSeg.FastAck = 0;
                newSeg.Xmit = 0;
            }

            // 计算重传参数
            uint resent = FastResend > 0 ? (uint)FastResend : uint.MaxValue;
            uint rtomin = NoDelayMode == 0 ? (RxRto >> 3) : 0;

            bool change = false;
            bool lost = false;

            // 发送数据段
            var p = SndBuf.Head.Next;
            while (p != SndBuf.Head)
            {
                var seg = p;
                p = p.Next;
                bool needsend = false;

                if (seg.Xmit == 0)
                {
                    needsend = true;
                    seg.Xmit++;
                    seg.Rto = RxRto;
                    seg.ResendTs = Current + seg.Rto + rtomin;
                }
                else if ((int)(Current - seg.ResendTs) >= 0)
                {
                    needsend = true;
                    seg.Xmit++;
                    Xmit++;
                    if (NoDelayMode == 0)
                        seg.Rto += Math.Max(seg.Rto, RxRto);
                    else
                    {
                        int step = NoDelayMode < 2 ? (int)seg.Rto : (int)RxRto;
                        seg.Rto += (uint)(step / 2);
                    }
                    seg.ResendTs = Current + seg.Rto;
                    lost = true;
                }
                else if (seg.FastAck >= resent)
                {
                    if (seg.Xmit <= FastLimit || FastLimit <= 0)
                    {
                        needsend = true;
                        seg.Xmit++;
                        seg.FastAck = 0;
                        seg.ResendTs = Current + seg.Rto;
                        change = true;
                    }
                }

                if (needsend)
                {
                    seg.Ts = Current;
                    seg.Wnd = (uint)Math.Max(0, (int)RcvWnd - RcvQueue.Count);
                    seg.Una = RcvNxt;

                    int need = (int)KcpConst.Overhead + (int)seg.Len;
                    if (ptr + need > Mtu)
                    {
                        OutputBuffer(ptr);
                        ptr = 0;
                    }

                    ptr = KcpEncode.EncodeSegment(_buffer, ptr, seg);
                    if (seg.Len > 0)
                    {
                        Buffer.BlockCopy(seg.Data, 0, _buffer, ptr, (int)seg.Len);
                        ptr += (int)seg.Len;
                    }

                    if (seg.Xmit >= DeadLink) State = uint.MaxValue;
                }
            }

            if (ptr > 0) OutputBuffer(ptr);

            // 更新 ssthresh
            if (change)
            {
                uint inflight = SndNxt - SndUna;
                Ssthresh = inflight / 2;
                if (Ssthresh < KcpConst.ThreshMin) Ssthresh = KcpConst.ThreshMin;
                Cwnd = Ssthresh + resent;
                Incr = Cwnd * Mss;
            }

            if (lost)
            {
                Ssthresh = Cwnd / 2;
                if (Ssthresh < KcpConst.ThreshMin) Ssthresh = KcpConst.ThreshMin;
                Cwnd = 1;
                Incr = Mss;
            }

            if (Cwnd < 1) { Cwnd = 1; Incr = Mss; }
        }
    }
}