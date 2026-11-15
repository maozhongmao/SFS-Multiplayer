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
        /// 更新状态（需定期调用）
        /// </summary>
        public void Update(uint current)
        {
            Current = current;

            if (Updated == 0)
            {
                Updated = 1;
                TsFlush = Current;
            }

            int slap = (int)(Current - TsFlush);
            if (slap >= 10000 || slap < -10000)
            {
                TsFlush = Current;
                slap = 0;
            }

            if (slap >= 0)
            {
                TsFlush += Interval;
                if ((int)(Current - TsFlush) >= 0)
                    TsFlush = Current + Interval;
                Flush();
            }
        }

        /// <summary>
        /// 检查下次更新时间
        /// </summary>
        public uint Check(uint current)
        {
            uint tsFlush = TsFlush;
            if (Updated == 0) return current;

            if ((int)(current - tsFlush) >= 10000 || (int)(current - tsFlush) < -10000)
                tsFlush = current;

            if ((int)(current - tsFlush) >= 0) return current;

            int tmFlush = (int)(tsFlush - current);
            int tmPacket = int.MaxValue;

            var cur = SndBuf.Head.Next;
            while (cur != SndBuf.Head)
            {
                int diff = (int)(cur.ResendTs - current);
                if (diff <= 0) return current;
                if (diff < tmPacket) tmPacket = diff;
                cur = cur.Next;
            }

            uint minimal = (uint)KcpMath.Min(tmPacket, tmFlush);
            if (minimal >= Interval) minimal = Interval;
            return current + minimal;
        }
    }
}