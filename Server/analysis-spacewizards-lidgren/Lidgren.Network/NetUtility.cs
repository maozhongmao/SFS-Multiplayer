using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Numerics;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Lidgren.Network;

public static class NetUtility
{
	public delegate void ResolveEndPointCallback(IPEndPoint? endPoint);

	public delegate void ResolveAddressCallback(IPAddress? adr);

	private static readonly bool IsMono = Type.GetType("Mono.Runtime") != null;

	private static IPAddress? s_broadcastAddress;

	private static readonly long s_timeInitialized = Stopwatch.GetTimestamp();

	private static readonly double s_dInvFreq = 1.0 / (double)Stopwatch.Frequency;

	public static double Now => NowCore;

	private static double NowCore => (double)(Stopwatch.GetTimestamp() - s_timeInitialized) * s_dInvFreq;

	public static IPAddress? GetCachedBroadcastAddress()
	{
		if (s_broadcastAddress == null)
		{
			s_broadcastAddress = GetBroadcastAddress();
		}
		return s_broadcastAddress;
	}

	public static string ToHexString(long data)
	{
		return ToHexString(BitConverter.GetBytes(data));
	}

	public static string ToHexString(byte[] data)
	{
		return ToHexString(data.AsSpan());
	}

	public static string ToHexString(byte[] data, int offset, int length)
	{
		return ToHexString(data.AsSpan(offset, length));
	}

	public static string ToHexString(ReadOnlySpan<byte> data)
	{
		char[] array = new char[data.Length * 2];
		for (int i = 0; i < data.Length; i++)
		{
			byte b = (byte)(data[i] >> 4);
			array[i * 2] = (char)((b > 9) ? (b + 55) : (b + 48));
			b = (byte)(data[i] & 0xF);
			array[i * 2 + 1] = (char)((b > 9) ? (b + 55) : (b + 48));
		}
		return new string(array);
	}

	public static bool IsLocal(IPEndPoint endPoint)
	{
		if (endPoint == null)
		{
			return false;
		}
		return IsLocal(endPoint.Address);
	}

	public static bool IsLocal(IPAddress remote)
	{
		IPAddress mask;
		IPAddress myAddress = GetMyAddress(out mask);
		if (myAddress == null)
		{
			throw new InvalidOperationException("Unable to determine local address");
		}
		uint num = BitConverter.ToUInt32(mask.GetAddressBytes(), 0);
		uint num2 = BitConverter.ToUInt32(remote.GetAddressBytes(), 0);
		uint num3 = BitConverter.ToUInt32(myAddress.GetAddressBytes(), 0);
		return (num2 & num) == (num3 & num);
	}

	[CLSCompliant(false)]
	public static int BitsToHoldUInt(uint value)
	{
		return Math.Max(32 - BitOperations.LeadingZeroCount(value), 1);
	}

	[CLSCompliant(false)]
	public static int BitsToHoldUInt64(ulong value)
	{
		return Math.Max(64 - BitOperations.LeadingZeroCount(value), 1);
	}

	public static int BytesToHoldBits(int numBits)
	{
		return (numBits + 7) / 8;
	}

	internal static bool CompareElements(byte[] one, byte[] two)
	{
		if (one.Length != two.Length)
		{
			return false;
		}
		for (int i = 0; i < one.Length; i++)
		{
			if (one[i] != two[i])
			{
				return false;
			}
		}
		return true;
	}

	public static byte[] ToByteArray(string hexString)
	{
		byte[] array = new byte[hexString.Length / 2];
		for (int i = 0; i < hexString.Length; i += 2)
		{
			array[i / 2] = Convert.ToByte(hexString.Substring(i, 2), 16);
		}
		return array;
	}

	public static string ToHumanReadable(long bytes)
	{
		if (bytes < 4000)
		{
			return bytes + " bytes";
		}
		if (bytes < 1000000)
		{
			return Math.Round((double)bytes / 1000.0, 2) + " kilobytes";
		}
		return Math.Round((double)bytes / 1000000.0, 2) + " megabytes";
	}

	internal static int RelativeSequenceNumber(int nr, int expected)
	{
		return (nr - expected + 1024 + 512) % 1024 - 512;
	}

	public static int GetWindowSize(NetDeliveryMethod method)
	{
		switch (method)
		{
		case NetDeliveryMethod.Unreliable:
		case NetDeliveryMethod.UnreliableSequenced:
			if (method - 1 > NetDeliveryMethod.Unreliable)
			{
				break;
			}
			return 128;
		case NetDeliveryMethod.Unknown:
			return 0;
		case NetDeliveryMethod.ReliableOrdered:
			return 64;
		}
		return 64;
	}

