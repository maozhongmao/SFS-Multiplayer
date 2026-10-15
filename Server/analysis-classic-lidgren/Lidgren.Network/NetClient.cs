using System.Collections.Generic;
using System.Net;

namespace Lidgren.Network;

public class NetClient : NetPeer
{
	public NetConnection ServerConnection
	{
		get
		{
			NetConnection result = null;
			if (m_connections.Count > 0)
			{
				try
				{
					result = m_connections[0];
				}
				catch
				{
					return null;
				}
			}
			return result;
		}
	}

	public NetConnectionStatus ConnectionStatus => ServerConnection?.Status ?? NetConnectionStatus.Disconnected;

	public NetClient(NetPeerConfiguration config)
		: base(config)
	{
		config.AcceptIncomingConnections = false;
	}

	public override NetConnection Connect(IPEndPoint remoteEndPoint, NetOutgoingMessage hailMessage)
	{
		lock (m_connections)
		{
			if (m_connections.Count > 0)
			{
				LogWarning("Connect attempt failed; Already connected");
				return null;
			}
		}
		lock (m_handshakes)
		{
			if (m_handshakes.Count > 0)
			{
				LogWarning("Connect attempt failed; Handshake already in progress");
				return null;
			}
		}
		return base.Connect(remoteEndPoint, hailMessage);
	}

	public void Disconnect(string byeMessage)
	{
		NetConnection serverConnection = ServerConnection;
		if (serverConnection == null)
		{
			lock (m_handshakes)
			{
				if (m_handshakes.Count > 0)
				{
					foreach (KeyValuePair<IPEndPoint, NetConnection> handshake in m_handshakes)
					{
						handshake.Value.Disconnect(byeMessage);
					}
					return;
				}
			}
			LogWarning("Disconnect requested when not connected!");
		}
		else
		{
			serverConnection.Disconnect(byeMessage);
		}
	}

	public NetSendResult SendMessage(NetOutgoingMessage msg, NetDeliveryMethod method)
	{
		NetConnection serverConnection = ServerConnection;
		if (serverConnection == null)
		{
			LogWarning("Cannot send message, no server connection!");
			return NetSendResult.FailedNotConnected;
		}
		return serverConnection.SendMessage(msg, method, 0);
	}

	public NetSendResult SendMessage(NetOutgoingMessage msg, NetDeliveryMethod method, int sequenceChannel)
	{
		NetConnection serverConnection = ServerConnection;
		if (serverConnection == null)
		{
			LogWarning("Cannot send message, no server connection!");
			Recycle(msg);
			return NetSendResult.FailedNotConnected;
		}
		return serverConnection.SendMessage(msg, method, sequenceChannel);
	}

	public override string ToString()
	{
		return string.Concat("[NetClient ", ServerConnection, "]");
	}
}
