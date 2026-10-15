using System.Text;

namespace Lidgren.Network;

public abstract class NetEncryption
{
	protected NetPeer m_peer;

	public NetEncryption(NetPeer peer)
	{
		if (peer == null)
		{
			throw new NetException("Peer must not be null");
		}
		m_peer = peer;
	}

	public void SetKey(string str)
	{
		byte[] bytes = Encoding.ASCII.GetBytes(str);
		SetKey(bytes, 0, bytes.Length);
	}

	public abstract void SetKey(byte[] data, int offset, int count);

	public abstract bool Encrypt(NetOutgoingMessage msg);

	public abstract bool Decrypt(NetIncomingMessage msg);
}
