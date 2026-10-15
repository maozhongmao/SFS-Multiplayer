using System;
using System.Collections.Generic;
using System.Globalization;
using System.Text;

namespace Lidgren.Network;

internal class NetBigInteger
{
	private const long IMASK = 4294967295L;

	private const ulong UIMASK = 4294967295uL;

	private static readonly int[] ZeroMagnitude = Array.Empty<int>();

	private static readonly byte[] ZeroEncoding = Array.Empty<byte>();

	public static readonly NetBigInteger Zero = new NetBigInteger(0, ZeroMagnitude, checkMag: false);

	public static readonly NetBigInteger One = createUValueOf(1uL);

	public static readonly NetBigInteger Two = createUValueOf(2uL);

	public static readonly NetBigInteger Three = createUValueOf(3uL);

	public static readonly NetBigInteger Ten = createUValueOf(10uL);

	private const int chunk2 = 1;

	private static readonly NetBigInteger radix2 = ValueOf(2L);

	private static readonly NetBigInteger radix2E = radix2.Pow(1);

	private const int chunk10 = 19;

	private static readonly NetBigInteger radix10 = ValueOf(10L);

	private static readonly NetBigInteger radix10E = radix10.Pow(19);

	private const int chunk16 = 16;

	private static readonly NetBigInteger radix16 = ValueOf(16L);

	private static readonly NetBigInteger radix16E = radix16.Pow(16);

	private const int BitsPerByte = 8;

	private const int BitsPerInt = 32;

	private const int BytesPerInt = 4;

	private int m_sign;

	private int[] m_magnitude;

	private int m_numBits = -1;

	private int m_numBitLength = -1;

	private long m_quote = -1L;

	public int BitLength
	{
		get
		{
			if (m_numBitLength == -1)
			{
				m_numBitLength = ((m_sign != 0) ? calcBitLength(0, m_magnitude) : 0);
			}
			return m_numBitLength;
		}
	}

	public int IntValue
	{
		get
		{
			if (m_sign != 0)
			{
				if (m_sign <= 0)
				{
					return -m_magnitude[m_magnitude.Length - 1];
				}
				return m_magnitude[m_magnitude.Length - 1];
			}
			return 0;
		}
	}

	public int SignValue => m_sign;

	private static int GetByteLength(int nBits)
	{
		return (nBits + 8 - 1) / 8;
	}

	private NetBigInteger()
	{
		m_magnitude = null;
	}

	private NetBigInteger(int signum, int[] mag, bool checkMag)
	{
		if (checkMag)
		{
			int i;
			for (i = 0; i < mag.Length && mag[i] == 0; i++)
			{
			}
			if (i == mag.Length)
			{
				m_magnitude = ZeroMagnitude;
				return;
			}
			m_sign = signum;
			if (i == 0)
			{
				m_magnitude = mag;
				return;
			}
			m_magnitude = new int[mag.Length - i];
			Array.Copy(mag, i, m_magnitude, 0, m_magnitude.Length);
		}
		else
		{
			m_sign = signum;
			m_magnitude = mag;
		}
	}

	public NetBigInteger(string value)
		: this(value, 10)
	{
	}

	public NetBigInteger(string str, int radix)
	{
		if (str.Length == 0)
		{
			throw new FormatException("Zero length BigInteger");
		}
		NumberStyles style;
		int num;
		NetBigInteger netBigInteger;
		NetBigInteger val;
		switch (radix)
		{
		case 2:
			style = NumberStyles.Integer;
			num = 1;
			netBigInteger = radix2;
			val = radix2E;
			break;
		case 10:
			style = NumberStyles.Integer;
			num = 19;
			netBigInteger = radix10;
			val = radix10E;
			break;
		case 16:
			style = NumberStyles.AllowHexSpecifier;
			num = 16;
			netBigInteger = radix16;
			val = radix16E;
			break;
		default:
			throw new FormatException("Only bases 2, 10, or 16 allowed");
		}
		int i = 0;
		m_sign = 1;
		if (str[0] == '-')
		{
			if (str.Length == 1)
			{
				throw new FormatException("Zero length BigInteger");
			}
			m_sign = -1;
			i = 1;
		}
		for (; i < str.Length && int.Parse(str[i].ToString(), style) == 0; i++)
		{
		}
		if (i >= str.Length)
		{
			m_sign = 0;
			m_magnitude = ZeroMagnitude;
			return;
		}
		NetBigInteger netBigInteger2 = Zero;
		int num2 = i + num;
		if (num2 <= str.Length)
		{
			do
			{
				string text = str.Substring(i, num);
				ulong num3 = ulong.Parse(text, style);
				NetBigInteger value = createUValueOf(num3);
				switch (radix)
				{
				case 2:
					if (num3 > 1)
					{
						throw new FormatException("Bad character in radix 2 string: " + text);
					}
					netBigInteger2 = netBigInteger2.ShiftLeft(1);
					break;
				case 16:
					netBigInteger2 = netBigInteger2.ShiftLeft(64);
					break;
				default:
					netBigInteger2 = netBigInteger2.Multiply(val);
					break;
				}
				netBigInteger2 = netBigInteger2.Add(value);
				i = num2;
				num2 += num;
			}
			while (num2 <= str.Length);
		}
		if (i < str.Length)
		{
			string text2 = str.Substring(i);
			NetBigInteger netBigInteger3 = createUValueOf(ulong.Parse(text2, style));
			if (netBigInteger2.m_sign > 0)
			{
				switch (radix)
				{
				case 16:
					netBigInteger2 = netBigInteger2.ShiftLeft(text2.Length << 2);
					break;
				default:
					netBigInteger2 = netBigInteger2.Multiply(netBigInteger.Pow(text2.Length));
					break;
				case 2:
					break;
				}
				netBigInteger2 = netBigInteger2.Add(netBigInteger3);
			}
			else
			{
				netBigInteger2 = netBigInteger3;
			}
		}
		m_magnitude = netBigInteger2.m_magnitude;
	}

	public NetBigInteger(byte[] bytes)
		: this(bytes.AsSpan())
	{
	}

	public NetBigInteger(byte[] bytes, int offset, int length)
		: this(bytes.AsSpan(offset, length))
	{
	}

