using System;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal struct NetIpv6Address : IEquatable<NetIpv6Address>
{
	public unsafe fixed byte Address[16];

	public unsafe bool Equals(NetIpv6Address other)
	{
		ReadOnlySpan<byte> span = MemoryMarshal.CreateReadOnlySpan(in Address[0], 16);
		ReadOnlySpan<byte> other2 = MemoryMarshal.CreateReadOnlySpan(in other.Address[0], 16);
		return span.SequenceEqual(other2);
	}
}
