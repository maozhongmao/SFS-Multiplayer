namespace Lidgren.Network;

internal sealed class NetReliableOrderedReceiver : NetReceiverChannelBase
{
	private int m_windowStart;

	private readonly int m_windowSize;

	private readonly NetBitVector m_earlyReceived;

	internal NetIncomingMessage?[] m_withheldMessages;

	public NetReliableOrderedReceiver(NetConnection connection, int windowSize)
		: base(connection)
	{
		m_windowSize = windowSize;
		m_withheldMessages = new NetIncomingMessage[windowSize];
		m_earlyReceived = new NetBitVector(windowSize);
	}

	private void AdvanceWindow()
	{
		m_earlyReceived.Set(m_windowStart % m_windowSize, value: false);
		m_windowStart = (m_windowStart + 1) % 1024;
	}

	internal override void ReceiveMessage(NetIncomingMessage message)
	{
		int num = NetUtility.RelativeSequenceNumber(message.m_sequenceNumber, m_windowStart);
		m_connection.QueueAck(message.m_receivedMessageType, message.m_sequenceNumber);
		if (num == 0)
		{
			AdvanceWindow();
			m_peer.ReleaseMessage(message);
			for (int i = (message.m_sequenceNumber + 1) % 1024; m_earlyReceived[i % m_windowSize]; i++)
			{
				message = m_withheldMessages[i % m_windowSize];
				m_withheldMessages[i % m_windowSize] = null;
				m_peer.ReleaseMessage(message);
				AdvanceWindow();
			}
		}
		else if (num < 0)
		{
			m_connection.m_statistics.MessageDropped();
		}
		else if (num > m_windowSize)
		{
			m_connection.m_statistics.MessageDropped();
		}
		else
		{
			m_earlyReceived.Set(message.m_sequenceNumber % m_windowSize, value: true);
			m_withheldMessages[message.m_sequenceNumber % m_windowSize] = message;
		}
	}
}
