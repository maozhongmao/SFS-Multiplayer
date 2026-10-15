using System;
using System.Text;

namespace Lidgren.Network;

public static class NetSRP
{
	private static readonly NetBigInteger N = new NetBigInteger("0115b8b692e0e045692cf280b436735c77a5a9e8a9e7ed56c965f87db5b2a2ece3", 16);

	private static readonly NetBigInteger g = NetBigInteger.Two;

	private static readonly NetBigInteger k = ComputeMultiplier();

	private static NetBigInteger ComputeMultiplier()
	{
		string text = NetUtility.ToHexString(N.ToByteArrayUnsigned());
		string text2 = NetUtility.ToHexString(g.ToByteArrayUnsigned());
		return new NetBigInteger(NetUtility.ToHexString(NetUtility.ComputeSHAHash(NetUtility.ToByteArray(text + text2.PadLeft(text.Length, '0')))), 16);
	}

	public static byte[] CreateRandomSalt()
	{
		byte[] array = new byte[16];
		CryptoRandom.Instance.NextBytes(array);
		return array;
	}

	public static byte[] CreateRandomEphemeral()
	{
		byte[] array = new byte[32];
		CryptoRandom.Instance.NextBytes(array);
		return array;
	}

	public static byte[] ComputePrivateKey(string username, string password, ReadOnlySpan<byte> salt)
	{
		byte[] array = NetUtility.ComputeSHAHash(Encoding.UTF8.GetBytes(username + ":" + password));
		byte[] array2 = new byte[array.Length + salt.Length];
		salt.CopyTo(array2);
		Buffer.BlockCopy(array, 0, array2, salt.Length, array.Length);
		return new NetBigInteger(NetUtility.ToHexString(NetUtility.ComputeSHAHash(array2)), 16).ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerVerifier(ReadOnlySpan<byte> privateKey)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(privateKey), 16);
		return g.ModPow(exponent, N).ToByteArrayUnsigned();
	}

	public static byte[] ComputeClientEphemeral(ReadOnlySpan<byte> clientPrivateEphemeral)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(clientPrivateEphemeral), 16);
		return g.ModPow(exponent, N).ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerEphemeral(ReadOnlySpan<byte> serverPrivateEphemeral, ReadOnlySpan<byte> verifier)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(serverPrivateEphemeral), 16);
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(verifier), 16);
		NetBigInteger value = g.ModPow(exponent, N);
		return netBigInteger.Multiply(k).Add(value).Mod(N)
			.ToByteArrayUnsigned();
	}

	public static byte[] ComputeU(ReadOnlySpan<byte> clientPublicEphemeral, ReadOnlySpan<byte> serverPublicEphemeral)
	{
		string text = NetUtility.ToHexString(clientPublicEphemeral);
		string text2 = NetUtility.ToHexString(serverPublicEphemeral);
		int totalWidth = 66;
		return new NetBigInteger(NetUtility.ToHexString(NetUtility.ComputeSHAHash(NetUtility.ToByteArray(text.PadLeft(totalWidth, '0') + text2.PadLeft(totalWidth, '0')))), 16).ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerSessionValue(ReadOnlySpan<byte> clientPublicEphemeral, ReadOnlySpan<byte> verifier, ReadOnlySpan<byte> udata, ReadOnlySpan<byte> serverPrivateEphemeral)
	{
		NetBigInteger val = new NetBigInteger(NetUtility.ToHexString(clientPublicEphemeral), 16);
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(verifier), 16);
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(udata), 16);
		NetBigInteger exponent2 = new NetBigInteger(NetUtility.ToHexString(serverPrivateEphemeral), 16);
		return netBigInteger.ModPow(exponent, N).Multiply(val).Mod(N)
			.ModPow(exponent2, N)
			.Mod(N)
			.ToByteArrayUnsigned();
	}

	public static byte[] ComputeClientSessionValue(ReadOnlySpan<byte> serverPublicEphemeral, ReadOnlySpan<byte> xdata, ReadOnlySpan<byte> udata, ReadOnlySpan<byte> clientPrivateEphemeral)
	{
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(serverPublicEphemeral), 16);
		NetBigInteger netBigInteger2 = new NetBigInteger(NetUtility.ToHexString(xdata), 16);
		NetBigInteger val = new NetBigInteger(NetUtility.ToHexString(udata), 16);
		NetBigInteger value = new NetBigInteger(NetUtility.ToHexString(clientPrivateEphemeral), 16);
		NetBigInteger netBigInteger3 = g.ModPow(netBigInteger2, N);
		return netBigInteger.Add(N.Multiply(k)).Subtract(netBigInteger3.Multiply(k)).Mod(N)
			.ModPow(netBigInteger2.Multiply(val).Add(value), N)
			.ToByteArrayUnsigned();
	}
}