	public NetBigInteger(ReadOnlySpan<byte> bytes)
	{
		if (bytes.Length == 0)
		{
			throw new FormatException("Zero length BigInteger");
		}
		if ((sbyte)bytes[0] < 0)
		{
			m_sign = -1;
			int length = bytes.Length;
			int i;
			for (i = 0; i < length && (sbyte)bytes[i] == -1; i++)
			{
			}
			if (i >= length)
			{
				m_magnitude = One.m_magnitude;
				return;
			}
			int num = length - i;
			byte[] array = new byte[num];
			int num2 = 0;
			while (num2 < num)
			{
				array[num2++] = (byte)(~bytes[i++]);
			}
			while (array[--num2] == byte.MaxValue)
			{
				array[num2] = 0;
			}
			array[num2]++;
			m_magnitude = MakeMagnitude(array);
		}
		else
		{
			m_magnitude = MakeMagnitude(bytes);
			m_sign = ((m_magnitude.Length != 0) ? 1 : 0);
		}
	}

	private static int[] MakeMagnitude(ReadOnlySpan<byte> bytes)
	{
		int length = bytes.Length;
		int i;
		for (i = 0; i < length && bytes[i] == 0; i++)
		{
		}
		if (i >= length)
		{
			return ZeroMagnitude;
		}
		int num = (length - i + 3) / 4;
		int num2 = (length - i) % 4;
		if (num2 == 0)
		{
			num2 = 4;
		}
		if (num < 1)
		{
			return ZeroMagnitude;
		}
		int[] array = new int[num];
		int num3 = 0;
		int num4 = 0;
		for (int j = i; j < length; j++)
		{
			num3 <<= 8;
			num3 |= bytes[j] & 0xFF;
			num2--;
			if (num2 <= 0)
			{
				array[num4] = num3;
				num4++;
				num2 = 4;
				num3 = 0;
			}
		}
		if (num4 < array.Length)
		{
			array[num4] = num3;
		}
		return array;
	}

	public NetBigInteger(int sign, byte[] bytes)
		: this(sign, bytes.AsSpan())
	{
	}

	public NetBigInteger(int sign, byte[] bytes, int offset, int length)
		: this(sign, bytes.AsSpan(offset, length))
	{
	}

	public NetBigInteger(int sign, ReadOnlySpan<byte> bytes)
	{
		switch (sign)
		{
		default:
			throw new FormatException("Invalid sign value");
		case 0:
			m_magnitude = ZeroMagnitude;
			break;
		case -1:
		case 1:
			m_magnitude = MakeMagnitude(bytes);
			m_sign = ((m_magnitude.Length >= 1) ? sign : 0);
			break;
		}
	}

	public NetBigInteger Abs()
	{
		if (m_sign < 0)
		{
			return Negate();
		}
		return this;
	}

	private static int[] AddMagnitudes(int[] a, int[] b)
	{
		int num = a.Length - 1;
		int num2 = b.Length - 1;
		long num3 = 0L;
		while (num2 >= 0)
		{
			num3 += (long)(uint)a[num] + (long)(uint)b[num2--];
			a[num--] = (int)num3;
			num3 >>>= 32;
		}
		if (num3 != 0L)
		{
			while (num >= 0 && ++a[num--] == 0)
			{
			}
		}
		return a;
	}

	public NetBigInteger Add(NetBigInteger value)
	{
		if (m_sign == 0)
		{
			return value;
		}
		if (m_sign != value.m_sign)
		{
			if (value.m_sign == 0)
			{
				return this;
			}
			if (value.m_sign < 0)
			{
				return Subtract(value.Negate());
			}
			return value.Subtract(Negate());
		}
		return AddToMagnitude(value.m_magnitude);
	}

	private NetBigInteger AddToMagnitude(int[] magToAdd)
	{
		int[] array;
		int[] array2;
		if (m_magnitude.Length < magToAdd.Length)
		{
			array = magToAdd;
			array2 = m_magnitude;
		}
		else
		{
			array = m_magnitude;
			array2 = magToAdd;
		}
		uint num = uint.MaxValue;
		if (array.Length == array2.Length)
		{
			num -= (uint)array2[0];
		}
		bool flag = (uint)array[0] >= num;
		int[] array3;
		if (flag)
		{
			array3 = new int[array.Length + 1];
			array.CopyTo(array3, 1);
		}
		else
		{
			array3 = (int[])array.Clone();
		}
		array3 = AddMagnitudes(array3, array2);
		return new NetBigInteger(m_sign, array3, flag);
	}

	public NetBigInteger And(NetBigInteger value)
	{
		if (m_sign == 0 || value.m_sign == 0)
		{
			return Zero;
		}
		int[] array = ((m_sign > 0) ? m_magnitude : Add(One).m_magnitude);
		int[] array2 = ((value.m_sign > 0) ? value.m_magnitude : value.Add(One).m_magnitude);
		bool flag = m_sign < 0 && value.m_sign < 0;
		int[] array3 = new int[Math.Max(array.Length, array2.Length)];
		int num = array3.Length - array.Length;
		int num2 = array3.Length - array2.Length;
		for (int i = 0; i < array3.Length; i++)
		{
			int num3 = ((i >= num) ? array[i - num] : 0);
			int num4 = ((i >= num2) ? array2[i - num2] : 0);
			if (m_sign < 0)
			{
				num3 = ~num3;
			}
			if (value.m_sign < 0)
			{
				num4 = ~num4;
			}
			array3[i] = num3 & num4;
			if (flag)
			{
				array3[i] = ~array3[i];
			}
		}
		NetBigInteger netBigInteger = new NetBigInteger(1, array3, checkMag: true);
		if (flag)
		{
			netBigInteger = netBigInteger.Not();
		}
		return netBigInteger;
	}

	private int calcBitLength(int indx, int[] mag)
	{
		while (true)
		{
			if (indx >= mag.Length)
			{
				return 0;
			}
			if (mag[indx] != 0)
			{
				break;
			}
			indx++;
		}
		int num = 32 * (mag.Length - indx - 1);
		int num2 = mag[indx];
		num += BitLen(num2);
		if (m_sign < 0 && (num2 & -num2) == num2)
		{
			do
			{
				if (++indx >= mag.Length)
				{
					num--;
					break;
				}
			}
			while (mag[indx] == 0);
		}
		return num;
	}

