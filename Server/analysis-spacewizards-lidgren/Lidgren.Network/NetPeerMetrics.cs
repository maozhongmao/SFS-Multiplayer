using System;
using System.Collections.Generic;
using System.Diagnostics.Metrics;

namespace Lidgren.Network;

public static class NetPeerMetrics
{
	[Flags]
	public enum Flags
	{
		None = 0,
		Statistics = 1
	}

	private const string MeterName = "Lidgren.Network.NetPeer";

	public static void AddMetrics(this NetPeer netPeer, Flags flags = Flags.Statistics, IEnumerable<KeyValuePair<string, object?>>? tags = null, IMeterFactory? factory = null)
	{
		lock (netPeer.m_initializeLock)
		{
			if (netPeer.Status != NetPeerStatus.Running)
			{
				throw new InvalidOperationException("Peer must be running.");
			}
			Meter meter;
			if (factory != null)
			{
				meter = factory.Create("Lidgren.Network.NetPeer", null, tags);
			}
			else
			{
				meter = new Meter("Lidgren.Network.NetPeer", null, tags);
			}
			if ((flags & Flags.Statistics) != Flags.None)
			{
				NetPeerStatistics statistics = netPeer.Statistics;
				meter.CreateObservableCounter("packets_sent", () => statistics.SentPackets, null, "Number of packets sent by this NetPeer.");
				meter.CreateObservableCounter("packets_received", () => statistics.ReceivedPackets, null, "Number of packets received by this NetPeer.");
				meter.CreateObservableCounter("messages_sent", () => statistics.SentMessages, null, "Number of messages sent by this NetPeer.");
				meter.CreateObservableCounter("messages_received", () => statistics.ReceivedMessages, null, "Number of messages received by this NetPeer.");
				meter.CreateObservableCounter("sent_bytes", () => statistics.SentBytes, "bytes", "Number of bytes sent by this NetPeer.");
				meter.CreateObservableCounter("sent_bytes", () => statistics.ReceivedBytes, "bytes", "Number of bytes received by this NetPeer.");
				meter.CreateObservableCounter("storage_allocated_bytes", () => statistics.StorageBytesAllocated, "bytes", "Number of bytes allocated (and possibly garbage collected) for message storage.");
				meter.CreateObservableUpDownCounter("recycle_pool_bytes", () => statistics.BytesInRecyclePool, "bytes", "Number of bytes in the recycled pool.");
				meter.CreateObservableCounter("messages_resent", (Func<IEnumerable<Measurement<long>>>)delegate
				{
					List<Measurement<long>> list = new List<Measurement<long>>();
					long resentMessagesDueToHole = statistics.ResentMessagesDueToHole;
					_003C_003Ey__InlineArray1<KeyValuePair<string, object>> buffer = default(_003C_003Ey__InlineArray1<KeyValuePair<string, object>>);
					global::_003CPrivateImplementationDetails_003E.InlineArrayElementRef<_003C_003Ey__InlineArray1<KeyValuePair<string, object>>, KeyValuePair<string, object>>(ref buffer, 0) = new KeyValuePair<string, object>("drop_reason", "hole");
					list.Add(new Measurement<long>(resentMessagesDueToHole, global::_003CPrivateImplementationDetails_003E.InlineArrayAsReadOnlySpan<_003C_003Ey__InlineArray1<KeyValuePair<string, object>>, KeyValuePair<string, object>>(in buffer, 1)));
					long resentMessagesDueToDelay = statistics.ResentMessagesDueToDelay;
					_003C_003Ey__InlineArray1<KeyValuePair<string, object>> buffer2 = default(_003C_003Ey__InlineArray1<KeyValuePair<string, object>>);
					global::_003CPrivateImplementationDetails_003E.InlineArrayElementRef<_003C_003Ey__InlineArray1<KeyValuePair<string, object>>, KeyValuePair<string, object>>(ref buffer2, 0) = new KeyValuePair<string, object>("drop_reason", "delay");
					list.Add(new Measurement<long>(resentMessagesDueToDelay, global::_003CPrivateImplementationDetails_003E.InlineArrayAsReadOnlySpan<_003C_003Ey__InlineArray1<KeyValuePair<string, object>>, KeyValuePair<string, object>>(in buffer2, 1)));
					return list;
				}, (string?)null, "Number of messages that had to be resent.");
				meter.CreateObservableCounter("messages_dropped", () => statistics.DroppedMessages, null, "Number of messages that were dropped");
			}
			netPeer.m_onShutdown = (Action)Delegate.Combine(netPeer.m_onShutdown, (Action)delegate
			{
				meter.Dispose();
			});
		}
	}
}
