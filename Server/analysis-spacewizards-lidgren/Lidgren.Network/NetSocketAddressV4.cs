using System;
using System.Net;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal struct NetSocketAddressV4 : IEquatable<NetSocketAddressV4>
{
	public ushort Port;

	public NetIpv4Address Address;

	public NetSocketAddressV4(NetIpv4Address address, ushort port)
	{
		Address = address;
		Port = port;
	}

	public bool Equals(NetSocketAddressV4 other)
	{
		if (Port == other.Port)
		{
			return Address.Equals(other.Address);
		}
		return false;
	}

	public override bool Equals(object? obj)
	{
		if (obj is NetSocketAddressV4 other)
		{
			return Equals(other);
		}
		return false;
	}

	public override int GetHashCode()
	{
		return HashCode.Combine(Port, Address);
	}

	public static bool operator ==(NetSocketAddressV4 left, NetSocketAddressV4 right)
	{
		return left.Equals(right);
	}

	public static bool operator !=(NetSocketAddressV4 left, NetSocketAddressV4 right)
	{
		return !left.Equals(right);
	}

	public unsafe static explicit operator IPEndPoint(in NetSocketAddressV4 address)
	{
		return new IPEndPoint(new IPAddress(MemoryMarshal.CreateReadOnlySpan(in Unsafe.AsRef(in address).Address.Address[0], 4)), address.Port);
	}
}