	private static int BitLen(int w)
	{
		if (w >= 32768)
		{
			if (w >= 8388608)
			{
				if (w >= 134217728)
				{
					if (w >= 536870912)
					{
						if (w >= 1073741824)
						{
							return 31;
						}
						return 30;
					}
					if (w >= 268435456)
					{
						return 29;
					}
					return 28;
				}
				if (w >= 33554432)
				{
					if (w >= 67108864)
					{
						return 27;
					}
					return 26;
				}
				if (w >= 16777216)
				{
					return 25;
				}
				return 24;
			}
			if (w >= 524288)
			{
				if (w >= 2097152)
				{
					if (w >= 4194304)
					{
						return 23;
					}
					return 22;
				}
				if (w >= 1048576)
				{
					return 21;
				}
				return 20;
			}
			if (w >= 131072)
			{
				if (w >= 262144)
				{
					return 19;
				}
				return 18;
			}
			if (w >= 65536)
			{
				return 17;
			}
			return 16;
		}
		if (w >= 128)
		{
			if (w >= 2048)
			{
				if (w >= 8192)
				{
					if (w >= 16384)
					{
						return 15;
					}
					return 14;
				}
				if (w >= 4096)
				{
					return 13;
				}
				return 12;
			}
			if (w >= 512)
			{
				if (w >= 1024)
				{
					return 11;
				}
				return 10;
			}
			if (w >= 256)
			{
				return 9;
			}
			return 8;
		}
		if (w >= 8)
		{
			if (w >= 32)
			{
				if (w >= 64)
				{
					return 7;
				}
				return 6;
			}
			if (w >= 16)
			{
				return 5;
			}
			return 4;
		}
		if (w >= 2)
		{
			if (w >= 4)
			{
				return 3;
			}
			return 2;
		}
		if (w >= 1)
		{
			return 1;
		}
		if (w >= 0)
		{
			return 0;
		}
		return 32;
	}

	private bool QuickPow2Check()
	{
		if (m_sign > 0)
		{
			return m_numBits == 1;
		}
		return false;
	}

	public int CompareTo(object obj)
	{
		return CompareTo((NetBigInteger)obj);
	}

	private static int CompareTo(int xIndx, int[] x, int yIndx, int[] y)
	{
		while (xIndx != x.Length && x[xIndx] == 0)
		{
			xIndx++;
		}
		while (yIndx != y.Length && y[yIndx] == 0)
		{
			yIndx++;
		}
		return CompareNoLeadingZeroes(xIndx, x, yIndx, y);
	}

	private static int CompareNoLeadingZeroes(int xIndx, int[] x, int yIndx, int[] y)
	{
		int num = x.Length - y.Length - (xIndx - yIndx);
		if (num != 0)
		{
			if (num >= 0)
			{
				return 1;
			}
			return -1;
		}
		while (xIndx < x.Length)
		{
			uint num2 = (uint)x[xIndx++];
			uint num3 = (uint)y[yIndx++];
			if (num2 != num3)
			{
				if (num2 >= num3)
				{
					return 1;
				}
				return -1;
			}
		}
		return 0;
	}

	public int CompareTo(NetBigInteger value)
	{
		if (m_sign >= value.m_sign)
		{
			if (m_sign <= value.m_sign)
			{
				if (m_sign != 0)
				{
					return m_sign * CompareNoLeadingZeroes(0, m_magnitude, 0, value.m_magnitude);
				}
				return 0;
			}
			return 1;
		}
		return -1;
	}

	private int[] Divide(int[] x, int[] y)
	{
		int i;
		for (i = 0; i < x.Length && x[i] == 0; i++)
		{
		}
		int j;
		for (j = 0; j < y.Length && y[j] == 0; j++)
		{
		}
		int num = CompareNoLeadingZeroes(i, x, j, y);
		int[] array3;
		if (num > 0)
		{
			int num2 = calcBitLength(j, y);
			int num3 = calcBitLength(i, x);
			int num4 = num3 - num2;
			int k = 0;
			int l = 0;
			int num5 = num2;
			int[] array;
			int[] array2;
			if (num4 > 0)
			{
				array = new int[(num4 >> 5) + 1];
				array[0] = 1 << num4 % 32;
				array2 = ShiftLeft(y, num4);
				num5 += num4;
			}
			else
			{
				array = new int[1] { 1 };
				int num6 = y.Length - j;
				array2 = new int[num6];
				Array.Copy(y, j, array2, 0, num6);
			}
			array3 = new int[array.Length];
			while (true)
			{
				if (num5 < num3 || CompareNoLeadingZeroes(i, x, l, array2) >= 0)
				{
					Subtract(i, x, l, array2);
					AddMagnitudes(array3, array);
					while (x[i] == 0)
					{
						if (++i == x.Length)
						{
							return array3;
						}
					}
					num3 = 32 * (x.Length - i - 1) + BitLen(x[i]);
					if (num3 <= num2)
					{
						if (num3 < num2)
						{
							return array3;
						}
						num = CompareNoLeadingZeroes(i, x, j, y);
						if (num <= 0)
						{
							break;
						}
					}
				}
				num4 = num5 - num3;
				if (num4 == 1)
				{
					int num7 = array2[l] >>> 1;
					uint num8 = (uint)x[i];
					if ((uint)num7 > num8)
					{
						num4++;
					}
				}
				if (num4 < 2)
				{
					array2 = ShiftRightOneInPlace(l, array2);
					num5--;
					array = ShiftRightOneInPlace(k, array);
				}
				else
				{
					array2 = ShiftRightInPlace(l, array2, num4);
					num5 -= num4;
					array = ShiftRightInPlace(k, array, num4);
				}
				for (; array2[l] == 0; l++)
				{
				}
				for (; array[k] == 0; k++)
				{
				}
			}
		}
		else
		{
			array3 = new int[1];
		}
		if (num == 0)
		{
			AddMagnitudes(array3, One.m_magnitude);
			Array.Clear(x, i, x.Length - i);
		}
		return array3;
	}

	public NetBigInteger Divide(NetBigInteger val)
	{
		if (val.m_sign == 0)
		{
			throw new ArithmeticException("Division by zero error");
		}
		if (m_sign == 0)
		{
			return Zero;
		}
		if (val.QuickPow2Check())
		{
			NetBigInteger netBigInteger = Abs().ShiftRight(val.Abs().BitLength - 1);
			if (val.m_sign != m_sign)
			{
				return netBigInteger.Negate();
			}
			return netBigInteger;
		}
		int[] x = (int[])m_magnitude.Clone();
		return new NetBigInteger(m_sign * val.m_sign, Divide(x, val.m_magnitude), checkMag: true);
	}

