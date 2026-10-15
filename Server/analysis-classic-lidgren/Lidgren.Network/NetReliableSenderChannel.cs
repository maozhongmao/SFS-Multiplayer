using System.Threading;

namespace Lidgren.Network;

internal sealed class NetReliableSenderChannel : NetSenderChannelBase
{
	private NetConnection m_connection;

	private int m_windowStart;

	private int m_windowSize;

	private int m_sendStart;

	private bool m_anyStoredResends;

	private NetBitVector m_receivedAcks;

	internal NetStoredReliableMessage[] m_storedMessages;

	internal double m_resendDelay;

	internal override int WindowSize => m_windowSize;

	internal override bool NeedToSendMessages()
	{
		if (!base.NeedToSendMessages())
		{
			return m_anyStoredResends;
		}
		return true;
	}

	internal NetReliableSenderChannel(NetConnection connection, int windowSize)
	{
		m_connection = connection;
		m_windowSize = windowSize;
		m_windowStart = 0;
		m_sendStart = 0;
		m_anyStoredResends = false;
		m_receivedAcks = new NetBitVector(1024);
		m_storedMessages = new NetStoredReliableMessage[m_windowSize];
		m_queuedSends = new NetQueue<NetOutgoingMessage>(8);
		m_resendDelay = m_connection.GetResendDelay();
	}

	internal override int GetAllowedSends()
	{
		return m_windowSize - (m_sendStart + 1024 - m_windowStart) % 1024;
	}

	internal override void Reset()
	{
		m_receivedAcks.Clear();
		for (int i = 0; i < m_storedMessages.Length; i++)
		{
			m_storedMessages[i].Reset();
		}
		m_anyStoredResends = false;
		m_queuedSends.Clear();
		m_windowStart = 0;
		m_sendStart = 0;
	}

	internal override NetSendResult Enqueue(NetOutgoingMessage message)
	{
		m_queuedSends.Enqueue(message);
		m_connection.m_peer.m_needFlushSendQueue = true;
		if (m_queuedSends.Count <= GetAllowedSends())
		{
			return NetSendResult.Sent;
		}
		return NetSendResult.Queued;
	}

	internal override void SendQueuedMessages(double now)
	{
		m_anyStoredResends = false;
		for (int i = 0; i < m_storedMessages.Length; i++)
		{
			NetStoredReliableMessage netStoredReliableMessage = m_storedMessages[i];
			NetOutgoingMessage message = netStoredReliableMessage.Message;
			if (message != null)
			{
				m_anyStoredResends = true;
				double lastSent = netStoredReliableMessage.LastSent;
				if (lastSent > 0.0 && now - lastSent > m_resendDelay)
				{
					Interlocked.Increment(ref message.m_recyclingCount);
					m_connection.QueueSendMessage(message, netStoredReliableMessage.SequenceNumber);
					m_storedMessages[i].LastSent = now;
					m_storedMessages[i].NumSent++;
				}
			}
		}
		int num = GetAllowedSends();
		if (num < 1)
		{
			return;
		}
		while (num > 0 && m_queuedSends.Count > 0)
		{
			if (m_queuedSends.TryDequeue(out var item))
			{
				ExecuteSend(now, item);
			}
			num--;
		}
	}

	private void ExecuteSend(double now, NetOutgoingMessage message)
	{
		int sendStart = m_sendStart;
		m_sendStart = (m_sendStart + 1) % 1024;
		Interlocked.Increment(ref message.m_recyclingCount);
		m_connection.QueueSendMessage(message, sendStart);
		int num = sendStart % m_windowSize;
		m_storedMessages[num].NumSent++;
		m_storedMessages[num].Message = message;
		m_storedMessages[num].LastSent = now;
		m_storedMessages[num].SequenceNumber = sendStart;
		m_anyStoredResends = true;
	}

	private void DestoreMessage(double now, int storeIndex, out bool resetTimeout)
	{
		NetStoredReliableMessage netStoredReliableMessage = m_storedMessages[storeIndex];
		resetTimeout = netStoredReliableMessage.NumSent == 1 && now - netStoredReliableMessage.LastSent < 2.0;
		NetOutgoingMessage message = netStoredReliableMessage.Message;
		Interlocked.Decrement(ref message.m_recyclingCount);
		if (message != null && message.m_recyclingCount <= 0)
		{
			m_connection.m_peer.Recycle(message);
		}
		m_storedMessages[storeIndex] = default(NetStoredReliableMessage);
	}

	internal override void ReceiveAcknowledge(double now, int seqNr)
	{
		int num = NetUtility.RelativeSequenceNumber(seqNr, m_windowStart);
		if (num < 0)
		{
			return;
		}
		if (num == 0)
		{
			m_receivedAcks[m_windowStart] = false;
			DestoreMessage(now, m_windowStart % m_windowSize, out var resetTimeout);
			m_windowStart = (m_windowStart + 1) % 1024;
			while (m_receivedAcks.Get(m_windowStart))
			{
				m_receivedAcks[m_windowStart] = false;
				DestoreMessage(now, m_windowStart % m_windowSize, out var resetTimeout2);
				resetTimeout = resetTimeout || resetTimeout2;
				m_windowStart = (m_windowStart + 1) % 1024;
			}
			if (resetTimeout)
			{
				m_connection.ResetTimeout(now);
			}
			return;
		}
		int num2 = NetUtility.RelativeSequenceNumber(seqNr, m_sendStart);
		if (num2 <= 0)
		{
			if (!m_receivedAcks[seqNr])
			{
				m_receivedAcks[seqNr] = true;
			}
		}
		else if (num2 > 0)
		{
			return;
		}
		int num3 = seqNr;
		do
		{
			num3--;
			if (num3 < 0)
			{
				num3 = 1023;
			}
			if (m_receivedAcks[num3])
			{
				continue;
			}
			int num4 = num3 % m_windowSize;
			if (m_storedMessages[num4].NumSent == 1)
			{
				NetOutgoingMessage message = m_storedMessages[num4].Message;
				if (!(now - m_storedMessages[num4].LastSent < m_resendDelay * 0.35))
				{
					m_storedMessages[num4].LastSent = now;
					m_storedMessages[num4].NumSent++;
					Interlocked.Increment(ref message.m_recyclingCount);
					m_connection.QueueSendMessage(message, num3);
				}
			}
		}
		while (num3 != m_windowStart);
	}
}
