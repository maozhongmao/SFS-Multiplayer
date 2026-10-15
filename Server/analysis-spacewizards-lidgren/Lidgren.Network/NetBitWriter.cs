using System;
using System.Buffers.Binary;

namespace Lidgren.Network;

public static class NetBitWriter
{
	public static byte ReadByte(byte[] fromBuffer, int numberOfBits, int readBitOffset)
	{
		return ReadByte(fromBuffer.AsSpan(), numberOfBits, readBitOffset);
	}

	public static byte ReadByte(ReadOnlySpan<byte> fromBuffer, int numberOfBits, int readBitOffset)
	{
		int num = readBitOffset >> 3;
		int num2 = readBitOffset - num * 8;
		if (num2 == 0 && numberOfBits == 8)
		{
			return fromBuffer[num];
		}
		byte b = (byte)(fromBuffer[num] >> num2);
		int num3 = numberOfBits - (8 - num2);
		if (num3 < 1)
		{
			return (byte)(b & (255 >> 8 - numberOfBits));
		}
		byte b2 = fromBuffer[num + 1];
		b2 &= (byte)(255 >> 8 - num3);
		return (byte)(b | (byte)(b2 << numberOfBits - num3));
	}

	public static void ReadBytes(byte[] fromBuffer, int numberOfBytes, int readBitOffset, byte[] destination, int destinationByteOffset)
	{
		ReadBytes(fromBuffer, readBitOffset, destination.AsSpan(destinationByteOffset, numberOfBytes));
	}

	public static void ReadBytes(ReadOnlySpan<byte> fromBuffer, int readBitOffset, Span<byte> destination)
	{
		int num = readBitOffset >> 3;
		int num2 = readBitOffset - num * 8;
		if (num2 == 0)
		{
			fromBuffer.Slice(num, destination.Length).CopyTo(destination);
			return;
		}
		int num3 = 8 - num2;
		int num4 = 255 >> num3;
		for (int i = 0; i < destination.Length; i++)
		{
			int num5 = fromBuffer[num] >> num2;
			num++;
			int num6 = fromBuffer[num] & num4;
			destination[i] = (byte)(num5 | (num6 << num3));
		}
	}

	public static void WriteByte(byte source, int numberOfBits, byte[] destination, int destBitOffset)
	{
		WriteByte(source, numberOfBits, destination.AsSpan(), destBitOffset);
	}

	public static void WriteByte(byte source, int numberOfBits, Span<byte> destination, int destBitOffset)
	{
		if (numberOfBits != 0)
		{
			source = (byte)(source & (255 >> 8 - numberOfBits));
			int num = destBitOffset >> 3;
			int num2 = destBitOffset & 7;
			int num3 = 8 - num2;
			int num4 = num3 - numberOfBits;
			if (num4 >= 0)
			{
				int num5 = (255 >> num3) | (255 << 8 - num4);
				destination[num] = (byte)((destination[num] & num5) | (source << num2));
			}
			else
			{
				destination[num] = (byte)((destination[num] & (255 >> num3)) | (source << num2));
				num++;
				destination[num] = (byte)((destination[num] & (255 << numberOfBits - num3)) | (source >> num3));
			}
		}
	}

	public static void WriteBytes(byte[] source, int sourceByteOffset, int numberOfBytes, byte[] destination, int destBitOffset)
	{
		WriteBytes(source.AsSpan(sourceByteOffset, numberOfBytes), destination, destBitOffset);
	}

	public static void WriteBytes(ReadOnlySpan<byte> source, byte[] destination, int destBitOffset)
	{
		int num = destBitOffset >> 3;
		int num2 = destBitOffset & 7;
		if (num2 == 0)
		{
			source.CopyTo(destination.AsSpan(num));
			return;
		}
		int num3 = 8 - num2;
		for (int i = 0; i < source.Length; i++)
		{
			byte b = source[i];
			destination[num] &= (byte)(255 >> num3);
			destination[num] |= (byte)(b << num2);
			num++;
			destination[num] &= (byte)(255 << num2);
			destination[num] |= (byte)(b >> num3);
		}
	}

	[CLSCompliant(false)]
	public static ushort ReadUInt16(ReadOnlySpan<byte> fromBuffer, int numberOfBits, int readBitOffset)
	{
		if (numberOfBits == 16 && (readBitOffset & 7) == 0)
		{
			return NetUtility.ReadUnaligned<ushort>(fromBuffer.Slice(readBitOffset >> 3));
		}
		if (numberOfBits <= 8)
		{
			return ReadByte(fromBuffer, numberOfBits, readBitOffset);
		}
		ushort num = ReadByte(fromBuffer, 8, readBitOffset);
		numberOfBits -= 8;
		readBitOffset += 8;
		if (numberOfBits <= 8)
		{
			num |= (ushort)(ReadByte(fromBuffer, numberOfBits, readBitOffset) << 8);
		}
		if (!BitConverter.IsLittleEndian)
		{
			return BinaryPrimitives.ReverseEndianness(num);
		}
		return num;
	}