	public NetBigInteger[] DivideAndRemainder(NetBigInteger val)
	{
		if (val.m_sign == 0)
		{
			throw new ArithmeticException("Division by zero error");
		}
		NetBigInteger[] array = new NetBigInteger[2];
		if (m_sign == 0)
		{
			array[0] = Zero;
			array[1] = Zero;
		}
		else if (val.QuickPow2Check())
		{
			int n = val.Abs().BitLength - 1;
			NetBigInteger netBigInteger = Abs().ShiftRight(n);
			int[] mag = LastNBits(n);
			array[0] = ((val.m_sign == m_sign) ? netBigInteger : netBigInteger.Negate());
			array[1] = new NetBigInteger(m_sign, mag, checkMag: true);
		}
		else
		{
			int[] array2 = (int[])m_magnitude.Clone();
			int[] mag2 = Divide(array2, val.m_magnitude);
			array[0] = new NetBigInteger(m_sign * val.m_sign, mag2, checkMag: true);
			array[1] = new NetBigInteger(m_sign, array2, checkMag: true);
		}
		return array;
	}

	public override bool Equals(object? obj)
	{
		if (obj == this)
		{
			return true;
		}
		if (!(obj is NetBigInteger netBigInteger))
		{
			return false;
		}
		if (netBigInteger.m_sign != m_sign || netBigInteger.m_magnitude.Length != m_magnitude.Length)
		{
			return false;
		}
		for (int i = 0; i < m_magnitude.Length; i++)
		{
			if (netBigInteger.m_magnitude[i] != m_magnitude[i])
			{
				return false;
			}
		}
		return true;
	}

	public NetBigInteger Gcd(NetBigInteger value)
	{
		if (value.m_sign == 0)
		{
			return Abs();
		}
		if (m_sign == 0)
		{
			return value.Abs();
		}
		NetBigInteger netBigInteger = this;
		NetBigInteger netBigInteger2 = value;
		while (netBigInteger2.m_sign != 0)
		{
			NetBigInteger netBigInteger3 = netBigInteger.Mod(netBigInteger2);
			netBigInteger = netBigInteger2;
			netBigInteger2 = netBigInteger3;
		}
		return netBigInteger;
	}

	public override int GetHashCode()
	{
		int num = m_magnitude.Length;
		if (m_magnitude.Length != 0)
		{
			num ^= m_magnitude[0];
			if (m_magnitude.Length > 1)
			{
				num ^= m_magnitude[m_magnitude.Length - 1];
			}
		}
		if (m_sign >= 0)
		{
			return num;
		}
		return ~num;
	}

	private NetBigInteger Inc()
	{
		if (m_sign == 0)
		{
			return One;
		}
		if (m_sign < 0)
		{
			return new NetBigInteger(-1, doSubBigLil(m_magnitude, One.m_magnitude), checkMag: true);
		}
		return AddToMagnitude(One.m_magnitude);
	}

	public NetBigInteger Max(NetBigInteger value)
	{
		if (CompareTo(value) <= 0)
		{
			return value;
		}
		return this;
	}

	public NetBigInteger Min(NetBigInteger value)
	{
		if (CompareTo(value) >= 0)
		{
			return value;
		}
		return this;
	}

	public NetBigInteger Mod(NetBigInteger m)
	{
		if (m.m_sign < 1)
		{
			throw new ArithmeticException("Modulus must be positive");
		}
		NetBigInteger netBigInteger = Remainder(m);
		if (netBigInteger.m_sign < 0)
		{
			return netBigInteger.Add(m);
		}
		return netBigInteger;
	}

	public NetBigInteger ModInverse(NetBigInteger m)
	{
		if (m.m_sign < 1)
		{
			throw new ArithmeticException("Modulus must be positive");
		}
		if (!ExtEuclid(this, m, out NetBigInteger u1Out, null).Equals(One))
		{
			throw new ArithmeticException("Numbers not relatively prime.");
		}
		if (u1Out.m_sign < 0)
		{
			u1Out.m_sign = 1;
			u1Out.m_magnitude = doSubBigLil(m.m_magnitude, u1Out.m_magnitude);
		}
		return u1Out;
	}

	private static NetBigInteger ExtEuclid(NetBigInteger a, NetBigInteger b, out NetBigInteger u1Out, NetBigInteger? u2Out)
	{
		NetBigInteger netBigInteger = One;
		NetBigInteger netBigInteger2 = a;
		NetBigInteger netBigInteger3 = Zero;
		NetBigInteger netBigInteger4 = b;
		while (netBigInteger4.m_sign > 0)
		{
			NetBigInteger[] array = netBigInteger2.DivideAndRemainder(netBigInteger4);
			NetBigInteger n = netBigInteger3.Multiply(array[0]);
			NetBigInteger netBigInteger5 = netBigInteger.Subtract(n);
			netBigInteger = netBigInteger3;
			netBigInteger3 = netBigInteger5;
			netBigInteger2 = netBigInteger4;
			netBigInteger4 = array[1];
		}
		u1Out = new NetBigInteger(netBigInteger.m_sign, netBigInteger.m_magnitude, checkMag: false);
		if (u2Out != null)
		{
			NetBigInteger n2 = netBigInteger.Multiply(a);
			n2 = netBigInteger2.Subtract(n2);
			NetBigInteger netBigInteger6 = n2.Divide(b);
			u2Out.m_sign = netBigInteger6.m_sign;
			u2Out.m_magnitude = netBigInteger6.m_magnitude;
		}
		return netBigInteger2;
	}

	private static void ZeroOut(int[] x)
	{
		Array.Clear(x, 0, x.Length);
	}

