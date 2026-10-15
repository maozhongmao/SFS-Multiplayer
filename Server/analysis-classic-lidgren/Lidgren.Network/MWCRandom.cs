using System;

namespace Lidgren.Network;

public class MWCRandom : NetRandom
{
	public new static readonly MWCRandom Instance = new MWCRandom();

	private uint m_w;

	private uint m_z;

	public MWCRandom()
	{
		Initialize(NetRandomSeed.GetUInt64());
	}

	[CLSCompliant(false)]
	public override void Initialize(uint seed)
	{
		m_w = seed;
		m_z = seed * 16777619;
	}

	[CLSCompliant(false)]
	public void Initialize(ulong seed)
	{
		m_w = (uint)seed;
		m_z = (uint)(seed >> 32);
	}

	[CLSCompliant(false)]
	public override uint NextUInt32()
	{
		m_z = 36969 * (m_z & 0xFFFF) + (m_z >> 16);
		m_w = 18000 * (m_w & 0xFFFF) + (m_w >> 16);
		return (m_z << 16) + m_w;
	}
}
