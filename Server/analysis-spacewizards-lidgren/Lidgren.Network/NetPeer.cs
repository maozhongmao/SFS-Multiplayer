using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;

namespace Lidgren.Network;

public class NetPeer
{
	private sealed class DelayedPacket
	{
		public byte[] Data;

		public double DelayedUntil;

		public IPEndPoint Target;

		public DelayedPacket(byte[] data, double delayedUntil, IPEndPoint target)
		{
			Data = data;
			DelayedUntil = delayedUntil;
			Target = target;
		}
	}

	[InterpolatedStringHandler]
	internal ref struct NetErrorLogInterpolatedStringHandler
	{
		public DefaultInterpolatedStringHandler Implementation;

		public NetErrorLogInterpolatedStringHandler(int literalLength, int formattedCount, NetPeer peer, out bool handlerIsValid)
		{
			Implementation = default(DefaultInterpolatedStringHandler);
			handlerIsValid = peer.CheckLogEnabled(NetIncomingMessageType.ErrorMessage);
			if (handlerIsValid)
			{
				Implementation = new DefaultInterpolatedStringHandler(literalLength, formattedCount);
			}
		}

		public void AppendLiteral(string value)
		{
			Implementation.AppendLiteral(value);
		}

		public void AppendFormatted<T>(T t, int alignment = 0, string? format = null)
		{
			Implementation.AppendFormatted(t, alignment, format);
		}

		public string ToStringAndClear()
		{
			return Implementation.ToStringAndClear();
		}
	}

	[InterpolatedStringHandler]
	internal ref struct NetWarningLogInterpolatedStringHandler
	{
		public DefaultInterpolatedStringHandler Implementation;

		public NetWarningLogInterpolatedStringHandler(int literalLength, int formattedCount, NetPeer peer, out bool handlerIsValid)
		{
			Implementation = default(DefaultInterpolatedStringHandler);
			handlerIsValid = peer.CheckLogEnabled(NetIncomingMessageType.WarningMessage);
			if (handlerIsValid)
			{
				Implementation = new DefaultInterpolatedStringHandler(literalLength, formattedCount);
			}
		}

		public void AppendLiteral(string value)
		{
			Implementation.AppendLiteral(value);
		}

		public void AppendFormatted<T>(T t, int alignment = 0, string? format = null)
		{
			Implementation.AppendFormatted(t, alignment, format);
		}

		public string ToStringAndClear()
		{
			return Implementation.ToStringAndClear();
		}
	}

	private sealed class ReceivedFragmentGroup
	{
		public byte[] Data { get; }

		public NetBitVector ReceivedChunks { get; }

		public ReceivedFragmentGroup(byte[] data, NetBitVector receivedChunks)
		{
			Data = data;
			ReceivedChunks = receivedChunks;
		}
	}

	private const byte HostByte = 1;

	private const byte ClientByte = 0;

	private static int s_initializedPeersCount;

	private int m_listenPort;

	private object? m_tag;

	private readonly object m_messageReceivedEventCreationLock = new object();

	internal readonly List<NetConnection> m_connections;

	private readonly Dictionary<NetSocketAddress, NetConnection> m_connectionLookup;

	private string? m_shutdownReason;

	private int m_lastUsedFragmentGroup;

	private readonly Dictionary<NetConnection, Dictionary<int, ReceivedFragmentGroup>> m_receivedFragmentGroups;

	private NetPeerStatus m_status;

	private Thread? m_networkThread;

	private Socket? m_socket;

	internal readonly byte[] m_sendBuffer;

	internal readonly byte[] m_receiveBuffer;

	internal readonly NetIncomingMessage m_readHelperMessage;

	private EndPoint m_senderRemote;

	internal readonly object m_initializeLock = new object();

	private uint m_frameCounter;

	private double m_lastHeartbeat;

	private double m_lastSocketBind = -3.4028234663852886E+38;

	private NetUPnP? m_upnp;

	internal bool m_needFlushSendQueue;

	internal readonly NetPeerConfiguration m_configuration;

	private readonly NetQueue<NetIncomingMessage> m_releasedIncomingMessages;

	internal readonly NetQueue<(IPEndPoint, NetOutgoingMessage)> m_unsentUnconnectedMessages;

	internal Dictionary<IPEndPoint, NetConnection> m_handshakes;

	internal readonly NetPeerStatistics m_statistics;

	internal long m_uniqueIdentifier;

	internal bool m_executeFlushSendQueue;

	private AutoResetEvent? m_messageReceivedEvent;

	private List<(SynchronizationContext, SendOrPostCallback)>? m_receiveCallbacks;

	internal Action? m_onShutdown;

	private static readonly object _bindSocketMutex = new object();

	private readonly List<DelayedPacket> m_delayedPackets = new List<DelayedPacket>();

	private readonly MWCRandom m_latencyRandom = new MWCRandom();

	private readonly IPEndPoint targetCopy = new IPEndPoint(IPAddress.Any, 0);

	private readonly IPEndPoint targetCopy2 = new IPEndPoint(IPAddress.Any, 0);

	internal List<byte[]?>? m_storagePool;

	private NetQueue<NetOutgoingMessage>? m_outgoingMessagesPool;

	private NetQueue<NetIncomingMessage>? m_incomingMessagesPool;

	internal int m_storagePoolBytes;

	internal int m_storageSlotsUsedCount;

	private int m_maxCacheCount;

	public NetPeerStatus Status => m_status;

	public AutoResetEvent MessageReceivedEvent
	{
		get
		{
			if (m_messageReceivedEvent == null)
			{
				lock (m_messageReceivedEventCreationLock)
				{
					if (m_messageReceivedEvent == null)
					{
						m_messageReceivedEvent = new AutoResetEvent(initialState: false);
					}
				}
			}
			return m_messageReceivedEvent;
		}
	}

	public long UniqueIdentifier => m_uniqueIdentifier;

	public int Port => m_listenPort;

	public NetUPnP? UPnP => m_upnp;

	public object? Tag
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

	public List<NetConnection> Connections
	{
		get
		{
			lock (m_connections)
			{
				return new List<NetConnection>(m_connections);
			}
		}
	}

	public int ConnectionsCount => m_connections.Count;

	public NetPeerStatistics Statistics => m_statistics;

	public NetPeerConfiguration Configuration => m_configuration;

	public Socket? Socket => m_socket;

	private static bool CanAutoExpandMTU
	{
		get
		{
			if (!NetNativeSocket.IsLinux)
			{
				return NetNativeSocket.IsWindows;
			}
			return true;
		}
	}

	internal event Action<NetIncomingMessageType, string>? LogEvent;

	public void Introduce(IPEndPoint hostInternal, IPEndPoint hostExternal, IPEndPoint clientInternal, IPEndPoint clientExternal, string token)
	{
		NetOutgoingMessage netOutgoingMessage = CreateMessage(10 + token.Length + 1);
		netOutgoingMessage.m_messageType = NetMessageType.NatIntroduction;
		netOutgoingMessage.Write((byte)0);
		netOutgoingMessage.Write(hostInternal);
		netOutgoingMessage.Write(hostExternal);
		netOutgoingMessage.Write(token);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((clientExternal, netOutgoingMessage));
		netOutgoingMessage = CreateMessage(10 + token.Length + 1);
		netOutgoingMessage.m_messageType = NetMessageType.NatIntroduction;
		netOutgoingMessage.Write((byte)1);
		netOutgoingMessage.Write(clientInternal);
		netOutgoingMessage.Write(clientExternal);
		netOutgoingMessage.Write(token);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((hostExternal, netOutgoingMessage));
	}

