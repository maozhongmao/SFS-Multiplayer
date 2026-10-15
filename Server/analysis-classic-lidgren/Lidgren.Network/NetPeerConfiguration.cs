using System.Net;

namespace Lidgren.Network;

public sealed class NetPeerConfiguration
{
	public const int kDefaultMTU = 1408;

	private const string c_isLockedMessage = "You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer";

	private bool m_isLocked;

	private readonly string m_appIdentifier;

	private string m_networkThreadName;

	private IPAddress m_localAddress;

	private IPAddress m_broadcastAddress;

	internal bool m_acceptIncomingConnections;

	internal int m_maximumConnections;

	internal int m_defaultOutgoingMessageCapacity;

	internal float m_pingInterval;

	internal bool m_useMessageRecycling;

	internal int m_recycledCacheMaxCount;

	internal float m_connectionTimeout;

	internal bool m_enableUPnP;

	internal bool m_autoFlushSendQueue;

	private NetUnreliableSizeBehaviour m_unreliableSizeBehaviour;

	internal bool m_suppressUnreliableUnorderedAcks;

	internal NetIncomingMessageType m_disabledTypes;

	internal int m_port;

	internal int m_receiveBufferSize;

	internal int m_sendBufferSize;

	internal float m_resendHandshakeInterval;

	internal int m_maximumHandshakeAttempts;

	internal float m_loss;

	internal float m_duplicates;

	internal float m_minimumOneWayLatency;

	internal float m_randomOneWayLatency;

	internal int m_maximumTransmissionUnit;

	internal bool m_autoExpandMTU;

	internal float m_expandMTUFrequency;

	internal int m_expandMTUFailAttempts;

	public string AppIdentifier => m_appIdentifier;

	public NetUnreliableSizeBehaviour UnreliableSizeBehaviour
	{
		get
		{
			return m_unreliableSizeBehaviour;
		}
		set
		{
			m_unreliableSizeBehaviour = value;
		}
	}

