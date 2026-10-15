using System;
using System.Diagnostics;
using System.Net;
using System.Threading;

namespace Lidgren.Network;

[DebuggerDisplay("RemoteUniqueIdentifier={RemoteUniqueIdentifier} RemoteEndPoint={m_remoteEndPoint}")]
public class NetConnection
{
	private enum ExpandMTUStatus
	{
		None,
		InProgress,
		Finished
	}

	private const int m_infrequentEventsSkipFrames = 8;

	private const int m_messageCoalesceFrames = 3;

	private const int c_protocolMaxMTU = 8190;

	internal NetPeer m_peer;

	internal NetPeerConfiguration m_peerConfiguration;

	internal NetConnectionStatus m_status;

	internal NetConnectionStatus m_outputtedStatus;

	internal NetConnectionStatus m_visibleStatus;

	internal IPEndPoint m_remoteEndPoint;

	internal NetSenderChannelBase[] m_sendChannels;

	internal NetReceiverChannelBase[] m_receiveChannels;

	internal NetOutgoingMessage m_localHailMessage;

	internal long m_remoteUniqueIdentifier;

	internal NetQueue<NetTuple<NetMessageType, int>> m_queuedOutgoingAcks;

	internal NetQueue<NetTuple<NetMessageType, int>> m_queuedIncomingAcks;

	private int m_sendBufferWritePtr;

	private int m_sendBufferNumMessages;

	private object m_tag;

	internal NetConnectionStatistics m_statistics;

	internal bool m_connectRequested;

	internal bool m_disconnectRequested;

	internal bool m_disconnectReqSendBye;

	internal string m_disconnectMessage;

	internal bool m_connectionInitiator;

	internal NetIncomingMessage m_remoteHailMessage;

	internal double m_lastHandshakeSendTime;

	internal int m_handshakeAttempts;

	private double m_sentPingTime;

	private int m_sentPingNumber;

	private double m_averageRoundtripTime;

	private double m_timeoutDeadline = double.MaxValue;

	internal double m_remoteTimeOffset;

	private ExpandMTUStatus m_expandMTUStatus;

	private int m_largestSuccessfulMTU;

	private int m_smallestFailedMTU;

	private int m_lastSentMTUAttemptSize;

	private double m_lastSentMTUAttemptTime;

	private int m_mtuAttemptFails;

	internal int m_currentMTU;

	public object Tag
	{
		get
		{
			return m_tag;
		}
		set
		{
			m_tag = value;
		}
	}

	public NetPeer Peer => m_peer;

	public NetConnectionStatus Status => m_visibleStatus;

	public NetConnectionStatistics Statistics => m_statistics;

	public IPEndPoint RemoteEndPoint => m_remoteEndPoint;

	public long RemoteUniqueIdentifier => m_remoteUniqueIdentifier;

	public NetOutgoingMessage LocalHailMessage => m_localHailMessage;

	public NetIncomingMessage RemoteHailMessage => m_remoteHailMessage;

	public float AverageRoundtripTime => (float)m_averageRoundtripTime;

	public float RemoteTimeOffset => (float)m_remoteTimeOffset;

	public int CurrentMTU => m_currentMTU;

	internal double GetResendDelay()
	{
		double num = m_averageRoundtripTime;
		if (num <= 0.0)
		{
			num = 0.1;
		}
		return 0.025 + num * 2.1;
	}

	internal NetConnection(NetPeer peer, IPEndPoint remoteEndPoint)
	{
		m_peer = peer;
		m_peerConfiguration = m_peer.Configuration;
		m_status = NetConnectionStatus.None;
		m_outputtedStatus = NetConnectionStatus.None;
		m_visibleStatus = NetConnectionStatus.None;
		m_remoteEndPoint = remoteEndPoint;
		m_sendChannels = new NetSenderChannelBase[99];
		m_receiveChannels = new NetReceiverChannelBase[99];
		m_queuedOutgoingAcks = new NetQueue<NetTuple<NetMessageType, int>>(4);
		m_queuedIncomingAcks = new NetQueue<NetTuple<NetMessageType, int>>(4);
		m_statistics = new NetConnectionStatistics(this);
		m_averageRoundtripTime = -1.0;
		m_currentMTU = m_peerConfiguration.MaximumTransmissionUnit;
	}

	internal void MutateEndPoint(IPEndPoint endPoint)
	{
		m_remoteEndPoint = endPoint;
	}

	internal void ResetTimeout(double now)
	{
		m_timeoutDeadline = now + (double)m_peerConfiguration.m_connectionTimeout;
	}

