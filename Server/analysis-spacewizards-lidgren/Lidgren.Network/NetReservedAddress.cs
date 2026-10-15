using System;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;

namespace Lidgren.Network;

internal static class NetReservedAddress
{
	public static readonly (int ip, int mask)[] ReservedRanges = new(int, int)[18]
	{
		(IP(0, 0, 0, 0), 8),
		(IP(10, 0, 0, 0), 8),
		(IP(100, 64, 0, 0), 10),
		(IP(127, 0, 0, 0), 8),
		(IP(169, 254, 0, 0), 16),
		(IP(172, 16, 0, 0), 12),
		(IP(192, 0, 0, 0), 24),
		(IP(192, 0, 2, 0), 24),
		(IP(192, 31, 196, 0), 24),
		(IP(192, 52, 193, 0), 24),
		(IP(192, 88, 99, 0), 24),
		(IP(192, 168, 0, 0), 16),
		(IP(192, 175, 48, 0), 24),
		(IP(198, 18, 0, 0), 15),
		(IP(198, 51, 100, 0), 24),
		(IP(203, 0, 113, 0), 24),
		(IP(224, 0, 0, 0), 4),
		(IP(240, 0, 0, 0), 4)
	};

	private static int IP(byte a, byte b, byte c, byte d)
	{
		return (a << 24) | (b << 16) | (c << 8) | d;
	}

	public static bool IsAddressReserved(IPAddress address)
	{
		if (address.AddressFamily != AddressFamily.InterNetwork)
		{
			return false;
		}
		Span<byte> span = stackalloc byte[4];
		address.TryWriteBytes(span, out var bytesWritten);
		int num = BinaryPrimitives.ReadInt32BigEndian(span);
		(int, int)[] reservedRanges = ReservedRanges;
		for (bytesWritten = 0; bytesWritten < reservedRanges.Length; bytesWritten++)
		{
			(int, int) tuple = reservedRanges[bytesWritten];
			int item = tuple.Item1;
			int item2 = tuple.Item2;
			uint num2 = (uint)(-1 << item2);
			if ((num & num2) == (item & num2))
			{
				return true;
			}
		}
		return false;
	}
}