	public NetBigInteger ModPow(NetBigInteger exponent, NetBigInteger m)
	{
		if (m.m_sign < 1)
		{
			throw new ArithmeticException("Modulus must be positive");
		}
		if (m.Equals(One))
		{
			return Zero;
		}
		if (exponent.m_sign == 0)
		{
			return One;
		}
		if (m_sign == 0)
		{
			return Zero;
		}
		int[] array = null;
		int[] array2 = null;
		bool flag = (m.m_magnitude[m.m_magnitude.Length - 1] & 1) == 1;
		long mQuote = 0L;
		if (flag)
		{
			mQuote = m.GetMQuote();
			array = ShiftLeft(32 * m.m_magnitude.Length).Mod(m).m_magnitude;
			flag = array.Length <= m.m_magnitude.Length;
			if (flag)
			{
				array2 = new int[m.m_magnitude.Length + 1];
				if (array.Length < m.m_magnitude.Length)
				{
					int[] array3 = new int[m.m_magnitude.Length];
					array.CopyTo(array3, array3.Length - array.Length);
					array = array3;
				}
			}
		}
		if (!flag)
		{
			if (m_magnitude.Length <= m.m_magnitude.Length)
			{
				array = new int[m.m_magnitude.Length];
				m_magnitude.CopyTo(array, array.Length - m_magnitude.Length);
			}
			else
			{
				NetBigInteger netBigInteger = Remainder(m);
				array = new int[m.m_magnitude.Length];
				netBigInteger.m_magnitude.CopyTo(array, array.Length - netBigInteger.m_magnitude.Length);
			}
			array2 = new int[m.m_magnitude.Length * 2];
		}
		int[] array4 = new int[m.m_magnitude.Length];
		for (int i = 0; i < exponent.m_magnitude.Length; i++)
		{
			int num = exponent.m_magnitude[i];
			int j = 0;
			if (i == 0)
			{
				while (num > 0)
				{
					num <<= 1;
					j++;
				}
				array.CopyTo(array4, 0);
				num <<= 1;
				j++;
			}
			while (num != 0)
			{
				if (flag)
				{
					MultiplyMonty(array2, array4, array4, m.m_magnitude, mQuote);
				}
				else
				{
					Square(array2, array4);
					Remainder(array2, m.m_magnitude);
					Array.Copy(array2, array2.Length - array4.Length, array4, 0, array4.Length);
					ZeroOut(array2);
				}
				j++;
				if (num < 0)
				{
					if (flag)
					{
						MultiplyMonty(array2, array4, array, m.m_magnitude, mQuote);
					}
					else
					{
						Multiply(array2, array4, array);
						Remainder(array2, m.m_magnitude);
						Array.Copy(array2, array2.Length - array4.Length, array4, 0, array4.Length);
						ZeroOut(array2);
					}
				}
				num <<= 1;
			}
			for (; j < 32; j++)
			{
				if (flag)
				{
					MultiplyMonty(array2, array4, array4, m.m_magnitude, mQuote);
					continue;
				}
				Square(array2, array4);
				Remainder(array2, m.m_magnitude);
				Array.Copy(array2, array2.Length - array4.Length, array4, 0, array4.Length);
				ZeroOut(array2);
			}
		}
		if (flag)
		{
			ZeroOut(array);
			array[^1] = 1;
			MultiplyMonty(array2, array4, array, m.m_magnitude, mQuote);
		}
		NetBigInteger netBigInteger2 = new NetBigInteger(1, array4, checkMag: true);
		if (exponent.m_sign <= 0)
		{
			return netBigInteger2.ModInverse(m);
		}
		return netBigInteger2;
	}

	private static int[] Square(int[] w, int[] x)
	{
		int num = w.Length - 1;
		ulong num5;
		ulong num4;
		for (int num2 = x.Length - 1; num2 != 0; num2--)
		{
			ulong num3 = (uint)x[num2];
			num4 = num3 * num3;
			num5 = num4 >> 32;
			num4 = (uint)num4;
			num4 += (uint)w[num];
			w[num] = (int)num4;
			ulong num6 = num5 + (num4 >> 32);
			for (int num7 = num2 - 1; num7 >= 0; num7--)
			{
				num--;
				num4 = num3 * (uint)x[num7];
				num5 = num4 >> 31;
				num4 <<= 1;
				num4 += num6 + (uint)w[num];
				w[num] = (int)num4;
				num6 = num5 + (num4 >> 32);
			}
			num6 += (uint)w[--num];
			w[num] = (int)num6;
			if (--num >= 0)
			{
				w[num] = (int)(num6 >> 32);
			}
			num += num2;
		}
		num4 = (uint)x[0];
		num4 *= num4;
		num5 = num4 >> 32;
		num4 &= 0xFFFFFFFFu;
		num4 += (uint)w[num];
		w[num] = (int)num4;
		if (--num >= 0)
		{
			w[num] = (int)(num5 + (num4 >> 32) + (uint)w[num]);
		}
		return w;
	}

	private static int[] Multiply(int[] x, int[] y, int[] z)
	{
		int num = z.Length;
		if (num < 1)
		{
			return x;
		}
		int num2 = x.Length - y.Length;
		long num4;
		while (true)
		{
			long num3 = z[--num] & 0xFFFFFFFFu;
			num4 = 0L;
			for (int num5 = y.Length - 1; num5 >= 0; num5--)
			{
				num4 += num3 * (y[num5] & 0xFFFFFFFFu) + (x[num2 + num5] & 0xFFFFFFFFu);
				x[num2 + num5] = (int)num4;
				num4 >>>= 32;
			}
			num2--;
			if (num < 1)
			{
				break;
			}
			x[num2] = (int)num4;
		}
		if (num2 >= 0)
		{
			x[num2] = (int)num4;
		}
		return x;
	}

	private static long FastExtEuclid(long a, long b, long[] uOut)
	{
		long num = 1L;
		long num2 = a;
		long num3 = 0L;
		long num4 = b;
		while (num4 > 0)
		{
			long num5 = num2 / num4;
			long num6 = num - num3 * num5;
			num = num3;
			num3 = num6;
			long num7 = num2 - num4 * num5;
			num2 = num4;
			num4 = num7;
		}
		uOut[0] = num;
		uOut[1] = (num2 - num * a) / b;
		return num2;
	}

	private static long FastModInverse(long v, long m)
	{
		if (m < 1)
		{
			throw new ArithmeticException("Modulus must be positive");
		}
		long[] array = new long[2];
		if (FastExtEuclid(v, m, array) != 1)
		{
			throw new ArithmeticException("Numbers not relatively prime.");
		}
		if (array[0] < 0)
		{
			array[0] += m;
		}
		return array[0];
	}

	private long GetMQuote()
	{
		if (m_quote != -1)
		{
			return m_quote;
		}
		if (m_magnitude.Length == 0 || (m_magnitude[m_magnitude.Length - 1] & 1) == 0)
		{
			return -1L;
		}
		long v = (~m_magnitude[m_magnitude.Length - 1] | 1) & 0xFFFFFFFFu;
		m_quote = FastModInverse(v, 4294967296L);
		return m_quote;
	}