	internal void SetStatus(NetConnectionStatus status, string reason)
	{
		m_status = status;
		if (reason == null)
		{
			reason = string.Empty;
		}
		if (m_status == NetConnectionStatus.Connected)
		{
			m_timeoutDeadline = NetTime.Now + (double)m_peerConfiguration.m_connectionTimeout;
		}
		if (m_peerConfiguration.IsMessageTypeEnabled(NetIncomingMessageType.StatusChanged))
		{
			if (m_outputtedStatus != status)
			{
				NetIncomingMessage netIncomingMessage = m_peer.CreateIncomingMessage(NetIncomingMessageType.StatusChanged, 4 + reason.Length + ((reason.Length <= 126) ? 1 : 2));
				netIncomingMessage.m_senderConnection = this;
				netIncomingMessage.m_senderEndPoint = m_remoteEndPoint;
				netIncomingMessage.Write((byte)m_status);
				netIncomingMessage.Write(reason);
				m_peer.ReleaseMessage(netIncomingMessage);
				m_outputtedStatus = status;
			}
		}
		else
		{
			m_outputtedStatus = m_status;
			m_visibleStatus = m_status;
		}
	}

	internal void Heartbeat(double now, uint frameCounter)
	{
		if (frameCounter % 8 == 0)
		{
			if (now > m_timeoutDeadline)
			{
				ExecuteDisconnect("Connection timed out", sendByeMessage: true);
				return;
			}
			if (m_status == NetConnectionStatus.Connected)
			{
				if (now > m_sentPingTime + (double)m_peer.m_configuration.m_pingInterval)
				{
					SendPing();
				}
				MTUExpansionHeartbeat(now);
			}
			if (m_disconnectRequested)
			{
				ExecuteDisconnect(m_disconnectMessage, m_disconnectReqSendBye);
				return;
			}
		}
		byte[] sendBuffer = m_peer.m_sendBuffer;
		int currentMTU = m_currentMTU;
		bool connectionReset;
		if (frameCounter % 3 == 0)
		{
			while (m_queuedOutgoingAcks.Count > 0)
			{
				int num = (currentMTU - (m_sendBufferWritePtr + 5)) / 3;
				if (num > m_queuedOutgoingAcks.Count)
				{
					num = m_queuedOutgoingAcks.Count;
				}
				m_sendBufferNumMessages++;
				sendBuffer[m_sendBufferWritePtr++] = 134;
				sendBuffer[m_sendBufferWritePtr++] = 0;
				sendBuffer[m_sendBufferWritePtr++] = 0;
				int num2 = num * 3 * 8;
				sendBuffer[m_sendBufferWritePtr++] = (byte)num2;
				sendBuffer[m_sendBufferWritePtr++] = (byte)(num2 >> 8);
				for (int i = 0; i < num; i++)
				{
					m_queuedOutgoingAcks.TryDequeue(out var item);
					sendBuffer[m_sendBufferWritePtr++] = (byte)item.Item1;
					sendBuffer[m_sendBufferWritePtr++] = (byte)item.Item2;
					sendBuffer[m_sendBufferWritePtr++] = (byte)(item.Item2 >> 8);
				}
				if (m_queuedOutgoingAcks.Count > 0)
				{
					m_peer.SendPacket(m_sendBufferWritePtr, m_remoteEndPoint, m_sendBufferNumMessages, out connectionReset);
					m_sendBufferWritePtr = 0;
					m_sendBufferNumMessages = 0;
				}
			}
			NetTuple<NetMessageType, int> item2;
			while (m_queuedIncomingAcks.TryDequeue(out item2))
			{
				m_sendChannels[(uint)(item2.Item1 - 1)]?.ReceiveAcknowledge(now, item2.Item2);
			}
		}
		if (m_peer.m_executeFlushSendQueue)
		{
			for (int num3 = m_sendChannels.Length - 1; num3 >= 0; num3--)
			{
				NetSenderChannelBase netSenderChannelBase = m_sendChannels[num3];
				if (netSenderChannelBase != null)
				{
					netSenderChannelBase.SendQueuedMessages(now);
					if (netSenderChannelBase.NeedToSendMessages())
					{
						m_peer.m_needFlushSendQueue = true;
					}
				}
			}
		}
		if (m_sendBufferWritePtr > 0)
		{
			m_peer.SendPacket(m_sendBufferWritePtr, m_remoteEndPoint, m_sendBufferNumMessages, out connectionReset);
			m_sendBufferWritePtr = 0;
			m_sendBufferNumMessages = 0;
		}
	}

	internal void QueueSendMessage(NetOutgoingMessage om, int seqNr)
	{
		int encodedSize = om.GetEncodedSize();
		bool connectionReset;
		if (m_sendBufferWritePtr + encodedSize > m_currentMTU && m_sendBufferWritePtr > 0 && m_sendBufferNumMessages > 0)
		{
			m_peer.SendPacket(m_sendBufferWritePtr, m_remoteEndPoint, m_sendBufferNumMessages, out connectionReset);
			m_sendBufferWritePtr = 0;
			m_sendBufferNumMessages = 0;
		}
		m_sendBufferWritePtr = om.Encode(m_peer.m_sendBuffer, m_sendBufferWritePtr, seqNr);
		m_sendBufferNumMessages++;
		if (m_sendBufferWritePtr > m_currentMTU)
		{
			m_peer.SendPacket(m_sendBufferWritePtr, m_remoteEndPoint, m_sendBufferNumMessages, out connectionReset);
			m_sendBufferWritePtr = 0;
			m_sendBufferNumMessages = 0;
		}
		if (m_sendBufferWritePtr > 0)
		{
			m_peer.m_needFlushSendQueue = true;
		}
		Interlocked.Decrement(ref om.m_recyclingCount);
	}

