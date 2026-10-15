using System;
using System.Collections.Generic;
using System.Net;
using System.Reflection;
using System.Text;

namespace Lidgren.Network;

public class NetBuffer
{
	protected const int c_overAllocateAmount = 4;

	private const string c_readOverflowError = "Trying to read past the buffer size - likely caused by mismatching Write/Reads, different size or order.";

	private static readonly Dictionary<Type, MethodInfo> s_readMethods;

	private static readonly Dictionary<Type, MethodInfo> s_writeMethods;

	internal byte[] m_data;

	internal int m_bitLength;

	internal int m_readPosition;

	public byte[] Data
	{
		get
		{
			return m_data;
		}
		set
		{
			m_data = value;
		}
	}

	public int LengthBytes
	{
		get
		{
			return m_bitLength + 7 >> 3;
		}
		set
		{
			m_bitLength = value * 8;
			InternalEnsureBufferSize(m_bitLength);
		}
	}

	public int LengthBits
	{
		get
		{
			return m_bitLength;
		}
		set
		{
			m_bitLength = value;
			InternalEnsureBufferSize(m_bitLength);
		}
	}

	public long Position
	{
		get
		{
			return m_readPosition;
		}
		set
		{
			m_readPosition = (int)value;
		}
	}

	public int PositionInBytes => m_readPosition / 8;

	static NetBuffer()
	{
		s_readMethods = new Dictionary<Type, MethodInfo>();
		MethodInfo[] methods = typeof(NetIncomingMessage).GetMethods(BindingFlags.Instance | BindingFlags.Public);
		MethodInfo[] array = methods;
		foreach (MethodInfo methodInfo in array)
		{
			if (methodInfo.GetParameters().Length == 0 && methodInfo.Name.StartsWith("Read", StringComparison.InvariantCulture) && methodInfo.Name.Substring(4) == methodInfo.ReturnType.Name)
			{
				s_readMethods[methodInfo.ReturnType] = methodInfo;
			}
		}
		s_writeMethods = new Dictionary<Type, MethodInfo>();
		methods = typeof(NetOutgoingMessage).GetMethods(BindingFlags.Instance | BindingFlags.Public);
		MethodInfo[] array2 = methods;
		foreach (MethodInfo methodInfo2 in array2)
		{
			if (methodInfo2.Name.Equals("Write", StringComparison.InvariantCulture))
			{
				ParameterInfo[] parameters = methodInfo2.GetParameters();
				if (parameters.Length == 1)
				{
					s_writeMethods[parameters[0].ParameterType] = methodInfo2;
				}
			}
		}
	}

	public byte[] PeekDataBuffer()
	{
		return m_data;
	}

	public bool PeekBoolean()
	{
		byte b = NetBitWriter.ReadByte(m_data, 1, m_readPosition);
		if (b <= 0)
		{
			return false;
		}
		return true;
	}

	public byte PeekByte()
	{
		return NetBitWriter.ReadByte(m_data, 8, m_readPosition);
	}

	[CLSCompliant(false)]
	public sbyte PeekSByte()
	{
		byte b = NetBitWriter.ReadByte(m_data, 8, m_readPosition);
		return (sbyte)b;
	}

	public byte PeekByte(int numberOfBits)
	{
		return NetBitWriter.ReadByte(m_data, numberOfBits, m_readPosition);
	}

	public byte[] PeekBytes(int numberOfBytes)
	{
		byte[] array = new byte[numberOfBytes];
		NetBitWriter.ReadBytes(m_data, numberOfBytes, m_readPosition, array, 0);
		return array;
	}

	public void PeekBytes(byte[] into, int offset, int numberOfBytes)
	{
		NetBitWriter.ReadBytes(m_data, numberOfBytes, m_readPosition, into, offset);
	}

	public short PeekInt16()
	{
		uint num = NetBitWriter.ReadUInt16(m_data, 16, m_readPosition);
		return (short)num;
	}

