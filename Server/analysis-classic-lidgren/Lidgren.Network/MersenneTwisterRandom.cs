using System;

namespace Lidgren.Network;

public sealed class MersenneTwisterRandom : NetRandom
{
	private const int N = 624;

	private const int M = 397;

	private const uint MATRIX_A = 2567483615u;

	private const uint UPPER_MASK = 2147483648u;

	private const uint LOWER_MASK = 2147483647u;

	private const uint TEMPER1 = 2636928640u;

	private const uint TEMPER2 = 4022730752u;

	private const int TEMPER3 = 11;

	private const int TEMPER4 = 7;

	private const int TEMPER5 = 15;

	private const int TEMPER6 = 18;

	private const double c_realUnitInt = 4.656612873077393E-10;

	public new static readonly MersenneTwisterRandom Instance = new MersenneTwisterRandom();

	private uint[] mt;

	private int mti;

	private uint[] mag01;

	public MersenneTwisterRandom()
	{
		Initialize(NetRandomSeed.GetUInt32());
	}

	[CLSCompliant(false)]
	public MersenneTwisterRandom(uint seed)
	{
		Initialize(seed);
	}

	[CLSCompliant(false)]
	public override void Initialize(uint seed)
	{
		mt = new uint[624];
		mti = 625;
		mag01 = new uint[2] { 0u, 2567483615u };
		mt[0] = seed;
		for (int i = 1; i < 624; i++)
		{
			mt[i] = (uint)(1812433253 * (mt[i - 1] ^ (mt[i - 1] >> 30)) + i);
		}
	}

	[CLSCompliant(false)]
	public override uint NextUInt32()
	{
		if (mti >= 624)
		{
			GenRandAll();
			mti = 0;
		}
		uint num = mt[mti++];
		num ^= num >> 11;
		num ^= (num << 7) & 0x9D2C5680u;
		num ^= (num << 15) & 0xEFC60000u;
		return num ^ (num >> 18);
	}

	private void GenRandAll()
	{
		int num = 1;
		uint num2 = mt[0] & 0x80000000u;
		uint num3;
		do
		{
			num3 = mt[num];
			mt[num - 1] = mt[num + 396] ^ ((num2 | (num3 & 0x7FFFFFFF)) >> 1) ^ mag01[num3 & 1];
			num2 = num3 & 0x80000000u;
		}
		while (++num < 228);
		do
		{
			num3 = mt[num];
			mt[num - 1] = mt[num + -228] ^ ((num2 | (num3 & 0x7FFFFFFF)) >> 1) ^ mag01[num3 & 1];
			num2 = num3 & 0x80000000u;
		}
		while (++num < 624);
		num3 = mt[0];
		mt[623] = mt[396] ^ ((num2 | (num3 & 0x7FFFFFFF)) >> 1) ^ mag01[num3 & 1];
	}
}
