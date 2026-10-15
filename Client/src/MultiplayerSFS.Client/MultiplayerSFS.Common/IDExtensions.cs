// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;

namespace MultiplayerSFS.Common;

public static class IDExtensions
{
	private static readonly Random generator = new Random();

	public static int InsertNew<T>(this Dictionary<int, T> dict, T item)
	{
		int num;
		do
		{
			num = generator.Next();
		}
		while (dict.ContainsKey(num));
		dict.Add(num, item);
		return num;
	}

	/// <summary>
	/// 由零件存档数据算出的**确定性** id：同一枚零件在任何客户端、任何一次重建上都会得到同一个值。
	///
	/// 为什么必须有它：InsertNew 用 Random.Next()，于是"同一枚零件"在不同客户端、
	/// 甚至同一客户端的 RocketState 与 LocalRocket 里会拿到不同编号（两套随机空间）。
	/// 后果是 DestroyPart 里的 PartId 到了对面就是"另一个零件"或"不存在" ——
	/// 实测日志：destroy part ... not found ×12，且结构分叉是双向的（打错零件）。
	/// </summary>
	public static int DeterministicPartID(SFS.Parts.PartSave save)
	{
		string name = save != null ? (save.name ?? string.Empty) : string.Empty;
		uint hash = 2166136261u;
		for (int i = 0; i < name.Length; i++)
		{
			hash ^= name[i];
			hash *= 16777619u;
		}
		if (save != null)
		{
			// 位置在同一枚火箭里是唯一的，足以区分同型号零件
			long x = BitConverter.DoubleToInt64Bits(save.position.x);
			long y = BitConverter.DoubleToInt64Bits(save.position.y);
			hash = Mix(hash, (uint)(x ^ (x >> 32)));
			hash = Mix(hash, (uint)(y ^ (y >> 32)));
		}
		int id = (int)(hash & 0x7FFFFFFF);
		return id == 0 ? 1 : id;
	}

	private static uint Mix(uint hash, uint value)
	{
		hash ^= value;
		hash *= 16777619u;
		return hash;
	}

	public static int InsertNew(this HashSet<int> set)
	{
		int num;
		do
		{
			num = generator.Next();
		}
		while (set.Contains(num));
		set.Add(num);
		return num;
	}
}