	private static void MultiplyMonty(int[] a, int[] x, int[] y, int[] m, long mQuote)
	{
		if (m.Length == 1)
		{
			x[0] = (int)MultiplyMontyNIsOne((uint)x[0], (uint)y[0], (uint)m[0], (ulong)mQuote);
			return;
		}
		int num = m.Length;
		int num2 = num - 1;
		long num3 = y[num2] & 0xFFFFFFFFu;
		Array.Clear(a, 0, num + 1);
		for (int num4 = num; num4 > 0; num4--)
		{
			long num5 = x[num4 - 1] & 0xFFFFFFFFu;
			long num6 = ((((a[num] & 0xFFFFFFFFu) + ((num5 * num3) & 0xFFFFFFFFu)) & 0xFFFFFFFFu) * mQuote) & 0xFFFFFFFFu;
			long num7 = num5 * num3;
			long num8 = num6 * (m[num2] & 0xFFFFFFFFu);
			long num9 = (a[num] & 0xFFFFFFFFu) + (num7 & 0xFFFFFFFFu) + (num8 & 0xFFFFFFFFu);
			long num10 = (num7 >>> 32) + (num8 >>> 32) + (num9 >>> 32);
			for (int num11 = num2; num11 > 0; num11--)
			{
				num7 = num5 * (y[num11 - 1] & 0xFFFFFFFFu);
				num8 = num6 * (m[num11 - 1] & 0xFFFFFFFFu);
				num9 = (a[num11] & 0xFFFFFFFFu) + (num7 & 0xFFFFFFFFu) + (num8 & 0xFFFFFFFFu) + (num10 & 0xFFFFFFFFu);
				num10 = (num10 >>> 32) + (num7 >>> 32) + (num8 >>> 32) + (num9 >>> 32);
				a[num11 + 1] = (int)num9;
			}
			num10 += a[0] & 0xFFFFFFFFu;
			a[1] = (int)num10;
			a[0] = (int)(num10 >>> 32);
		}
		if (CompareTo(0, a, 0, m) >= 0)
		{
			Subtract(0, a, 0, m);
		}
		Array.Copy(a, 1, x, 0, num);
	}

	private static uint MultiplyMontyNIsOne(uint x, uint y, uint m, ulong mQuote)
	{
		ulong num = m;
		long num2 = (long)x * (long)y;
		ulong num3 = (ulong)((num2 * (long)mQuote) & 0xFFFFFFFFu) * num;
		ulong num4 = (ulong)(num2 & 0xFFFFFFFFu) + (num3 & 0xFFFFFFFFu);
		ulong num5 = (ulong)((num2 >>> 32) + (long)(num3 >> 32)) + (num4 >> 32);
		if (num5 > num)
		{
			num5 -= num;
		}
		return (uint)(num5 & 0xFFFFFFFFu);
	}

	public NetBigInteger Modulus(NetBigInteger val)
	{
		return Mod(val);
	}

	public NetBigInteger Multiply(NetBigInteger val)
	{
		if (m_sign == 0 || val.m_sign == 0)
		{
			return Zero;
		}
		if (val.QuickPow2Check())
		{
			NetBigInteger netBigInteger = ShiftLeft(val.Abs().BitLength - 1);
			if (val.m_sign <= 0)
			{
				return netBigInteger.Negate();
			}
			return netBigInteger;
		}
		if (QuickPow2Check())
		{
			NetBigInteger netBigInteger2 = val.ShiftLeft(Abs().BitLength - 1);
			if (m_sign <= 0)
			{
				return netBigInteger2.Negate();
			}
			return netBigInteger2;
		}
		int[] array = new int[(BitLength + val.BitLength + 32 - 1) / 32];
		if (val == this)
		{
			Square(array, m_magnitude);
		}
		else
		{
			Multiply(array, m_magnitude, val.m_magnitude);
		}
		return new NetBigInteger(m_sign * val.m_sign, array, checkMag: true);
	}

	public NetBigInteger Negate()
	{
		if (m_sign == 0)
		{
			return this;
		}
		return new NetBigInteger(-m_sign, m_magnitude, checkMag: false);
	}

	public NetBigInteger Not()
	{
		return Inc().Negate();
	}

	public NetBigInteger Pow(int exp)
	{
		if (exp < 0)
		{
			throw new ArithmeticException("Negative exponent");
		}
		if (exp == 0)
		{
			return One;
		}
		if (m_sign == 0 || Equals(One))
		{
			return this;
		}
		NetBigInteger netBigInteger = One;
		NetBigInteger netBigInteger2 = this;
		while (true)
		{
			if ((exp & 1) == 1)
			{
				netBigInteger = netBigInteger.Multiply(netBigInteger2);
			}
			exp >>= 1;
			if (exp == 0)
			{
				break;
			}
			netBigInteger2 = netBigInteger2.Multiply(netBigInteger2);
		}
		return netBigInteger;
	}

	private int Remainder(int m)
	{
		long num = 0L;
		for (int i = 0; i < m_magnitude.Length; i++)
		{
			long num2 = (uint)m_magnitude[i];
			num = ((num << 32) | num2) % m;
		}
		return (int)num;
	}

	private int[] Remainder(int[] x, int[] y)
	{
		int i;
		for (i = 0; i < x.Length && x[i] == 0; i++)
		{
		}
		int j;
		for (j = 0; j < y.Length && y[j] == 0; j++)
		{
		}
		int num = CompareNoLeadingZeroes(i, x, j, y);
		if (num > 0)
		{
			int num2 = calcBitLength(j, y);
			int num3 = calcBitLength(i, x);
			int num4 = num3 - num2;
			int k = 0;
			int num5 = num2;
			int[] array;
			if (num4 > 0)
			{
				array = ShiftLeft(y, num4);
				num5 += num4;
			}
			else
			{
				int num6 = y.Length - j;
				array = new int[num6];
				Array.Copy(y, j, array, 0, num6);
			}
			while (true)
			{
				if (num5 < num3 || CompareNoLeadingZeroes(i, x, k, array) >= 0)
				{
					Subtract(i, x, k, array);
					while (x[i] == 0)
					{
						if (++i == x.Length)
						{
							return x;
						}
					}
					num3 = 32 * (x.Length - i - 1) + BitLen(x[i]);
					if (num3 <= num2)
					{
						if (num3 < num2)
						{
							return x;
						}
						num = CompareNoLeadingZeroes(i, x, j, y);
						if (num <= 0)
						{
							break;
						}
					}
				}
				num4 = num5 - num3;
				if (num4 == 1)
				{
					int num7 = array[k] >>> 1;
					uint num8 = (uint)x[i];
					if ((uint)num7 > num8)
					{
						num4++;
					}
				}
				if (num4 < 2)
				{
					array = ShiftRightOneInPlace(k, array);
					num5--;
				}
				else
				{
					array = ShiftRightInPlace(k, array, num4);
					num5 -= num4;
				}
				for (; array[k] == 0; k++)
				{
				}
			}
		}
		if (num == 0)
		{
			Array.Clear(x, i, x.Length - i);
		}
		return x;
	}

