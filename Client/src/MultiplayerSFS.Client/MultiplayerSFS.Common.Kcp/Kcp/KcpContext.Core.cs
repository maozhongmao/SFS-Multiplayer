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
    /// <summary>
    /// KCP 上下文，对应 C 版的 ikcpcb
    /// 纯安全 C# 实现，无 unsafe/指针/Fody 依赖
    /// </summary>
    public partial class KcpContext : IDisposable
    {
        // 基础配置
        public uint Conv { get; private set; }
        public uint Mtu { get; private set; }
        public uint Mss { get; private set; }
        public uint State { get; private set; }

        // 发送相关
        public uint SndUna { get; private set; }   // 未确认的最小序列号
        public uint SndNxt { get; private set; }   // 下一个待分配的序列号
        public uint SndWnd { get; private set; }   // 发送窗口大小
        public uint RmtWnd { get; private set; }   // 对端接收窗口
        public uint Cwnd { get; private set; }     // 拥塞窗口
        public uint Ssthresh { get; private set; } // 慢启动阈值
        public uint Incr { get; private set; }     // 拥塞窗口增长量

        // 接收相关
        public uint RcvNxt { get; private set; }   // 期望接收的下一个序列号
        public uint RcvWnd { get; private set; }   // 接收窗口大小

        // 时间相关
        public uint Current { get; private set; }    // 当前时间戳
        public uint Interval { get; private set; }   // 刷新间隔
        public uint TsFlush { get; private set; }    // 下次刷新时间
        public uint RxSrtt { get; private set; }     // 平滑 RTT
        public int RxRttval { get; private set; }    // RTT 变动
        public uint RxRto { get; private set; }      // 重传超时
        public uint RxMinrto { get; private set; }   // 最小 RTO

        // 探测相关
        public uint Probe { get; private set; }
        public uint TsProbe { get; private set; }
        public uint ProbeWait { get; private set; }

        // 队列
        public KcpQueue SndQueue { get; } = new KcpQueue();
        public KcpQueue RcvQueue { get; } = new KcpQueue();
        public KcpQueue SndBuf { get; } = new KcpQueue();
        public KcpQueue RcvBuf { get; } = new KcpQueue();

        // ACK 列表
        private readonly List<uint> _ackList = new List<uint>();
        private readonly List<uint> _ackTsList = new List<uint>();

        // 其他参数
        public uint DeadLink { get; private set; } = KcpConst.DeadLink;
        public int FastResend { get; private set; } = 0;
        public int FastLimit { get; private set; } = (int)KcpConst.FastAckLimit;
        public int NoCwnd { get; private set; } = 0;
        public int Stream { get; private set; } = 0;
        public int NoDelayMode { get; private set; } = 0;
        public int Updated { get; private set; } = 0;
        public uint Xmit { get; private set; } = 0;

        // 发送缓冲区
        private byte[] _buffer;
        private readonly Action<byte[], KcpContext> _output;

        /// <summary>
        /// 创建 KCP 上下文
        /// </summary>
        /// <param name="conv">会话 ID（两端必须一致）</param>
        /// <param name="output">输出回调：发送数据到网络层</param>
        public KcpContext(uint conv, Action<byte[], KcpContext> output)
        {
            Conv = conv;
            _output = output ?? throw new ArgumentNullException(nameof(output));
            Mtu = KcpConst.MtuDef;
            Mss = Mtu - KcpConst.Overhead;
            SndWnd = KcpConst.WndSnd;
            RcvWnd = KcpConst.WndRcv;
            RmtWnd = KcpConst.WndRcv;
            Cwnd = 0;
            Incr = 0;
            Ssthresh = KcpConst.ThreshInit;
            RxRto = KcpConst.RtoDef;
            RxMinrto = KcpConst.RtoMin;
            Interval = KcpConst.Interval;
            TsFlush = KcpConst.Interval;
            DeadLink = KcpConst.DeadLink;
            _buffer = new byte[(Mtu + KcpConst.Overhead) * 3];
        }

        /// <summary>
        /// 设置 MTU
        /// </summary>
        public int SetMtu(int mtu)
        {
            if (mtu < 50 || mtu < KcpConst.Overhead) return -1;
            Mtu = (uint)mtu;
            Mss = Mtu - KcpConst.Overhead;
            _buffer = new byte[(Mtu + KcpConst.Overhead) * 3];
            return 0;
        }

        /// <summary>
        /// 设置窗口大小
        /// </summary>
        public int WndSize(int sndwnd, int rcvwnd)
        {
            if (sndwnd > 0) SndWnd = (uint)sndwnd;
            if (rcvwnd > 0) RcvWnd = (uint)Math.Max(rcvwnd, KcpConst.WndRcv);
            return 0;
        }

        /// <summary>
        /// 设置 nodelay 模式
        /// </summary>
        public int SetNoDelay(int nodelay, int interval, int resend, int nc)
        {
            if (nodelay >= 0)
            {
                NoDelayMode = nodelay;
                RxMinrto = nodelay != 0 ? KcpConst.RtoNdl : KcpConst.RtoMin;
            }
            if (interval >= 0)
            {
                Interval = (uint)KcpMath.Clamp(interval, 10, 5000);
            }
            if (resend >= 0) FastResend = resend;
            if (nc >= 0) NoCwnd = nc;
            return 0;
        }
    }
}