	internal static void SortMembersList(MemberInfo[] list)
	{
		int num = 1;
		while (num * 3 + 1 <= list.Length)
		{
			num = 3 * num + 1;
		}
		while (num > 0)
		{
			for (int i = num - 1; i < list.Length; i++)
			{
				MemberInfo memberInfo = list[i];
				int num2 = i;
				while (num2 >= num && string.Compare(list[num2 - num].Name, memberInfo.Name, StringComparison.InvariantCulture) > 0)
				{
					list[num2] = list[num2 - num];
					num2 -= num;
				}
				list[num2] = memberInfo;
			}
			num /= 3;
		}
	}

	internal static NetDeliveryMethod GetDeliveryMethod(NetMessageType mtp)
	{
		if ((int)mtp >= 34)
		{
			if ((int)mtp < 67)
			{
				if ((int)mtp >= 35)
				{
					return NetDeliveryMethod.ReliableSequenced;
				}
				return NetDeliveryMethod.ReliableUnordered;
			}
			return NetDeliveryMethod.ReliableOrdered;
		}
		if ((int)mtp >= 2)
		{
			return NetDeliveryMethod.UnreliableSequenced;
		}
		return NetDeliveryMethod.Unreliable;
	}

	public static string MakeCommaDelimitedList<T>(IReadOnlyList<T> list) where T : notnull
	{
		int count = list.Count;
		StringBuilder stringBuilder = new StringBuilder(count * 5);
		for (int i = 0; i < count; i++)
		{
			stringBuilder.Append(list[i].ToString());
			if (i != count - 1)
			{
				stringBuilder.Append(", ");
			}
		}
		return stringBuilder.ToString();
	}

	public static byte[] ComputeSHAHash(byte[] bytes)
	{
		return ComputeSHAHash(bytes, 0, bytes.Length);
	}

	internal static void CopyEndpoint(IPEndPoint src, IPEndPoint dst)
	{
		dst.Port = src.Port;
		if (src.AddressFamily == AddressFamily.InterNetwork)
		{
			dst.Address = src.Address.MapToIPv6();
		}
		else
		{
			dst.Address = src.Address;
		}
	}

	internal static IPEndPoint MapToIPv6(IPEndPoint endPoint)
	{
		if (endPoint.AddressFamily == AddressFamily.InterNetwork)
		{
			return new IPEndPoint(endPoint.Address.MapToIPv6(), endPoint.Port);
		}
		return endPoint;
	}

	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	internal static T ReadUnaligned<T>(ReadOnlySpan<byte> source) where T : unmanaged
	{
		if (Unsafe.SizeOf<T>() > source.Length)
		{
			throw new ArgumentOutOfRangeException();
		}
		return Unsafe.ReadUnaligned<T>(in MemoryMarshal.GetReference(source));
	}

	[MethodImpl(MethodImplOptions.AggressiveInlining)]
	internal static void WriteUnaligned<T>(Span<byte> destination, ref T value) where T : unmanaged
	{
		if ((uint)Unsafe.SizeOf<T>() > (uint)destination.Length)
		{
			throw new ArgumentOutOfRangeException();
		}
		Unsafe.WriteUnaligned(ref MemoryMarshal.GetReference(destination), value);
	}

	[Obsolete("This function does not handle network errors properly, prefer task-based ResolveAsync instead.")]
	public static void ResolveAsync(string ipOrHost, int port, ResolveEndPointCallback callback)
	{
		ResolveAsync(ipOrHost, port, null, callback);
	}

	[Obsolete("This function does not handle network errors properly, prefer task-based ResolveAsync instead.")]
	public static void ResolveAsync(string ipOrHost, int port, AddressFamily? allowedFamily, ResolveEndPointCallback callback)
	{
		ResolveAsync(ipOrHost, port, allowedFamily).ContinueWith(delegate(Task<IPEndPoint?> t)
		{
			callback(t.Result);
		});
	}

	public static IPEndPoint? Resolve(string ipOrHost, int port)
	{
		return Resolve(ipOrHost, port, null);
	}

	public static IPEndPoint? Resolve(string ipOrHost, int port, AddressFamily? allowedFamily)
	{
		IPAddress iPAddress = Resolve(ipOrHost, allowedFamily);
		if (iPAddress != null)
		{
			return new IPEndPoint(iPAddress, port);
		}
		return null;
	}

