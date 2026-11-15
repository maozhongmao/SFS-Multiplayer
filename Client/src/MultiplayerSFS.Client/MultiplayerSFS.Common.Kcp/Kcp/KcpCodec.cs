// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common.Kcp
{
	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 解码后的 KCP 包头（24 字节，小端）。字段顺序与 ikcp.c 的 ikcp_encode_seg 一致：
	/// conv、cmd、frg、wnd、ts、sn、una、len。
	/// </summary>
	public struct KcpHeader
	{
		/// <summary>协商号码。</summary>
		public uint Conv;

		/// <summary>命令字（81=PUSH，82=ACK，83=WASK，84=WINS）。</summary>
		public uint Cmd;

		/// <summary>分片剩余数（0 表示消息最后一片）。</summary>
		public uint Frg;

		/// <summary>发送方可用接收窗口。</summary>
		public uint Wnd;

		/// <summary>发送时间戳（毫秒）。</summary>
		public uint Ts;

		/// <summary>序列号。</summary>
		public uint Sn;

		/// <summary>发送方的 rcv_nxt。</summary>
		public uint Una;

		/// <summary>载荷长度。</summary>
		public uint Len;
	}

	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 对应 ikcp.c 里的一组 ikcp_encode8u / ikcp_decode8u / ...32u 静态函数：把小端字段读写换成了
	/// 普通数组下标运算（不依赖 IWORDS_MUST_ALIGN / memcpy 对齐技巧），并额外提供整包头的编解码。
	/// 所有方法都是纯函数，不修改任何 KCP 状态。
	/// </summary>
	public static class KcpCodec
	{
		/// <summary>写 1 字节无符号整数，返回新的偏移。</summary>
		public static int Encode8u(byte[] p, int offset, byte value)
		{
			p[offset] = value;
			return offset + 1;
		}

		/// <summary>写 2 字节无符号整数（小端），返回新的偏移。</summary>
		public static int Encode16u(byte[] p, int offset, ushort value)
		{
			p[offset] = (byte)(value & 0xFF);
			p[offset + 1] = (byte)((value >> 8) & 0xFF);
			return offset + 2;
		}

		/// <summary>写 4 字节无符号整数（小端），返回新的偏移。</summary>
		public static int Encode32u(byte[] p, int offset, uint value)
		{
			p[offset] = (byte)(value & 0xFF);
			p[offset + 1] = (byte)((value >> 8) & 0xFF);
			p[offset + 2] = (byte)((value >> 16) & 0xFF);
			p[offset + 3] = (byte)((value >> 24) & 0xFF);
			return offset + 4;
		}

		/// <summary>读 1 字节无符号整数，返回新的偏移。</summary>
		public static int Decode8u(byte[] p, int offset, out byte value)
		{
			value = p[offset];
			return offset + 1;
		}

		/// <summary>读 2 字节无符号整数（小端），返回新的偏移。</summary>
		public static int Decode16u(byte[] p, int offset, out ushort value)
		{
			value = (ushort)(p[offset] | (p[offset + 1] << 8));
			return offset + 2;
		}

		/// <summary>读 4 字节无符号整数（小端），返回新的偏移。</summary>
		public static int Decode32u(byte[] p, int offset, out uint value)
		{
			value = (uint)(p[offset] | (p[offset + 1] << 8) | (p[offset + 2] << 16) | (p[offset + 3] << 24));
			return offset + 4;
		}

		/// <summary>按 ikcp_encode_seg 的顺序写入 24 字节包头，返回新的偏移。</summary>
		public static int EncodeSegmentHeader(byte[] p, int offset, uint conv, uint cmd, uint frg, uint wnd, uint ts, uint sn, uint una, uint len)
		{
			offset = Encode32u(p, offset, conv);
			offset = Encode8u(p, offset, (byte)cmd);
			offset = Encode8u(p, offset, (byte)frg);
			offset = Encode16u(p, offset, (ushort)wnd);
			offset = Encode32u(p, offset, ts);
			offset = Encode32u(p, offset, sn);
			offset = Encode32u(p, offset, una);
			offset = Encode32u(p, offset, len);
			return offset;
		}

		/// <summary>
		/// 读取一个 KCP 包头（24 字节），返回包头之后的偏移。
		/// 调用方需自行保证 buffer 在 offset 之后至少有 <see cref="KcpConstants.Overhead"/> 字节。
		/// </summary>
		public static int DecodeHeader(byte[] buffer, int offset, out KcpHeader header)
		{
			KcpHeader h;
			offset = Decode32u(buffer, offset, out h.Conv);
			offset = Decode8u(buffer, offset, out byte cmd);
			offset = Decode8u(buffer, offset, out byte frg);
			offset = Decode16u(buffer, offset, out ushort wnd);
			h.Cmd = cmd;
			h.Frg = frg;
			h.Wnd = wnd;
			offset = Decode32u(buffer, offset, out h.Ts);
			offset = Decode32u(buffer, offset, out h.Sn);
			offset = Decode32u(buffer, offset, out h.Una);
			offset = Decode32u(buffer, offset, out h.Len);
			header = h;
			return offset;
		}

		/// <summary>读取数据报前 4 字节里的 conv（对应 ikcp_getconv）。</summary>
		public static uint GetConv(byte[] data)
		{
			return GetConv(data, 0);
		}

		/// <summary>读取指定偏移处 4 字节里的 conv（对应 ikcp_getconv）。</summary>
		public static uint GetConv(byte[] data, int offset)
		{
			if (data == null || data.Length < offset + 4) return 0;
			return (uint)(data[offset] | (data[offset + 1] << 8) | (data[offset + 2] << 16) | (data[offset + 3] << 24));
		}
	}
}
