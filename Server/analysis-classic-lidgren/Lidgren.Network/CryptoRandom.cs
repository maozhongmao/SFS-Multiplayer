using System;
using System.Security.Cryptography;

namespace Lidgren.Network;

public class CryptoRandom : NetRandom
{
	public new static readonly CryptoRandom Instance = new CryptoRandom();

	private RandomNumberGenerator m_rnd = new RNGCryptoServiceProvider();

	[CLSCompliant(false)]
	public override void Initialize(uint seed)
	{
		byte[] data = new byte[seed % 16];
		m_rnd.GetBytes(data);
	}

	[CLSCompliant(false)]
	public override uint NextUInt32()
	{
		byte[] array = new byte[4];
		m_rnd.GetBytes(array);
		return (uint)(array[0] | (array[1] << 8) | (array[2] << 16) | (array[3] << 24));
	}

	public override void NextBytes(byte[] buffer)
	{
		m_rnd.GetBytes(buffer);
	}

	public override void NextBytes(byte[] buffer, int offset, int length)
	{
		byte[] array = new byte[length];
		m_rnd.GetBytes(array);
		Array.Copy(array, 0, buffer, offset, length);
	}
}
