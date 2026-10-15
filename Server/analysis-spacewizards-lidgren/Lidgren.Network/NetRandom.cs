using System;

namespace Lidgren.Network;

public abstract class NetRandom : Random
{
	public static NetRandom Instance = new MWCRandom();

	private const double c_realUnitInt = 4.656612873077393E-10;

	private uint m_boolValues;

	private int m_nextBoolIndex;

	public NetRandom()
	{
		Initialize(NetRandomSeed.GetUInt32());
	}

	public NetRandom(int seed)
	{
		Initialize((uint)seed);
	}

	[CLSCompliant(false)]
	public virtual void Initialize(uint seed)
	{
		throw new NotImplementedException("Implement this in inherited classes");
	}

	[CLSCompliant(false)]
	public virtual uint NextUInt32()
	{
		throw new NotImplementedException("Implement this in inherited classes");
	}

	public override int Next()
	{
		int num = (int)(0x7FFFFFFF & NextUInt32());
		if (num == int.MaxValue)
		{
			return NextInt32();
		}
		return num;
	}

	public int NextInt32()
	{
		return (int)(0x7FFFFFFF & NextUInt32());
	}

	public override double NextDouble()
	{
		return 4.656612873077393E-10 * (double)NextInt32();
	}

	protected override double Sample()
	{
		return 4.656612873077393E-10 * (double)NextInt32();
	}

	public override float NextSingle()
	{
		float num = (float)(4.656612873077393E-10 * (double)NextInt32());
		if (num == 1f)
		{
			return NextSingle();
		}
		return num;
	}

	public override int Next(int maxValue)
	{
		return (int)(NextDouble() * (double)maxValue);
	}

	public override int Next(int minValue, int maxValue)
	{
		return minValue + (int)(NextDouble() * (double)(maxValue - minValue));
	}

	[CLSCompliant(false)]
	public ulong NextUInt64()
	{
		return (ulong)NextUInt32() | (ulong)NextUInt32();
	}

	public bool NextBool()
	{
		if (m_nextBoolIndex >= 32)
		{
			m_boolValues = NextUInt32();
			m_nextBoolIndex = 1;
		}
		bool result = ((m_boolValues >> m_nextBoolIndex) & 1) == 1;
		m_nextBoolIndex++;
		return result;
	}

	public virtual void NextBytes(byte[] buffer, int offset, int length)
	{
		int num = length / 4;
		int num2 = offset;
		for (int i = 0; i < num; i++)
		{
			uint num3 = NextUInt32();
			buffer[num2++] = (byte)num3;
			buffer[num2++] = (byte)(num3 >> 8);
			buffer[num2++] = (byte)(num3 >> 16);
			buffer[num2++] = (byte)(num3 >> 24);
		}
		int num4 = length - num * 4;
		for (int j = 0; j < num4; j++)
		{
			buffer[num2++] = (byte)NextUInt32();
		}
	}

	public override void NextBytes(byte[] buffer)
	{
		NextBytes(buffer, 0, buffer.Length);
	}
}