	[CLSCompliant(false)]
	public ushort PeekUInt16()
	{
		uint num = NetBitWriter.ReadUInt16(m_data, 16, m_readPosition);
		return (ushort)num;
	}

	public int PeekInt32()
	{
		return (int)NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
	}

	public int PeekInt32(int numberOfBits)
	{
		uint num = NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
		if (numberOfBits == 32)
		{
			return (int)num;
		}
		int num2 = 1 << numberOfBits - 1;
		if ((num & num2) == 0)
		{
			return (int)num;
		}
		uint num3 = uint.MaxValue >> 33 - numberOfBits;
		uint num4 = (num & num3) + 1;
		return (int)(0 - num4);
	}

	[CLSCompliant(false)]
	public uint PeekUInt32()
	{
		return NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
	}

	[CLSCompliant(false)]
	public uint PeekUInt32(int numberOfBits)
	{
		return NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
	}

	[CLSCompliant(false)]
	public ulong PeekUInt64()
	{
		ulong num = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		ulong num2 = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition + 32);
		return num + (num2 << 32);
	}

	public long PeekInt64()
	{
		return (long)PeekUInt64();
	}

	[CLSCompliant(false)]
	public ulong PeekUInt64(int numberOfBits)
	{
		if (numberOfBits <= 32)
		{
			return NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
		}
		ulong num = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		return num | ((ulong)NetBitWriter.ReadUInt32(m_data, numberOfBits - 32, m_readPosition + 32) << 32);
	}

	public long PeekInt64(int numberOfBits)
	{
		return (long)PeekUInt64(numberOfBits);
	}

	public float PeekFloat()
	{
		return PeekSingle();
	}

	public float PeekSingle()
	{
		if ((m_readPosition & 7) == 0)
		{
			return BitConverter.ToSingle(m_data, m_readPosition >> 3);
		}
		byte[] value = PeekBytes(4);
		return BitConverter.ToSingle(value, 0);
	}

	public double PeekDouble()
	{
		if ((m_readPosition & 7) == 0)
		{
			return BitConverter.ToDouble(m_data, m_readPosition >> 3);
		}
		byte[] value = PeekBytes(8);
		return BitConverter.ToDouble(value, 0);
	}

	public string PeekString()
	{
		int readPosition = m_readPosition;
		string result = ReadString();
		m_readPosition = readPosition;
		return result;
	}

	public bool ReadBoolean()
	{
		byte b = NetBitWriter.ReadByte(m_data, 1, m_readPosition);
		m_readPosition++;
		if (b <= 0)
		{
			return false;
		}
		return true;
	}

	public byte ReadByte()
	{
		byte result = NetBitWriter.ReadByte(m_data, 8, m_readPosition);
		m_readPosition += 8;
		return result;
	}

	public bool ReadByte(out byte result)
	{
		if (m_bitLength - m_readPosition < 8)
		{
			result = 0;
			return false;
		}
		result = NetBitWriter.ReadByte(m_data, 8, m_readPosition);
		m_readPosition += 8;
		return true;
	}

	[CLSCompliant(false)]
	public sbyte ReadSByte()
	{
		byte b = NetBitWriter.ReadByte(m_data, 8, m_readPosition);
		m_readPosition += 8;
		return (sbyte)b;
	}

	public byte ReadByte(int numberOfBits)
	{
		byte result = NetBitWriter.ReadByte(m_data, numberOfBits, m_readPosition);
		m_readPosition += numberOfBits;
		return result;
	}

	public byte[] ReadBytes(int numberOfBytes)
	{
		byte[] array = new byte[numberOfBytes];
		NetBitWriter.ReadBytes(m_data, numberOfBytes, m_readPosition, array, 0);
		m_readPosition += 8 * numberOfBytes;
		return array;
	}

	public bool ReadBytes(int numberOfBytes, out byte[] result)
	{
		if (m_bitLength - m_readPosition + 7 < numberOfBytes * 8)
		{
			result = null;
			return false;
		}
		result = new byte[numberOfBytes];
		NetBitWriter.ReadBytes(m_data, numberOfBytes, m_readPosition, result, 0);
		m_readPosition += 8 * numberOfBytes;
		return true;
	}

	public void ReadBytes(byte[] into, int offset, int numberOfBytes)
	{
		NetBitWriter.ReadBytes(m_data, numberOfBytes, m_readPosition, into, offset);
		m_readPosition += 8 * numberOfBytes;
	}

	public void ReadBits(byte[] into, int offset, int numberOfBits)
	{
		int num = numberOfBits / 8;
		int num2 = numberOfBits - num * 8;
		NetBitWriter.ReadBytes(m_data, num, m_readPosition, into, offset);
		m_readPosition += 8 * num;
		if (num2 > 0)
		{
			into[offset + num] = ReadByte(num2);
		}
	}

	public short ReadInt16()
	{
		uint num = NetBitWriter.ReadUInt16(m_data, 16, m_readPosition);
		m_readPosition += 16;
		return (short)num;
	}

	[CLSCompliant(false)]
	public ushort ReadUInt16()
	{
		uint num = NetBitWriter.ReadUInt16(m_data, 16, m_readPosition);
		m_readPosition += 16;
		return (ushort)num;
	}

	public int ReadInt32()
	{
		uint result = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		m_readPosition += 32;
		return (int)result;
	}

	[CLSCompliant(false)]
	public bool ReadInt32(out int result)
	{
		if (m_bitLength - m_readPosition < 32)
		{
			result = 0;
			return false;
		}
		result = (int)NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		m_readPosition += 32;
		return true;
	}

	public int ReadInt32(int numberOfBits)
	{
		uint num = NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
		m_readPosition += numberOfBits;
		if (numberOfBits == 32)
		{
			return (int)num;
		}
		int num2 = 1 << numberOfBits - 1;
		if ((num & num2) == 0)
		{
			return (int)num;
		}
		uint num3 = uint.MaxValue >> 33 - numberOfBits;
		uint num4 = (num & num3) + 1;
		return (int)(0 - num4);
	}

	[CLSCompliant(false)]
	public uint ReadUInt32()
	{
		uint result = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		m_readPosition += 32;
		return result;
	}

	[CLSCompliant(false)]
	public bool ReadUInt32(out uint result)
	{
		if (m_bitLength - m_readPosition < 32)
		{
			result = 0u;
			return false;
		}
		result = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		m_readPosition += 32;
		return true;
	}

	[CLSCompliant(false)]
	public uint ReadUInt32(int numberOfBits)
	{
		uint result = NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
		m_readPosition += numberOfBits;
		return result;
	}

	[CLSCompliant(false)]
	public ulong ReadUInt64()
	{
		ulong num = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		m_readPosition += 32;
		ulong num2 = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
		ulong result = num + (num2 << 32);
		m_readPosition += 32;
		return result;
	}

	public long ReadInt64()
	{
		return (long)ReadUInt64();
	}

	[CLSCompliant(false)]
	public ulong ReadUInt64(int numberOfBits)
	{
		ulong result;
		if (numberOfBits <= 32)
		{
			result = NetBitWriter.ReadUInt32(m_data, numberOfBits, m_readPosition);
		}
		else
		{
			result = NetBitWriter.ReadUInt32(m_data, 32, m_readPosition);
			result |= (ulong)NetBitWriter.ReadUInt32(m_data, numberOfBits - 32, m_readPosition + 32) << 32;
		}
		m_readPosition += numberOfBits;
		return result;
	}

	public long ReadInt64(int numberOfBits)
	{
		return (long)ReadUInt64(numberOfBits);
	}

	public float ReadFloat()
	{
		return ReadSingle();
	}

	public float ReadSingle()
	{
		if ((m_readPosition & 7) == 0)
		{
			float result = BitConverter.ToSingle(m_data, m_readPosition >> 3);
			m_readPosition += 32;
			return result;
		}
		byte[] value = ReadBytes(4);
		return BitConverter.ToSingle(value, 0);
	}

	public bool ReadSingle(out float result)
	{
		if (m_bitLength - m_readPosition < 32)
		{
			result = 0f;
			return false;
		}
		if ((m_readPosition & 7) == 0)
		{
			result = BitConverter.ToSingle(m_data, m_readPosition >> 3);
			m_readPosition += 32;
			return true;
		}
		byte[] value = ReadBytes(4);
		result = BitConverter.ToSingle(value, 0);
		return true;
	}

	public double ReadDouble()
	{
		if ((m_readPosition & 7) == 0)
		{
			double result = BitConverter.ToDouble(m_data, m_readPosition >> 3);
			m_readPosition += 64;
			return result;
		}
		byte[] value = ReadBytes(8);
		return BitConverter.ToDouble(value, 0);
	}

	[CLSCompliant(false)]
	public uint ReadVariableUInt32()
	{
		int num = 0;
		int num2 = 0;
		while (m_bitLength - m_readPosition >= 8)
		{
			byte b = ReadByte();
			num |= (b & 0x7F) << num2;
			num2 += 7;
			if ((b & 0x80) == 0)
			{
				return (uint)num;
			}
		}
		return (uint)num;
	}

	[CLSCompliant(false)]
	public bool ReadVariableUInt32(out uint result)
	{
		int num = 0;
		int num2 = 0;
		while (m_bitLength - m_readPosition >= 8)
		{
			if (!ReadByte(out var result2))
			{
				result = 0u;
				return false;
			}
			num |= (result2 & 0x7F) << num2;
			num2 += 7;
			if ((result2 & 0x80) == 0)
			{
				result = (uint)num;
				return true;
			}
		}
		result = (uint)num;
		return false;
	}

	public int ReadVariableInt32()
	{
		uint num = ReadVariableUInt32();
		return (int)((num >> 1) ^ (0 - (num & 1)));
	}

	public long ReadVariableInt64()
	{
		ulong num = ReadVariableUInt64();
		return (long)((num >> 1) ^ (0L - (num & 1)));
	}

	[CLSCompliant(false)]
	public ulong ReadVariableUInt64()
	{
		ulong num = 0uL;
		int num2 = 0;
		while (m_bitLength - m_readPosition >= 8)
		{
			byte b = ReadByte();
			num |= ((ulong)b & 0x7FuL) << num2;
			num2 += 7;
			if ((b & 0x80) == 0)
			{
				return num;
			}
		}
		return num;
	}

	public float ReadSignedSingle(int numberOfBits)
	{
		uint num = ReadUInt32(numberOfBits);
		int num2 = (1 << numberOfBits) - 1;
		return ((float)(num + 1) / (float)(num2 + 1) - 0.5f) * 2f;
	}

	public float ReadUnitSingle(int numberOfBits)
	{
		uint num = ReadUInt32(numberOfBits);
		int num2 = (1 << numberOfBits) - 1;
		return (float)(num + 1) / (float)(num2 + 1);
	}

	public float ReadRangedSingle(float min, float max, int numberOfBits)
	{
		float num = max - min;
		int num2 = (1 << numberOfBits) - 1;
		float num3 = ReadUInt32(numberOfBits);
		float num4 = num3 / (float)num2;
		return min + num4 * num;
	}

	public int ReadRangedInteger(int min, int max)
	{
		uint value = (uint)(max - min);
		int numberOfBits = NetUtility.BitsToHoldUInt(value);
		uint num = ReadUInt32(numberOfBits);
		return (int)(min + num);
	}

	public long ReadRangedInteger(long min, long max)
	{
		ulong value = (ulong)(max - min);
		int numberOfBits = NetUtility.BitsToHoldUInt64(value);
		ulong num = ReadUInt64(numberOfBits);
		return min + (long)num;
	}

	public string ReadString()
	{
		int num = (int)ReadVariableUInt32();
		if (num <= 0)
		{
			return string.Empty;
		}
		if ((ulong)(m_bitLength - m_readPosition) < (ulong)((long)num * 8L))
		{
			m_readPosition = m_bitLength;
			return null;
		}
		if ((m_readPosition & 7) == 0)
		{
			string result = Encoding.UTF8.GetString(m_data, m_readPosition >> 3, num);
			m_readPosition += 8 * num;
			return result;
		}
		byte[] array = ReadBytes(num);
		return Encoding.UTF8.GetString(array, 0, array.Length);
	}

	public bool ReadString(out string result)
	{
		if (!ReadVariableUInt32(out var result2))
		{
			result = string.Empty;
			return false;
		}
		if (result2 == 0)
		{
			result = string.Empty;
			return true;
		}
		if (m_bitLength - m_readPosition < result2 * 8)
		{
			result = string.Empty;
			return false;
		}
		if ((m_readPosition & 7) == 0)
		{
			result = Encoding.UTF8.GetString(m_data, m_readPosition >> 3, (int)result2);
			m_readPosition += (int)(8 * result2);
			return true;
		}
		if (!ReadBytes((int)result2, out var result3))
		{
			result = string.Empty;
			return false;
		}
		result = Encoding.UTF8.GetString(result3, 0, result3.Length);
		return true;
	}

	public double ReadTime(NetConnection connection, bool highPrecision)
	{
		double num = (highPrecision ? ReadDouble() : ((double)ReadSingle()));
		if (connection == null)
		{
			throw new NetException("Cannot call ReadTime() on message without a connected sender (ie. unconnected messages)");
		}
		return num - connection.m_remoteTimeOffset;
	}

	public IPEndPoint ReadIPEndPoint()
	{
		byte numberOfBytes = ReadByte();
		byte[] bytes = ReadBytes(numberOfBytes);
		int port = ReadUInt16();
		IPAddress address = NetUtility.CreateAddressFromBytes(bytes);
		return new IPEndPoint(address, port);
	}

	public void SkipPadBits()
	{
		m_readPosition = (m_readPosition + 7 >> 3) * 8;
	}

	public void ReadPadBits()
	{
		m_readPosition = (m_readPosition + 7 >> 3) * 8;
	}

	public void SkipPadBits(int numberOfBits)
	{
		m_readPosition += numberOfBits;
	}

	public void ReadAllFields(object target)
	{
		ReadAllFields(target, BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
	}

	public void ReadAllFields(object target, BindingFlags flags)
	{
		if (target == null)
		{
			throw new ArgumentNullException("target");
		}
		Type type = target.GetType();
		FieldInfo[] fields = type.GetFields(flags);
		NetUtility.SortMembersList(fields);
		FieldInfo[] array = fields;
		foreach (FieldInfo fieldInfo in array)
		{
			if (s_readMethods.TryGetValue(fieldInfo.FieldType, out var value))
			{
				object value2 = value.Invoke(this, null);
				fieldInfo.SetValue(target, value2);
			}
		}
	}

	public void ReadAllProperties(object target)
	{
		ReadAllProperties(target, BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
	}

	public void ReadAllProperties(object target, BindingFlags flags)
	{
		if (target == null)
		{
			throw new ArgumentNullException("target");
		}
		Type type = target.GetType();
		PropertyInfo[] properties = type.GetProperties(flags);
		NetUtility.SortMembersList(properties);
		PropertyInfo[] array = properties;
		foreach (PropertyInfo propertyInfo in array)
		{
			if (s_readMethods.TryGetValue(propertyInfo.PropertyType, out var value))
			{
				object obj = value.Invoke(this, null);
				MethodInfo setMethod = propertyInfo.GetSetMethod();
				if (setMethod != null)
				{
					setMethod.Invoke(target, new object[1] { obj });
				}
			}
		}
	}

	public void EnsureBufferSize(int numberOfBits)
	{
		int num = numberOfBits + 7 >> 3;
		if (m_data == null)
		{
			m_data = new byte[num + 4];
		}
		else if (m_data.Length < num)
		{
			Array.Resize(ref m_data, num + 4);
		}
	}

	internal void InternalEnsureBufferSize(int numberOfBits)
	{
		int num = numberOfBits + 7 >> 3;
		if (m_data == null)
		{
			m_data = new byte[num];
		}
		else if (m_data.Length < num)
		{
			Array.Resize(ref m_data, num);
		}
	}

	public void Write(bool value)
	{
		EnsureBufferSize(m_bitLength + 1);
		NetBitWriter.WriteByte((byte)(value ? 1 : 0), 1, m_data, m_bitLength);
		m_bitLength++;
	}

	public void Write(byte source)
	{
		EnsureBufferSize(m_bitLength + 8);
		NetBitWriter.WriteByte(source, 8, m_data, m_bitLength);
		m_bitLength += 8;
	}

	[CLSCompliant(false)]
	public void Write(sbyte source)
	{
		EnsureBufferSize(m_bitLength + 8);
		NetBitWriter.WriteByte((byte)source, 8, m_data, m_bitLength);
		m_bitLength += 8;
	}

	public void Write(byte source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		NetBitWriter.WriteByte(source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	public void Write(byte[] source)
	{
		if (source == null)
		{
			throw new ArgumentNullException("source");
		}
		int num = source.Length * 8;
		EnsureBufferSize(m_bitLength + num);
		NetBitWriter.WriteBytes(source, 0, source.Length, m_data, m_bitLength);
		m_bitLength += num;
	}

	public void Write(byte[] source, int offsetInBytes, int numberOfBytes)
	{
		if (source == null)
		{
			throw new ArgumentNullException("source");
		}
		int num = numberOfBytes * 8;
		EnsureBufferSize(m_bitLength + num);
		NetBitWriter.WriteBytes(source, offsetInBytes, numberOfBytes, m_data, m_bitLength);
		m_bitLength += num;
	}

	[CLSCompliant(false)]
	public void Write(ushort source)
	{
		EnsureBufferSize(m_bitLength + 16);
		NetBitWriter.WriteUInt16(source, 16, m_data, m_bitLength);
		m_bitLength += 16;
	}

	[CLSCompliant(false)]
	public void WriteAt(int offset, ushort source)
	{
		int num = Math.Max(m_bitLength, offset + 16);
		EnsureBufferSize(num);
		NetBitWriter.WriteUInt16(source, 16, m_data, offset);
		m_bitLength = num;
	}

	[CLSCompliant(false)]
	public void Write(ushort source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		NetBitWriter.WriteUInt16(source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	public void Write(short source)
	{
		EnsureBufferSize(m_bitLength + 16);
		NetBitWriter.WriteUInt16((ushort)source, 16, m_data, m_bitLength);
		m_bitLength += 16;
	}

	public void WriteAt(int offset, short source)
	{
		int num = Math.Max(m_bitLength, offset + 16);
		EnsureBufferSize(num);
		NetBitWriter.WriteUInt16((ushort)source, 16, m_data, offset);
		m_bitLength = num;
	}

	public void Write(int source)
	{
		EnsureBufferSize(m_bitLength + 32);
		NetBitWriter.WriteUInt32((uint)source, 32, m_data, m_bitLength);
		m_bitLength += 32;
	}

	public void WriteAt(int offset, int source)
	{
		int num = Math.Max(m_bitLength, offset + 32);
		EnsureBufferSize(num);
		NetBitWriter.WriteUInt32((uint)source, 32, m_data, offset);
		m_bitLength = num;
	}

	[CLSCompliant(false)]
	public void Write(uint source)
	{
		EnsureBufferSize(m_bitLength + 32);
		NetBitWriter.WriteUInt32(source, 32, m_data, m_bitLength);
		m_bitLength += 32;
	}

	[CLSCompliant(false)]
	public void WriteAt(int offset, uint source)
	{
		int num = Math.Max(m_bitLength, offset + 32);
		EnsureBufferSize(num);
		NetBitWriter.WriteUInt32(source, 32, m_data, offset);
		m_bitLength = num;
	}

	[CLSCompliant(false)]
	public void Write(uint source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		NetBitWriter.WriteUInt32(source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	public void Write(int source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		if (numberOfBits != 32)
		{
			int num = 1 << numberOfBits - 1;
			source = ((source >= 0) ? (source & ~num) : ((-source - 1) | num));
		}
		NetBitWriter.WriteUInt32((uint)source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	[CLSCompliant(false)]
	public void Write(ulong source)
	{
		EnsureBufferSize(m_bitLength + 64);
		NetBitWriter.WriteUInt64(source, 64, m_data, m_bitLength);
		m_bitLength += 64;
	}

	[CLSCompliant(false)]
	public void WriteAt(int offset, ulong source)
	{
		int num = Math.Max(m_bitLength, offset + 64);
		EnsureBufferSize(num);
		NetBitWriter.WriteUInt64(source, 64, m_data, offset);
		m_bitLength = num;
	}

	[CLSCompliant(false)]
	public void Write(ulong source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		NetBitWriter.WriteUInt64(source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	public void Write(long source)
	{
		EnsureBufferSize(m_bitLength + 64);
		NetBitWriter.WriteUInt64((ulong)source, 64, m_data, m_bitLength);
		m_bitLength += 64;
	}

	public void Write(long source, int numberOfBits)
	{
		EnsureBufferSize(m_bitLength + numberOfBits);
		NetBitWriter.WriteUInt64((ulong)source, numberOfBits, m_data, m_bitLength);
		m_bitLength += numberOfBits;
	}

	public void Write(float source)
	{
		SingleUIntUnion singleUIntUnion = default(SingleUIntUnion);
		singleUIntUnion.UIntValue = 0u;
		singleUIntUnion.SingleValue = source;
		Write(singleUIntUnion.UIntValue);
	}

	public void Write(double source)
	{
		byte[] bytes = BitConverter.GetBytes(source);
		Write(bytes);
	}

	[CLSCompliant(false)]
	public int WriteVariableUInt32(uint value)
	{
		int num = 1;
		uint num2 = value;
		while (num2 >= 128)
		{
			Write((byte)(num2 | 0x80));
			num2 >>= 7;
			num++;
		}
		Write((byte)num2);
		return num;
	}

	public int WriteVariableInt32(int value)
	{
		uint value2 = (uint)((value << 1) ^ (value >> 31));
		return WriteVariableUInt32(value2);
	}

	public int WriteVariableInt64(long value)
	{
		ulong value2 = (ulong)((value << 1) ^ (value >> 63));
		return WriteVariableUInt64(value2);
	}

	[CLSCompliant(false)]
	public int WriteVariableUInt64(ulong value)
	{
		int num = 1;
		ulong num2 = value;
		while (num2 >= 128)
		{
			Write((byte)(num2 | 0x80));
			num2 >>= 7;
			num++;
		}
		Write((byte)num2);
		return num;
	}

	public void WriteSignedSingle(float value, int numberOfBits)
	{
		float num = (value + 1f) * 0.5f;
		int num2 = (1 << numberOfBits) - 1;
		uint source = (uint)(num * (float)num2);
		Write(source, numberOfBits);
	}

	public void WriteUnitSingle(float value, int numberOfBits)
	{
		int num = (1 << numberOfBits) - 1;
		uint source = (uint)(value * (float)num);
		Write(source, numberOfBits);
	}

	public void WriteRangedSingle(float value, float min, float max, int numberOfBits)
	{
		float num = max - min;
		float num2 = (value - min) / num;
		int num3 = (1 << numberOfBits) - 1;
		Write((uint)((float)num3 * num2), numberOfBits);
	}

	public int WriteRangedInteger(int min, int max, int value)
	{
		uint value2 = (uint)(max - min);
		int num = NetUtility.BitsToHoldUInt(value2);
		uint source = (uint)(value - min);
		Write(source, num);
		return num;
	}

	public int WriteRangedInteger(long min, long max, long value)
	{
		ulong value2 = (ulong)(max - min);
		int num = NetUtility.BitsToHoldUInt64(value2);
		ulong source = (ulong)(value - min);
		Write(source, num);
		return num;
	}

	public void Write(string source)
	{
		if (string.IsNullOrEmpty(source))
		{
			WriteVariableUInt32(0u);
			return;
		}
		byte[] bytes = Encoding.UTF8.GetBytes(source);
		EnsureBufferSize(m_bitLength + 8 + bytes.Length * 8);
		WriteVariableUInt32((uint)bytes.Length);
		Write(bytes);
	}

	public void Write(IPEndPoint endPoint)
	{
		byte[] addressBytes = endPoint.Address.GetAddressBytes();
		Write((byte)addressBytes.Length);
		Write(addressBytes);
		Write((ushort)endPoint.Port);
	}

	public void WriteTime(bool highPrecision)
	{
		double now = NetTime.Now;
		if (highPrecision)
		{
			Write(now);
		}
		else
		{
			Write((float)now);
		}
	}

	public void WriteTime(double localTime, bool highPrecision)
	{
		if (highPrecision)
		{
			Write(localTime);
		}
		else
		{
			Write((float)localTime);
		}
	}

	public void WritePadBits()
	{
		m_bitLength = (m_bitLength + 7 >> 3) * 8;
		EnsureBufferSize(m_bitLength);
	}

	public void WritePadBits(int numberOfBits)
	{
		m_bitLength += numberOfBits;
		EnsureBufferSize(m_bitLength);
	}

	public void Write(NetBuffer buffer)
	{
		EnsureBufferSize(m_bitLength + buffer.LengthBytes * 8);
		Write(buffer.m_data, 0, buffer.LengthBytes);
		int num = buffer.m_bitLength % 8;
		if (num != 0)
		{
			int num2 = 8 - num;
			m_bitLength -= num2;
		}
	}

	public void WriteAllFields(object ob)
	{
		WriteAllFields(ob, BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
	}

	public void WriteAllFields(object ob, BindingFlags flags)
	{
		if (ob == null)
		{
			return;
		}
		Type type = ob.GetType();
		FieldInfo[] fields = type.GetFields(flags);
		NetUtility.SortMembersList(fields);
		FieldInfo[] array = fields;
		foreach (FieldInfo fieldInfo in array)
		{
			object value = fieldInfo.GetValue(ob);
			if (s_writeMethods.TryGetValue(fieldInfo.FieldType, out var value2))
			{
				value2.Invoke(this, new object[1] { value });
				continue;
			}
			throw new NetException("Failed to find write method for type " + fieldInfo.FieldType);
		}
	}

	public void WriteAllProperties(object ob)
	{
		WriteAllProperties(ob, BindingFlags.DeclaredOnly | BindingFlags.Instance | BindingFlags.Public | BindingFlags.NonPublic);
	}

	public void WriteAllProperties(object ob, BindingFlags flags)
	{
		if (ob == null)
		{
			return;
		}
		Type type = ob.GetType();
		PropertyInfo[] properties = type.GetProperties(flags);
		NetUtility.SortMembersList(properties);
		PropertyInfo[] array = properties;
		foreach (PropertyInfo propertyInfo in array)
		{
			MethodInfo getMethod = propertyInfo.GetGetMethod();
			if (getMethod != null)
			{
				object obj = getMethod.Invoke(ob, null);
				if (s_writeMethods.TryGetValue(propertyInfo.PropertyType, out var value))
				{
					value.Invoke(this, new object[1] { obj });
				}
			}
		}
	}
}
