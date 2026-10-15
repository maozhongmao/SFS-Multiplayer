using System;

namespace Lidgren.Network;

public abstract class NetBlockEncryptionBase : NetEncryption
{
	private byte[] m_tmp;

	public abstract int BlockSize { get; }

	public NetBlockEncryptionBase(NetPeer peer)
		: base(peer)
	{
		m_tmp = new byte[BlockSize];
	}

	public override bool Encrypt(NetOutgoingMessage msg)
	{
		int lengthBits = msg.LengthBits;
		int lengthBytes = msg.LengthBytes;
		int blockSize = BlockSize;
		int num = (int)Math.Ceiling((double)lengthBytes / (double)blockSize);
		int num2 = num * blockSize;
		msg.EnsureBufferSize(num2 * 8 + 32);
		msg.LengthBits = num2 * 8;
		for (int i = 0; i < num; i++)
		{
			EncryptBlock(msg.m_data, i * blockSize, m_tmp);
			Buffer.BlockCopy(m_tmp, 0, msg.m_data, i * blockSize, m_tmp.Length);
		}
		msg.Write((uint)lengthBits);
		return true;
	}

	public override bool Decrypt(NetIncomingMessage msg)
	{
		int num = msg.LengthBytes - 4;
		int blockSize = BlockSize;
		int num2 = num / blockSize;
		if (num2 * blockSize != num)
		{
			return false;
		}
		for (int i = 0; i < num2; i++)
		{
			DecryptBlock(msg.m_data, i * blockSize, m_tmp);
			Buffer.BlockCopy(m_tmp, 0, msg.m_data, i * blockSize, m_tmp.Length);
		}
		uint bitLength = NetBitWriter.ReadUInt32(msg.m_data, 32, num * 8);
		msg.m_bitLength = (int)bitLength;
		return true;
	}

	protected abstract void EncryptBlock(byte[] source, int sourceOffset, byte[] destination);

	protected abstract void DecryptBlock(byte[] source, int sourceOffset, byte[] destination);
}
