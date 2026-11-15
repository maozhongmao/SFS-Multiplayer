// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common.Transport.Kcp
{
	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 对应 C 版的 output 函数指针（<c>int (*output)(const char *buf, int len, ikcpcb *kcp, void *user)</c>），
	/// 改成普通 C# 接口，避免 IL2CPP 下的函数指针/委托封送问题。
	/// </summary>
	public interface IKcpOutput
	{
		/// <summary>
		/// 发送一个底层数据报（通常是 UDP）。实现方必须在本方法返回前把数据拷贝/发送出去：
		/// <paramref name="buffer"/> 是 KCP 内部的复用缓冲区，返回后内容就会被覆盖。
		/// </summary>
		/// <param name="buffer">KCP 内部缓冲区（不要保存引用）。</param>
		/// <param name="offset">本数据报在缓冲区中的起始偏移。</param>
		/// <param name="length">本数据报的字节数。</param>
		/// <param name="kcp">发起发送的 KCP 控制块。</param>
		/// <returns>返回值会被 KCP 忽略（与 ikcp.c 一致），习惯上 0 表示成功、负数表示失败。</returns>
		int Output(byte[] buffer, int offset, int length, KcpContext kcp);
	}

	/// <summary>
	/// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。
	///
	/// 把 <see cref="IKcpOutput"/> 适配成 <c>Action&lt;byte[], KcpContext&gt;</c>：内部会先复制出一份
	/// 定长数组再回调，因此回调方可以安全地保存数组引用（代价是每个数据报一次分配）。
	/// </summary>
	public sealed class KcpActionOutput : IKcpOutput
	{
		private readonly Action<byte[], KcpContext> handler;

		/// <summary>用回调构造。</summary>
		public KcpActionOutput(Action<byte[], KcpContext> handler)
		{
			if (handler == null) throw new ArgumentNullException("handler");
			this.handler = handler;
		}

		/// <inheritdoc/>
		public int Output(byte[] buffer, int offset, int length, KcpContext kcp)
		{
			byte[] copy = new byte[length];
			if (length > 0)
			{
				Buffer.BlockCopy(buffer, offset, copy, 0, length);
			}

			try
			{
				handler(copy, kcp);
				return 0;
			}
			catch (Exception ex)
			{
				// 简单的错误处理，避免依赖 Logger
				System.Diagnostics.Debug.WriteLine("kcp output callback failed: " + ex.Message);
				return -1;
			}
		}
	}
}
