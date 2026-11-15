// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public static class NetDataExtensions
{
	public static void Write(this NetOutgoingMessage msg, INetData data)
	{
		data.Serialize(msg);
	}

	public static D Read<D>(this NetIncomingMessage msg) where D : INetData, new()
	{
		D result = new D();
		result.Deserialize(msg);
		return result;
	}

	public static void WriteCollection<T>(this NetOutgoingMessage msg, ICollection<T> collection, Action<T> writeFunc)
	{
		msg.WriteCompressedInt(collection.Count);
		foreach (T item in collection)
		{
			writeFunc(item);
		}
	}

	public static C ReadCollection<C, T>(this NetIncomingMessage msg, Func<int, C> initFunc, Func<T> readFunc) where C : ICollection<T>
	{
		int num = msg.ReadCompressedInt();
		C result = initFunc(num);
		for (int i = 0; i < num; i++)
		{
			result.Add(readFunc());
		}
		return result;
	}
}