	[CLSCompliant(false)]
	public static uint ReadUInt32(ReadOnlySpan<byte> fromBuffer, int numberOfBits, int readBitOffset)
	{
		if (numberOfBits == 32 && (readBitOffset & 7) == 0)
		{
			return NetUtility.ReadUnaligned<uint>(fromBuffer.Slice(readBitOffset >> 3));
		}
		if (numberOfBits <= 8)
		{
			return ReadByte(fromBuffer, numberOfBits, readBitOffset);
		}
		uint num = ReadByte(fromBuffer, 8, readBitOffset);
		numberOfBits -= 8;
		readBitOffset += 8;
		if (numberOfBits <= 8)
		{
			return num | (uint)(ReadByte(fromBuffer, numberOfBits, readBitOffset) << 8);
		}
		num |= (uint)(ReadByte(fromBuffer, 8, readBitOffset) << 8);
		numberOfBits -= 8;
		readBitOffset += 8;
		if (numberOfBits <= 8)
		{
			uint num2 = ReadByte(fromBuffer, numberOfBits, readBitOffset);
			num2 <<= 16;
			return num | num2;
		}
		num |= (uint)(ReadByte(fromBuffer, 8, readBitOffset) << 16);
		numberOfBits -= 8;
		readBitOffset += 8;
		num |= (uint)(ReadByte(fromBuffer, numberOfBits, readBitOffset) << 24);
		if (!BitConverter.IsLittleEndian)
		{
			return BinaryPrimitives.ReverseEndianness(num);
		}
		return num;
	}

	[CLSCompliant(false)]
	public static void WriteUInt16(ushort source, int numberOfBits, Span<byte> destination, int destinationBitOffset)
	{
		if (numberOfBits == 0)
		{
			return;
		}
		if (!BitConverter.IsLittleEndian)
		{
			source = BinaryPrimitives.ReverseEndianness(source);
		}
		if (numberOfBits <= 8)
		{
			WriteByte((byte)source, numberOfBits, destination, destinationBitOffset);
			return;
		}
		WriteByte((byte)source, 8, destination, destinationBitOffset);
		numberOfBits -= 8;
		if (numberOfBits > 0)
		{
			WriteByte((byte)(source >> 8), numberOfBits, destination, destinationBitOffset + 8);
		}
	}

	[CLSCompliant(false)]
	public static int WriteUInt32(uint source, int numberOfBits, Span<byte> destination, int destinationBitOffset)
	{
		if (!BitConverter.IsLittleEndian)
		{
			source = BinaryPrimitives.ReverseEndianness(source);
		}
		int result = destinationBitOffset + numberOfBits;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)source, numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)source, 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 8), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 8), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 16), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 16), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		WriteByte((byte)(source >> 24), numberOfBits, destination, destinationBitOffset);
		return result;
	}

	[CLSCompliant(false)]
	public static int WriteUInt64(ulong source, int numberOfBits, Span<byte> destination, int destinationBitOffset)
	{
		if (!BitConverter.IsLittleEndian)
		{
			source = BinaryPrimitives.ReverseEndianness(source);
		}
		int result = destinationBitOffset + numberOfBits;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)source, numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)source, 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 8), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 8), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 16), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 16), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 24), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 24), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 32), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 32), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 40), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 40), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 48), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 48), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		if (numberOfBits <= 8)
		{
			WriteByte((byte)(source >> 56), numberOfBits, destination, destinationBitOffset);
			return result;
		}
		WriteByte((byte)(source >> 56), 8, destination, destinationBitOffset);
		destinationBitOffset += 8;
		numberOfBits -= 8;
		return result;
	}

	[CLSCompliant(false)]
	public static int WriteVariableUInt32(Span<byte> intoBuffer, int offset, uint value)
	{
		int num = 0;
		uint num2 = value;
		while (num2 >= 128)
		{
			intoBuffer[offset + num] = (byte)(num2 | 0x80);
			num2 >>= 7;
			num++;
		}
		intoBuffer[offset + num] = (byte)num2;
		return num + 1;
	}

	[CLSCompliant(false)]
	public static uint ReadVariableUInt32(ReadOnlySpan<byte> buffer, ref int offset)
	{
		int num = 0;
		int num2 = 0;
		byte b;
		do
		{
			b = buffer[offset++];
			num |= (b & 0x7F) << (num2 & 0x1F);
			num2 += 7;
		}
		while ((b & 0x80) != 0);
		return (uint)num;
	}

	public static void Zero(Span<byte> destination, int numberOfBits, int destBitOffset)
	{
		int num = destBitOffset >> 3;
		int num2 = destBitOffset & 7;
		int num3 = numberOfBits >> 3;
		int num4 = numberOfBits & 7;
		if (num2 == 0)
		{
			destination.Slice(destBitOffset / 8, num3).Fill(0);
			if (num4 > 0)
			{
				Span<byte> span = destination.Slice(num3, 1);
				span[0] = (byte)(span[0] & ~(255 >> 8 - num4));
			}
		}
		else
		{
			int num5 = 8 - num2;
			destination[num] &= (byte)(255 >> num5);
			num++;
			destination.Slice(num, num3 - 2).Fill(0);
			destination[num3 - 2] &= (byte)(255 << num2);
		}
	}
}