	public NetSendResult SendMessage(NetOutgoingMessage msg, NetDeliveryMethod method, int sequenceChannel)
	{
		return m_peer.SendMessage(msg, this, method, sequenceChannel);
	}

	internal NetSendResult EnqueueMessage(NetOutgoingMessage msg, NetDeliveryMethod method, int sequenceChannel)
	{
		if (m_status != NetConnectionStatus.Connected)
		{
			return NetSendResult.FailedNotConnected;
		}
		NetMessageType tp = (msg.m_messageType = (NetMessageType)((uint)method + (uint)sequenceChannel));
		int num = (int)(method - 1) + sequenceChannel;
		NetSenderChannelBase netSenderChannelBase = m_sendChannels[num];
		if (netSenderChannelBase == null)
		{
			netSenderChannelBase = CreateSenderChannel(tp);
		}
		if (method != NetDeliveryMethod.Unreliable && method != NetDeliveryMethod.UnreliableSequenced && msg.GetEncodedSize() > m_currentMTU)
		{
			m_peer.ThrowOrLog("Reliable message too large! Fragmentation failure?");
		}
		return netSenderChannelBase.Enqueue(msg);
	}

	private NetSenderChannelBase CreateSenderChannel(NetMessageType tp)
	{
		NetSenderChannelBase netSenderChannelBase;
		lock (m_sendChannels)
		{
			NetDeliveryMethod deliveryMethod = NetUtility.GetDeliveryMethod(tp);
			int num = (int)tp - (int)deliveryMethod;
			int num2 = (int)(deliveryMethod - 1) + num;
			if (m_sendChannels[num2] != null)
			{
				netSenderChannelBase = m_sendChannels[num2];
			}
			else
			{
				switch (deliveryMethod)
				{
				case NetDeliveryMethod.Unreliable:
				case NetDeliveryMethod.UnreliableSequenced:
					netSenderChannelBase = new NetUnreliableSenderChannel(this, NetUtility.GetWindowSize(deliveryMethod), deliveryMethod);
					break;
				case NetDeliveryMethod.ReliableOrdered:
					netSenderChannelBase = new NetReliableSenderChannel(this, NetUtility.GetWindowSize(deliveryMethod));
					break;
				default:
					netSenderChannelBase = new NetReliableSenderChannel(this, NetUtility.GetWindowSize(deliveryMethod));
					break;
				}
				m_sendChannels[num2] = netSenderChannelBase;
			}
		}
		return netSenderChannelBase;
	}

	internal void ReceivedLibraryMessage(NetMessageType tp, int ptr, int payloadLength)
	{
		double now = NetTime.Now;
		switch (tp)
		{
		case NetMessageType.ConnectResponse:
			HandleConnectResponse(now, tp, ptr, payloadLength);
			break;
		case NetMessageType.LibraryError:
			m_peer.ThrowOrLog("LibraryError received by ReceivedLibraryMessage; this usually indicates a malformed message");
			break;
		case NetMessageType.Disconnect:
		{
			NetIncomingMessage netIncomingMessage3 = m_peer.SetupReadHelperMessage(ptr, payloadLength);
			m_disconnectRequested = true;
			m_disconnectMessage = netIncomingMessage3.ReadString();
			m_disconnectReqSendBye = false;
			break;
		}
		case NetMessageType.Acknowledge:
		{
			for (int i = 0; i < payloadLength; i += 3)
			{
				NetMessageType item = (NetMessageType)m_peer.m_receiveBuffer[ptr++];
				int num = m_peer.m_receiveBuffer[ptr++];
				num |= m_peer.m_receiveBuffer[ptr++] << 8;
				m_queuedIncomingAcks.Enqueue(new NetTuple<NetMessageType, int>(item, num));
			}
			break;
		}
		case NetMessageType.Ping:
		{
			int pingNumber = m_peer.m_receiveBuffer[ptr++];
			SendPong(pingNumber);
			break;
		}
		case NetMessageType.Pong:
		{
			NetIncomingMessage netIncomingMessage2 = m_peer.SetupReadHelperMessage(ptr, payloadLength);
			int pongNumber = netIncomingMessage2.ReadByte();
			float remoteSendTime = netIncomingMessage2.ReadSingle();
			ReceivedPong(now, pongNumber, remoteSendTime);
			break;
		}
		case NetMessageType.ExpandMTURequest:
			SendMTUSuccess(payloadLength);
			break;
		case NetMessageType.ExpandMTUSuccess:
			if (m_peer.Configuration.AutoExpandMTU)
			{
				NetIncomingMessage netIncomingMessage = m_peer.SetupReadHelperMessage(ptr, payloadLength);
				int size = netIncomingMessage.ReadInt32();
				HandleExpandMTUSuccess(now, size);
			}
			break;
		case NetMessageType.NatIntroduction:
			m_peer.HandleNatIntroduction(ptr);
			break;
		default:
			m_peer.LogWarning("Connection received unhandled library message: " + tp);
			break;
		case NetMessageType.Connect:
		case NetMessageType.ConnectionEstablished:
			break;
		}
	}

