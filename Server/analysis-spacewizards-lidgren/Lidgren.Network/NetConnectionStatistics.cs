using System.Text;

namespace Lidgren.Network;

public sealed class NetConnectionStatistics
{
	private readonly NetConnection m_connection;

	internal long m_sentPackets;

	internal long m_receivedPackets;

	internal long m_sentMessages;

	internal long m_receivedMessages;

	internal long m_droppedMessages;

	internal long m_receivedFragments;

	internal long m_sentBytes;

	internal long m_receivedBytes;

	internal long m_resentMessagesDueToDelay;

	internal long m_resentMessagesDueToHole;

	public long SentPackets => m_sentPackets;

	public long ReceivedPackets => m_receivedPackets;

	public long SentBytes => m_sentBytes;

	public long ReceivedBytes => m_receivedBytes;

	public long SentMessages => m_sentMessages;

	public long ReceivedMessages => m_receivedMessages;

	public long ResentMessages => m_resentMessagesDueToHole + m_resentMessagesDueToDelay;

	public long ResentMessagesDueToHole => m_resentMessagesDueToHole;

	public long ResentMessagesDueToDelay => m_resentMessagesDueToDelay;

	public long DroppedMessages => m_droppedMessages;

	internal NetConnectionStatistics(NetConnection conn)
	{
		m_connection = conn;
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
		m_resentMessagesDueToDelay = 0L;
		m_resentMessagesDueToHole = 0L;
	}

	private int GetWithheldMessages()
	{
		int num = 0;
		NetReceiverChannelBase[] receiveChannels = m_connection.m_receiveChannels;
		for (int i = 0; i < receiveChannels.Length; i++)
		{
			if (!(receiveChannels[i] is NetReliableOrderedReceiver netReliableOrderedReceiver))
			{
				continue;
			}
			for (int j = 0; j < netReliableOrderedReceiver.m_withheldMessages.Length; j++)
			{
				if (netReliableOrderedReceiver.m_withheldMessages[j] != null)
				{
					num++;
				}
			}
		}
		return num;
	}

	private void CalculateUnsentAndStoredMessages(out int numUnsent, out int numStored)
	{
		numUnsent = 0;
		numStored = 0;
		NetSenderChannelBase[] sendChannels = m_connection.m_sendChannels;
		foreach (NetSenderChannelBase netSenderChannelBase in sendChannels)
		{
			if (netSenderChannelBase == null)
			{
				continue;
			}
			numUnsent += netSenderChannelBase.QueuedSendsCount;
			if (!(netSenderChannelBase is NetReliableSenderChannel netReliableSenderChannel))
			{
				continue;
			}
			for (int j = 0; j < netReliableSenderChannel.m_storedMessages.Length; j++)
			{
				if (netReliableSenderChannel.m_storedMessages[j].Message != null)
				{
					numStored++;
				}
			}
		}
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
		m_connection.m_peer.m_statistics.MessageResent(reason);
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
		m_connection.m_peer.m_statistics.MessageDropped();
		m_droppedMessages++;
	}

	public override string ToString()
	{
		StringBuilder stringBuilder = new StringBuilder();
		stringBuilder.AppendLine("Current MTU: " + m_connection.m_currentMTU);
		stringBuilder.AppendLine("Sent " + m_sentBytes + " bytes in " + m_sentMessages + " messages in " + m_sentPackets + " packets");
		stringBuilder.AppendLine("Received " + m_receivedBytes + " bytes in " + m_receivedMessages + " messages (of which " + m_receivedFragments + " fragments) in " + m_receivedPackets + " packets");
		stringBuilder.AppendLine("Dropped " + m_droppedMessages + " messages (dupes/late/early)");
		if (m_resentMessagesDueToDelay > 0)
		{
			stringBuilder.AppendLine("Resent messages (delay): " + m_resentMessagesDueToDelay);
		}
		if (m_resentMessagesDueToHole > 0)
		{
			stringBuilder.AppendLine("Resent messages (holes): " + m_resentMessagesDueToHole);
		}
		CalculateUnsentAndStoredMessages(out var numUnsent, out var numStored);
		int withheldMessages = GetWithheldMessages();
		stringBuilder.AppendLine("Unsent messages: " + numUnsent);
		stringBuilder.AppendLine("Stored messages: " + numStored);
		stringBuilder.AppendLine("Withheld messages: " + withheldMessages);
		return stringBuilder.ToString();
	}
}