	public string NetworkThreadName
	{
		get
		{
			return m_networkThreadName;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("NetworkThreadName may not be set after the NetPeer which uses the configuration has been started");
			}
			m_networkThreadName = value;
		}
	}

	public int MaximumConnections
	{
		get
		{
			return m_maximumConnections;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_maximumConnections = value;
		}
	}

	public int MaximumTransmissionUnit
	{
		get
		{
			return m_maximumTransmissionUnit;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			if (value < 1 || value >= 8192)
			{
				throw new NetException("MaximumTransmissionUnit must be between 1 and " + 8191 + " bytes");
			}
			m_maximumTransmissionUnit = value;
		}
	}

	public int DefaultOutgoingMessageCapacity
	{
		get
		{
			return m_defaultOutgoingMessageCapacity;
		}
		set
		{
			m_defaultOutgoingMessageCapacity = value;
		}
	}

	public float PingInterval
	{
		get
		{
			return m_pingInterval;
		}
		set
		{
			m_pingInterval = value;
		}
	}

	public bool UseMessageRecycling
	{
		get
		{
			return m_useMessageRecycling;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_useMessageRecycling = value;
		}
	}

	public int RecycledCacheMaxCount
	{
		get
		{
			return m_recycledCacheMaxCount;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_recycledCacheMaxCount = value;
		}
	}

	public float ConnectionTimeout
	{
		get
		{
			return m_connectionTimeout;
		}
		set
		{
			if (value < m_pingInterval)
			{
				throw new NetException("Connection timeout cannot be lower than ping interval!");
			}
			m_connectionTimeout = value;
		}
	}

	public bool EnableUPnP
	{
		get
		{
			return m_enableUPnP;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_enableUPnP = value;
		}
	}

	public bool AutoFlushSendQueue
	{
		get
		{
			return m_autoFlushSendQueue;
		}
		set
		{
			m_autoFlushSendQueue = value;
		}
	}

	public bool SuppressUnreliableUnorderedAcks
	{
		get
		{
			return m_suppressUnreliableUnorderedAcks;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_suppressUnreliableUnorderedAcks = value;
		}
	}

	public IPAddress LocalAddress
	{
		get
		{
			return m_localAddress;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_localAddress = value;
		}
	}

	public IPAddress BroadcastAddress
	{
		get
		{
			return m_broadcastAddress;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_broadcastAddress = value;
		}
	}

	public int Port
	{
		get
		{
			return m_port;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_port = value;
		}
	}

	public int ReceiveBufferSize
	{
		get
		{
			return m_receiveBufferSize;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_receiveBufferSize = value;
		}
	}

	public int SendBufferSize
	{
		get
		{
			return m_sendBufferSize;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_sendBufferSize = value;
		}
	}

	public bool AcceptIncomingConnections
	{
		get
		{
			return m_acceptIncomingConnections;
		}
		set
		{
			m_acceptIncomingConnections = value;
		}
	}

	public float ResendHandshakeInterval
	{
		get
		{
			return m_resendHandshakeInterval;
		}
		set
		{
			m_resendHandshakeInterval = value;
		}
	}

	public int MaximumHandshakeAttempts
	{
		get
		{
			return m_maximumHandshakeAttempts;
		}
		set
		{
			if (value < 1)
			{
				throw new NetException("MaximumHandshakeAttempts must be at least 1");
			}
			m_maximumHandshakeAttempts = value;
		}
	}

	public bool AutoExpandMTU
	{
		get
		{
			return m_autoExpandMTU;
		}
		set
		{
			if (m_isLocked)
			{
				throw new NetException("You may not modify the NetPeerConfiguration after it has been used to initialize a NetPeer");
			}
			m_autoExpandMTU = value;
		}
	}

	public float ExpandMTUFrequency
	{
		get
		{
			return m_expandMTUFrequency;
		}
		set
		{
			m_expandMTUFrequency = value;
		}
	}

	public int ExpandMTUFailAttempts
	{
		get
		{
			return m_expandMTUFailAttempts;
		}
		set
		{
			m_expandMTUFailAttempts = value;
		}
	}

	public NetPeerConfiguration(string appIdentifier)
	{
		if (string.IsNullOrEmpty(appIdentifier))
		{
			throw new NetException("App identifier must be at least one character long");
		}
		m_appIdentifier = appIdentifier;
		m_disabledTypes = (NetIncomingMessageType)6278;
		m_networkThreadName = "Lidgren network thread";
		m_localAddress = IPAddress.Any;
		m_broadcastAddress = IPAddress.Broadcast;
		IPAddress broadcastAddress = NetUtility.GetBroadcastAddress();
		if (broadcastAddress != null)
		{
			m_broadcastAddress = broadcastAddress;
		}
		m_port = 0;
		m_receiveBufferSize = 131071;
		m_sendBufferSize = 131071;
		m_acceptIncomingConnections = false;
		m_maximumConnections = 32;
		m_defaultOutgoingMessageCapacity = 16;
		m_pingInterval = 4f;
		m_connectionTimeout = 25f;
		m_useMessageRecycling = true;
		m_recycledCacheMaxCount = 64;
		m_resendHandshakeInterval = 3f;
		m_maximumHandshakeAttempts = 5;
		m_autoFlushSendQueue = true;
		m_suppressUnreliableUnorderedAcks = false;
		m_maximumTransmissionUnit = 1408;
		m_autoExpandMTU = false;
		m_expandMTUFrequency = 2f;
		m_expandMTUFailAttempts = 5;
		m_unreliableSizeBehaviour = NetUnreliableSizeBehaviour.IgnoreMTU;
		m_loss = 0f;
		m_minimumOneWayLatency = 0f;
		m_randomOneWayLatency = 0f;
		m_duplicates = 0f;
		m_isLocked = false;
	}

	internal void Lock()
	{
		m_isLocked = true;
	}

	public void EnableMessageType(NetIncomingMessageType type)
	{
		m_disabledTypes &= ~type;
	}

	public void DisableMessageType(NetIncomingMessageType type)
	{
		m_disabledTypes |= type;
	}

	public void SetMessageTypeEnabled(NetIncomingMessageType type, bool enabled)
	{
		if (enabled)
		{
			m_disabledTypes &= ~type;
		}
		else
		{
			m_disabledTypes |= type;
		}
	}

	public bool IsMessageTypeEnabled(NetIncomingMessageType type)
	{
		return (m_disabledTypes & type) != type;
	}

	public NetPeerConfiguration Clone()
	{
		NetPeerConfiguration netPeerConfiguration = MemberwiseClone() as NetPeerConfiguration;
		netPeerConfiguration.m_isLocked = false;
		return netPeerConfiguration;
	}
}
