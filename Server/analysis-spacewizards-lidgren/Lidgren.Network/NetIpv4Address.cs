using System;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal struct NetIpv4Address : IEquatable<NetIpv4Address>
{
	public unsafe fixed byte Address[4];

	public unsafe NetIpv6Address ToIpv6Mapped()
	{
		NetIpv6Address result = default(NetIpv6Address);
		result.Address[10] = byte.MaxValue;
		result.Address[11] = byte.MaxValue;
		result.Address[12] = Address[0];
		result.Address[13] = Address[1];
		result.Address[14] = Address[2];
		result.Address[15] = Address[3];
		return result;
	}

	public unsafe bool Equals(NetIpv4Address other)
	{
		ReadOnlySpan<byte> span = MemoryMarshal.CreateReadOnlySpan(in Address[0], 4);
		ReadOnlySpan<byte> other2 = MemoryMarshal.CreateReadOnlySpan(in other.Address[0], 4);
		return span.SequenceEqual(other2);
	}
}
