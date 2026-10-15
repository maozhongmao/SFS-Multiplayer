using System;
using System.Text;

namespace Lidgren.Network;

public class NetXorEncryption : NetEncryption
{
	private byte[] m_key;

	public NetXorEncryption(NetPeer peer, byte[] key)
		: base(peer)
	{
		m_key = key;
	}

	public override void SetKey(byte[] data, int offset, int count)
	{
		m_key = new byte[count];
		Array.Copy(data, offset, m_key, 0, count);
	}

	public NetXorEncryption(NetPeer peer, string key)
		: base(peer)
	{
		m_key = Encoding.UTF8.GetBytes(key);
	}

	public override bool Encrypt(NetOutgoingMessage msg)
	{
		int lengthBytes = msg.LengthBytes;
		for (int i = 0; i < lengthBytes; i++)
		{
			int num = i % m_key.Length;
			msg.m_data[i] = (byte)(msg.m_data[i] ^ m_key[num]);
		}
		return true;
	}

	public override bool Decrypt(NetIncomingMessage msg)
	{
		int lengthBytes = msg.LengthBytes;
		for (int i = 0; i < lengthBytes; i++)
		{
			int num = i % m_key.Length;
			msg.m_data[i] = (byte)(msg.m_data[i] ^ m_key[num]);
		}
		return true;
	}
}