	[Obsolete("This function does not handle network errors properly, prefer task-based ResolveAsync instead.")]
	public static void ResolveAsync(string ipOrHost, ResolveAddressCallback callback)
	{
		ResolveAsync(ipOrHost, null, callback);
	}

	[Obsolete("This function does not handle network errors properly, prefer task-based ResolveAsync instead.")]
	public static void ResolveAsync(string ipOrHost, AddressFamily? allowedFamily, ResolveAddressCallback callback)
	{
		ResolveAsync(ipOrHost, allowedFamily).ContinueWith(delegate(Task<IPAddress?> t)
		{
			callback(t.Result);
		});
	}

	public static async Task<IPEndPoint?> ResolveAsync(string ipOrHost, int port)
	{
		return await ResolveAsync(ipOrHost, port, (AddressFamily?)null);
	}

	public static async Task<IPEndPoint?> ResolveAsync(string ipOrHost, int port, AddressFamily? allowedFamily)
	{
		IPAddress iPAddress = await ResolveAsync(ipOrHost, allowedFamily);
		return (iPAddress == null) ? null : new IPEndPoint(iPAddress, port);
	}

	public static async Task<IPAddress?> ResolveAsync(string ipOrHost, AddressFamily? allowedFamily = null)
	{
		if (ResolveHead(ref ipOrHost, allowedFamily, out IPAddress resolve))
		{
			return resolve;
		}
		try
		{
			return ResolveFilter(allowedFamily, await Dns.GetHostAddressesAsync(ipOrHost));
		}
		catch (SocketException ex)
		{
			if (ex.SocketErrorCode == SocketError.HostNotFound)
			{
				return null;
			}
			throw;
		}
	}

	public static IPAddress? Resolve(string ipOrHost)
	{
		return Resolve(ipOrHost, null);
	}

	public static IPAddress? Resolve(string ipOrHost, AddressFamily? allowedFamily)
	{
		if (ResolveHead(ref ipOrHost, allowedFamily, out IPAddress resolve))
		{
			return resolve;
		}
		try
		{
			IPAddress[] hostAddresses = Dns.GetHostAddresses(ipOrHost);
			return ResolveFilter(allowedFamily, hostAddresses);
		}
		catch (SocketException ex) when (ex.SocketErrorCode == SocketError.HostNotFound)
		{
			return null;
		}
	}

	private static IPAddress? ResolveFilter(AddressFamily? allowedFamily, IPAddress[] addresses)
	{
		foreach (IPAddress iPAddress in addresses)
		{
			AddressFamily addressFamily = iPAddress.AddressFamily;
			if ((addressFamily == AddressFamily.InterNetwork || addressFamily == AddressFamily.InterNetworkV6) && (!allowedFamily.HasValue || allowedFamily == addressFamily))
			{
				return iPAddress;
			}
		}
		return null;
	}

	private static bool ResolveHead(ref string ipOrHost, AddressFamily? allowedFamily, out IPAddress? resolve)
	{
		if (string.IsNullOrEmpty(ipOrHost))
		{
			throw new ArgumentException("Supplied string must not be empty", "ipOrHost");
		}
		if (allowedFamily.HasValue && allowedFamily != AddressFamily.InterNetwork && allowedFamily != AddressFamily.InterNetworkV6)
		{
			throw new ArgumentException("Address family must be either InterNetwork, InterNetworkV6 or null", "allowedFamily");
		}
		ipOrHost = ipOrHost.Trim();
		if (IPAddress.TryParse(ipOrHost, out IPAddress address))
		{
			if (allowedFamily.HasValue && address.AddressFamily != allowedFamily)
			{
				resolve = null;
				return true;
			}
			if (address.AddressFamily == AddressFamily.InterNetwork || address.AddressFamily == AddressFamily.InterNetworkV6)
			{
				resolve = address;
				return true;
			}
			throw new ArgumentException("This method will not currently resolve other than IPv4 or IPv6 addresses");
		}
		resolve = null;
		return false;
	}

	public static byte[]? GetMacAddressBytes()
	{
		return GetMacAddressBytesCore();
	}

	public static IPAddress? GetBroadcastAddress()
	{
		return GetBroadcastAddressCore();
	}

	public static IPAddress? GetMyAddress(out IPAddress? mask)
	{
		return GetMyAddressCore(out mask);
	}

	public static void Sleep(int milliseconds)
	{
		SleepCore(milliseconds);
	}

	public static IPAddress CreateAddressFromBytes(byte[] bytes)
	{
		return CreateAddressFromBytesCore(bytes);
	}

