using System.IO;
using System.Security.Cryptography;

namespace Lidgren.Network;

public abstract class NetCryptoProviderBase : NetEncryption
{
	protected SymmetricAlgorithm m_algorithm;

	public NetCryptoProviderBase(NetPeer peer, SymmetricAlgorithm algo)
		: base(peer)
	{
		m_algorithm = algo;
		m_algorithm.GenerateKey();
		m_algorithm.GenerateIV();
	}

	public override void SetKey(byte[] data, int offset, int count)
	{
		int num = m_algorithm.Key.Length;
		byte[] array = new byte[num];
		for (int i = 0; i < num; i++)
		{
			array[i] = data[offset + i % count];
		}
		m_algorithm.Key = array;
		num = m_algorithm.IV.Length;
		array = new byte[num];
		for (int j = 0; j < num; j++)
		{
			array[num - 1 - j] = data[offset + j % count];
		}
		m_algorithm.IV = array;
	}

	public override bool Encrypt(NetOutgoingMessage msg)
	{
		int lengthBits = msg.LengthBits;
		MemoryStream memoryStream = new MemoryStream();
		CryptoStream cryptoStream = new CryptoStream(memoryStream, m_algorithm.CreateEncryptor(), CryptoStreamMode.Write);
		cryptoStream.Write(msg.m_data, 0, msg.LengthBytes);
		cryptoStream.Close();
		byte[] array = memoryStream.ToArray();
		memoryStream.Close();
		msg.EnsureBufferSize((array.Length + 4) * 8);
		msg.LengthBits = 0;
		msg.Write((uint)lengthBits);
		msg.Write(array);
		msg.LengthBits = (array.Length + 4) * 8;
		return true;
	}

	public override bool Decrypt(NetIncomingMessage msg)
	{
		int num = (int)msg.ReadUInt32();
		MemoryStream stream = new MemoryStream(msg.m_data, 4, msg.LengthBytes - 4);
		CryptoStream cryptoStream = new CryptoStream(stream, m_algorithm.CreateDecryptor(), CryptoStreamMode.Read);
		int num2 = NetUtility.BytesToHoldBits(num);
		byte[] storage = m_peer.GetStorage(num2);
		cryptoStream.Read(storage, 0, num2);
		cryptoStream.Close();
		msg.m_data = storage;
		msg.m_bitLength = num;
		msg.m_readPosition = 0;
		return true;
	}
}
