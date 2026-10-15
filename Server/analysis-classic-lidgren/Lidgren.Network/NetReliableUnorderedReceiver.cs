namespace Lidgren.Network;

internal sealed class NetReliableUnorderedReceiver : NetReceiverChannelBase
{
	private int m_windowStart;

	private int m_windowSize;

	private NetBitVector m_earlyReceived;

	public NetReliableUnorderedReceiver(NetConnection connection, int windowSize)
		: base(connection)
	{
		m_windowSize = windowSize;
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
				AdvanceWindow();
			}
		}
		else if (num >= 0 && num <= m_windowSize && !m_earlyReceived.Get(message.m_sequenceNumber % m_windowSize))
		{
			m_earlyReceived.Set(message.m_sequenceNumber % m_windowSize, value: true);
			m_peer.ReleaseMessage(message);
		}
	}
}
