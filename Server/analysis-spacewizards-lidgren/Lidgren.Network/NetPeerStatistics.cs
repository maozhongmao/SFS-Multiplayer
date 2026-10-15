using System.Collections.Generic;
using System.Text;

namespace Lidgren.Network;

public sealed class NetPeerStatistics
{
	private readonly NetPeer m_peer;

	internal long m_sentPackets;

	internal long m_receivedPackets;

	internal long m_sentMessages;

	internal long m_receivedMessages;

	internal long m_receivedFragments;

	internal long m_droppedMessages;

	internal long m_sentBytes;

	internal long m_receivedBytes;

	internal long m_bytesAllocated;

	internal long m_resentMessagesDueToDelay;

	internal long m_resentMessagesDueToHole;

	public long SentPackets => m_sentPackets;

	public long ReceivedPackets => m_receivedPackets;

	public long SentMessages => m_sentMessages;

	public long ReceivedMessages => m_receivedMessages;

	public long SentBytes => m_sentBytes;

	public long ReceivedBytes => m_receivedBytes;

	public long StorageBytesAllocated => m_bytesAllocated;

	public int BytesInRecyclePool
	{
		get
		{
			List<byte[]> storagePool = m_peer.m_storagePool;
			if (storagePool == null)
			{
				return 0;
			}
			lock (storagePool)
			{
				return m_peer.m_storagePoolBytes;
			}
		}
	}

	public long ResentMessages => m_resentMessagesDueToHole + m_resentMessagesDueToDelay;

	public long ResentMessagesDueToHole => m_resentMessagesDueToHole;

	public long ResentMessagesDueToDelay => m_resentMessagesDueToDelay;

	public long DroppedMessages => m_droppedMessages;

	internal NetPeerStatistics(NetPeer peer)
	{
		m_peer = peer;
		Reset();
	}

	internal void Reset()
	{
		m_sentPackets = 0L;
		m_receivedPackets = 0L;
		m_sentMessages = 0L;
		m_receivedMessages = 0L;
		m_receivedFragments = 0L;
		m_sentBytes = 0L;
		m_receivedBytes = 0L;
		m_bytesAllocated = 0L;
	}

	internal void PacketSent(int numBytes, int numMessages)
	{
		m_sentPackets++;
		m_sentBytes += numBytes;
		m_sentMessages += numMessages;
	}

	internal void PacketReceived(int numBytes, int numMessages, int numFragments)
	{
		m_receivedPackets++;
		m_receivedBytes += numBytes;
		m_receivedMessages += numMessages;
		m_receivedFragments += numFragments;
	}

	internal void MessageResent(MessageResendReason reason)
	{
		if (reason == MessageResendReason.Delay)
		{
			m_resentMessagesDueToDelay++;
		}
		else
		{
			m_resentMessagesDueToHole++;
		}
	}

	internal void MessageDropped()
	{
		m_droppedMessages++;
	}

	public override string ToString()
	{
		StringBuilder stringBuilder = new StringBuilder();
		stringBuilder.AppendLine(m_peer.ConnectionsCount + " connections");
		stringBuilder.AppendLine("Sent " + m_sentBytes + " bytes in " + m_sentMessages + " messages in " + m_sentPackets + " packets");
		stringBuilder.AppendLine("Received " + m_receivedBytes + " bytes in " + m_receivedMessages + " messages (of which " + m_receivedFragments + " fragments) in " + m_receivedPackets + " packets");
		stringBuilder.AppendLine("Storage allocated " + m_bytesAllocated + " bytes");
		if (m_peer.m_storagePool != null)
		{
			stringBuilder.AppendLine("Recycled pool " + m_peer.m_storagePoolBytes + " bytes (" + m_peer.m_storageSlotsUsedCount + " entries)");
		}
		return stringBuilder.ToString();
	}
}
