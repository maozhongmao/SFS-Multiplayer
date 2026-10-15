using System;
using System.Runtime.CompilerServices;
using System.Text;

namespace Lidgren.Network;

public sealed class NetBitVector
{
	private readonly int m_capacity;

	private readonly int[] m_data;

	private int m_numBitsSet;

	public int Capacity => m_capacity;

	[IndexerName("Bit")]
	public bool this[int index]
	{
		get
		{
			return Get(index);
		}
		set
		{
			Set(index, value);
		}
	}

	public NetBitVector(int bitsCapacity)
	{
		m_capacity = bitsCapacity;
		m_data = new int[(bitsCapacity + 31) / 32];
	}

	public bool IsEmpty()
	{
		return m_numBitsSet == 0;
	}

	public int Count()
	{
		return m_numBitsSet;
	}

	public void RotateDown()
	{
		int num = m_data.Length - 1;
		int num2 = m_data[0] & 1;
		for (int i = 0; i < num; i++)
		{
			m_data[i] = ((m_data[i] >> 1) & 0x7FFFFFFF) | (m_data[i + 1] << 31);
		}
		int num3 = m_capacity - 1 - 32 * num;
		int num4 = m_data[num];
		num4 >>= 1;
		num4 |= num2 << num3;
		m_data[num] = num4;
	}

	public int GetFirstSetIndex()
	{
		int num = 0;
		int num2;
		for (num2 = m_data[0]; num2 == 0; num2 = m_data[num])
		{
			num++;
		}
		int i;
		for (i = 0; ((num2 >> i) & 1) == 0; i++)
		{
		}
		return num * 32 + i;
	}

	public bool Get(int bitIndex)
	{
		return (m_data[bitIndex / 32] & (1 << bitIndex % 32)) != 0;
	}

	public void Set(int bitIndex, bool value)
	{
		int num = bitIndex / 32;
		if (value)
		{
			if ((m_data[num] & (1 << bitIndex % 32)) == 0)
			{
				m_numBitsSet++;
			}
			m_data[num] |= 1 << bitIndex % 32;
		}
		else
		{
			if ((m_data[num] & (1 << bitIndex % 32)) != 0)
			{
				m_numBitsSet--;
			}
			m_data[num] &= ~(1 << bitIndex % 32);
		}
	}

	public void Clear()
	{
		Array.Clear(m_data, 0, m_data.Length);
		m_numBitsSet = 0;
	}

	public override string ToString()
	{
		StringBuilder stringBuilder = new StringBuilder(m_capacity + 2);
		stringBuilder.Append('[');
		for (int i = 0; i < m_capacity; i++)
		{
			stringBuilder.Append(Get(m_capacity - i - 1) ? '1' : '0');
		}
		stringBuilder.Append(']');
		return stringBuilder.ToString();
	}
}
