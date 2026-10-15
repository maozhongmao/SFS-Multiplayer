using System.Diagnostics;
using System.Net;

namespace Lidgren.Network;

[DebuggerDisplay("Type={MessageType} LengthBits={LengthBits}")]
public sealed class NetIncomingMessage : NetBuffer
{
	internal NetIncomingMessageType m_incomingMessageType;

	internal IPEndPoint m_senderEndPoint;

	internal NetConnection m_senderConnection;

	internal int m_sequenceNumber;

	internal NetMessageType m_receivedMessageType;

	internal bool m_isFragment;

	internal double m_receiveTime;

	public NetIncomingMessageType MessageType => m_incomingMessageType;

	public NetDeliveryMethod DeliveryMethod => NetUtility.GetDeliveryMethod(m_receivedMessageType);

	public int SequenceChannel => (int)m_receivedMessageType - (int)NetUtility.GetDeliveryMethod(m_receivedMessageType);

	public IPEndPoint SenderEndPoint => m_senderEndPoint;

	public NetConnection SenderConnection => m_senderConnection;

	public double ReceiveTime => m_receiveTime;

	internal NetIncomingMessage()
	{
	}

	internal NetIncomingMessage(NetIncomingMessageType tp)
	{
		m_incomingMessageType = tp;
	}

	internal void Reset()
	{
		m_incomingMessageType = NetIncomingMessageType.Error;
		m_readPosition = 0;
		m_receivedMessageType = NetMessageType.LibraryError;
		m_senderConnection = null;
		m_bitLength = 0;
		m_isFragment = false;
	}

	public bool Decrypt(NetEncryption encryption)
	{
		return encryption.Decrypt(this);
	}

	public double ReadTime(bool highPrecision)
	{
		return ReadTime(m_senderConnection, highPrecision);
	}

	public override string ToString()
	{
		return "[NetIncomingMessage #" + m_sequenceNumber + " " + base.LengthBytes + " bytes]";
	}
}
