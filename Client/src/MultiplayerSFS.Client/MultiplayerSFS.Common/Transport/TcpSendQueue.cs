// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Threading;

namespace MultiplayerSFS.Common;

public sealed class TcpSendQueue
{
	private readonly object sync = new object();
	private readonly Queue<TcpFrame> critical = new Queue<TcpFrame>();
	private readonly Dictionary<long, TcpFrame> latest = new Dictionary<long, TcpFrame>();
	private readonly Queue<long> latestOrder = new Queue<long>();
	private long overwrittenStates;

	public long OverwrittenStates { get { lock (sync) return overwrittenStates; } }
	public int Count { get { lock (sync) return critical.Count + latest.Count; } }

	public void EnqueueCritical(TcpFrame frame)
	{
		lock (sync)
		{
			critical.Enqueue(frame);
			Monitor.PulseAll(sync);
		}
	}

	public void EnqueueLatest(long key, TcpFrame frame)
	{
		lock (sync)
		{
			if (latest.ContainsKey(key)) overwrittenStates++;
			else latestOrder.Enqueue(key);
			latest[key] = frame;
			Monitor.PulseAll(sync);
		}
	}

	public bool TryDequeue(out TcpFrame frame)
	{
		lock (sync) return TryDequeueUnsafe(out frame);
	}

	public bool WaitDequeue(int millisecondsTimeout, out TcpFrame frame)
	{
		lock (sync)
		{
			if (TryDequeueUnsafe(out frame)) return true;
			Monitor.Wait(sync, millisecondsTimeout);
			return TryDequeueUnsafe(out frame);
		}
	}

	private bool TryDequeueUnsafe(out TcpFrame frame)
	{
		if (critical.Count > 0)
		{
			frame = critical.Dequeue();
			return true;
		}
		while (latestOrder.Count > 0)
		{
			long key = latestOrder.Dequeue();
			if (latest.TryGetValue(key, out frame))
			{
				latest.Remove(key);
				return true;
			}
		}
		frame = null;
		return false;
	}
}
