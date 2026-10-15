using System.Diagnostics;
using System.Text;

namespace Lidgren.Network;

public sealed class NetPeerStatistics
{
	private readonly NetPeer m_peer;

	internal int m_sentPackets;

	internal int m_receivedPackets;

	internal int m_sentMessages;

	internal int m_receivedMessages;

	internal int m_receivedFragments;

	internal int m_sentBytes;

	internal int m_receivedBytes;

	internal long m_bytesAllocated;

	public int SentPackets => m_sentPackets;

	public int ReceivedPackets => m_receivedPackets;

	public int SentMessages => m_sentMessages;

	public int ReceivedMessages => m_receivedMessages;

	public int SentBytes => m_sentBytes;

	public int ReceivedBytes => m_receivedBytes;

	public long StorageBytesAllocated => m_bytesAllocated;

	public int BytesInRecyclePool
	{
		get
		{
			lock (m_peer.m_storagePool)
			{
				return m_peer.m_storagePoolBytes;
			}
		}
	}

	internal NetPeerStatistics(NetPeer peer)
	{
		m_peer = peer;
		Reset();
	}

	internal void Reset()
	{
		m_sentPackets = 0;
		m_receivedPackets = 0;
		m_sentMessages = 0;
		m_receivedMessages = 0;
		m_receivedFragments = 0;
		m_sentBytes = 0;
		m_receivedBytes = 0;
		m_bytesAllocated = 0L;
	}

	[Conditional("DEBUG")]
	internal void PacketSent(int numBytes, int numMessages)
	{
		m_sentPackets++;
		m_sentBytes += numBytes;
		m_sentMessages += numMessages;
	}

	[Conditional("DEBUG")]
	internal void PacketReceived(int numBytes, int numMessages, int numFragments)
	{
		m_receivedPackets++;
		m_receivedBytes += numBytes;
		m_receivedMessages += numMessages;
		m_receivedFragments += numFragments;
	}

	public override string ToString()
	{
		StringBuilder stringBuilder = new StringBuilder();
		stringBuilder.AppendLine(m_peer.ConnectionsCount + " connections");
		stringBuilder.AppendLine("Sent (n/a) bytes in (n/a) messages in (n/a) packets");
		stringBuilder.AppendLine("Received (n/a) bytes in (n/a) messages in (n/a) packets");
		stringBuilder.AppendLine("Storage allocated " + m_bytesAllocated + " bytes");
		if (m_peer.m_storagePool != null)
		{
			stringBuilder.AppendLine("Recycled pool " + m_peer.m_storagePoolBytes + " bytes (" + m_peer.m_storageSlotsUsedCount + " entries)");
		}
		return stringBuilder.ToString();
	}
}