	public NetBigInteger Remainder(NetBigInteger n)
	{
		if (n.m_sign == 0)
		{
			throw new ArithmeticException("Division by zero error");
		}
		if (m_sign == 0)
		{
			return Zero;
		}
		if (n.m_magnitude.Length == 1)
		{
			int num = n.m_magnitude[0];
			if (num > 0)
			{
				if (num == 1)
				{
					return Zero;
				}
				int num2 = Remainder(num);
				if (num2 != 0)
				{
					return new NetBigInteger(m_sign, new int[1] { num2 }, checkMag: false);
				}
				return Zero;
			}
		}
		if (CompareNoLeadingZeroes(0, m_magnitude, 0, n.m_magnitude) < 0)
		{
			return this;
		}
		int[] mag;
		if (n.QuickPow2Check())
		{
			mag = LastNBits(n.Abs().BitLength - 1);
		}
		else
		{
			mag = (int[])m_magnitude.Clone();
			mag = Remainder(mag, n.m_magnitude);
		}
		return new NetBigInteger(m_sign, mag, checkMag: true);
	}

	private int[] LastNBits(int n)
	{
		if (n < 1)
		{
			return ZeroMagnitude;
		}
		int val = (n + 32 - 1) / 32;
		val = Math.Min(val, m_magnitude.Length);
		int[] array = new int[val];
		Array.Copy(m_magnitude, m_magnitude.Length - val, array, 0, val);
		int num = n % 32;
		if (num != 0)
		{
			array[0] &= ~(-1 << num);
		}
		return array;
	}

	private static int[] ShiftLeft(int[] mag, int n)
	{
		int num = n >>> 5;
		int num2 = n & 0x1F;
		int num3 = mag.Length;
		int[] array;
		if (num2 == 0)
		{
			array = new int[num3 + num];
			mag.CopyTo(array, 0);
		}
		else
		{
			int num4 = 0;
			int num5 = 32 - num2;
			int num6 = mag[0] >>> num5;
			if (num6 != 0)
			{
				array = new int[num3 + num + 1];
				array[num4++] = num6;
			}
			else
			{
				array = new int[num3 + num];
			}
			int num7 = mag[0];
			for (int i = 0; i < num3 - 1; i++)
			{
				int num8 = mag[i + 1];
				array[num4++] = (num7 << num2) | (num8 >>> num5);
				num7 = num8;
			}
			array[num4] = mag[num3 - 1] << num2;
		}
		return array;
	}

	public NetBigInteger ShiftLeft(int n)
	{
		if (m_sign == 0 || m_magnitude.Length == 0)
		{
			return Zero;
		}
		if (n == 0)
		{
			return this;
		}
		if (n < 0)
		{
			return ShiftRight(-n);
		}
		NetBigInteger netBigInteger = new NetBigInteger(m_sign, ShiftLeft(m_magnitude, n), checkMag: true);
		if (m_numBits != -1)
		{
			netBigInteger.m_numBits = ((m_sign > 0) ? m_numBits : (m_numBits + n));
		}
		if (m_numBitLength != -1)
		{
			netBigInteger.m_numBitLength = m_numBitLength + n;
		}
		return netBigInteger;
	}

	private static int[] ShiftRightInPlace(int start, int[] mag, int n)
	{
		int num = (n >>> 5) + start;
		int num2 = n & 0x1F;
		int num3 = mag.Length - 1;
		if (num != start)
		{
			int num4 = num - start;
			for (int num5 = num3; num5 >= num; num5--)
			{
				mag[num5] = mag[num5 - num4];
			}
			for (int num6 = num - 1; num6 >= start; num6--)
			{
				mag[num6] = 0;
			}
		}
		if (num2 != 0)
		{
			int num7 = 32 - num2;
			int num8 = mag[num3];
			for (int num9 = num3; num9 > num; num9--)
			{
				int num10 = mag[num9 - 1];
				mag[num9] = (num8 >>> num2) | (num10 << num7);
				num8 = num10;
			}
			mag[num] >>>= num2;
		}
		return mag;
	}

	private static int[] ShiftRightOneInPlace(int start, int[] mag)
	{
		int num = mag.Length;
		int num2 = mag[num - 1];
		while (--num > start)
		{
			int num3 = mag[num - 1];
			mag[num] = (num2 >>> 1) | (num3 << 31);
			num2 = num3;
		}
		mag[start] >>>= 1;
		return mag;
	}

	public NetBigInteger ShiftRight(int n)
	{
		if (n == 0)
		{
			return this;
		}
		if (n < 0)
		{
			return ShiftLeft(-n);
		}
		if (n >= BitLength)
		{
			if (m_sign >= 0)
			{
				return Zero;
			}
			return One.Negate();
		}
		int num = BitLength - n + 31 >> 5;
		int[] array = new int[num];
		int num2 = n >> 5;
		int num3 = n & 0x1F;
		if (num3 == 0)
		{
			Array.Copy(m_magnitude, 0, array, 0, array.Length);
		}
		else
		{
			int num4 = 32 - num3;
			int num5 = m_magnitude.Length - 1 - num2;
			for (int num6 = num - 1; num6 >= 0; num6--)
			{
				array[num6] = m_magnitude[num5--] >>> num3;
				if (num5 >= 0)
				{
					array[num6] |= m_magnitude[num5] << num4;
				}
			}
		}
		return new NetBigInteger(m_sign, array, checkMag: false);
	}

	private static int[] Subtract(int xStart, int[] x, int yStart, int[] y)
	{
		int num = x.Length;
		int num2 = y.Length;
		int num3 = 0;
		do
		{
			long num4 = (x[--num] & 0xFFFFFFFFu) - (y[--num2] & 0xFFFFFFFFu) + num3;
			x[num] = (int)num4;
			num3 = (int)(num4 >> 63);
		}
		while (num2 > yStart);
		if (num3 != 0)
		{
			while (--x[--num] == -1)
			{
			}
		}
		return x;
	}

	public NetBigInteger Subtract(NetBigInteger n)
	{
		if (n.m_sign == 0)
		{
			return this;
		}
		if (m_sign == 0)
		{
			return n.Negate();
		}
		if (m_sign != n.m_sign)
		{
			return Add(n.Negate());
		}
		int num = CompareNoLeadingZeroes(0, m_magnitude, 0, n.m_magnitude);
		if (num == 0)
		{
			return Zero;
		}
		NetBigInteger netBigInteger;
		NetBigInteger netBigInteger2;
		if (num < 0)
		{
			netBigInteger = n;
			netBigInteger2 = this;
		}
		else
		{
			netBigInteger = this;
			netBigInteger2 = n;
		}
		return new NetBigInteger(m_sign * num, doSubBigLil(netBigInteger.m_magnitude, netBigInteger2.m_magnitude), checkMag: true);
	}

