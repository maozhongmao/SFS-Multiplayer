using System;

namespace Lidgren.Network;

public sealed class XorShiftRandom : NetRandom
{
	private const uint c_x = 123456789u;

	private const uint c_y = 362436069u;

	private const uint c_z = 521288629u;

	private const uint c_w = 88675123u;

	public new static readonly XorShiftRandom Instance = new XorShiftRandom();

	private uint m_x;

	private uint m_y;

	private uint m_z;

	private uint m_w;

	public XorShiftRandom()
	{
		Initialize(NetRandomSeed.GetUInt64());
	}

	[CLSCompliant(false)]
	public XorShiftRandom(ulong seed)
	{
		Initialize(seed);
	}

	[CLSCompliant(false)]
	public override void Initialize(uint seed)
	{
		m_x = seed;
		m_y = 362436069u;
		m_z = 521288629u;
		m_w = 88675123u;
	}

	[CLSCompliant(false)]
	public void Initialize(ulong seed)
	{
		m_x = (uint)seed;
		m_y = 362436069u;
		m_z = (uint)(seed << 32);
		m_w = 88675123u;
	}

	[CLSCompliant(false)]
	public override uint NextUInt32()
	{
		uint num = m_x ^ (m_x << 11);
		m_x = m_y;
		m_y = m_z;
		m_z = m_w;
		return m_w = m_w ^ (m_w >> 19) ^ (num ^ (num >> 8));
	}
}
