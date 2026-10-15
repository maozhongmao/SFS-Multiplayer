using System.Collections.Generic;

namespace Lidgren.Network;

public class NetServer : NetPeer
{
	public NetServer(NetPeerConfiguration config)
		: base(config)
	{
		config.AcceptIncomingConnections = true;
	}

	public void SendToAll(NetOutgoingMessage msg, NetDeliveryMethod method)
	{
		List<NetConnection> connections = base.Connections;
		if (connections.Count <= 0)
		{
			if (!msg.m_isSent)
			{
				Recycle(msg);
			}
		}
		else
		{
			SendMessage(msg, connections, method, 0);
		}
	}

	public void SendToAll(NetOutgoingMessage msg, NetConnection except, NetDeliveryMethod method, int sequenceChannel)
	{
		List<NetConnection> connections = base.Connections;
		if (connections.Count <= 0)
		{
			if (!msg.m_isSent)
			{
				Recycle(msg);
			}
			return;
		}
		if (except == null)
		{
			SendMessage(msg, connections, method, sequenceChannel);
			return;
		}
		List<NetConnection> list = new List<NetConnection>(connections.Count - 1);
		foreach (NetConnection item in connections)
		{
			if (item != except)
			{
				list.Add(item);
			}
		}
		if (list.Count > 0)
		{
			SendMessage(msg, list, method, sequenceChannel);
		}
	}

	public override string ToString()
	{
		return "[NetServer " + base.ConnectionsCount + " connections]";
	}
}
