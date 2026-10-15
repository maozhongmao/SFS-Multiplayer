using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace Lidgren.Network;

public class NetPeer
{
	private static int s_initializedPeersCount;

	private int m_listenPort;

	private object m_tag;

	private object m_messageReceivedEventCreationLock = new object();

	internal readonly List<NetConnection> m_connections;

	private readonly Dictionary<IPEndPoint, NetConnection> m_connectionLookup;

	private string m_shutdownReason;

	private int m_lastUsedFragmentGroup;

	private Dictionary<NetConnection, Dictionary<int, ReceivedFragmentGroup>> m_receivedFragmentGroups;

	private NetPeerStatus m_status;

	private Thread m_networkThread;

	private Socket m_socket;

	internal byte[] m_sendBuffer;

	internal byte[] m_receiveBuffer;

	internal NetIncomingMessage m_readHelperMessage;

	private EndPoint m_senderRemote;

	private object m_initializeLock = new object();

	private uint m_frameCounter;

	private double m_lastHeartbeat;

	private double m_lastSocketBind = -3.4028234663852886E+38;

	private NetUPnP m_upnp;

	internal bool m_needFlushSendQueue;

	internal readonly NetPeerConfiguration m_configuration;

	private readonly NetQueue<NetIncomingMessage> m_releasedIncomingMessages;

	internal readonly NetQueue<NetTuple<IPEndPoint, NetOutgoingMessage>> m_unsentUnconnectedMessages;

	internal Dictionary<IPEndPoint, NetConnection> m_handshakes;

	internal readonly NetPeerStatistics m_statistics;

	internal long m_uniqueIdentifier;

	internal bool m_executeFlushSendQueue;

	private AutoResetEvent m_messageReceivedEvent;

	private List<NetTuple<SynchronizationContext, SendOrPostCallback>> m_receiveCallbacks;

	internal List<byte[]> m_storagePool;

	private NetQueue<NetOutgoingMessage> m_outgoingMessagesPool;

	private NetQueue<NetIncomingMessage> m_incomingMessagesPool;

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

	public NetUPnP UPnP => m_upnp;

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

	public Socket Socket => m_socket;

