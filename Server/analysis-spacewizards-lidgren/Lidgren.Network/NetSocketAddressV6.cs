using System;
using System.Net;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal struct NetSocketAddressV6 : IEquatable<NetSocketAddressV6>
{
	public ushort Port;

	public NetIpv6Address Address;

	public uint ScopeId;

	public NetSocketAddressV6(NetIpv6Address address, ushort port, uint scopeId)
	{
		Address = address;
		Port = port;
		ScopeId = scopeId;
	}

	public bool Equals(NetSocketAddressV6 other)
	{
		if (Port == other.Port && Address.Equals(other.Address))
		{
			return ScopeId == other.ScopeId;
		}
		return false;
	}

	public override bool Equals(object? obj)
	{
		if (obj is NetSocketAddressV6 other)
		{
			return Equals(other);
		}
		return false;
	}

	public override int GetHashCode()
	{
		return HashCode.Combine(Port, Address, ScopeId);
	}

	public static bool operator ==(NetSocketAddressV6 left, NetSocketAddressV6 right)
	{
		return left.Equals(right);
	}

	public static bool operator !=(NetSocketAddressV6 left, NetSocketAddressV6 right)
	{
		return !left.Equals(right);
	}

	public unsafe static explicit operator IPEndPoint(in NetSocketAddressV6 address)
	{
		return new IPEndPoint(new IPAddress(MemoryMarshal.CreateReadOnlySpan(in Unsafe.AsRef(in address).Address.Address[0], 16), address.ScopeId), address.Port);
	}
}
