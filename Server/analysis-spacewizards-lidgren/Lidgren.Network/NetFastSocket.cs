using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace Lidgren.Network;

internal static class NetFastSocket
{
	public static int SendTo(Socket socket, byte[] buffer, int offset, int size, SocketFlags socketFlags, IPEndPoint endPoint)
	{
		if (!NetNativeSocket.IsLinux && !NetNativeSocket.IsWindows)
		{
			return socket.SendTo(buffer, offset, size, socketFlags, endPoint);
		}
		NetSocketAddress socketAddress = (NetSocketAddress)endPoint;
		return SendTo(socket, buffer.AsSpan(offset, size), socketFlags, in socketAddress);
	}

	public unsafe static int ReceiveFrom(Socket socket, byte[] buffer, int offset, int size, SocketFlags socketFlags, out NetSocketAddress socketAddress, ref EndPoint endPoint)
	{
		if (!NetNativeSocket.IsLinux && !NetNativeSocket.IsWindows)
		{
			int result = socket.ReceiveFrom(buffer, offset, size, socketFlags, ref endPoint);
			IPEndPoint iPEndPoint = (IPEndPoint)endPoint;
			int bytesWritten;
			if (iPEndPoint.AddressFamily == AddressFamily.InterNetworkV6)
			{
				NetIpv6Address address = default(NetIpv6Address);
				iPEndPoint.Address.TryWriteBytes(MemoryMarshal.CreateSpan(ref address.Address[0], 16), out bytesWritten);
				socketAddress = new NetSocketAddressV6(address, (ushort)iPEndPoint.Port, (uint)iPEndPoint.Address.ScopeId);
				return result;
			}
			NetIpv4Address address2 = default(NetIpv4Address);
			iPEndPoint.Address.TryWriteBytes(MemoryMarshal.CreateSpan(ref address2.Address[0], 4), out bytesWritten);
			socketAddress = new NetSocketAddressV4(address2, (ushort)iPEndPoint.Port);
			return result;
		}
		return ReceiveFrom(socket, buffer.AsSpan(offset, size), socketFlags, out socketAddress);
	}

	public unsafe static int SendTo(Socket socket, ReadOnlySpan<byte> buffer, SocketFlags socketFlags, in NetSocketAddress socketAddress)
	{
		if (socketFlags != SocketFlags.None)
		{
			throw new ArgumentException("Non-none socket flags not currently supported.");
		}
		if (!NetNativeSocket.IsWindows && !NetNativeSocket.IsLinux)
		{
			throw new NotSupportedException();
		}
		NetNativeSocket.sockaddr* ptr;
		int num;
		if (socketAddress.Family == NetIpAddressFamily.V6)
		{
			NetNativeSocket.sockaddr_in6 sockaddr_in = default(NetNativeSocket.sockaddr_in6);
			ref NetSocketAddressV6 reference = ref Unsafe.AsRef(in socketAddress.V6);
			sockaddr_in.sin6_port = NetNativeSocket.htons(reference.Port);
			sockaddr_in.sin6_family = NetNativeSocket.AF_INET6;
			sockaddr_in.sin6_scope_id = reference.ScopeId;
			sockaddr_in.sin6_addr = Unsafe.As<NetIpv6Address, NetNativeSocket.in6_addr>(ref reference.Address);
			ptr = (NetNativeSocket.sockaddr*)(&sockaddr_in);
			num = sizeof(NetNativeSocket.sockaddr_in6);
		}
		else
		{
			ref NetSocketAddressV4 reference2 = ref Unsafe.AsRef(in socketAddress.V4);
			NetNativeSocket.sockaddr_in sockaddr_in2 = new NetNativeSocket.sockaddr_in
			{
				sin_port = NetNativeSocket.htons(reference2.Port),
				sin_family = NetNativeSocket.AF_INET,
				sin_addr = Unsafe.As<NetIpv4Address, NetNativeSocket.in_addr>(ref reference2.Address)
			};
			ptr = (NetNativeSocket.sockaddr*)(&sockaddr_in2);
			num = sizeof(NetNativeSocket.sockaddr_in);
		}
		int num2;
		fixed (byte* buf = buffer)
		{
			num2 = (int)((!NetNativeSocket.IsWindows) ? NetNativeSocket.sendto_linux((int)socket.Handle, buf, buffer.Length, (int)socketFlags, ptr, (uint)num) : NetNativeSocket.sendto_win32(socket.Handle, buf, buffer.Length, (int)socketFlags, ptr, num));
		}
		if (num2 != -1)
		{
			return num2;
		}
		if (NetNativeSocket.IsWindows)
		{
			throw new SocketException(NetNativeSocket.WSAGetLastError());
		}
		throw new SocketException();
	}

	public unsafe static int ReceiveFrom(Socket socket, Span<byte> buffer, SocketFlags socketFlags, out NetSocketAddress socketAddress)
	{
		if (socketFlags != SocketFlags.None)
		{
			throw new ArgumentException("Non-none socket flags not currently supported.");
		}
		if (!NetNativeSocket.IsWindows && !NetNativeSocket.IsLinux)
		{
			throw new NotSupportedException();
		}
		int num2;
		NetNativeSocket.sockaddr_in6 source = default(NetNativeSocket.sockaddr_in6);
		fixed (byte* buf = buffer)
		{
			if (NetNativeSocket.IsWindows)
			{
				int num = sizeof(NetNativeSocket.sockaddr_in6);
				num2 = NetNativeSocket.recvfrom_win32(socket.Handle, buf, buffer.Length, (int)socketFlags, (NetNativeSocket.sockaddr*)(&source), &num);
			}
			else
			{
				uint num3 = (uint)sizeof(NetNativeSocket.sockaddr_in6);
				num2 = (int)NetNativeSocket.recvfrom_linux((int)socket.Handle, buf, buffer.Length, (int)socketFlags, (NetNativeSocket.sockaddr*)(&source), &num3);
			}
		}
		if (num2 != -1)
		{
			if (source.sin6_family == NetNativeSocket.AF_INET6)
			{
				NetSocketAddressV6 v = new NetSocketAddressV6
				{
					Address = Unsafe.As<NetNativeSocket.in6_addr, NetIpv6Address>(ref source.sin6_addr),
					Port = NetNativeSocket.ntohs(source.sin6_port),
					ScopeId = source.sin6_scope_id
				};
				socketAddress = v;
			}
			else
			{
				if (source.sin6_family != NetNativeSocket.AF_INET)
				{
					throw new SocketException(10046);
				}
				ref NetNativeSocket.sockaddr_in reference = ref Unsafe.As<NetNativeSocket.sockaddr_in6, NetNativeSocket.sockaddr_in>(ref source);
				socketAddress = new NetSocketAddressV4
				{
					Address = Unsafe.As<NetNativeSocket.in_addr, NetIpv4Address>(ref reference.sin_addr),
					Port = NetNativeSocket.ntohs(reference.sin_port)
				};
			}
			return num2;
		}
		if (NetNativeSocket.IsWindows)
		{
			throw new SocketException(NetNativeSocket.WSAGetLastError());
		}
		throw new SocketException();
	}
}