	public static byte[] ComputeSHAHash(byte[] bytes, int offset, int count)
	{
		return ComputeSHAHashCore(bytes, offset, count);
	}

	[CLSCompliant(false)]
	[Obsolete("Use NetRandomSeed instead.")]
	public static ulong GetPlatformSeed(int seedInc)
	{
		return GetPlatformSeedCore(seedInc);
	}

	internal static ulong GetPlatformSeedCore(int seedInc)
	{
		return (ulong)(Stopwatch.GetTimestamp() ^ (Environment.WorkingSet + seedInc));
	}

	private static NetworkInterface? GetNetworkInterface()
	{
		IPAddress defaultAddress = ProbeDefaultRouteAddress();
		return (from nic in NetworkInterface.GetAllNetworkInterfaces()
			where nic.NetworkInterfaceType != NetworkInterfaceType.Loopback && nic.NetworkInterfaceType != NetworkInterfaceType.Unknown && nic.Supports(NetworkInterfaceComponent.IPv4)
			select nic).OrderByDescending(delegate(NetworkInterface nic)
		{
			if (nic.OperationalStatus != OperationalStatus.Up)
			{
				return 0;
			}
			if (nic.GetPhysicalAddress().GetAddressBytes().Length == 0)
			{
				return 1;
			}
			foreach (UnicastIPAddressInformation unicastAddress in nic.GetIPProperties().UnicastAddresses)
			{
				if (defaultAddress != null && unicastAddress.Address.Equals(defaultAddress))
				{
					return 4;
				}
				if (unicastAddress != null)
				{
					IPAddress address = unicastAddress.Address;
					if (address != null && address.AddressFamily == AddressFamily.InterNetwork)
					{
						return 3;
					}
				}
			}
			return 2;
		}).FirstOrDefault();
	}

	private static IPAddress? ProbeDefaultRouteAddress()
	{
		try
		{
			using Socket socket = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
			socket.Connect(new IPAddress(new byte[4] { 1, 1, 1, 1 }), 12345);
			return ((IPEndPoint)socket.LocalEndPoint).Address;
		}
		catch
		{
			return null;
		}
	}

	private static byte[]? GetMacAddressBytesCore()
	{
		return GetNetworkInterface()?.GetPhysicalAddress().GetAddressBytes();
	}

	private static IPAddress? GetBroadcastAddressCore()
	{
		NetworkInterface networkInterface = GetNetworkInterface();
		if (networkInterface == null)
		{
			return null;
		}
		foreach (UnicastIPAddressInformation unicastAddress in networkInterface.GetIPProperties().UnicastAddresses)
		{
			if (unicastAddress != null && unicastAddress.Address != null && unicastAddress.Address.AddressFamily == AddressFamily.InterNetwork)
			{
				IPAddress iPv4Mask = unicastAddress.IPv4Mask;
				byte[] addressBytes = unicastAddress.Address.GetAddressBytes();
				byte[] addressBytes2 = iPv4Mask.GetAddressBytes();
				if (addressBytes.Length != addressBytes2.Length)
				{
					throw new ArgumentException("Lengths of IP address and subnet mask do not match.");
				}
				byte[] array = new byte[addressBytes.Length];
				for (int i = 0; i < array.Length; i++)
				{
					array[i] = (byte)(addressBytes[i] | (addressBytes2[i] ^ 0xFF));
				}
				return new IPAddress(array);
			}
		}
		return IPAddress.Broadcast;
	}

	private static IPAddress? GetMyAddressCore(out IPAddress? mask)
	{
		NetworkInterface networkInterface = GetNetworkInterface();
		if (networkInterface == null)
		{
			mask = null;
			return null;
		}
		foreach (UnicastIPAddressInformation unicastAddress in networkInterface.GetIPProperties().UnicastAddresses)
		{
			if (unicastAddress != null && unicastAddress.Address != null && unicastAddress.Address.AddressFamily == AddressFamily.InterNetwork)
			{
				mask = unicastAddress.IPv4Mask;
				return unicastAddress.Address;
			}
		}
		mask = null;
		return null;
	}

	private static void SleepCore(int milliseconds)
	{
		Thread.Sleep(milliseconds);
	}

	private static IPAddress CreateAddressFromBytesCore(byte[] bytes)
	{
		return new IPAddress(bytes);
	}

	private static byte[] ComputeSHAHashCore(byte[] bytes, int offset, int count)
	{
		using SHA256 sHA = SHA256.Create();
		return sHA.ComputeHash(bytes, offset, count);
	}
}
