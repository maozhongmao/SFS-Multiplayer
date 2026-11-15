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
    /// KCP 数据段结构，对应 C 版的 IKCPSEG
    /// </summary>
    public class KcpSegment
    {
        public uint Conv;       // 会话 ID
        public uint Cmd;        // 命令类型
        public uint Frg;        // 分片序号（倒序：最后一片为 0）
        public uint Wnd;        // 窗口大小
        public uint Ts;         // 发送时间戳
        public uint Sn;         // 序列号
        public uint Una;        // 确认号（对端已收到的最大 sn + 1）
        public uint Len;        // 数据长度
        public uint ResendTs;   // 下次重传时间戳
        public uint Rto;        // 重传超时时间
        public uint FastAck;    // 快速 ACK 计数
        public uint Xmit;       // 传输次数
        public byte[] Data;     // 数据负载

        // 队列节点指针（用 List 模拟双向链表）
        internal KcpSegment Next;
        internal KcpSegment Prev;

        public KcpSegment(int dataSize = 0)
        {
            Data = dataSize > 0 ? new byte[dataSize] : Array.Empty<byte>();
        }
    }

    /// <summary>
    /// KCP 命令类型常量
    /// </summary>
    public static class KcpCmd
    {
        public const uint Push = 81;  // 推送数据
        public const uint Ack  = 82;  // 确认
        public const uint Wask = 83;  // 窗口探测（询问）
        public const uint Wins = 84;  // 窗口大小（告知）
    }

    /// <summary>
    /// KCP 协议常量
    /// </summary>
    public static class KcpConst
    {
        public const uint RtoNdl = 30;         // 无延迟模式最小 RTO
        public const uint RtoMin = 100;        // 正常模式最小 RTO
        public const uint RtoDef = 200;        // 默认 RTO
        public const uint RtoMax = 60000;      // 最大 RTO
        public const uint AskSend = 1;         // 需要发送 WASK
        public const uint AskTell = 2;         // 需要发送 WINS
        public const uint WndSnd = 32;         // 默认发送窗口
        public const uint WndRcv = 128;        // 默认接收窗口（必须 >= 最大分片数）
        public const uint MtuDef = 1400;       // 默认 MTU
        public const uint AckFast = 3;         // 快速重传触发阈值
        public const uint Interval = 100;      // 内部更新间隔
        public const uint Overhead = 24;       // KCP 头部开销（字节）
        public const uint DeadLink = 20;       // 最大重传次数判死链
        public const uint ThreshInit = 2;      // 初始慢启动阈值
        public const uint ThreshMin = 2;       // 最小慢启动阈值
        public const uint ProbeInit = 5000;    // 窗口探测初始间隔
        public const uint ProbeLimit = 120000; // 窗口探测最大间隔
        public const uint FastAckLimit = 5;    // 最大快速重传次数
    }
}