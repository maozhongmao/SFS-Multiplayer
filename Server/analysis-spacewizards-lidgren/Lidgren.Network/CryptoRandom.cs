using System;
using System.Security.Cryptography;

namespace Lidgren.Network;

public class CryptoRandom : NetRandom
{
	public new static readonly CryptoRandom Instance = new CryptoRandom();

	[CLSCompliant(false)]
	public override void Initialize(uint seed)
	{
	}

	[CLSCompliant(false)]
	public override uint NextUInt32()
	{
		byte[] array = new byte[4];
		RandomNumberGenerator.Fill(array);
		return (uint)(array[0] | (array[1] << 8) | (array[2] << 16) | (array[3] << 24));
	}

	public override void NextBytes(byte[] buffer)
	{
		RandomNumberGenerator.Fill(buffer);
	}

	public override void NextBytes(byte[] buffer, int offset, int length)
	{
		RandomNumberGenerator.Fill(buffer.AsSpan(offset, length));
	}
}
