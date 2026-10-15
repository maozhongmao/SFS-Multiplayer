using System;
using System.Buffers.Binary;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal static class NetNativeSocket
{
	internal struct sockaddr
	{
		public ushort sa_family;

		public unsafe fixed byte sa_data[14];
	}

	internal struct sockaddr_in
	{
		public ushort sin_family;

		public ushort sin_port;

		public in_addr sin_addr;

		public unsafe fixed byte sin_zero[8];
	}

	internal struct in_addr
	{
		public uint s_addr;
	}

	internal struct sockaddr_in6
	{
		public ushort sin6_family;

		public ushort sin6_port;

		public uint sin6_flowinfo;

		public in6_addr sin6_addr;

		public uint sin6_scope_id;
	}

	internal struct in6_addr
	{
		public unsafe fixed byte s6_addr[16];
	}

	internal static readonly bool IsWindows = RuntimeInformation.IsOSPlatform(OSPlatform.Windows);

	internal static readonly bool IsLinux = RuntimeInformation.IsOSPlatform(OSPlatform.Linux);

	internal static readonly ushort AF_INET = 2;

	internal static readonly ushort AF_INET6 = (ushort)(IsWindows ? 23u : 10u);

	public static ushort htons(ushort hostshort)
	{
		if (!BitConverter.IsLittleEndian)
		{
			return hostshort;
		}
		return BinaryPrimitives.ReverseEndianness(hostshort);
	}

	public static uint htonl(uint hostlong)
	{
		if (!BitConverter.IsLittleEndian)
		{
			return hostlong;
		}
		return BinaryPrimitives.ReverseEndianness(hostlong);
	}

	public static ushort ntohs(ushort netshort)
	{
		if (!BitConverter.IsLittleEndian)
		{
			return netshort;
		}
		return BinaryPrimitives.ReverseEndianness(netshort);
	}

	public static uint ntohl(uint netlong)
	{
		if (!BitConverter.IsLittleEndian)
		{
			return netlong;
		}
		return BinaryPrimitives.ReverseEndianness(netlong);
	}

	[DllImport("libc", EntryPoint = "sendto", SetLastError = true)]
	internal unsafe static extern nint sendto_linux(int sockfd, void* buf, nint len, int flags, sockaddr* dest_addr, uint addrlen);

	[DllImport("Ws2_32.dll", EntryPoint = "sendto")]
	internal unsafe static extern int sendto_win32(nint s, byte* buf, int len, int flags, sockaddr* to, int tolen);

	[DllImport("libc", EntryPoint = "recvfrom", SetLastError = true)]
	internal unsafe static extern nint recvfrom_linux(int sockfd, void* buf, nint len, int flags, sockaddr* src_addr, uint* addrlen);

	[DllImport("Ws2_32.dll", EntryPoint = "recvfrom")]
	internal unsafe static extern int recvfrom_win32(nint s, byte* buf, int len, int flags, sockaddr* from, int* fromlen);

	[DllImport("Ws2_32.dll")]
	internal static extern int WSAGetLastError();
}
