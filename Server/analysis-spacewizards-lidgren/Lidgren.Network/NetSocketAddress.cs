using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

[StructLayout(LayoutKind.Explicit)]
internal struct NetSocketAddress : IEquatable<NetSocketAddress>
{
	[FieldOffset(0)]
	public NetSocketAddressV4 V4;

	[FieldOffset(0)]
	public NetSocketAddressV6 V6;

	[FieldOffset(26)]
	public readonly NetIpAddressFamily Family;

	public readonly bool IsIpv4 => Family == NetIpAddressFamily.V4;

	public ushort Port
	{
		readonly get
		{
			if (!IsIpv4)
			{
				return V6.Port;
			}
			return V4.Port;
		}
		set
		{
			if (IsIpv4)
			{
				V4.Port = value;
			}
			else
			{
				V6.Port = value;
			}
		}
	}

	public readonly NetIpAddress IpAddress
	{
		get
		{
			if (Family != NetIpAddressFamily.V4)
			{
				return new NetIpAddress(in V6.Address);
			}
			return new NetIpAddress(in V4.Address);
		}
	}

	public NetSocketAddress(NetIpAddress address, ushort port)
	{
		this = default(NetSocketAddress);
		Family = address.Family;
		if (address.Family == NetIpAddressFamily.V4)
		{
			V4.Port = port;
			V4.Address = address.V4;
		}
		else
		{
			V6.Port = port;
			V6.Address = address.V6;
		}
	}

	public NetSocketAddress(in NetSocketAddressV4 v4)
	{
		V6 = default(NetSocketAddressV6);
		V4 = v4;
		Family = NetIpAddressFamily.V4;
	}

	public NetSocketAddress(in NetSocketAddressV6 v6)
	{
		V4 = default(NetSocketAddressV4);
		V6 = v6;
		Family = NetIpAddressFamily.V6;
	}

	public static explicit operator NetSocketAddress(IPEndPoint endPoint)
	{
		int bytesWritten;
		if (endPoint.AddressFamily == AddressFamily.InterNetwork)
		{
			NetIpv4Address v = default(NetIpv4Address);
			Span<byte> destination = MemoryMarshal.CreateSpan(ref Unsafe.As<NetIpv4Address, byte>(ref v), 4);
			endPoint.Address.TryWriteBytes(destination, out bytesWritten);
			return new NetSocketAddress(v, (ushort)endPoint.Port);
		}
		NetIpv6Address source = default(NetIpv6Address);
		Span<byte> destination2 = MemoryMarshal.CreateSpan(ref Unsafe.As<NetIpv6Address, byte>(ref source), 16);
		endPoint.Address.TryWriteBytes(destination2, out bytesWritten);
		return new NetSocketAddressV6(source, (ushort)endPoint.Port, (uint)endPoint.Address.ScopeId);
	}

	public static explicit operator IPEndPoint(in NetSocketAddress address)
	{
		if (address.IsIpv4)
		{
			return (IPEndPoint)address.V4;
		}
		return (IPEndPoint)address.V6;
	}

	public static implicit operator NetSocketAddress(in NetSocketAddressV4 v4)
	{
		return new NetSocketAddress(in v4);
	}

	public static implicit operator NetSocketAddress(in NetSocketAddressV6 v6)
	{
		return new NetSocketAddress(in v6);
	}

	public readonly bool Equals(NetSocketAddress other)
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

	public override readonly int GetHashCode()
	{
		if (IsIpv4)
		{
			return HashCode.Combine(Family, V4.GetHashCode());
		}
		return HashCode.Combine(Family, V6.GetHashCode());
	}
}