	internal void ReceivedMessage(NetIncomingMessage msg)
	{
		NetMessageType receivedMessageType = msg.m_receivedMessageType;
		int num = (int)(receivedMessageType - 1);
		NetReceiverChannelBase netReceiverChannelBase = m_receiveChannels[num];
		if (netReceiverChannelBase == null)
		{
			netReceiverChannelBase = CreateReceiverChannel(receivedMessageType);
		}
		netReceiverChannelBase.ReceiveMessage(msg);
	}

	private NetReceiverChannelBase CreateReceiverChannel(NetMessageType tp)
	{
		NetReceiverChannelBase netReceiverChannelBase = NetUtility.GetDeliveryMethod(tp) switch
		{
			NetDeliveryMethod.Unreliable => new NetUnreliableUnorderedReceiver(this), 
			NetDeliveryMethod.ReliableOrdered => new NetReliableOrderedReceiver(this, 64), 
			NetDeliveryMethod.UnreliableSequenced => new NetUnreliableSequencedReceiver(this), 
			NetDeliveryMethod.ReliableUnordered => new NetReliableUnorderedReceiver(this, 64), 
			NetDeliveryMethod.ReliableSequenced => new NetReliableSequencedReceiver(this, 64), 
			_ => throw new NetException("Unhandled NetDeliveryMethod!"), 
		};
		int num = (int)(tp - 1);
		m_receiveChannels[num] = netReceiverChannelBase;
		return netReceiverChannelBase;
	}

	internal void QueueAck(NetMessageType tp, int sequenceNumber)
	{
		m_queuedOutgoingAcks.Enqueue(new NetTuple<NetMessageType, int>(tp, sequenceNumber));
	}

	public void GetSendQueueInfo(NetDeliveryMethod method, int sequenceChannel, out int windowSize, out int freeWindowSlots)
	{
		int num = (int)(method - 1) + sequenceChannel;
		NetSenderChannelBase netSenderChannelBase = m_sendChannels[num];
		if (netSenderChannelBase == null)
		{
			windowSize = NetUtility.GetWindowSize(method);
			freeWindowSlots = windowSize;
		}
		else
		{
			windowSize = netSenderChannelBase.WindowSize;
			freeWindowSlots = netSenderChannelBase.GetFreeWindowSlots();
		}
	}

	public bool CanSendImmediately(NetDeliveryMethod method, int sequenceChannel)
	{
		int num = (int)(method - 1) + sequenceChannel;
		NetSenderChannelBase netSenderChannelBase = m_sendChannels[num];
		if (netSenderChannelBase == null)
		{
			return true;
		}
		return netSenderChannelBase.GetFreeWindowSlots() > 0;
	}

	internal void Shutdown(string reason)
	{
		ExecuteDisconnect(reason, sendByeMessage: true);
	}

	public override string ToString()
	{
		return string.Concat("[NetConnection to ", m_remoteEndPoint, "]");
	}

	internal void UnconnectedHeartbeat(double now)
	{
		if (m_disconnectRequested)
		{
			ExecuteDisconnect(m_disconnectMessage, sendByeMessage: true);
		}
		if (m_connectRequested)
		{
			switch (m_status)
			{
			case NetConnectionStatus.RespondedConnect:
			case NetConnectionStatus.Connected:
				ExecuteDisconnect("Reconnecting", sendByeMessage: true);
				break;
			case NetConnectionStatus.InitiatedConnect:
				SendConnect(now);
				break;
			case NetConnectionStatus.Disconnected:
				m_peer.ThrowOrLog("This connection is Disconnected; spent. A new one should have been created");
				break;
			default:
				SendConnect(now);
				break;
			case NetConnectionStatus.Disconnecting:
				break;
			}
		}
		else
		{
			if (!(now - m_lastHandshakeSendTime > (double)m_peerConfiguration.m_resendHandshakeInterval))
			{
				return;
			}
			if (m_handshakeAttempts >= m_peerConfiguration.m_maximumHandshakeAttempts)
			{
				ExecuteDisconnect("Failed to establish connection - no response from remote host", sendByeMessage: true);
				return;
			}
			switch (m_status)
			{
			case NetConnectionStatus.InitiatedConnect:
				SendConnect(now);
				break;
			case NetConnectionStatus.RespondedConnect:
				SendConnectResponse(now, onLibraryThread: true);
				break;
			case NetConnectionStatus.RespondedAwaitingApproval:
				m_lastHandshakeSendTime = now;
				break;
			default:
				m_peer.LogWarning("Time to resend handshake, but status is " + m_status);
				break;
			}
		}
	}

