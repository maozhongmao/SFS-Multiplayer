namespace Lidgren.Network;

internal abstract class NetSenderChannelBase
{
	protected readonly NetQueue<NetOutgoingMessage> m_queuedSends;

	internal abstract int WindowSize { get; }

	internal int QueuedSendsCount => m_queuedSends.Count;

	protected NetSenderChannelBase(NetQueue<NetOutgoingMessage> queuedSends)
	{
		m_queuedSends = queuedSends;
	}

	internal abstract int GetAllowedSends();

	internal virtual bool NeedToSendMessages()
	{
		return m_queuedSends.Count > 0;
	}

	public int GetFreeWindowSlots()
	{
		return GetAllowedSends() - m_queuedSends.Count;
	}

	internal abstract NetSendResult Enqueue(NetOutgoingMessage message);

	internal abstract void SendQueuedMessages(double now);

	internal abstract void Reset();

	internal abstract void ReceiveAcknowledge(double now, int sequenceNumber);
}
