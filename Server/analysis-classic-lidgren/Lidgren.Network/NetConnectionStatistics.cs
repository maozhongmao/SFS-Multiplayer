using System.Diagnostics;
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

	[Conditional("DEBUG")]
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

	[Conditional("DEBUG")]
	internal void MessageDropped()
	{
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
		int num = 0;
		int num2 = 0;
		NetSenderChannelBase[] sendChannels = m_connection.m_sendChannels;
		foreach (NetSenderChannelBase netSenderChannelBase in sendChannels)
		{
			if (netSenderChannelBase == null)
			{
				continue;
			}
			num += netSenderChannelBase.QueuedSendsCount;
			if (!(netSenderChannelBase is NetReliableSenderChannel netReliableSenderChannel))
			{
				continue;
			}
			for (int j = 0; j < netReliableSenderChannel.m_storedMessages.Length; j++)
			{
				if (netReliableSenderChannel.m_storedMessages[j].Message != null)
				{
					num2++;
				}
			}
		}
		int num3 = 0;
		NetReceiverChannelBase[] receiveChannels = m_connection.m_receiveChannels;
		foreach (NetReceiverChannelBase netReceiverChannelBase in receiveChannels)
		{
			if (!(netReceiverChannelBase is NetReliableOrderedReceiver netReliableOrderedReceiver))
			{
				continue;
			}
			for (int l = 0; l < netReliableOrderedReceiver.m_withheldMessages.Length; l++)
			{
				if (netReliableOrderedReceiver.m_withheldMessages[l] != null)
				{
					num3++;
				}
			}
		}
		stringBuilder.AppendLine("Unsent messages: " + num);
		stringBuilder.AppendLine("Stored messages: " + num2);
		stringBuilder.AppendLine("Withheld messages: " + num3);
		return stringBuilder.ToString();
	}
}
