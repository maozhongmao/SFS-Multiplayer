using System;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

[StructLayout(LayoutKind.Explicit)]
internal struct NetIpAddress : IEquatable<NetIpAddress>
{
	[FieldOffset(0)]
	public NetIpv4Address V4;

	[FieldOffset(0)]
	public NetIpv6Address V6;

	[FieldOffset(16)]
	public readonly NetIpAddressFamily Family;

	public NetIpAddress(in NetIpv4Address v4)
	{
		V6 = default(NetIpv6Address);
		V4 = v4;
		Family = NetIpAddressFamily.V4;
	}

	public NetIpAddress(in NetIpv6Address v6)
	{
		V4 = default(NetIpv4Address);
		V6 = v6;
		Family = NetIpAddressFamily.V6;
	}

	public readonly bool Equals(NetIpAddress other)
	{
		if (Family == NetIpAddressFamily.V4)
		{
			if (other.Family != NetIpAddressFamily.V4)
			{
				return false;
			}
			return V4.Equals(other.V4);
		}
		if (other.Family != NetIpAddressFamily.V6)
		{
			return false;
		}
		return V6.Equals(other.V6);
	}

	public static implicit operator NetIpAddress(in NetIpv4Address v4)
	{
		return new NetIpAddress(in v4);
	}

	public static implicit operator NetIpAddress(in NetIpv6Address v6)
	{
		return new NetIpAddress(in v6);
	}
}