	internal void HandleNatIntroduction(int ptr)
	{
		NetIncomingMessage netIncomingMessage = SetupReadHelperMessage(ptr, 1000);
		byte b = netIncomingMessage.ReadByte();
		IPEndPoint item = netIncomingMessage.ReadIPEndPoint();
		IPEndPoint item2 = netIncomingMessage.ReadIPEndPoint();
		string source = netIncomingMessage.ReadString();
		if (b != 0 || m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.NatIntroductionSuccess))
		{
			NetOutgoingMessage netOutgoingMessage = CreateMessage(1);
			netOutgoingMessage.m_messageType = NetMessageType.NatPunchMessage;
			netOutgoingMessage.Write(b);
			netOutgoingMessage.Write(source);
			Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue((item, netOutgoingMessage));
			netOutgoingMessage = CreateMessage(1);
			netOutgoingMessage.m_messageType = NetMessageType.NatPunchMessage;
			netOutgoingMessage.Write(b);
			netOutgoingMessage.Write(source);
			Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue((item2, netOutgoingMessage));
		}
	}

	private void HandleNatPunch(int ptr, IPEndPoint senderEndPoint)
	{
		NetIncomingMessage netIncomingMessage = SetupReadHelperMessage(ptr, 1000);
		bool flag = netIncomingMessage.ReadByte() == 0;
		string source = netIncomingMessage.ReadString();
		if (flag)
		{
			NetOutgoingMessage netOutgoingMessage = CreateMessage(1);
			netOutgoingMessage.m_messageType = NetMessageType.NatIntroductionConfirmed;
			netOutgoingMessage.Write((byte)1);
			netOutgoingMessage.Write(source);
			Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue((senderEndPoint, netOutgoingMessage));
		}
		else
		{
			NetOutgoingMessage netOutgoingMessage2 = CreateMessage(1);
			netOutgoingMessage2.m_messageType = NetMessageType.NatIntroductionConfirmRequest;
			netOutgoingMessage2.Write((byte)0);
			netOutgoingMessage2.Write(source);
			Interlocked.Increment(ref netOutgoingMessage2.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue((senderEndPoint, netOutgoingMessage2));
		}
	}

	private void HandleNatPunchConfirmRequest(int ptr, IPEndPoint senderEndPoint)
	{
		NetIncomingMessage netIncomingMessage = SetupReadHelperMessage(ptr, 1000);
		bool flag = netIncomingMessage.ReadByte() == 0;
		string source = netIncomingMessage.ReadString();
		NetOutgoingMessage netOutgoingMessage = CreateMessage(1);
		netOutgoingMessage.m_messageType = NetMessageType.NatIntroductionConfirmed;
		netOutgoingMessage.Write(flag ? ((byte)1) : ((byte)0));
		netOutgoingMessage.Write(source);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((senderEndPoint, netOutgoingMessage));
	}

	private void HandleNatPunchConfirmed(int ptr, IPEndPoint senderEndPoint)
	{
		NetIncomingMessage netIncomingMessage = SetupReadHelperMessage(ptr, 1000);
		if (netIncomingMessage.ReadByte() != 0)
		{
			string source = netIncomingMessage.ReadString();
			NetIncomingMessage netIncomingMessage2 = CreateIncomingMessage(NetIncomingMessageType.NatIntroductionSuccess, 10);
			netIncomingMessage2.m_senderEndPoint = senderEndPoint;
			netIncomingMessage2.Write(source);
			ReleaseMessage(netIncomingMessage2);
		}
	}

	public NetPeer(NetPeerConfiguration config)
	{
		m_configuration = config;
		m_statistics = new NetPeerStatistics(this);
		m_releasedIncomingMessages = new NetQueue<NetIncomingMessage>(4);
		m_unsentUnconnectedMessages = new NetQueue<(IPEndPoint, NetOutgoingMessage)>(2);
		m_connections = new List<NetConnection>();
		m_connectionLookup = new Dictionary<NetSocketAddress, NetConnection>();
		m_handshakes = new Dictionary<IPEndPoint, NetConnection>();
		m_status = NetPeerStatus.NotRunning;
		m_receivedFragmentGroups = new Dictionary<NetConnection, Dictionary<int, ReceivedFragmentGroup>>();
		if (m_configuration.LocalAddress.AddressFamily == AddressFamily.InterNetworkV6)
		{
			m_senderRemote = new IPEndPoint(IPAddress.IPv6Any, 0);
		}
		else
		{
			m_senderRemote = new IPEndPoint(IPAddress.Any, 0);
		}
		m_receiveBuffer = new byte[m_configuration.ReceiveBufferSize];
		m_sendBuffer = new byte[m_configuration.SendBufferSize];
		m_readHelperMessage = new NetIncomingMessage(NetIncomingMessageType.Error);
		m_readHelperMessage.m_data = m_receiveBuffer;
	}

	public void Start()
	{
		if (m_status != NetPeerStatus.NotRunning)
		{
			LogWarning("Start() called on already running NetPeer - ignoring.");
			return;
		}
		m_status = NetPeerStatus.Starting;
		if (m_configuration.NetworkThreadName == "Lidgren network thread")
		{
			int num = Interlocked.Increment(ref s_initializedPeersCount);
			m_configuration.NetworkThreadName = "Lidgren network thread " + num;
		}
		InitializeNetwork();
		m_networkThread = new Thread(NetworkLoop);
		m_networkThread.Name = m_configuration.NetworkThreadName;
		m_networkThread.IsBackground = true;
		m_networkThread.Start();
		if (m_upnp != null)
		{
			m_upnp.Discover(this);
		}
		NetUtility.Sleep(50);
	}

	public NetConnection? GetConnection(IPEndPoint ep)
	{
		m_connectionLookup.TryGetValue((NetSocketAddress)ep, out NetConnection value);
		return value;
	}

	public NetIncomingMessage? WaitMessage(int maxMillis)
	{
		NetIncomingMessage netIncomingMessage;
		for (netIncomingMessage = ReadMessage(); netIncomingMessage == null; netIncomingMessage = ReadMessage())
		{
			if (!MessageReceivedEvent.WaitOne(maxMillis))
			{
				return null;
			}
		}
		return netIncomingMessage;
	}

	public NetIncomingMessage? ReadMessage()
	{
		if (m_releasedIncomingMessages.TryDequeue(out NetIncomingMessage item) && item.MessageType == NetIncomingMessageType.StatusChanged)
		{
			NetConnectionStatus visibleStatus = (NetConnectionStatus)item.PeekByte();
			item.SenderConnection.m_visibleStatus = visibleStatus;
		}
		return item;
	}

	public bool ReadMessage([MaybeNullWhen(false)] out NetIncomingMessage message)
	{
		message = ReadMessage();
		return message != null;
	}

	public int ReadMessages(IList<NetIncomingMessage> addTo)
	{
		int num = m_releasedIncomingMessages.TryDrain(addTo);
		if (num > 0)
		{
			for (int i = 0; i < num; i++)
			{
				int index = addTo.Count - num + i;
				NetIncomingMessage netIncomingMessage = addTo[index];
				if (netIncomingMessage.MessageType == NetIncomingMessageType.StatusChanged)
				{
					NetConnectionStatus visibleStatus = (NetConnectionStatus)netIncomingMessage.PeekByte();
					netIncomingMessage.SenderConnection.m_visibleStatus = visibleStatus;
				}
			}
		}
		return num;
	}

	internal void SendLibrary(NetOutgoingMessage msg, IPEndPoint recipient)
	{
		int numBytes = msg.Encode(m_sendBuffer, 0, 0);
		SendPacket(numBytes, recipient, 1, out var _);
		msg.m_recyclingCount = 0;
		Recycle(msg);
	}

	private IPEndPoint GetNetEndPoint(string host, int port)
	{
		AddressFamily? allowedFamily = (m_configuration.DualStack ? ((AddressFamily?)null) : new AddressFamily?(m_configuration.LocalAddress.AddressFamily));
		return new IPEndPoint(NetUtility.Resolve(host, allowedFamily) ?? throw new NetException("Could not resolve host"), port);
	}

	public NetConnection Connect(string host, int port)
	{
		return Connect(GetNetEndPoint(host, port), null);
	}

	public NetConnection Connect(string host, int port, NetOutgoingMessage hailMessage)
	{
		return Connect(GetNetEndPoint(host, port), hailMessage);
	}

	public NetConnection Connect(IPEndPoint remoteEndPoint)
	{
		return Connect(remoteEndPoint, null);
	}

	public virtual NetConnection Connect(IPEndPoint remoteEndPoint, NetOutgoingMessage? hailMessage)
	{
		if (remoteEndPoint == null)
		{
			throw new ArgumentNullException("remoteEndPoint");
		}
		if (m_configuration.DualStack)
		{
			remoteEndPoint = NetUtility.MapToIPv6(remoteEndPoint);
		}
		lock (m_connections)
		{
			if (m_status == NetPeerStatus.NotRunning)
			{
				throw new NetException("Must call Start() first");
			}
			if (m_connectionLookup.ContainsKey((NetSocketAddress)remoteEndPoint))
			{
				throw new NetException("Already connected to that endpoint!");
			}
			if (m_handshakes.TryGetValue(remoteEndPoint, out NetConnection value))
			{
				switch (value.m_status)
				{
				case NetConnectionStatus.InitiatedConnect:
					value.m_connectRequested = true;
					break;
				case NetConnectionStatus.RespondedConnect:
					value.SendConnectResponse(NetTime.Now, onLibraryThread: false);
					break;
				default:
				{
					bool handlerIsValid;
					NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(84, 1, this, out handlerIsValid);
					if (handlerIsValid)
					{
						text.AppendLiteral("Weird situation; Connect() already in progress to remote endpoint; but hs status is ");
						text.AppendFormatted(value.m_status);
					}
					LogWarning(text);
					break;
				}
				}
				return value;
			}
			NetConnection netConnection = new NetConnection(this, remoteEndPoint);
			netConnection.SetStatus(NetConnectionStatus.InitiatedConnect, "user called connect");
			netConnection.m_localHailMessage = hailMessage;
			netConnection.m_connectRequested = true;
			netConnection.m_connectionInitiator = true;
			m_handshakes.Add(remoteEndPoint, netConnection);
			return netConnection;
		}
	}

	public void RawSend(byte[] arr, int offset, int length, IPEndPoint destination)
	{
		Array.Copy(arr, offset, m_sendBuffer, 0, length);
		SendPacket(length, destination, 1, out var _);
	}

	internal void ThrowOrLog(string message)
	{
		LogError(message);
	}

	public void Shutdown(string? bye)
	{
		if (m_socket != null)
		{
			m_shutdownReason = bye;
			m_status = NetPeerStatus.ShutdownRequested;
		}
	}

	public void DiscoverLocalPeers(int serverPort)
	{
		NetOutgoingMessage netOutgoingMessage = CreateMessage(0);
		netOutgoingMessage.m_messageType = NetMessageType.Discovery;
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		IPAddress broadcastAddress = NetUtility.GetBroadcastAddress();
		if (broadcastAddress == null)
		{
			throw new NetException("Unable to determine broadcast address.");
		}
		m_unsentUnconnectedMessages.Enqueue((new IPEndPoint(broadcastAddress, serverPort), netOutgoingMessage));
	}

	public bool DiscoverKnownPeer(string host, int serverPort)
	{
		IPAddress iPAddress = NetUtility.Resolve(host);
		if (iPAddress == null)
		{
			return false;
		}
		DiscoverKnownPeer(new IPEndPoint(iPAddress, serverPort));
		return true;
	}

	public void DiscoverKnownPeer(IPEndPoint endPoint)
	{
		NetOutgoingMessage netOutgoingMessage = CreateMessage(0);
		netOutgoingMessage.m_messageType = NetMessageType.Discovery;
		netOutgoingMessage.m_recyclingCount = 1;
		m_unsentUnconnectedMessages.Enqueue((endPoint, netOutgoingMessage));
	}

	public void SendDiscoveryResponse(NetOutgoingMessage msg, IPEndPoint recipient)
	{
		if (recipient == null)
		{
			throw new ArgumentNullException("recipient");
		}
		if (msg == null)
		{
			msg = CreateMessage(0);
		}
		else if (msg.m_isSent)
		{
			throw new NetException("Message has already been sent!");
		}
		int num = m_configuration.MTUForEndPoint(recipient);
		if (msg.LengthBytes >= num)
		{
			throw new NetException("Cannot send discovery message larger than MTU (currently " + num + " bytes)");
		}
		msg.m_messageType = NetMessageType.DiscoveryResponse;
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((recipient, msg));
	}

	private NetSendResult SendFragmentedMessage(NetOutgoingMessage msg, IList<NetConnection> recipients, NetDeliveryMethod method, int sequenceChannel)
	{
		int num = Interlocked.Increment(ref m_lastUsedFragmentGroup);
		if (num >= 65534)
		{
			m_lastUsedFragmentGroup = 1;
			num = 1;
		}
		msg.m_fragmentGroup = num;
		int lengthBytes = msg.LengthBytes;
		int mTU = GetMTU(recipients);
		int bestChunkSize = NetFragmentationHelper.GetBestChunkSize(num, lengthBytes, mTU);
		int num2 = lengthBytes / bestChunkSize;
		if (num2 * bestChunkSize < lengthBytes)
		{
			num2++;
		}
		NetSendResult netSendResult = NetSendResult.Sent;
		int num3 = bestChunkSize * 8;
		int num4 = msg.LengthBits;
		for (int i = 0; i < num2; i++)
		{
			NetOutgoingMessage netOutgoingMessage = CreateMessage(0);
			netOutgoingMessage.m_bitLength = ((num4 > num3) ? num3 : num4);
			netOutgoingMessage.m_data = msg.m_data;
			netOutgoingMessage.m_fragmentGroup = num;
			netOutgoingMessage.m_fragmentGroupTotalBits = lengthBytes * 8;
			netOutgoingMessage.m_fragmentChunkByteSize = bestChunkSize;
			netOutgoingMessage.m_fragmentChunkNumber = i;
			Interlocked.Add(ref netOutgoingMessage.m_recyclingCount, recipients.Count);
			foreach (NetConnection recipient in recipients)
			{
				NetSendResult netSendResult2 = recipient.EnqueueMessage(netOutgoingMessage, method, sequenceChannel);
				if (netSendResult2 == NetSendResult.Dropped)
				{
					Interlocked.Decrement(ref netOutgoingMessage.m_recyclingCount);
				}
				if (netSendResult2 > netSendResult)
				{
					netSendResult = netSendResult2;
				}
			}
			num4 -= num3;
		}
		return netSendResult;
	}

	private void HandleReleasedFragment(NetIncomingMessage im)
	{
		int group;
		int totalBits;
		int chunkByteSize;
		int chunkNumber;
		int num = NetFragmentationHelper.ReadHeader(im.Data, 0, out group, out totalBits, out chunkByteSize, out chunkNumber);
		int num2 = NetUtility.BytesToHoldBits(totalBits);
		int num3 = num2 / chunkByteSize;
		if (num3 * chunkByteSize < num2)
		{
			num3++;
		}
		if (chunkNumber >= num3)
		{
			bool handlerIsValid;
			NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(46, 2, this, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("Index out of bounds for chunk ");
				text.AppendFormatted(chunkNumber);
				text.AppendLiteral(" (total chunks ");
				text.AppendFormatted(num3);
				text.AppendLiteral(")");
			}
			LogWarning(text);
			return;
		}
		if (!m_receivedFragmentGroups.TryGetValue(im.SenderConnection, out Dictionary<int, ReceivedFragmentGroup> value))
		{
			value = new Dictionary<int, ReceivedFragmentGroup>();
			m_receivedFragmentGroups[im.SenderConnection] = value;
		}
		if (!value.TryGetValue(group, out var value2))
		{
			value2 = (value[group] = new ReceivedFragmentGroup(new byte[num2], new NetBitVector(num3)));
		}
		value2.ReceivedChunks[chunkNumber] = true;
		int dstOffset = chunkNumber * chunkByteSize;
		Buffer.BlockCopy(im.Data, num, value2.Data, dstOffset, im.LengthBytes - num);
		value2.ReceivedChunks.Count();
		if (value2.ReceivedChunks.Count() == num3)
		{
			im.m_data = value2.Data;
			im.m_bitLength = totalBits;
			im.m_isFragment = false;
			value.Remove(group);
			ReleaseMessage(im);
		}
		else
		{
			Recycle(im);
		}
	}

	public void RegisterReceivedCallback(SendOrPostCallback callback, SynchronizationContext? syncContext = null)
	{
		if (syncContext == null)
		{
			syncContext = SynchronizationContext.Current;
		}
		if (syncContext == null)
		{
			throw new NetException("Need a SynchronizationContext to register callback on correct thread!");
		}
		if (m_receiveCallbacks == null)
		{
			m_receiveCallbacks = new List<(SynchronizationContext, SendOrPostCallback)>();
		}
		m_receiveCallbacks.Add((syncContext, callback));
	}

	public void UnregisterReceivedCallback(SendOrPostCallback callback)
	{
		if (m_receiveCallbacks != null)
		{
			m_receiveCallbacks.RemoveAll(((SynchronizationContext, SendOrPostCallback) tuple) => tuple.Item2.Equals(callback));
			if (m_receiveCallbacks.Count < 1)
			{
				m_receiveCallbacks = null;
			}
		}
	}

	internal void ReleaseMessage(NetIncomingMessage msg)
	{
		if (msg.m_isFragment)
		{
			HandleReleasedFragment(msg);
			return;
		}
		m_releasedIncomingMessages.Enqueue(msg);
		if (m_messageReceivedEvent != null)
		{
			m_messageReceivedEvent.Set();
		}
		if (m_receiveCallbacks == null)
		{
			return;
		}
		foreach (var receiveCallback in m_receiveCallbacks)
		{
			try
			{
				receiveCallback.Item1.Post(receiveCallback.Item2, this);
			}
			catch (Exception t)
			{
				bool handlerIsValid;
				NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(27, 1, this, out handlerIsValid);
				if (handlerIsValid)
				{
					text.AppendLiteral("Receive callback exception:");
					text.AppendFormatted(t);
				}
				LogWarning(text);
			}
		}
	}

	private void BindSocket(bool reBind)
	{
		double now = NetTime.Now;
		if (now - m_lastSocketBind < 1.0)
		{
			return;
		}
		m_lastSocketBind = now;
		if (m_socket == null)
		{
			m_socket = new Socket(m_configuration.LocalAddress.AddressFamily, SocketType.Dgram, ProtocolType.Udp);
		}
		if (reBind)
		{
			m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, 1);
		}
		m_socket.ReceiveBufferSize = m_configuration.ReceiveBufferSize;
		m_socket.SendBufferSize = m_configuration.SendBufferSize;
		m_socket.Blocking = false;
		if (m_configuration.DualStack)
		{
			if (m_configuration.LocalAddress.AddressFamily != AddressFamily.InterNetworkV6)
			{
				LogWarning("Configuration specifies Dual Stack but does not use IPv6 local address; Dual stack will not work.");
			}
			else
			{
				m_socket.DualMode = true;
			}
		}
		EndPoint localEP = new IPEndPoint(m_configuration.LocalAddress, reBind ? m_listenPort : m_configuration.Port);
		m_socket.Bind(localEP);
		try
		{
			uint ioControlCode = 2550136844u;
			m_socket.IOControl((int)ioControlCode, new byte[1] { Convert.ToByte(value: false) }, null);
		}
		catch
		{
		}
		IPEndPoint iPEndPoint = (IPEndPoint)m_socket.LocalEndPoint;
		m_listenPort = iPEndPoint.Port;
	}

	private void InitializeNetwork()
	{
		lock (m_initializeLock)
		{
			m_configuration.Lock();
			if (m_configuration.m_enableUPnP)
			{
				m_upnp = new NetUPnP(this);
			}
			if (m_configuration.AutoExpandMTU && !CanAutoExpandMTU)
			{
				LogWarning("AutoExpandMTU is not supported on this platform: ignoring.");
				m_configuration.m_autoExpandMTU = false;
			}
			InitializePools();
			m_releasedIncomingMessages.Clear();
			m_unsentUnconnectedMessages.Clear();
			m_handshakes.Clear();
			BindSocket(reBind: false);
			byte[] array = NetUtility.GetMacAddressBytes() ?? Array.Empty<byte>();
			byte[] bytes = BitConverter.GetBytes(((IPEndPoint)m_socket.LocalEndPoint).GetHashCode());
			byte[] array2 = new byte[bytes.Length + array.Length];
			Array.Copy(bytes, 0, array2, 0, bytes.Length);
			Array.Copy(array, 0, array2, bytes.Length, array.Length);
			m_uniqueIdentifier = BitConverter.ToInt64(NetUtility.ComputeSHAHash(array2), 0);
			m_status = NetPeerStatus.Running;
		}
	}

	private void NetworkLoop()
	{
		do
		{
			try
			{
				Heartbeat();
			}
			catch (Exception ex)
			{
				LogWarning(ex.ToString());
			}
		}
		while (m_status == NetPeerStatus.Running);
		ExecutePeerShutdown();
	}

	private void ExecutePeerShutdown()
	{
		List<NetConnection> list = new List<NetConnection>(m_handshakes.Count + m_connections.Count);
		lock (m_connections)
		{
			foreach (NetConnection connection in m_connections)
			{
				if (connection != null)
				{
					list.Add(connection);
				}
			}
		}
		lock (m_handshakes)
		{
			foreach (NetConnection value in m_handshakes.Values)
			{
				if (value != null && !list.Contains(value))
				{
					list.Add(value);
				}
			}
		}
		foreach (NetConnection item in list)
		{
			item.Shutdown(m_shutdownReason);
		}
		FlushDelayedPackets();
		Heartbeat();
		NetUtility.Sleep(10);
		lock (m_initializeLock)
		{
			try
			{
				if (m_socket != null)
				{
					try
					{
						m_socket.Shutdown(SocketShutdown.Receive);
					}
					catch (Exception)
					{
					}
					try
					{
						m_socket.Close(2);
					}
					catch (Exception)
					{
					}
				}
			}
			finally
			{
				m_socket = null;
				m_status = NetPeerStatus.NotRunning;
				if (m_messageReceivedEvent != null)
				{
					m_messageReceivedEvent.Set();
				}
			}
			m_lastSocketBind = -3.4028234663852886E+38;
			m_unsentUnconnectedMessages.Clear();
			m_connections.Clear();
			m_connectionLookup.Clear();
			m_handshakes.Clear();
			m_onShutdown?.Invoke();
		}
	}

	private void Heartbeat()
	{
		double now = NetTime.Now;
		double num = now - m_lastHeartbeat;
		int num2 = 1250 - m_connections.Count;
		if (num2 < 250)
		{
			num2 = 250;
		}
		if (num > 1.0 / (double)num2 || num < 0.0)
		{
			m_frameCounter++;
			m_lastHeartbeat = now;
			if (m_frameCounter % 3 == 0)
			{
				foreach (KeyValuePair<IPEndPoint, NetConnection> handshake in m_handshakes)
				{
					NetConnection value = handshake.Value;
					value.UnconnectedHeartbeat(now);
					if (value.m_status == NetConnectionStatus.Connected || value.m_status == NetConnectionStatus.Disconnected)
					{
						break;
					}
				}
			}
			SendDelayedPackets();
			if (m_configuration.m_autoFlushSendQueue && m_needFlushSendQueue)
			{
				m_executeFlushSendQueue = true;
				m_needFlushSendQueue = false;
			}
			lock (m_connections)
			{
				for (int num3 = m_connections.Count - 1; num3 >= 0; num3--)
				{
					NetConnection netConnection = m_connections[num3];
					netConnection.Heartbeat(now, m_frameCounter);
					if (netConnection.m_status == NetConnectionStatus.Disconnected)
					{
						m_connections.RemoveAt(num3);
						m_connectionLookup.Remove((NetSocketAddress)netConnection.RemoteEndPoint);
					}
				}
			}
			m_executeFlushSendQueue = false;
			(IPEndPoint, NetOutgoingMessage) item;
			while (m_unsentUnconnectedMessages.TryDequeue(out item))
			{
				NetOutgoingMessage item2 = item.Item2;
				int numBytes = item2.Encode(m_sendBuffer, 0, 0);
				Interlocked.Decrement(ref item2.m_recyclingCount);
				if (item2.m_recyclingCount <= 0)
				{
					Recycle(item2);
				}
				SendPacket(numBytes, item.Item1, 1, out var _);
			}
		}
		if (m_upnp != null)
		{
			m_upnp.CheckForDiscoveryTimeout();
		}
		if (m_socket == null || !m_socket.Poll(1000, SelectMode.SelectRead))
		{
			return;
		}
		now = NetTime.Now;
		try
		{
			do
			{
				ReceiveSocketData(now);
			}
			while (m_socket.Available > 0);
		}
		catch (SocketException ex)
		{
			switch (ex.SocketErrorCode)
			{
			case SocketError.ConnectionReset:
				LogWarning("ConnectionReset");
				return;
			case SocketError.NotConnected:
				BindSocket(reBind: true);
				return;
			}
			bool handlerIsValid;
			NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(18, 1, this, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("Socket exception: ");
				text.AppendFormatted(ex);
			}
			LogWarning(text);
		}
	}

	private void ReceiveSocketData(double now)
	{
		NetSocketAddress socketAddress;
		int num = NetFastSocket.ReceiveFrom(m_socket, m_receiveBuffer, 0, m_receiveBuffer.Length, SocketFlags.None, out socketAddress, ref m_senderRemote);
		if (num < 5)
		{
			return;
		}
		if (m_upnp != null && now < m_upnp.m_discoveryResponseDeadline && num > 32)
		{
			string text = Encoding.UTF8.GetString(m_receiveBuffer, 0, num);
			if (text.Contains("upnp:rootdevice") || text.Contains("UPnP/1.0"))
			{
				try
				{
					text = text.Substring(text.ToLower().IndexOf("location:") + 9);
					text = text.Substring(0, text.IndexOf("\r")).Trim();
				}
				catch (Exception)
				{
					return;
				}
				try
				{
					m_upnp.ExtractServiceUrl(text);
					return;
				}
				catch (Exception)
				{
					return;
				}
			}
		}
		m_connectionLookup.TryGetValue(socketAddress, out NetConnection value);
		int num2 = 0;
		int num3 = 0;
		int num6;
		for (int i = 0; num - i >= 5; i += num6)
		{
			num2++;
			NetMessageType netMessageType = (NetMessageType)m_receiveBuffer[i++];
			byte num4 = m_receiveBuffer[i++];
			byte b = m_receiveBuffer[i++];
			bool flag = (num4 & 1) == 1;
			ushort sequenceNumber = (ushort)((num4 >> 1) | (b << 7));
			if (flag)
			{
				num3++;
			}
			ushort num5 = (ushort)(m_receiveBuffer[i++] | (m_receiveBuffer[i++] << 8));
			num6 = NetUtility.BytesToHoldBits(num5);
			bool handlerIsValid;
			if (num - i < num6)
			{
				NetPeer netPeer = this;
				NetPeer netPeer2 = netPeer;
				NetWarningLogInterpolatedStringHandler text2 = new NetWarningLogInterpolatedStringHandler(64, 3, netPeer, out handlerIsValid);
				if (handlerIsValid)
				{
					text2.AppendLiteral("Malformed packet from ");
					text2.AppendFormatted((IPEndPoint)socketAddress);
					text2.AppendLiteral("; stated payload length ");
					text2.AppendFormatted(num6);
					text2.AppendLiteral(", remaining bytes ");
					text2.AppendFormatted(num - i);
				}
				netPeer2.LogWarning(text2);
				return;
			}
			if ((int)netMessageType >= 99 && (int)netMessageType <= 127)
			{
				NetPeer netPeer = this;
				NetPeer netPeer3 = netPeer;
				NetWarningLogInterpolatedStringHandler text2 = new NetWarningLogInterpolatedStringHandler(27, 1, netPeer, out handlerIsValid);
				if (handlerIsValid)
				{
					text2.AppendLiteral("Unexpected NetMessageType: ");
					text2.AppendFormatted(netMessageType);
				}
				netPeer3.LogWarning(text2);
				return;
			}
			try
			{
				if ((int)netMessageType >= 128)
				{
					if (value != null)
					{
						value.ReceivedLibraryMessage(netMessageType, i, num6);
					}
					else
					{
						ReceivedUnconnectedLibraryMessage(now, (IPEndPoint)socketAddress, netMessageType, i, num6);
					}
					continue;
				}
				if (value == null && !m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.UnconnectedData))
				{
					return;
				}
				NetIncomingMessage netIncomingMessage = CreateIncomingMessage(NetIncomingMessageType.Data, num6);
				netIncomingMessage.m_isFragment = flag;
				netIncomingMessage.m_receiveTime = now;
				netIncomingMessage.m_sequenceNumber = sequenceNumber;
				netIncomingMessage.m_receivedMessageType = netMessageType;
				netIncomingMessage.m_senderConnection = value;
				netIncomingMessage.m_senderEndPoint = (IPEndPoint)socketAddress;
				netIncomingMessage.m_bitLength = num5;
				Buffer.BlockCopy(m_receiveBuffer, i, netIncomingMessage.Data, 0, num6);
				if (value != null)
				{
					if (netMessageType == NetMessageType.Unconnected)
					{
						netIncomingMessage.m_incomingMessageType = NetIncomingMessageType.UnconnectedData;
						ReleaseMessage(netIncomingMessage);
					}
					else
					{
						value.ReceivedMessage(netIncomingMessage);
					}
				}
				else
				{
					netIncomingMessage.m_incomingMessageType = NetIncomingMessageType.UnconnectedData;
					ReleaseMessage(netIncomingMessage);
				}
			}
			catch (Exception ex3)
			{
				NetPeer netPeer = this;
				NetPeer netPeer4 = netPeer;
				NetErrorLogInterpolatedStringHandler text3 = new NetErrorLogInterpolatedStringHandler(28, 2, netPeer, out handlerIsValid);
				if (handlerIsValid)
				{
					text3.AppendLiteral("Packet parsing error: ");
					text3.AppendFormatted(ex3.Message);
					text3.AppendLiteral(" from ");
					text3.AppendFormatted((IPEndPoint)socketAddress);
				}
				netPeer4.LogError(text3);
			}
		}
		m_statistics.PacketReceived(num, num2, num3);
		value?.m_statistics.PacketReceived(num, num2, num3);
	}

	public void FlushSendQueue()
	{
		m_executeFlushSendQueue = true;
	}

	internal void HandleIncomingDiscoveryRequest(double now, IPEndPoint senderEndPoint, int ptr, int payloadByteLength)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.DiscoveryRequest))
		{
			NetIncomingMessage netIncomingMessage = CreateIncomingMessage(NetIncomingMessageType.DiscoveryRequest, payloadByteLength);
			if (payloadByteLength > 0)
			{
				Buffer.BlockCopy(m_receiveBuffer, ptr, netIncomingMessage.Data, 0, payloadByteLength);
			}
			netIncomingMessage.m_receiveTime = now;
			netIncomingMessage.m_bitLength = payloadByteLength * 8;
			netIncomingMessage.m_senderEndPoint = senderEndPoint;
			ReleaseMessage(netIncomingMessage);
		}
	}

	internal void HandleIncomingDiscoveryResponse(double now, IPEndPoint senderEndPoint, int ptr, int payloadByteLength)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.DiscoveryResponse))
		{
			NetIncomingMessage netIncomingMessage = CreateIncomingMessage(NetIncomingMessageType.DiscoveryResponse, payloadByteLength);
			if (payloadByteLength > 0)
			{
				Buffer.BlockCopy(m_receiveBuffer, ptr, netIncomingMessage.Data, 0, payloadByteLength);
			}
			netIncomingMessage.m_receiveTime = now;
			netIncomingMessage.m_bitLength = payloadByteLength * 8;
			netIncomingMessage.m_senderEndPoint = senderEndPoint;
			ReleaseMessage(netIncomingMessage);
		}
	}

	private void ReceivedUnconnectedLibraryMessage(double now, IPEndPoint senderEndPoint, NetMessageType tp, int ptr, int payloadByteLength)
	{
		if (m_handshakes.TryGetValue(senderEndPoint, out NetConnection value))
		{
			value.ReceivedHandshake(now, tp, ptr, payloadByteLength);
			return;
		}
		bool handlerIsValid;
		switch (tp)
		{
		case NetMessageType.Discovery:
			HandleIncomingDiscoveryRequest(now, senderEndPoint, ptr, payloadByteLength);
			break;
		case NetMessageType.DiscoveryResponse:
			HandleIncomingDiscoveryResponse(now, senderEndPoint, ptr, payloadByteLength);
			break;
		case NetMessageType.NatIntroduction:
			if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.NatIntroductionSuccess))
			{
				HandleNatIntroduction(ptr);
			}
			break;
		case NetMessageType.NatPunchMessage:
			if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.NatIntroductionSuccess))
			{
				HandleNatPunch(ptr, senderEndPoint);
			}
			break;
		case NetMessageType.NatIntroductionConfirmRequest:
			if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.NatIntroductionSuccess))
			{
				HandleNatPunchConfirmRequest(ptr, senderEndPoint);
			}
			break;
		case NetMessageType.NatIntroductionConfirmed:
			if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.NatIntroductionSuccess))
			{
				HandleNatPunchConfirmed(ptr, senderEndPoint);
			}
			break;
		case NetMessageType.ConnectResponse:
		{
			lock (m_handshakes)
			{
				foreach (KeyValuePair<IPEndPoint, NetConnection> handshake in m_handshakes)
				{
					IPAddress address = handshake.Key.Address;
					IPAddress address2 = senderEndPoint.Address;
					if (!address.Equals(address2))
					{
						if (!address.IsIPv6LinkLocal || !address2.IsIPv6LinkLocal)
						{
							continue;
						}
						byte[] addressBytes = address.GetAddressBytes();
						if (!MemoryExtensions.SequenceEqual(other: address2.GetAddressBytes(), span: addressBytes.AsSpan()))
						{
							continue;
						}
					}
					if (handshake.Value.m_connectionInitiator)
					{
						NetConnection value2 = handshake.Value;
						m_connectionLookup.Remove((NetSocketAddress)handshake.Key);
						m_handshakes.Remove(handshake.Key);
						value2.MutateEndPoint(senderEndPoint);
						m_handshakes.Add(senderEndPoint, value2);
						value2.ReceivedHandshake(now, tp, ptr, payloadByteLength);
						return;
					}
				}
			}
			NetPeer netPeer = this;
			NetPeer netPeer3 = netPeer;
			NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(41, 2, netPeer, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("Received unhandled library message ");
				text.AppendFormatted(tp);
				text.AppendLiteral(" from ");
				text.AppendFormatted(senderEndPoint);
			}
			netPeer3.LogWarning(text);
			break;
		}
		case NetMessageType.Connect:
			if (!m_configuration.AcceptIncomingConnections)
			{
				LogWarning("Received Connect, but we're not accepting incoming connections!");
			}
			else if (m_handshakes.Count + m_connections.Count >= m_configuration.m_maximumConnections)
			{
				NetOutgoingMessage netOutgoingMessage = CreateMessage("Server full");
				netOutgoingMessage.m_messageType = NetMessageType.Disconnect;
				SendLibrary(netOutgoingMessage, senderEndPoint);
			}
			else
			{
				NetConnection netConnection = new NetConnection(this, senderEndPoint);
				netConnection.m_status = NetConnectionStatus.ReceivedInitiation;
				m_handshakes.Add(senderEndPoint, netConnection);
				netConnection.ReceivedHandshake(now, tp, ptr, payloadByteLength);
			}
			break;
		case NetMessageType.Disconnect:
			break;
		default:
		{
			NetPeer netPeer = this;
			NetPeer netPeer2 = netPeer;
			NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(41, 2, netPeer, out handlerIsValid);
			if (handlerIsValid)
			{
				text.AppendLiteral("Received unhandled library message ");
				text.AppendFormatted(tp);
				text.AppendLiteral(" from ");
				text.AppendFormatted(senderEndPoint);
			}
			netPeer2.LogWarning(text);
			break;
		}
		}
	}

	internal void AcceptConnection(NetConnection conn)
	{
		conn.InitExpandMTU(NetTime.Now);
		if (!m_handshakes.Remove(conn.m_remoteEndPoint))
		{
			LogWarning("AcceptConnection called but m_handshakes did not contain it!");
		}
		lock (m_connections)
		{
			if (m_connections.Contains(conn))
			{
				LogWarning("AcceptConnection called but m_connection already contains it!");
				return;
			}
			m_connections.Add(conn);
			m_connectionLookup.Add((NetSocketAddress)conn.m_remoteEndPoint, conn);
		}
	}

	[Conditional("DEBUG")]
	internal void VerifyNetworkThread()
	{
		Thread currentThread = Thread.CurrentThread;
		if (Thread.CurrentThread != m_networkThread)
		{
			throw new NetException("Executing on wrong thread! Should be library system thread (is " + currentThread.Name + " mId " + currentThread.ManagedThreadId + ")");
		}
	}

	internal NetIncomingMessage SetupReadHelperMessage(int ptr, int payloadLength)
	{
		m_readHelperMessage.m_bitLength = (ptr + payloadLength) * 8;
		m_readHelperMessage.m_readPosition = ptr * 8;
		return m_readHelperMessage;
	}

	internal void SendPacket(int numBytes, IPEndPoint target, int numMessages, out bool connectionReset)
	{
		connectionReset = false;
		float loss = m_configuration.m_loss;
		if (loss > 0f && (float)m_latencyRandom.NextDouble() < loss)
		{
			return;
		}
		m_statistics.PacketSent(numBytes, numMessages);
		float minimumOneWayLatency = m_configuration.m_minimumOneWayLatency;
		float randomOneWayLatency = m_configuration.m_randomOneWayLatency;
		if (minimumOneWayLatency == 0f && randomOneWayLatency == 0f)
		{
			ActuallySendPacket(m_sendBuffer, numBytes, target, out connectionReset);
			if (m_configuration.m_duplicates > 0f && m_latencyRandom.NextDouble() < (double)m_configuration.m_duplicates)
			{
				ActuallySendPacket(m_sendBuffer, numBytes, target, out connectionReset);
			}
			return;
		}
		int num = 1;
		if (m_configuration.m_duplicates > 0f && m_latencyRandom.NextSingle() < m_configuration.m_duplicates)
		{
			num++;
		}
		for (int i = 0; i < num; i++)
		{
			float num2 = minimumOneWayLatency + m_latencyRandom.NextSingle() * randomOneWayLatency;
			DelayedPacket delayedPacket = new DelayedPacket(new byte[numBytes], NetTime.Now + (double)num2, target);
			Buffer.BlockCopy(m_sendBuffer, 0, delayedPacket.Data, 0, numBytes);
			m_delayedPackets.Add(delayedPacket);
		}
	}

	private void SendDelayedPackets()
	{
		if (m_delayedPackets.Count <= 0)
		{
			return;
		}
		double now = NetTime.Now;
		for (int i = 0; i < m_delayedPackets.Count; i++)
		{
			DelayedPacket delayedPacket = m_delayedPackets[i];
			if (!(now < delayedPacket.DelayedUntil))
			{
				ActuallySendPacket(delayedPacket.Data, delayedPacket.Data.Length, delayedPacket.Target, out var _);
				int index = m_delayedPackets.Count - 1;
				DelayedPacket value = m_delayedPackets[index];
				m_delayedPackets[i] = value;
				m_delayedPackets.RemoveAt(index);
				i--;
			}
		}
	}

	private void FlushDelayedPackets()
	{
		try
		{
			foreach (DelayedPacket delayedPacket in m_delayedPackets)
			{
				ActuallySendPacket(delayedPacket.Data, delayedPacket.Data.Length, delayedPacket.Target, out var _);
			}
			m_delayedPackets.Clear();
		}
		catch
		{
		}
	}

	internal bool ActuallySendPacket(byte[] data, int numBytes, IPEndPoint target, out bool connectionReset)
	{
		bool flag = m_configuration.DualStack && m_configuration.LocalAddress.AddressFamily == AddressFamily.InterNetworkV6;
		connectionReset = false;
		IPAddress obj = null;
		bool handlerIsValid;
		bool handlerIsValid2;
		try
		{
			IPEndPoint iPEndPoint = target;
			obj = NetUtility.GetCachedBroadcastAddress();
			if (target.Address.Equals(obj))
			{
				targetCopy.Address = m_configuration.BroadcastAddress;
				targetCopy.Port = target.Port;
				iPEndPoint = targetCopy;
				m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: true);
				if (flag)
				{
					NetUtility.CopyEndpoint(iPEndPoint, targetCopy2);
					iPEndPoint = targetCopy2;
				}
			}
			else if (flag)
			{
				NetUtility.CopyEndpoint(target, targetCopy);
				iPEndPoint = targetCopy;
			}
			int num = NetFastSocket.SendTo(m_socket, data, 0, numBytes, SocketFlags.None, iPEndPoint);
			if (numBytes != num)
			{
				NetPeer netPeer = this;
				NetPeer netPeer2 = netPeer;
				NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(53, 2, netPeer, out handlerIsValid);
				if (handlerIsValid)
				{
					text.AppendLiteral("Failed to send the full ");
					text.AppendFormatted(numBytes);
					text.AppendLiteral("; only ");
					text.AppendFormatted(num);
					text.AppendLiteral(" bytes sent in packet!");
				}
				netPeer2.LogWarning(text);
			}
		}
		catch (SocketException ex)
		{
			if (ex.SocketErrorCode == SocketError.WouldBlock)
			{
				LogWarning("Socket threw exception; would block - send buffer full? Increase in NetPeerConfiguration");
				handlerIsValid = false;
				return handlerIsValid;
			}
			if (ex.SocketErrorCode == SocketError.ConnectionReset)
			{
				connectionReset = true;
				handlerIsValid = false;
				return handlerIsValid;
			}
			NetPeer netPeer = this;
			NetPeer netPeer3 = netPeer;
			NetErrorLogInterpolatedStringHandler text2 = new NetErrorLogInterpolatedStringHandler(23, 1, netPeer, out handlerIsValid2);
			if (handlerIsValid2)
			{
				text2.AppendLiteral("Failed to send packet: ");
				text2.AppendFormatted(ex);
			}
			netPeer3.LogError(text2);
		}
		catch (Exception t)
		{
			NetPeer netPeer = this;
			NetPeer netPeer4 = netPeer;
			NetErrorLogInterpolatedStringHandler text2 = new NetErrorLogInterpolatedStringHandler(23, 1, netPeer, out handlerIsValid2);
			if (handlerIsValid2)
			{
				text2.AppendLiteral("Failed to send packet: ");
				text2.AppendFormatted(t);
			}
			netPeer4.LogError(text2);
		}
		finally
		{
			if (target.Address.Equals(obj))
			{
				m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: false);
			}
		}
		return true;
	}

	internal bool SendMTUPacket(int numBytes, IPEndPoint target)
	{
		if (!CanAutoExpandMTU)
		{
			throw new NotSupportedException("MTU expansion not currently supported on this operating system");
		}
		bool handlerIsValid;
		bool handlerIsValid2;
		try
		{
			if (m_socket.DualMode || target.AddressFamily == AddressFamily.InterNetwork)
			{
				m_socket.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.DontFragment, optionValue: true);
			}
			int num = NetFastSocket.SendTo(m_socket, m_sendBuffer, 0, numBytes, SocketFlags.None, target);
			if (numBytes != num)
			{
				NetPeer netPeer = this;
				NetPeer netPeer2 = netPeer;
				NetWarningLogInterpolatedStringHandler text = new NetWarningLogInterpolatedStringHandler(53, 2, netPeer, out handlerIsValid);
				if (handlerIsValid)
				{
					text.AppendLiteral("Failed to send the full ");
					text.AppendFormatted(numBytes);
					text.AppendLiteral("; only ");
					text.AppendFormatted(num);
					text.AppendLiteral(" bytes sent in packet!");
				}
				netPeer2.LogWarning(text);
			}
			m_statistics.PacketSent(numBytes, 1);
		}
		catch (SocketException ex)
		{
			if (ex.SocketErrorCode == SocketError.MessageSize)
			{
				handlerIsValid = false;
				return handlerIsValid;
			}
			if (ex.SocketErrorCode == SocketError.WouldBlock)
			{
				LogWarning("Socket threw exception; would block - send buffer full? Increase in NetPeerConfiguration");
				handlerIsValid = true;
				return handlerIsValid;
			}
			if (ex.SocketErrorCode == SocketError.ConnectionReset)
			{
				handlerIsValid = true;
				return handlerIsValid;
			}
			NetPeer netPeer = this;
			NetPeer netPeer3 = netPeer;
			NetErrorLogInterpolatedStringHandler text2 = new NetErrorLogInterpolatedStringHandler(26, 2, netPeer, out handlerIsValid2);
			if (handlerIsValid2)
			{
				text2.AppendLiteral("Failed to send packet: (");
				text2.AppendFormatted(ex.SocketErrorCode);
				text2.AppendLiteral(") ");
				text2.AppendFormatted(ex);
			}
			netPeer3.LogError(text2);
		}
		catch (Exception t)
		{
			NetPeer netPeer = this;
			NetPeer netPeer4 = netPeer;
			NetErrorLogInterpolatedStringHandler text2 = new NetErrorLogInterpolatedStringHandler(23, 1, netPeer, out handlerIsValid2);
			if (handlerIsValid2)
			{
				text2.AppendLiteral("Failed to send packet: ");
				text2.AppendFormatted(t);
			}
			netPeer4.LogError(text2);
		}
		finally
		{
			if (m_socket.DualMode || target.AddressFamily == AddressFamily.InterNetwork)
			{
				m_socket.SetSocketOption(SocketOptionLevel.IP, SocketOptionName.DontFragment, optionValue: false);
			}
		}
		return true;
	}

	[Conditional("DEBUG")]
	internal void LogVerbose(string message)
	{
		SendLogBase(NetIncomingMessageType.VerboseDebugMessage, message);
	}

	[Conditional("DEBUG")]
	internal void LogDebug(string message)
	{
		SendLogBase(NetIncomingMessageType.DebugMessage, message);
	}

	internal void LogWarning(string message)
	{
		SendLogBase(NetIncomingMessageType.WarningMessage, message);
	}

	internal void LogError(string message)
	{
		SendLogBase(NetIncomingMessageType.ErrorMessage, message);
	}

	private void SendLogBase(NetIncomingMessageType type, string text)
	{
		this.LogEvent?.Invoke(type, text);
		if (m_configuration.IsMessageTypeEnabled(type))
		{
			ReleaseMessage(CreateIncomingMessage(type, text));
		}
	}

	private bool CheckLogEnabled(NetIncomingMessageType type)
	{
		if (!m_configuration.IsMessageTypeEnabled(type))
		{
			return this.LogEvent != null;
		}
		return true;
	}

	internal void LogWarning([InterpolatedStringHandlerArgument("")] NetWarningLogInterpolatedStringHandler text)
	{
		SendLogBase(NetIncomingMessageType.WarningMessage, text.Implementation);
	}

	internal void LogError([InterpolatedStringHandlerArgument("")] NetErrorLogInterpolatedStringHandler text)
	{
		SendLogBase(NetIncomingMessageType.ErrorMessage, text.Implementation);
	}

	private void SendLogBase(NetIncomingMessageType type, DefaultInterpolatedStringHandler text)
	{
		if (CheckLogEnabled(type))
		{
			SendLogBase(type, text.ToStringAndClear());
		}
	}

	private void InitializePools()
	{
		m_storageSlotsUsedCount = 0;
		if (m_configuration.UseMessageRecycling)
		{
			m_storagePool = new List<byte[]>(16);
			m_outgoingMessagesPool = new NetQueue<NetOutgoingMessage>(4);
			m_incomingMessagesPool = new NetQueue<NetIncomingMessage>(4);
		}
		else
		{
			m_storagePool = null;
			m_outgoingMessagesPool = null;
			m_incomingMessagesPool = null;
		}
		m_maxCacheCount = m_configuration.RecycledCacheMaxCount;
	}

	internal byte[] GetStorage(int minimumCapacityInBytes)
	{
		if (m_storagePool == null)
		{
			return new byte[minimumCapacityInBytes];
		}
		lock (m_storagePool)
		{
			for (int i = 0; i < m_storagePool.Count; i++)
			{
				byte[] array = m_storagePool[i];
				if (array != null && array.Length >= minimumCapacityInBytes)
				{
					m_storagePool[i] = null;
					m_storageSlotsUsedCount--;
					m_storagePoolBytes -= array.Length;
					return array;
				}
			}
		}
		m_statistics.m_bytesAllocated += minimumCapacityInBytes;
		return new byte[minimumCapacityInBytes];
	}

	internal void Recycle(byte[] storage)
	{
		if (m_storagePool == null)
		{
			return;
		}
		lock (m_storagePool)
		{
			int count = m_storagePool.Count;
			for (int i = 0; i < count; i++)
			{
				if (m_storagePool[i] == null)
				{
					m_storageSlotsUsedCount++;
					m_storagePoolBytes += storage.Length;
					m_storagePool[i] = storage;
					return;
				}
			}
			if (m_storagePool.Count >= m_maxCacheCount)
			{
				int index = NetRandom.Instance.Next(m_storagePool.Count);
				m_storagePoolBytes -= m_storagePool[index].Length;
				m_storagePoolBytes += storage.Length;
				m_storagePool[index] = storage;
			}
			else
			{
				m_storageSlotsUsedCount++;
				m_storagePoolBytes += storage.Length;
				m_storagePool.Add(storage);
			}
		}
	}

	public NetOutgoingMessage CreateMessage()
	{
		return CreateMessage(m_configuration.m_defaultOutgoingMessageCapacity);
	}

	public NetOutgoingMessage CreateMessage(string? content)
	{
		NetOutgoingMessage netOutgoingMessage = ((!string.IsNullOrEmpty(content)) ? CreateMessage(2 + content.Length) : CreateMessage(1));
		netOutgoingMessage.Write(content);
		return netOutgoingMessage;
	}

	public NetOutgoingMessage CreateMessage(int initialCapacity)
	{
		if (m_outgoingMessagesPool == null || !m_outgoingMessagesPool.TryDequeue(out NetOutgoingMessage item))
		{
			item = new NetOutgoingMessage();
		}
		if (initialCapacity > 0)
		{
			item.m_data = GetStorage(initialCapacity);
		}
		return item;
	}

	internal NetIncomingMessage CreateIncomingMessage(NetIncomingMessageType tp, byte[] useStorageData)
	{
		if (m_incomingMessagesPool == null || !m_incomingMessagesPool.TryDequeue(out NetIncomingMessage item))
		{
			item = new NetIncomingMessage(tp);
		}
		else
		{
			item.m_incomingMessageType = tp;
		}
		item.m_data = useStorageData;
		return item;
	}

	internal NetIncomingMessage CreateIncomingMessage(NetIncomingMessageType tp, int minimumByteSize)
	{
		if (m_incomingMessagesPool == null || !m_incomingMessagesPool.TryDequeue(out NetIncomingMessage item))
		{
			item = new NetIncomingMessage(tp);
		}
		else
		{
			item.m_incomingMessageType = tp;
		}
		item.m_data = GetStorage(minimumByteSize);
		return item;
	}

	public void Recycle(NetIncomingMessage msg)
	{
		if (m_incomingMessagesPool != null && msg != null)
		{
			byte[] data = msg.Data;
			msg.m_data = null;
			Recycle(data);
			msg.Reset();
			if (m_incomingMessagesPool.Count < m_maxCacheCount)
			{
				m_incomingMessagesPool.Enqueue(msg);
			}
		}
	}

	public void Recycle(IEnumerable<NetIncomingMessage> toRecycle)
	{
		if (m_incomingMessagesPool == null)
		{
			return;
		}
		foreach (NetIncomingMessage item in toRecycle)
		{
			Recycle(item);
		}
	}

	internal void Recycle(NetOutgoingMessage msg)
	{
		if (m_outgoingMessagesPool != null)
		{
			msg.m_recyclingCount = 0;
			byte[] data = msg.Data;
			msg.m_data = null;
			if (msg.m_fragmentGroup == 0)
			{
				Recycle(data);
			}
			msg.Reset();
			if (m_outgoingMessagesPool.Count < m_maxCacheCount)
			{
				m_outgoingMessagesPool.Enqueue(msg);
			}
		}
	}

	internal NetIncomingMessage CreateIncomingMessage(NetIncomingMessageType tp, string text)
	{
		if (string.IsNullOrEmpty(text))
		{
			NetIncomingMessage netIncomingMessage = CreateIncomingMessage(tp, 1);
			netIncomingMessage.Write(string.Empty);
			return netIncomingMessage;
		}
		int byteCount = Encoding.UTF8.GetByteCount(text);
		NetIncomingMessage netIncomingMessage2 = CreateIncomingMessage(tp, byteCount + ((byteCount <= 127) ? 1 : 2));
		netIncomingMessage2.Write(text);
		return netIncomingMessage2;
	}

	public NetSendResult SendMessage(NetOutgoingMessage msg, NetConnection recipient, NetDeliveryMethod method)
	{
		return SendMessage(msg, recipient, method, 0);
	}

	public NetSendResult SendMessage(NetOutgoingMessage msg, NetConnection recipient, NetDeliveryMethod method, int sequenceChannel)
	{
		if (msg == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (recipient == null)
		{
			throw new ArgumentNullException("recipient");
		}
		if (sequenceChannel >= 32)
		{
			throw new ArgumentOutOfRangeException("sequenceChannel");
		}
		if (msg.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		msg.m_isSent = true;
		bool flag = (method == NetDeliveryMethod.Unreliable || method == NetDeliveryMethod.UnreliableSequenced) && m_configuration.UnreliableSizeBehaviour != NetUnreliableSizeBehaviour.NormalFragmentation;
		if (5 + msg.LengthBytes <= recipient.m_currentMTU || flag)
		{
			Interlocked.Increment(ref msg.m_recyclingCount);
			return recipient.EnqueueMessage(msg, method, sequenceChannel);
		}
		if (recipient.m_status != NetConnectionStatus.Connected)
		{
			return NetSendResult.FailedNotConnected;
		}
		return SendFragmentedMessage(msg, new NetConnection[1] { recipient }, method, sequenceChannel);
	}

	internal static int GetMTU(IList<NetConnection> recipients)
	{
		int count = recipients.Count;
		int num = int.MaxValue;
		if (count < 1)
		{
			return 508;
		}
		for (int i = 0; i < count; i++)
		{
			int currentMTU = recipients[i].m_currentMTU;
			if (currentMTU < num)
			{
				num = currentMTU;
			}
		}
		return num;
	}

	public void SendMessage(NetOutgoingMessage msg, IList<NetConnection> recipients, NetDeliveryMethod method, int sequenceChannel)
	{
		if (msg == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (recipients == null)
		{
			if (!msg.m_isSent)
			{
				Recycle(msg);
			}
			throw new ArgumentNullException("recipients");
		}
		if (recipients.Count < 1)
		{
			if (!msg.m_isSent)
			{
				Recycle(msg);
			}
			throw new NetException("recipients must contain at least one item");
		}
		if (method != NetDeliveryMethod.Unreliable)
		{
			_ = 34;
		}
		if (msg.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		msg.m_isSent = true;
		int mTU = GetMTU(recipients);
		if (msg.GetEncodedSize() <= mTU)
		{
			Interlocked.Add(ref msg.m_recyclingCount, recipients.Count);
			{
				foreach (NetConnection recipient in recipients)
				{
					if (recipient == null)
					{
						Interlocked.Decrement(ref msg.m_recyclingCount);
					}
					else if (recipient.EnqueueMessage(msg, method, sequenceChannel) == NetSendResult.Dropped)
					{
						Interlocked.Decrement(ref msg.m_recyclingCount);
					}
				}
				return;
			}
		}
		SendFragmentedMessage(msg, recipients, method, sequenceChannel);
	}

	public void SendUnconnectedMessage(NetOutgoingMessage msg, string host, int port)
	{
		if (msg == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (host == null)
		{
			throw new ArgumentNullException("host");
		}
		if (msg.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		IPAddress iPAddress = NetUtility.Resolve(host);
		if (iPAddress == null)
		{
			throw new NetException("Failed to resolve " + host);
		}
		int num = m_configuration.MTUForAddress(iPAddress);
		if (msg.LengthBytes > num)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (whichever is appropriate, currently " + num + ")");
		}
		msg.m_isSent = true;
		msg.m_messageType = NetMessageType.Unconnected;
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((new IPEndPoint(iPAddress, port), msg));
	}

	public void SendUnconnectedMessage(NetOutgoingMessage msg, IPEndPoint recipient)
	{
		if (msg == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (recipient == null)
		{
			throw new ArgumentNullException("recipient");
		}
		if (msg.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		int num = m_configuration.MTUForEndPoint(recipient);
		if (msg.LengthBytes > num)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (currently " + num + ")");
		}
		msg.m_messageType = NetMessageType.Unconnected;
		msg.m_isSent = true;
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue((recipient, msg));
	}

	public void SendUnconnectedMessage(NetOutgoingMessage msg, IList<IPEndPoint> recipients)
	{
		if (msg == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (recipients == null)
		{
			throw new ArgumentNullException("recipients");
		}
		if (recipients.Count < 1)
		{
			throw new NetException("recipients must contain at least one item");
		}
		if (msg.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		int num = int.MaxValue;
		foreach (IPEndPoint recipient in recipients)
		{
			num = Math.Min(num, m_configuration.MTUForEndPoint(recipient));
		}
		if (msg.LengthBytes > num)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (currently " + num + ")");
		}
		msg.m_messageType = NetMessageType.Unconnected;
		msg.m_isSent = true;
		Interlocked.Add(ref msg.m_recyclingCount, recipients.Count);
		foreach (IPEndPoint recipient2 in recipients)
		{
			m_unsentUnconnectedMessages.Enqueue((recipient2, msg));
		}
	}

	public void SendUnconnectedToSelf(NetOutgoingMessage om)
	{
		if (om == null)
		{
			throw new ArgumentNullException("msg");
		}
		if (om.m_isSent)
		{
			throw new NetException("This message has already been sent! Use NetPeer.SendMessage() to send to multiple recipients efficiently");
		}
		EndPoint endPoint = m_socket?.LocalEndPoint;
		if (endPoint == null)
		{
			throw new InvalidOperationException("Local socket is not bound");
		}
		om.m_messageType = NetMessageType.Unconnected;
		om.m_isSent = true;
		if (!m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.UnconnectedData))
		{
			Interlocked.Decrement(ref om.m_recyclingCount);
			return;
		}
		NetIncomingMessage netIncomingMessage = CreateIncomingMessage(NetIncomingMessageType.UnconnectedData, om.LengthBytes);
		netIncomingMessage.Write(om);
		netIncomingMessage.m_isFragment = false;
		netIncomingMessage.m_receiveTime = NetTime.Now;
		netIncomingMessage.m_senderConnection = null;
		netIncomingMessage.m_senderEndPoint = (IPEndPoint)endPoint;
		Recycle(om);
		ReleaseMessage(netIncomingMessage);
	}
}