	private static int[] doSubBigLil(int[] bigMag, int[] lilMag)
	{
		int[] x = (int[])bigMag.Clone();
		return Subtract(0, x, 0, lilMag);
	}

	public byte[] ToByteArray()
	{
		return ToByteArray(unsigned: false);
	}

	public byte[] ToByteArrayUnsigned()
	{
		return ToByteArray(unsigned: true);
	}

	private byte[] ToByteArray(bool unsigned)
	{
		if (m_sign == 0)
		{
			if (!unsigned)
			{
				return new byte[1];
			}
			return ZeroEncoding;
		}
		byte[] array = new byte[GetByteLength((unsigned && m_sign > 0) ? BitLength : (BitLength + 1))];
		int num = m_magnitude.Length;
		int num2 = array.Length;
		if (m_sign > 0)
		{
			while (num > 1)
			{
				uint num3 = (uint)m_magnitude[--num];
				array[--num2] = (byte)num3;
				array[--num2] = (byte)(num3 >> 8);
				array[--num2] = (byte)(num3 >> 16);
				array[--num2] = (byte)(num3 >> 24);
			}
			uint num4;
			for (num4 = (uint)m_magnitude[0]; num4 > 255; num4 >>= 8)
			{
				array[--num2] = (byte)num4;
			}
			array[--num2] = (byte)num4;
		}
		else
		{
			bool flag = true;
			while (num > 1)
			{
				uint num5 = (uint)(~m_magnitude[--num]);
				if (flag)
				{
					flag = ++num5 == 0;
				}
				array[--num2] = (byte)num5;
				array[--num2] = (byte)(num5 >> 8);
				array[--num2] = (byte)(num5 >> 16);
				array[--num2] = (byte)(num5 >> 24);
			}
			uint num6 = (uint)m_magnitude[0];
			if (flag)
			{
				num6--;
			}
			while (num6 > 255)
			{
				array[--num2] = (byte)(~num6);
				num6 >>= 8;
			}
			array[--num2] = (byte)(~num6);
			if (num2 > 0)
			{
				array[--num2] = byte.MaxValue;
			}
		}
		return array;
	}

	public override string ToString()
	{
		return ToString(10);
	}

	public string ToString(int radix)
	{
		if (radix != 2 && radix != 10 && radix != 16)
		{
			throw new FormatException("Only bases 2, 10, 16 are allowed");
		}
		if (m_magnitude == null)
		{
			return "null";
		}
		if (m_sign == 0)
		{
			return "0";
		}
		StringBuilder stringBuilder = new StringBuilder();
		switch (radix)
		{
		case 16:
		{
			stringBuilder.Append(m_magnitude[0].ToString("x"));
			for (int i = 1; i < m_magnitude.Length; i++)
			{
				stringBuilder.Append(m_magnitude[i].ToString("x8"));
			}
			break;
		}
		case 2:
		{
			stringBuilder.Append('1');
			for (int num = BitLength - 2; num >= 0; num--)
			{
				stringBuilder.Append(TestBit(num) ? '1' : '0');
			}
			break;
		}
		default:
		{
			Stack<string> stack = new Stack<string>();
			NetBigInteger netBigInteger = ValueOf(radix);
			NetBigInteger netBigInteger2 = Abs();
			while (netBigInteger2.m_sign != 0)
			{
				NetBigInteger netBigInteger3 = netBigInteger2.Mod(netBigInteger);
				if (netBigInteger3.m_sign == 0)
				{
					stack.Push("0");
				}
				else
				{
					stack.Push(netBigInteger3.m_magnitude[0].ToString("d"));
				}
				netBigInteger2 = netBigInteger2.Divide(netBigInteger);
			}
			while (stack.Count != 0)
			{
				stringBuilder.Append(stack.Pop());
			}
			break;
		}
		}
		string text = stringBuilder.ToString();
		if (text[0] == '0')
		{
			int num2 = 0;
			while (text[++num2] == '0')
			{
			}
			text = text.Substring(num2);
		}
		if (m_sign == -1)
		{
			text = "-" + text;
		}
		return text;
	}

	private static NetBigInteger createUValueOf(ulong value)
	{
		int num = (int)(value >> 32);
		int num2 = (int)value;
		if (num != 0)
		{
			return new NetBigInteger(1, new int[2] { num, num2 }, checkMag: false);
		}
		if (num2 != 0)
		{
			NetBigInteger netBigInteger = new NetBigInteger(1, new int[1] { num2 }, checkMag: false);
			if ((num2 & -num2) == num2)
			{
				netBigInteger.m_numBits = 1;
			}
			return netBigInteger;
		}
		return Zero;
	}

	private static NetBigInteger createValueOf(long value)
	{
		if (value < 0)
		{
			if (value == long.MinValue)
			{
				return createValueOf(~value).Not();
			}
			return createValueOf(-value).Negate();
		}
		return createUValueOf((ulong)value);
	}

	public static NetBigInteger ValueOf(long value)
	{
		if ((ulong)value <= 3uL)
		{
			switch (value)
			{
			case 0L:
				return Zero;
			case 1L:
				return One;
			case 2L:
				return Two;
			case 3L:
				return Three;
			}
		}
		if (value == 10)
		{
			return Ten;
		}
		return createValueOf(value);
	}

	public int GetLowestSetBit()
	{
		if (m_sign == 0)
		{
			return -1;
		}
		int num = m_magnitude.Length;
		while (--num > 0 && m_magnitude[num] == 0)
		{
		}
		int num2 = m_magnitude[num];
		int num3 = (((num2 & 0xFFFF) != 0) ? (((num2 & 0xFF) == 0) ? 23 : 31) : (((num2 & 0xFF0000) == 0) ? 7 : 15));
		while (num3 > 0 && num2 << num3 != int.MinValue)
		{
			num3--;
		}
		return (m_magnitude.Length - num) * 32 - (num3 + 1);
	}

	public bool TestBit(int n)
	{
		if (n < 0)
		{
			throw new ArithmeticException("Bit position must not be negative");
		}
		if (m_sign < 0)
		{
			return !Not().TestBit(n);
		}
		int num = n / 32;
		if (num >= m_magnitude.Length)
		{
			return false;
		}
		return ((m_magnitude[m_magnitude.Length - 1 - num] >> n % 32) & 1) > 0;
	}
}