	internal void ExecuteDisconnect(string reason, bool sendByeMessage)
	{
		for (int i = 0; i < m_sendChannels.Length; i++)
		{
			m_sendChannels[i]?.Reset();
		}
		if (sendByeMessage)
		{
			SendDisconnect(reason, onLibraryThread: true);
		}
		if (m_status == NetConnectionStatus.ReceivedInitiation)
		{
			m_status = NetConnectionStatus.Disconnected;
		}
		else
		{
			SetStatus(NetConnectionStatus.Disconnected, reason);
		}
		lock (m_peer.m_handshakes)
		{
			m_peer.m_handshakes.Remove(m_remoteEndPoint);
		}
		m_disconnectRequested = false;
		m_connectRequested = false;
		m_handshakeAttempts = 0;
	}

	internal void SendConnect(double now)
	{
		int num = 13 + m_peerConfiguration.AppIdentifier.Length;
		num += ((m_localHailMessage != null) ? m_localHailMessage.LengthBytes : 0);
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(num);
		netOutgoingMessage.m_messageType = NetMessageType.Connect;
		netOutgoingMessage.Write(m_peerConfiguration.AppIdentifier);
		netOutgoingMessage.Write(m_peer.m_uniqueIdentifier);
		netOutgoingMessage.Write((float)now);
		WriteLocalHail(netOutgoingMessage);
		m_peer.SendLibrary(netOutgoingMessage, m_remoteEndPoint);
		m_connectRequested = false;
		m_lastHandshakeSendTime = now;
		m_handshakeAttempts++;
		_ = m_handshakeAttempts;
		_ = 1;
		SetStatus(NetConnectionStatus.InitiatedConnect, "Locally requested connect");
	}

