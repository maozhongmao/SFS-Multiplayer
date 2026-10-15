namespace Lidgren.Network;

internal sealed class NetUnreliableSequencedReceiver : NetReceiverChannelBase
{
	private int m_lastReceivedSequenceNumber = -1;

	public NetUnreliableSequencedReceiver(NetConnection connection)
		: base(connection)
	{
	}

	internal override void ReceiveMessage(NetIncomingMessage msg)
	{
		int sequenceNumber = msg.m_sequenceNumber;
		m_connection.QueueAck(msg.m_receivedMessageType, sequenceNumber);
		int num = NetUtility.RelativeSequenceNumber(sequenceNumber, m_lastReceivedSequenceNumber + 1);
		if (num >= 0)
		{
			m_lastReceivedSequenceNumber = sequenceNumber;
			m_peer.ReleaseMessage(msg);
		}
	}
}
