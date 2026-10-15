namespace Lidgren.Network;

internal sealed class NetReliableSequencedReceiver : NetReceiverChannelBase
{
	private int m_windowStart;

	private int m_windowSize;

	public NetReliableSequencedReceiver(NetConnection connection, int windowSize)
		: base(connection)
	{
		m_windowSize = windowSize;
	}

	private void AdvanceWindow()
	{
		m_windowStart = (m_windowStart + 1) % 1024;
	}

	internal override void ReceiveMessage(NetIncomingMessage message)
	{
		int sequenceNumber = message.m_sequenceNumber;
		int num = NetUtility.RelativeSequenceNumber(sequenceNumber, m_windowStart);
		m_connection.QueueAck(message.m_receivedMessageType, sequenceNumber);
		if (num == 0)
		{
			AdvanceWindow();
			m_peer.ReleaseMessage(message);
		}
		else if (num >= 0 && num <= m_windowSize)
		{
			m_windowStart = (m_windowStart + num) % 1024;
			m_peer.ReleaseMessage(message);
		}
	}
}
