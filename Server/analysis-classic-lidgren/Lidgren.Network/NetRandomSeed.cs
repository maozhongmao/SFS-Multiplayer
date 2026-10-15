using System;

namespace Lidgren.Network;

public static class NetRandomSeed
{
	private static int m_seedIncrement = -1640531527;

	[CLSCompliant(false)]
	public static uint GetUInt32()
	{
		ulong uInt = GetUInt64();
		uint num = (uint)uInt;
		uint num2 = (uint)(uInt >> 32);
		return num ^ num2;
	}

	[CLSCompliant(false)]
	public static ulong GetUInt64()
	{
		byte[] array = Guid.NewGuid().ToByteArray();
		ulong num = array[0] | ((ulong)array[1] << 8) | ((ulong)array[2] << 16) | ((ulong)array[3] << 24) | ((ulong)array[4] << 32) | ((ulong)array[5] << 40) | ((ulong)array[6] << 48) | ((ulong)array[7] << 56);
		return num ^ NetUtility.GetPlatformSeed(m_seedIncrement);
	}
}