	public void Introduce(IPEndPoint hostInternal, IPEndPoint hostExternal, IPEndPoint clientInternal, IPEndPoint clientExternal, string token)
	{
		NetOutgoingMessage netOutgoingMessage = CreateMessage(10 + token.Length + 1);
		netOutgoingMessage.m_messageType = NetMessageType.NatIntroduction;
		netOutgoingMessage.Write((byte)0);
		netOutgoingMessage.Write(hostInternal);
		netOutgoingMessage.Write(hostExternal);
		netOutgoingMessage.Write(token);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(clientExternal, netOutgoingMessage));
		netOutgoingMessage = CreateMessage(10 + token.Length + 1);
		netOutgoingMessage.m_messageType = NetMessageType.NatIntroduction;
		netOutgoingMessage.Write((byte)1);
		netOutgoingMessage.Write(clientInternal);
		netOutgoingMessage.Write(clientExternal);
		netOutgoingMessage.Write(token);
		Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(hostExternal, netOutgoingMessage));
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
			m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(item, netOutgoingMessage));
			netOutgoingMessage = CreateMessage(1);
			netOutgoingMessage.m_messageType = NetMessageType.NatPunchMessage;
			netOutgoingMessage.Write(b);
			netOutgoingMessage.Write(source);
			Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(item2, netOutgoingMessage));
		}
	}

	private void HandleNatPunch(int ptr, IPEndPoint senderEndPoint)
	{
		NetIncomingMessage netIncomingMessage = SetupReadHelperMessage(ptr, 1000);
		if (netIncomingMessage.ReadByte() != 0)
		{
			string source = netIncomingMessage.ReadString();
			NetIncomingMessage netIncomingMessage2 = CreateIncomingMessage(NetIncomingMessageType.NatIntroductionSuccess, 10);
			netIncomingMessage2.m_senderEndPoint = senderEndPoint;
			netIncomingMessage2.Write(source);
			ReleaseMessage(netIncomingMessage2);
			NetOutgoingMessage netOutgoingMessage = CreateMessage(1);
			netOutgoingMessage.m_messageType = NetMessageType.NatPunchMessage;
			netOutgoingMessage.Write((byte)0);
			netOutgoingMessage.Write(source);
			Interlocked.Increment(ref netOutgoingMessage.m_recyclingCount);
			m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(senderEndPoint, netOutgoingMessage));
		}
	}

	public NetPeer(NetPeerConfiguration config)
	{
		m_configuration = config;
		m_statistics = new NetPeerStatistics(this);
		m_releasedIncomingMessages = new NetQueue<NetIncomingMessage>(4);
		m_unsentUnconnectedMessages = new NetQueue<NetTuple<IPEndPoint, NetOutgoingMessage>>(2);
		m_connections = new List<NetConnection>();
		m_connectionLookup = new Dictionary<IPEndPoint, NetConnection>();
		m_handshakes = new Dictionary<IPEndPoint, NetConnection>();
		m_senderRemote = new IPEndPoint(IPAddress.Any, 0);
		m_status = NetPeerStatus.NotRunning;
		m_receivedFragmentGroups = new Dictionary<NetConnection, Dictionary<int, ReceivedFragmentGroup>>();
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

	public NetConnection GetConnection(IPEndPoint ep)
	{
		m_connectionLookup.TryGetValue(ep, out var value);
		return value;
	}

	public NetIncomingMessage WaitMessage(int maxMillis)
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

	public NetIncomingMessage ReadMessage()
	{
		if (m_releasedIncomingMessages.TryDequeue(out var item) && item.MessageType == NetIncomingMessageType.StatusChanged)
		{
			NetConnectionStatus visibleStatus = (NetConnectionStatus)item.PeekByte();
			item.SenderConnection.m_visibleStatus = visibleStatus;
		}
		return item;
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

	private static IPEndPoint GetNetEndPoint(string host, int port)
	{
		IPAddress iPAddress = NetUtility.Resolve(host);
		if (iPAddress == null)
		{
			throw new NetException("Could not resolve host");
		}
		return new IPEndPoint(iPAddress, port);
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

	public virtual NetConnection Connect(IPEndPoint remoteEndPoint, NetOutgoingMessage hailMessage)
	{
		if (remoteEndPoint == null)
		{
			throw new ArgumentNullException("remoteEndPoint");
		}
		lock (m_connections)
		{
			if (m_status == NetPeerStatus.NotRunning)
			{
				throw new NetException("Must call Start() first");
			}
			if (m_connectionLookup.ContainsKey(remoteEndPoint))
			{
				throw new NetException("Already connected to that endpoint!");
			}
			if (m_handshakes.TryGetValue(remoteEndPoint, out var value))
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
					LogWarning("Weird situation; Connect() already in progress to remote endpoint; but hs status is " + value.m_status);
					break;
				}
				return value;
			}
			NetConnection netConnection = new NetConnection(this, remoteEndPoint);
			netConnection.m_status = NetConnectionStatus.InitiatedConnect;
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

	public void Shutdown(string bye)
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
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(new IPEndPoint(NetUtility.GetBroadcastAddress(), serverPort), netOutgoingMessage));
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
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(endPoint, netOutgoingMessage));
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
		if (msg.LengthBytes >= m_configuration.MaximumTransmissionUnit)
		{
			throw new NetException("Cannot send discovery message larger than MTU (currently " + m_configuration.MaximumTransmissionUnit + " bytes)");
		}
		msg.m_messageType = NetMessageType.DiscoveryResponse;
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(recipient, msg));
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
		int num = NetFragmentationHelper.ReadHeader(im.m_data, 0, out group, out totalBits, out chunkByteSize, out chunkNumber);
		int num2 = NetUtility.BytesToHoldBits(totalBits);
		int num3 = num2 / chunkByteSize;
		if (num3 * chunkByteSize < num2)
		{
			num3++;
		}
		if (chunkNumber >= num3)
		{
			LogWarning("Index out of bounds for chunk " + chunkNumber + " (total chunks " + num3 + ")");
			return;
		}
		if (!m_receivedFragmentGroups.TryGetValue(im.SenderConnection, out var value))
		{
			value = new Dictionary<int, ReceivedFragmentGroup>();
			m_receivedFragmentGroups[im.SenderConnection] = value;
		}
		if (!value.TryGetValue(group, out var value2))
		{
			value2 = new ReceivedFragmentGroup();
			value2.Data = new byte[num2];
			value2.ReceivedChunks = new NetBitVector(num3);
			value[group] = value2;
		}
		value2.ReceivedChunks[chunkNumber] = true;
		int dstOffset = chunkNumber * chunkByteSize;
		Buffer.BlockCopy(im.m_data, num, value2.Data, dstOffset, im.LengthBytes - num);
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

	public void RegisterReceivedCallback(SendOrPostCallback callback, SynchronizationContext syncContext = null)
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
			m_receiveCallbacks = new List<NetTuple<SynchronizationContext, SendOrPostCallback>>();
		}
		m_receiveCallbacks.Add(new NetTuple<SynchronizationContext, SendOrPostCallback>(syncContext, callback));
	}

	public void UnregisterReceivedCallback(SendOrPostCallback callback)
	{
		if (m_receiveCallbacks == null)
		{
			return;
		}
		while (true)
		{
			int num = 0;
			while (true)
			{
				if (num < m_receiveCallbacks.Count)
				{
					if (m_receiveCallbacks[num].Item2.Equals(callback))
					{
						break;
					}
					num++;
					continue;
				}
				if (m_receiveCallbacks.Count < 1)
				{
					m_receiveCallbacks = null;
				}
				return;
			}
			m_receiveCallbacks.RemoveAt(num);
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
		foreach (NetTuple<SynchronizationContext, SendOrPostCallback> receiveCallback in m_receiveCallbacks)
		{
			try
			{
				receiveCallback.Item1.Post(receiveCallback.Item2, this);
			}
			catch (Exception ex)
			{
				LogWarning("Receive callback exception:" + ex);
			}
		}
	}

	private void BindSocket(bool reBind)
	{
		double now = NetTime.Now;
		if (!(now - m_lastSocketBind < 1.0))
		{
			m_lastSocketBind = now;
			if (m_socket == null)
			{
				m_socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
			}
			if (reBind)
			{
				m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.ReuseAddress, 1);
			}
			m_socket.ReceiveBufferSize = m_configuration.ReceiveBufferSize;
			m_socket.SendBufferSize = m_configuration.SendBufferSize;
			m_socket.Blocking = false;
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
			IPEndPoint iPEndPoint = m_socket.LocalEndPoint as IPEndPoint;
			m_listenPort = iPEndPoint.Port;
		}
	}

	private void InitializeNetwork()
	{
		lock (m_initializeLock)
		{
			m_configuration.Lock();
			if (m_status != NetPeerStatus.Running)
			{
				if (m_configuration.m_enableUPnP)
				{
					m_upnp = new NetUPnP(this);
				}
				InitializePools();
				m_releasedIncomingMessages.Clear();
				m_unsentUnconnectedMessages.Clear();
				m_handshakes.Clear();
				BindSocket(reBind: false);
				m_receiveBuffer = new byte[m_configuration.ReceiveBufferSize];
				m_sendBuffer = new byte[m_configuration.SendBufferSize];
				m_readHelperMessage = new NetIncomingMessage(NetIncomingMessageType.Error);
				m_readHelperMessage.m_data = m_receiveBuffer;
				byte[] macAddressBytes = NetUtility.GetMacAddressBytes();
				IPEndPoint iPEndPoint = m_socket.LocalEndPoint as IPEndPoint;
				byte[] bytes = BitConverter.GetBytes(iPEndPoint.GetHashCode());
				byte[] array = new byte[bytes.Length + macAddressBytes.Length];
				Array.Copy(bytes, 0, array, 0, bytes.Length);
				Array.Copy(macAddressBytes, 0, array, bytes.Length, macAddressBytes.Length);
				m_uniqueIdentifier = BitConverter.ToInt64(NetUtility.ComputeSHAHash(array), 0);
				m_status = NetPeerStatus.Running;
			}
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
			m_receiveBuffer = null;
			m_sendBuffer = null;
			m_unsentUnconnectedMessages.Clear();
			m_connections.Clear();
			m_connectionLookup.Clear();
			m_handshakes.Clear();
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
						m_connectionLookup.Remove(netConnection.RemoteEndPoint);
					}
				}
			}
			m_executeFlushSendQueue = false;
			NetTuple<IPEndPoint, NetOutgoingMessage> item;
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
		do
		{
			int num4 = 0;
			try
			{
				num4 = m_socket.ReceiveFrom(m_receiveBuffer, 0, m_receiveBuffer.Length, SocketFlags.None, ref m_senderRemote);
			}
			catch (SocketException ex)
			{
				switch (ex.SocketErrorCode)
				{
				case SocketError.ConnectionReset:
					LogWarning("ConnectionReset");
					break;
				case SocketError.NotConnected:
					BindSocket(reBind: true);
					break;
				default:
					LogWarning("Socket exception: " + ex.ToString());
					break;
				}
				break;
			}
			if (num4 < 5)
			{
				break;
			}
			IPEndPoint iPEndPoint = (IPEndPoint)m_senderRemote;
			if (m_upnp != null && now < m_upnp.m_discoveryResponseDeadline && num4 > 32)
			{
				string text = Encoding.UTF8.GetString(m_receiveBuffer, 0, num4);
				if (text.Contains("upnp:rootdevice") || text.Contains("UPnP/1.0"))
				{
					try
					{
						text = text.Substring(text.ToLower().IndexOf("location:") + 9);
						text = text.Substring(0, text.IndexOf("\r")).Trim();
						m_upnp.ExtractServiceUrl(text);
						break;
					}
					catch (Exception)
					{
						break;
					}
				}
			}
			NetConnection value2 = null;
			m_connectionLookup.TryGetValue(iPEndPoint, out value2);
			int num5 = 0;
			int num6 = 0;
			int num8;
			for (int i = 0; num4 - i >= 5; i += num8)
			{
				num5++;
				NetMessageType netMessageType = (NetMessageType)m_receiveBuffer[i++];
				byte b = m_receiveBuffer[i++];
				byte b2 = m_receiveBuffer[i++];
				bool flag = (b & 1) == 1;
				ushort sequenceNumber = (ushort)((b >> 1) | (b2 << 7));
				if (flag)
				{
					num6++;
				}
				ushort num7 = (ushort)(m_receiveBuffer[i++] | (m_receiveBuffer[i++] << 8));
				num8 = NetUtility.BytesToHoldBits(num7);
				if (num4 - i < num8)
				{
					LogWarning("Malformed packet; stated payload length " + num8 + ", remaining bytes " + (num4 - i));
					return;
				}
				if ((int)netMessageType >= 99 && (int)netMessageType <= 127)
				{
					ThrowOrLog("Unexpected NetMessageType: " + netMessageType);
					return;
				}
				try
				{
					if ((int)netMessageType >= 128)
					{
						if (value2 != null)
						{
							value2.ReceivedLibraryMessage(netMessageType, i, num8);
						}
						else
						{
							ReceivedUnconnectedLibraryMessage(now, iPEndPoint, netMessageType, i, num8);
						}
						continue;
					}
					if (value2 == null && !m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.UnconnectedData))
					{
						return;
					}
					NetIncomingMessage netIncomingMessage = CreateIncomingMessage(NetIncomingMessageType.Data, num8);
					netIncomingMessage.m_isFragment = flag;
					netIncomingMessage.m_receiveTime = now;
					netIncomingMessage.m_sequenceNumber = sequenceNumber;
					netIncomingMessage.m_receivedMessageType = netMessageType;
					netIncomingMessage.m_senderConnection = value2;
					netIncomingMessage.m_senderEndPoint = iPEndPoint;
					netIncomingMessage.m_bitLength = num7;
					Buffer.BlockCopy(m_receiveBuffer, i, netIncomingMessage.m_data, 0, num8);
					if (value2 != null)
					{
						if (netMessageType == NetMessageType.Unconnected)
						{
							netIncomingMessage.m_incomingMessageType = NetIncomingMessageType.UnconnectedData;
							ReleaseMessage(netIncomingMessage);
						}
						else
						{
							value2.ReceivedMessage(netIncomingMessage);
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
					LogError("Packet parsing error: " + ex3.Message + " from " + iPEndPoint);
				}
			}
		}
		while (m_socket.Available > 0);
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
				Buffer.BlockCopy(m_receiveBuffer, ptr, netIncomingMessage.m_data, 0, payloadByteLength);
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
				Buffer.BlockCopy(m_receiveBuffer, ptr, netIncomingMessage.m_data, 0, payloadByteLength);
			}
			netIncomingMessage.m_receiveTime = now;
			netIncomingMessage.m_bitLength = payloadByteLength * 8;
			netIncomingMessage.m_senderEndPoint = senderEndPoint;
			ReleaseMessage(netIncomingMessage);
		}
	}

	private void ReceivedUnconnectedLibraryMessage(double now, IPEndPoint senderEndPoint, NetMessageType tp, int ptr, int payloadByteLength)
	{
		if (m_handshakes.TryGetValue(senderEndPoint, out var value))
		{
			value.ReceivedHandshake(now, tp, ptr, payloadByteLength);
			return;
		}
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
		case NetMessageType.ConnectResponse:
			lock (m_handshakes)
			{
				foreach (KeyValuePair<IPEndPoint, NetConnection> handshake in m_handshakes)
				{
					if (handshake.Key.Address.Equals(senderEndPoint.Address) && handshake.Value.m_connectionInitiator)
					{
						NetConnection value2 = handshake.Value;
						m_connectionLookup.Remove(handshake.Key);
						m_handshakes.Remove(handshake.Key);
						value2.MutateEndPoint(senderEndPoint);
						m_connectionLookup.Add(senderEndPoint, value2);
						m_handshakes.Add(senderEndPoint, value2);
						value2.ReceivedHandshake(now, tp, ptr, payloadByteLength);
						return;
					}
				}
			}
			LogWarning(string.Concat("Received unhandled library message ", tp, " from ", senderEndPoint));
			break;
		case NetMessageType.Connect:
		{
			if (!m_configuration.AcceptIncomingConnections)
			{
				LogWarning("Received Connect, but we're not accepting incoming connections!");
				break;
			}
			int num = m_handshakes.Count + m_connections.Count;
			if (num >= m_configuration.m_maximumConnections)
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
		}
		case NetMessageType.Disconnect:
			break;
		default:
			LogWarning(string.Concat("Received unhandled library message ", tp, " from ", senderEndPoint));
			break;
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
			m_connectionLookup.Add(conn.m_remoteEndPoint, conn);
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

	internal bool SendMTUPacket(int numBytes, IPEndPoint target)
	{
		try
		{
			m_socket.DontFragment = true;
			int num = m_socket.SendTo(m_sendBuffer, 0, numBytes, SocketFlags.None, target);
			if (numBytes != num)
			{
				LogWarning("Failed to send the full " + numBytes + "; only " + num + " bytes sent in packet!");
			}
		}
		catch (SocketException ex)
		{
			if (ex.SocketErrorCode == SocketError.MessageSize)
			{
				return false;
			}
			if (ex.SocketErrorCode == SocketError.WouldBlock)
			{
				LogWarning("Socket threw exception; would block - send buffer full? Increase in NetPeerConfiguration");
				return true;
			}
			if (ex.SocketErrorCode == SocketError.ConnectionReset)
			{
				return true;
			}
			LogError(string.Concat("Failed to send packet: (", ex.SocketErrorCode, ") ", ex));
		}
		catch (Exception ex2)
		{
			LogError("Failed to send packet: " + ex2);
		}
		finally
		{
			m_socket.DontFragment = false;
		}
		return true;
	}

	internal void SendPacket(int numBytes, IPEndPoint target, int numMessages, out bool connectionReset)
	{
		connectionReset = false;
		IPAddress iPAddress = null;
		try
		{
			iPAddress = NetUtility.GetCachedBroadcastAddress();
			if (target.Address == iPAddress)
			{
				m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: true);
			}
			int num = m_socket.SendTo(m_sendBuffer, 0, numBytes, SocketFlags.None, target);
			if (numBytes != num)
			{
				LogWarning("Failed to send the full " + numBytes + "; only " + num + " bytes sent in packet!");
			}
		}
		catch (SocketException ex)
		{
			if (ex.SocketErrorCode == SocketError.WouldBlock)
			{
				LogWarning("Socket threw exception; would block - send buffer full? Increase in NetPeerConfiguration");
			}
			else if (ex.SocketErrorCode == SocketError.ConnectionReset)
			{
				connectionReset = true;
			}
			else
			{
				LogError("Failed to send packet: " + ex);
			}
		}
		catch (Exception ex2)
		{
			LogError("Failed to send packet: " + ex2);
		}
		finally
		{
			if (target.Address == iPAddress)
			{
				m_socket.SetSocketOption(SocketOptionLevel.Socket, SocketOptionName.Broadcast, optionValue: false);
			}
		}
	}

	private void FlushDelayedPackets()
	{
	}

	[Conditional("DEBUG")]
	internal void LogVerbose(string message)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.VerboseDebugMessage))
		{
			ReleaseMessage(CreateIncomingMessage(NetIncomingMessageType.VerboseDebugMessage, message));
		}
	}

	[Conditional("DEBUG")]
	internal void LogDebug(string message)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.DebugMessage))
		{
			ReleaseMessage(CreateIncomingMessage(NetIncomingMessageType.DebugMessage, message));
		}
	}

	internal void LogWarning(string message)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.WarningMessage))
		{
			ReleaseMessage(CreateIncomingMessage(NetIncomingMessageType.WarningMessage, message));
		}
	}

	internal void LogError(string message)
	{
		if (m_configuration.IsMessageTypeEnabled(NetIncomingMessageType.ErrorMessage))
		{
			ReleaseMessage(CreateIncomingMessage(NetIncomingMessageType.ErrorMessage, message));
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
		if (m_storagePool == null || storage == null)
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

	public NetOutgoingMessage CreateMessage(string content)
	{
		NetOutgoingMessage netOutgoingMessage = ((!string.IsNullOrEmpty(content)) ? CreateMessage(2 + content.Length) : CreateMessage(1));
		netOutgoingMessage.Write(content);
		return netOutgoingMessage;
	}

	public NetOutgoingMessage CreateMessage(int initialCapacity)
	{
		if (m_outgoingMessagesPool == null || !m_outgoingMessagesPool.TryDequeue(out var item))
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
		if (m_incomingMessagesPool == null || !m_incomingMessagesPool.TryDequeue(out var item))
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
		if (m_incomingMessagesPool == null || !m_incomingMessagesPool.TryDequeue(out var item))
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
			byte[] data = msg.m_data;
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
			byte[] data = msg.m_data;
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
		NetIncomingMessage netIncomingMessage;
		if (string.IsNullOrEmpty(text))
		{
			netIncomingMessage = CreateIncomingMessage(tp, 1);
			netIncomingMessage.Write(string.Empty);
			return netIncomingMessage;
		}
		int byteCount = Encoding.UTF8.GetByteCount(text);
		netIncomingMessage = CreateIncomingMessage(tp, byteCount + ((byteCount <= 127) ? 1 : 2));
		netIncomingMessage.Write(text);
		return netIncomingMessage;
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
		int num = 5 + msg.LengthBytes;
		if (num <= recipient.m_currentMTU || flag)
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
			return 1408;
		}
		for (int i = 0; i < count; i++)
		{
			NetConnection netConnection = recipients[i];
			int currentMTU = netConnection.m_currentMTU;
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
		int encodedSize = msg.GetEncodedSize();
		if (encodedSize <= mTU)
		{
			Interlocked.Add(ref msg.m_recyclingCount, recipients.Count);
			{
				foreach (NetConnection recipient in recipients)
				{
					if (recipient == null)
					{
						Interlocked.Decrement(ref msg.m_recyclingCount);
						continue;
					}
					NetSendResult netSendResult = recipient.EnqueueMessage(msg, method, sequenceChannel);
					if (netSendResult == NetSendResult.Dropped)
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
		if (msg.LengthBytes > m_configuration.MaximumTransmissionUnit)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (currently " + m_configuration.MaximumTransmissionUnit + ")");
		}
		msg.m_isSent = true;
		msg.m_messageType = NetMessageType.Unconnected;
		IPAddress iPAddress = NetUtility.Resolve(host);
		if (iPAddress == null)
		{
			throw new NetException("Failed to resolve " + host);
		}
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(new IPEndPoint(iPAddress, port), msg));
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
		if (msg.LengthBytes > m_configuration.MaximumTransmissionUnit)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (currently " + m_configuration.MaximumTransmissionUnit + ")");
		}
		msg.m_messageType = NetMessageType.Unconnected;
		msg.m_isSent = true;
		Interlocked.Increment(ref msg.m_recyclingCount);
		m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(recipient, msg));
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
		if (msg.LengthBytes > m_configuration.MaximumTransmissionUnit)
		{
			throw new NetException("Unconnected messages too long! Must be shorter than NetConfiguration.MaximumTransmissionUnit (currently " + m_configuration.MaximumTransmissionUnit + ")");
		}
		msg.m_messageType = NetMessageType.Unconnected;
		msg.m_isSent = true;
		Interlocked.Add(ref msg.m_recyclingCount, recipients.Count);
		foreach (IPEndPoint recipient in recipients)
		{
			m_unsentUnconnectedMessages.Enqueue(new NetTuple<IPEndPoint, NetOutgoingMessage>(recipient, msg));
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
		netIncomingMessage.m_senderEndPoint = m_socket.LocalEndPoint as IPEndPoint;
		Recycle(om);
		ReleaseMessage(netIncomingMessage);
	}
}