	internal void SendConnectResponse(double now, bool onLibraryThread)
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(m_peerConfiguration.AppIdentifier.Length + 13 + ((m_localHailMessage != null) ? m_localHailMessage.LengthBytes : 0));
		netOutgoingMessage.m_messageType = NetMessageType.ConnectResponse;
		netOutgoingMessage.Write(m_peerConfiguration.AppIdentifier);
		netOutgoingMessage.Write(m_peer.m_uniqueIdentifier);
		netOutgoingMessage.Write((float)now);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		WriteLocalHail(netOutgoingMessage);
		if (onLibraryThread)
		{
			m_peer.SendLibrary(netOutgoingMessage, m_remoteEndPoint);
		}
		else
		{
			m_peer.m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(m_remoteEndPoint, netOutgoingMessage));
		}
		m_lastHandshakeSendTime = now;
		m_handshakeAttempts++;
		_ = m_handshakeAttempts;
		_ = 1;
		SetStatus(NetConnectionStatus.RespondedConnect, "Remotely requested connect");
	}

	internal void SendDisconnect(string reason, bool onLibraryThread)
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(reason);
		netOutgoingMessage.m_messageType = NetMessageType.Disconnect;
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		if (onLibraryThread)
		{
			m_peer.SendLibrary(netOutgoingMessage, m_remoteEndPoint);
		}
		else
		{
			m_peer.m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(m_remoteEndPoint, netOutgoingMessage));
		}
	}

	private void WriteLocalHail(NetOutgoingMessage om)
	{
		if (m_localHailMessage == null)
		{
			return;
		}
		byte[] data = m_localHailMessage.Data;
		if (data != null && data.Length >= m_localHailMessage.LengthBytes)
		{
			if (om.LengthBytes + m_localHailMessage.LengthBytes > m_peerConfiguration.m_maximumTransmissionUnit - 10)
			{
				m_peer.ThrowOrLog("Hail message too large; can maximally be " + (m_peerConfiguration.m_maximumTransmissionUnit - 10 - om.LengthBytes));
			}
			om.Write(m_localHailMessage.Data, 0, m_localHailMessage.LengthBytes);
		}
	}

	internal void SendConnectionEstablished()
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(4);
		netOutgoingMessage.m_messageType = NetMessageType.ConnectionEstablished;
		netOutgoingMessage.Write((float)NetTime.Now);
		m_peer.SendLibrary(netOutgoingMessage, m_remoteEndPoint);
		m_handshakeAttempts = 0;
		InitializePing();
		if (m_status != NetConnectionStatus.Connected)
		{
			SetStatus(NetConnectionStatus.Connected, "Connected to " + NetUtility.ToHexString(m_remoteUniqueIdentifier));
		}
	}

	public void Approve()
	{
		if (m_status != NetConnectionStatus.RespondedAwaitingApproval)
		{
			m_peer.LogWarning("Approve() called in wrong status; expected RespondedAwaitingApproval; got " + m_status);
			return;
		}
		m_localHailMessage = null;
		m_handshakeAttempts = 0;
		SendConnectResponse(NetTime.Now, onLibraryThread: false);
	}

	public void Approve(NetOutgoingMessage localHail)
	{
		if (m_status != NetConnectionStatus.RespondedAwaitingApproval)
		{
			m_peer.LogWarning("Approve() called in wrong status; expected RespondedAwaitingApproval; got " + m_status);
			return;
		}
		m_localHailMessage = localHail;
		m_handshakeAttempts = 0;
		SendConnectResponse(NetTime.Now, onLibraryThread: false);
	}

	public void Deny()
	{
		Deny(string.Empty);
	}

	public void Deny(string reason)
	{
		SendDisconnect(reason, onLibraryThread: false);
		lock (m_peer.m_handshakes)
		{
			m_peer.m_handshakes.Remove(m_remoteEndPoint);
		}
	}

	internal void ReceivedHandshake(double now, NetMessageType tp, int ptr, int payloadLength)
	{
		switch (tp)
		{
		case NetMessageType.Connect:
			if (m_status == NetConnectionStatus.ReceivedInitiation)
			{
				if (!ValidateHandshakeData(ptr, payloadLength, out var hail))
				{
					break;
				}
				if (hail != null)
				{
					m_remoteHailMessage = m_peer.CreateIncomingMessage(NetIncomingMessageType.Data, hail);
					m_remoteHailMessage.LengthBits = hail.Length * 8;
				}
				else
				{
					m_remoteHailMessage = null;
				}
				if (m_peerConfiguration.IsMessageTypeEnabled(NetIncomingMessageType.ConnectionApproval))
				{
					NetIncomingMessage netIncomingMessage2 = m_peer.CreateIncomingMessage(NetIncomingMessageType.ConnectionApproval, (m_remoteHailMessage != null) ? m_remoteHailMessage.LengthBytes : 0);
					netIncomingMessage2.m_receiveTime = now;
					netIncomingMessage2.m_senderConnection = this;
					netIncomingMessage2.m_senderEndPoint = m_remoteEndPoint;
					if (m_remoteHailMessage != null)
					{
						netIncomingMessage2.Write(m_remoteHailMessage.m_data, 0, m_remoteHailMessage.LengthBytes);
					}
					SetStatus(NetConnectionStatus.RespondedAwaitingApproval, "Awaiting approval");
					m_peer.ReleaseMessage(netIncomingMessage2);
				}
				else
				{
					SendConnectResponse((float)now, onLibraryThread: true);
				}
			}
			else if (m_status == NetConnectionStatus.RespondedAwaitingApproval)
			{
				m_peer.LogWarning("Ignoring multiple Connect() most likely due to a delayed Approval");
			}
			else if (m_status == NetConnectionStatus.RespondedConnect)
			{
				SendConnectResponse((float)now, onLibraryThread: true);
			}
			break;
		case NetMessageType.ConnectResponse:
			HandleConnectResponse(now, tp, ptr, payloadLength);
			break;
		case NetMessageType.ConnectionEstablished:
			switch (m_status)
			{
			case NetConnectionStatus.RespondedConnect:
			{
				NetIncomingMessage netIncomingMessage3 = m_peer.SetupReadHelperMessage(ptr, payloadLength);
				InitializeRemoteTimeOffset(netIncomingMessage3.ReadSingle());
				m_peer.AcceptConnection(this);
				InitializePing();
				SetStatus(NetConnectionStatus.Connected, "Connected to " + NetUtility.ToHexString(m_remoteUniqueIdentifier));
				break;
			}
			case NetConnectionStatus.None:
			case NetConnectionStatus.InitiatedConnect:
			case NetConnectionStatus.ReceivedInitiation:
			case NetConnectionStatus.RespondedAwaitingApproval:
			case NetConnectionStatus.Connected:
			case NetConnectionStatus.Disconnecting:
			case NetConnectionStatus.Disconnected:
				break;
			}
			break;
		case NetMessageType.Disconnect:
		{
			string reason = "Ouch";
			try
			{
				NetIncomingMessage netIncomingMessage = m_peer.SetupReadHelperMessage(ptr, payloadLength);
				reason = netIncomingMessage.ReadString();
			}
			catch
			{
			}
			ExecuteDisconnect(reason, sendByeMessage: false);
			break;
		}
		case NetMessageType.Discovery:
			m_peer.HandleIncomingDiscoveryRequest(now, m_remoteEndPoint, ptr, payloadLength);
			break;
		case NetMessageType.DiscoveryResponse:
			m_peer.HandleIncomingDiscoveryResponse(now, m_remoteEndPoint, ptr, payloadLength);
			break;
		case NetMessageType.Ping:
		case NetMessageType.Pong:
		case NetMessageType.Acknowledge:
			break;
		}
	}

	private void HandleConnectResponse(double now, NetMessageType tp, int ptr, int payloadLength)
	{
		switch (m_status)
		{
		case NetConnectionStatus.InitiatedConnect:
		{
			if (ValidateHandshakeData(ptr, payloadLength, out var hail))
			{
				if (hail != null)
				{
					m_remoteHailMessage = m_peer.CreateIncomingMessage(NetIncomingMessageType.Data, hail);
					m_remoteHailMessage.LengthBits = hail.Length * 8;
				}
				else
				{
					m_remoteHailMessage = null;
				}
				m_peer.AcceptConnection(this);
				SendConnectionEstablished();
			}
			break;
		}
		case NetConnectionStatus.Connected:
			SendConnectionEstablished();
			break;
		case NetConnectionStatus.None:
		case NetConnectionStatus.ReceivedInitiation:
		case NetConnectionStatus.RespondedAwaitingApproval:
		case NetConnectionStatus.RespondedConnect:
		case NetConnectionStatus.Disconnecting:
		case NetConnectionStatus.Disconnected:
			break;
		}
	}

	private bool ValidateHandshakeData(int ptr, int payloadLength, out byte[] hail)
	{
		hail = null;
		NetIncomingMessage netIncomingMessage = m_peer.SetupReadHelperMessage(ptr, payloadLength);
		try
		{
			string text = netIncomingMessage.ReadString();
			long remoteUniqueIdentifier = netIncomingMessage.ReadInt64();
			InitializeRemoteTimeOffset(netIncomingMessage.ReadSingle());
			int num = payloadLength - (netIncomingMessage.PositionInBytes - ptr);
			if (num > 0)
			{
				hail = netIncomingMessage.ReadBytes(num);
			}
			if (text != m_peer.m_configuration.AppIdentifier)
			{
				ExecuteDisconnect("Wrong application identifier!", sendByeMessage: true);
				return false;
			}
			m_remoteUniqueIdentifier = remoteUniqueIdentifier;
		}
		catch (Exception ex)
		{
			ExecuteDisconnect("Handshake data validation failed", sendByeMessage: true);
			m_peer.LogWarning("ReadRemoteHandshakeData failed: " + ex.Message);
			return false;
		}
		return true;
	}

	public void Disconnect(string byeMessage)
	{
		if (m_status != NetConnectionStatus.None && m_status != NetConnectionStatus.Disconnected)
		{
			m_disconnectMessage = byeMessage;
			if (m_status != NetConnectionStatus.Disconnected && m_status != NetConnectionStatus.None)
			{
				SetStatus(NetConnectionStatus.Disconnecting, byeMessage);
			}
			m_handshakeAttempts = 0;
			m_disconnectRequested = true;
			m_disconnectReqSendBye = true;
		}
	}

	internal void InitializeRemoteTimeOffset(float remoteSendTime)
	{
		m_remoteTimeOffset = (double)remoteSendTime + m_averageRoundtripTime / 2.0 - NetTime.Now;
	}

	public double GetLocalTime(double remoteTimestamp)
	{
		return remoteTimestamp - m_remoteTimeOffset;
	}

	public double GetRemoteTime(double localTimestamp)
	{
		return localTimestamp + m_remoteTimeOffset;
	}

	internal void InitializePing()
	{
		double num = (m_sentPingTime = NetTime.Now);
		m_sentPingTime -= m_peerConfiguration.PingInterval * 0.25f;
		m_sentPingTime -= MWCRandom.Instance.NextSingle() * (m_peerConfiguration.PingInterval * 0.75f);
		m_timeoutDeadline = num + (double)m_peerConfiguration.m_connectionTimeout * 2.0;
		SendPing();
	}

	internal void SendPing()
	{
		m_sentPingNumber++;
		m_sentPingTime = NetTime.Now;
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(1);
		netOutgoingMessage.Write((byte)m_sentPingNumber);
		netOutgoingMessage.m_messageType = NetMessageType.Ping;
		int numBytes = netOutgoingMessage.Encode(m_peer.m_sendBuffer, 0, 0);
		m_peer.SendPacket(numBytes, m_remoteEndPoint, 1, out var _);
		m_peer.Recycle(netOutgoingMessage);
	}

	internal void SendPong(int pingNumber)
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(5);
		netOutgoingMessage.Write((byte)pingNumber);
		netOutgoingMessage.Write((float)NetTime.Now);
		netOutgoingMessage.m_messageType = NetMessageType.Pong;
		int numBytes = netOutgoingMessage.Encode(m_peer.m_sendBuffer, 0, 0);
		m_peer.SendPacket(numBytes, m_remoteEndPoint, 1, out var _);
		m_peer.Recycle(netOutgoingMessage);
	}

	internal void ReceivedPong(double now, int pongNumber, float remoteSendTime)
	{
		if ((byte)pongNumber != (byte)m_sentPingNumber)
		{
			return;
		}
		m_timeoutDeadline = now + (double)m_peerConfiguration.m_connectionTimeout;
		double num = now - m_sentPingTime;
		double num2 = (double)remoteSendTime + num / 2.0 - now;
		if (m_averageRoundtripTime < 0.0)
		{
			m_remoteTimeOffset = num2;
			m_averageRoundtripTime = num;
		}
		else
		{
			m_averageRoundtripTime = m_averageRoundtripTime * 0.7 + num * 0.3;
			m_remoteTimeOffset = (m_remoteTimeOffset * (double)(m_sentPingNumber - 1) + num2) / (double)m_sentPingNumber;
		}
		double resendDelay = GetResendDelay();
		NetSenderChannelBase[] sendChannels = m_sendChannels;
		foreach (NetSenderChannelBase netSenderChannelBase in sendChannels)
		{
			if (netSenderChannelBase is NetReliableSenderChannel netReliableSenderChannel)
			{
				netReliableSenderChannel.m_resendDelay = resendDelay;
			}
		}
		if (m_peer.m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.ConnectionLatencyUpdated))
		{
			NetIncomingMessage netIncomingMessage = m_peer.CreateIncomingMessage(NetIncomingMessageType.ConnectionLatencyUpdated, 4);
			netIncomingMessage.m_senderConnection = this;
			netIncomingMessage.m_senderEndPoint = m_remoteEndPoint;
			netIncomingMessage.Write((float)num);
			m_peer.ReleaseMessage(netIncomingMessage);
		}
	}

	internal void InitExpandMTU(double now)
	{
		m_lastSentMTUAttemptTime = now + (double)m_peerConfiguration.m_expandMTUFrequency + 1.5 + m_averageRoundtripTime;
		m_largestSuccessfulMTU = 512;
		m_smallestFailedMTU = -1;
		m_currentMTU = m_peerConfiguration.MaximumTransmissionUnit;
	}

	private void MTUExpansionHeartbeat(double now)
	{
		if (m_expandMTUStatus == ExpandMTUStatus.Finished)
		{
			return;
		}
		if (m_expandMTUStatus == ExpandMTUStatus.None)
		{
			if (!m_peerConfiguration.m_autoExpandMTU)
			{
				FinalizeMTU(m_currentMTU);
			}
			else
			{
				ExpandMTU(now);
			}
		}
		else if (now > m_lastSentMTUAttemptTime + (double)m_peerConfiguration.ExpandMTUFrequency)
		{
			m_mtuAttemptFails++;
			if (m_mtuAttemptFails == 3)
			{
				FinalizeMTU(m_currentMTU);
				return;
			}
			m_smallestFailedMTU = m_lastSentMTUAttemptSize;
			ExpandMTU(now);
		}
	}

	private void ExpandMTU(double now)
	{
		int num = ((m_smallestFailedMTU != -1) ? ((int)(((float)m_smallestFailedMTU + (float)m_largestSuccessfulMTU) / 2f)) : ((int)((float)m_currentMTU * 1.25f)));
		if (num > 8190)
		{
			num = 8190;
		}
		if (num == m_largestSuccessfulMTU)
		{
			FinalizeMTU(m_largestSuccessfulMTU);
		}
		else
		{
			SendExpandMTU(now, num);
		}
	}

	private void SendExpandMTU(double now, int size)
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(size);
		byte[] source = new byte[size];
		netOutgoingMessage.Write(source);
		netOutgoingMessage.m_messageType = NetMessageType.ExpandMTURequest;
		int numBytes = netOutgoingMessage.Encode(m_peer.m_sendBuffer, 0, 0);
		if (!m_peer.SendMTUPacket(numBytes, m_remoteEndPoint))
		{
			if (m_smallestFailedMTU == -1 || size < m_smallestFailedMTU)
			{
				m_smallestFailedMTU = size;
				m_mtuAttemptFails++;
				if (m_mtuAttemptFails >= m_peerConfiguration.ExpandMTUFailAttempts)
				{
					FinalizeMTU(m_largestSuccessfulMTU);
					return;
				}
			}
			ExpandMTU(now);
		}
		else
		{
			m_lastSentMTUAttemptSize = size;
			m_lastSentMTUAttemptTime = now;
			m_peer.Recycle(netOutgoingMessage);
		}
	}

	private void FinalizeMTU(int size)
	{
		if (m_expandMTUStatus != ExpandMTUStatus.Finished)
		{
			m_expandMTUStatus = ExpandMTUStatus.Finished;
			m_currentMTU = size;
			_ = m_currentMTU;
			_ = m_peerConfiguration.m_maximumTransmissionUnit;
		}
	}

	private void SendMTUSuccess(int size)
	{
		NetOutgoingMessage netOutgoingMessage = m_peer.CreateMessage(4);
		netOutgoingMessage.Write(size);
		netOutgoingMessage.m_messageType = NetMessageType.ExpandMTUSuccess;
		int numBytes = netOutgoingMessage.Encode(m_peer.m_sendBuffer, 0, 0);
		m_peer.SendPacket(numBytes, m_remoteEndPoint, 1, out var _);
		m_peer.Recycle(netOutgoingMessage);
	}

	private void HandleExpandMTUSuccess(double now, int size)
	{
		if (size > m_largestSuccessfulMTU)
		{
			m_largestSuccessfulMTU = size;
		}
		if (size >= m_currentMTU)
		{
			m_currentMTU = size;
			ExpandMTU(now);
		}
	}
}
