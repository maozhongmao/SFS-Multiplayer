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
		string hexString = text + text2.PadLeft(text.Length, '0');
		byte[] bytes = NetUtility.ToByteArray(hexString);
		byte[] data = NetUtility.ComputeSHAHash(bytes);
		return new NetBigInteger(NetUtility.ToHexString(data), 16);
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

	public static byte[] ComputePrivateKey(string username, string password, byte[] salt)
	{
		byte[] bytes = Encoding.UTF8.GetBytes(username + ":" + password);
		byte[] array = NetUtility.ComputeSHAHash(bytes);
		byte[] array2 = new byte[array.Length + salt.Length];
		Buffer.BlockCopy(salt, 0, array2, 0, salt.Length);
		Buffer.BlockCopy(array, 0, array2, salt.Length, array.Length);
		return new NetBigInteger(NetUtility.ToHexString(NetUtility.ComputeSHAHash(array2)), 16).ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerVerifier(byte[] privateKey)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(privateKey), 16);
		NetBigInteger netBigInteger = g.ModPow(exponent, N);
		return netBigInteger.ToByteArrayUnsigned();
	}

	public static byte[] ComputeClientEphemeral(byte[] clientPrivateEphemeral)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(clientPrivateEphemeral), 16);
		NetBigInteger netBigInteger = g.ModPow(exponent, N);
		return netBigInteger.ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerEphemeral(byte[] serverPrivateEphemeral, byte[] verifier)
	{
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(serverPrivateEphemeral), 16);
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(verifier), 16);
		NetBigInteger value = g.ModPow(exponent, N);
		NetBigInteger netBigInteger2 = netBigInteger.Multiply(k);
		NetBigInteger netBigInteger3 = netBigInteger2.Add(value).Mod(N);
		return netBigInteger3.ToByteArrayUnsigned();
	}

	public static byte[] ComputeU(byte[] clientPublicEphemeral, byte[] serverPublicEphemeral)
	{
		string text = NetUtility.ToHexString(clientPublicEphemeral);
		string text2 = NetUtility.ToHexString(serverPublicEphemeral);
		int totalWidth = 66;
		string hexString = text.PadLeft(totalWidth, '0') + text2.PadLeft(totalWidth, '0');
		byte[] bytes = NetUtility.ToByteArray(hexString);
		byte[] data = NetUtility.ComputeSHAHash(bytes);
		return new NetBigInteger(NetUtility.ToHexString(data), 16).ToByteArrayUnsigned();
	}

	public static byte[] ComputeServerSessionValue(byte[] clientPublicEphemeral, byte[] verifier, byte[] udata, byte[] serverPrivateEphemeral)
	{
		NetBigInteger val = new NetBigInteger(NetUtility.ToHexString(clientPublicEphemeral), 16);
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(verifier), 16);
		NetBigInteger exponent = new NetBigInteger(NetUtility.ToHexString(udata), 16);
		NetBigInteger exponent2 = new NetBigInteger(NetUtility.ToHexString(serverPrivateEphemeral), 16);
		NetBigInteger netBigInteger2 = netBigInteger.ModPow(exponent, N).Multiply(val).Mod(N)
			.ModPow(exponent2, N)
			.Mod(N);
		return netBigInteger2.ToByteArrayUnsigned();
	}

	public static byte[] ComputeClientSessionValue(byte[] serverPublicEphemeral, byte[] xdata, byte[] udata, byte[] clientPrivateEphemeral)
	{
		NetBigInteger netBigInteger = new NetBigInteger(NetUtility.ToHexString(serverPublicEphemeral), 16);
		NetBigInteger netBigInteger2 = new NetBigInteger(NetUtility.ToHexString(xdata), 16);
		NetBigInteger val = new NetBigInteger(NetUtility.ToHexString(udata), 16);
		NetBigInteger value = new NetBigInteger(NetUtility.ToHexString(clientPrivateEphemeral), 16);
		NetBigInteger netBigInteger3 = g.ModPow(netBigInteger2, N);
		NetBigInteger netBigInteger4 = netBigInteger.Add(N.Multiply(k)).Subtract(netBigInteger3.Multiply(k)).Mod(N);
		return netBigInteger4.ModPow(netBigInteger2.Multiply(val).Add(value), N).ToByteArrayUnsigned();
	}

	public static NetXtea CreateEncryption(NetPeer peer, byte[] sessionValue)
	{
		byte[] array = NetUtility.ComputeSHAHash(sessionValue);
		byte[] array2 = new byte[16];
		for (int i = 0; i < 16; i++)
		{
			array2[i] = array[i];
			for (int j = 1; j < array.Length / 16; j++)
			{
				array2[i] ^= array[i + j * 16];
			}
		}
		return new NetXtea(peer, array2);
	}
}
