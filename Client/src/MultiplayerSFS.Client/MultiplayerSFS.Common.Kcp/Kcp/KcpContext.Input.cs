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
        /// 输入网络层数据
        /// </summary>
        public int Input(byte[] data, int offset, int size)
        {
            if (data == null || size < KcpConst.Overhead) return -1;

            uint prevUna = SndUna;
            uint prevNsndBuf = (uint)SndBuf.Count;
            uint maxAck = 0, latestTs = 0;
            int flag = 0;

            int pos = offset;
            int remaining = size;

            while (remaining >= KcpConst.Overhead)
            {
                if (remaining < KcpConst.Overhead) break;

                KcpSegment seg = new KcpSegment();
            KcpEncode.DecodeSegment(data, pos, seg);
                pos += (int)KcpConst.Overhead;
                remaining -= (int)KcpConst.Overhead;

                if (seg.Conv != Conv) return -1;
                if (remaining < seg.Len) return -2;
                if (seg.Cmd != KcpCmd.Push && seg.Cmd != KcpCmd.Ack &&
                    seg.Cmd != KcpCmd.Wask && seg.Cmd != KcpCmd.Wins)
                    return -3;

                RmtWnd = seg.Wnd;
                ParseUna(seg.Una);
                ShrinkBuf();

                if (seg.Cmd == KcpCmd.Ack)
                {
                    int rtt = (int)Current - (int)seg.Ts;
                    if (rtt >= 0) UpdateAck((uint)rtt);
                    ParseAck(seg.Sn);
                    ShrinkBuf();

                    if (flag == 0 || (int)(seg.Sn - maxAck) > 0)
                    {
                        flag = 1;
                        maxAck = seg.Sn;
                        latestTs = seg.Ts;
                    }
                }
                else if (seg.Cmd == KcpCmd.Push)
                {
                    if ((int)(seg.Sn - (RcvNxt + RcvWnd)) < 0)
                    {
                        AckPush(seg.Sn, seg.Ts);
                        if ((int)(seg.Sn - RcvNxt) >= 0)
                        {
                            var newSeg = new KcpSegment((int)seg.Len);
                            Buffer.BlockCopy(data, pos, newSeg.Data, 0, (int)seg.Len);
                            newSeg.Conv = seg.Conv;
                            newSeg.Cmd = seg.Cmd;
                            newSeg.Frg = seg.Frg;
                            newSeg.Wnd = seg.Wnd;
                            newSeg.Ts = seg.Ts;
                            newSeg.Sn = seg.Sn;
                            newSeg.Una = seg.Una;
                            newSeg.Len = seg.Len;
                            ParseData(newSeg);
                        }
                    }
                }
                else if (seg.Cmd == KcpCmd.Wask)
                {
                    Probe |= KcpConst.AskTell;
                }
                else if (seg.Cmd == KcpCmd.Wins)
                {
                    // do nothing
                }

                pos += (int)seg.Len;
                remaining -= (int)seg.Len;
            }

            if (flag != 0)
                ParseFastAck(maxAck, latestTs);

            if ((int)(SndUna - prevUna) > 0)
            {
                uint ackedSegs = SndUna - prevUna;
                uint priorInFlight = prevNsndBuf;
                // 简化的拥塞控制
                if (Cwnd < RmtWnd)
                {
                    uint mss = Mss;
                    if (Cwnd < Ssthresh)
                    {
                        Cwnd++;
                        Incr += mss;
                    }
                    else
                    {
                        if (Incr < mss) Incr = mss;
                        Incr += (mss * mss) / Incr + (mss / 16);
                        if ((Cwnd + 1) * mss <= Incr)
                            Cwnd = (Incr + mss - 1) / Math.Max(mss, 1);
                    }
                    if (Cwnd > RmtWnd) { Cwnd = RmtWnd; Incr = RmtWnd * mss; }
                }
            }

            return 0;
        }
    }
}