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
    /// KCP 编解码工具，处理小端序的定长整数读写
    /// </summary>
    public static class KcpEncode
    {
        /// <summary>
        /// 写入 8 位无符号整数
        /// </summary>
        public static int Write8(byte[] buffer, int offset, byte value)
        {
            buffer[offset] = value;
            return offset + 1;
        }

        /// <summary>
        /// 读取 8 位无符号整数
        /// </summary>
        public static int Read8(byte[] buffer, int offset, out byte value)
        {
            value = buffer[offset];
            return offset + 1;
        }

        /// <summary>
        /// 写入 16 位无符号整数（小端序）
        /// </summary>
        public static int Write16(byte[] buffer, int offset, ushort value)
        {
            buffer[offset] = (byte)(value & 0xFF);
            buffer[offset + 1] = (byte)((value >> 8) & 0xFF);
            return offset + 2;
        }

        /// <summary>
        /// 读取 16 位无符号整数（小端序）
        /// </summary>
        public static int Read16(byte[] buffer, int offset, out ushort value)
        {
            value = (ushort)(buffer[offset] | (buffer[offset + 1] << 8));
            return offset + 2;
        }

        /// <summary>
        /// 写入 32 位无符号整数（小端序）
        /// </summary>
        public static int Write32(byte[] buffer, int offset, uint value)
        {
            buffer[offset] = (byte)(value & 0xFF);
            buffer[offset + 1] = (byte)((value >> 8) & 0xFF);
            buffer[offset + 2] = (byte)((value >> 16) & 0xFF);
            buffer[offset + 3] = (byte)((value >> 24) & 0xFF);
            return offset + 4;
        }

        /// <summary>
        /// 读取 32 位无符号整数（小端序）
        /// </summary>
        public static int Read32(byte[] buffer, int offset, out uint value)
        {
            value = (uint)(buffer[offset]
                | (buffer[offset + 1] << 8)
                | (buffer[offset + 2] << 16)
                | (buffer[offset + 3] << 24));
            return offset + 4;
        }

        /// <summary>
        /// 编码 KCP 段头部（24 字节）
        /// </summary>
        public static int EncodeSegment(byte[] buffer, int offset, KcpSegment seg)
        {
            offset = Write32(buffer, offset, seg.Conv);
            offset = Write8(buffer, offset, (byte)seg.Cmd);
            offset = Write8(buffer, offset, (byte)seg.Frg);
            offset = Write16(buffer, offset, (ushort)seg.Wnd);
            offset = Write32(buffer, offset, seg.Ts);
            offset = Write32(buffer, offset, seg.Sn);
            offset = Write32(buffer, offset, seg.Una);
            offset = Write32(buffer, offset, seg.Len);
            return offset;
        }

        /// <summary>
        /// 解码 KCP 段头部（24 字节）
        /// </summary>
        public static int DecodeSegment(byte[] buffer, int offset, KcpSegment seg)
        {
            offset = Read32(buffer, offset, out seg.Conv);
            offset = Read8(buffer, offset, out byte cmd); seg.Cmd = cmd;
            offset = Read8(buffer, offset, out byte frg); seg.Frg = frg;
            offset = Read16(buffer, offset, out ushort wnd); seg.Wnd = wnd;
            offset = Read32(buffer, offset, out seg.Ts);
            offset = Read32(buffer, offset, out seg.Sn);
            offset = Read32(buffer, offset, out seg.Una);
            offset = Read32(buffer, offset, out seg.Len);
            return offset;
        }
    }
}