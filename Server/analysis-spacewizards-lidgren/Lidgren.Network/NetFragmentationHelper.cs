namespace Lidgren.Network;

internal static class NetFragmentationHelper
{
	internal static int WriteHeader(byte[] destination, int ptr, int group, int totalBits, int chunkByteSize, int chunkNumber)
	{
		uint num;
		for (num = (uint)group; num >= 128; num >>= 7)
		{
			destination[ptr++] = (byte)(num | 0x80);
		}
		destination[ptr++] = (byte)num;
		uint num2;
		for (num2 = (uint)totalBits; num2 >= 128; num2 >>= 7)
		{
			destination[ptr++] = (byte)(num2 | 0x80);
		}
		destination[ptr++] = (byte)num2;
		uint num3;
		for (num3 = (uint)chunkByteSize; num3 >= 128; num3 >>= 7)
		{
			destination[ptr++] = (byte)(num3 | 0x80);
		}
		destination[ptr++] = (byte)num3;
		uint num4;
		for (num4 = (uint)chunkNumber; num4 >= 128; num4 >>= 7)
		{
			destination[ptr++] = (byte)(num4 | 0x80);
		}
		destination[ptr++] = (byte)num4;
		return ptr;
	}

	internal static int ReadHeader(byte[] buffer, int ptr, out int group, out int totalBits, out int chunkByteSize, out int chunkNumber)
	{
		int num = 0;
		int num2 = 0;
		byte b;
		do
		{
			b = buffer[ptr++];
			num |= (b & 0x7F) << (num2 & 0x1F);
			num2 += 7;
		}
		while ((b & 0x80) != 0);
		group = num;
		num = 0;
		num2 = 0;
		byte b2;
		do
		{
			b2 = buffer[ptr++];
			num |= (b2 & 0x7F) << (num2 & 0x1F);
			num2 += 7;
		}
		while ((b2 & 0x80) != 0);
		totalBits = num;
		num = 0;
		num2 = 0;
		byte b3;
		do
		{
			b3 = buffer[ptr++];
			num |= (b3 & 0x7F) << (num2 & 0x1F);
			num2 += 7;
		}
		while ((b3 & 0x80) != 0);
		chunkByteSize = num;
		num = 0;
		num2 = 0;
		byte b4;
		do
		{
			b4 = buffer[ptr++];
			num |= (b4 & 0x7F) << (num2 & 0x1F);
			num2 += 7;
		}
		while ((b4 & 0x80) != 0);
		chunkNumber = num;
		return ptr;
	}

	internal static int GetFragmentationHeaderSize(int groupId, int totalBytes, int chunkByteSize, int numChunks)
	{
		int num = 4;
		for (uint num2 = (uint)groupId; num2 >= 128; num2 >>= 7)
		{
			num++;
		}
		for (uint num3 = (uint)(totalBytes * 8); num3 >= 128; num3 >>= 7)
		{
			num++;
		}
		for (uint num4 = (uint)chunkByteSize; num4 >= 128; num4 >>= 7)
		{
			num++;
		}
		for (uint num5 = (uint)numChunks; num5 >= 128; num5 >>= 7)
		{
			num++;
		}
		return num;
	}

	internal static int GetBestChunkSize(int group, int totalBytes, int mtu)
	{
		int num = mtu - 5 - 4;
		int fragmentationHeaderSize = GetFragmentationHeaderSize(group, totalBytes, num, totalBytes / num);
		num = mtu - 5 - fragmentationHeaderSize;
		int num2 = 0;
		do
		{
			num--;
			int num3 = totalBytes / num;
			if (num3 * num < totalBytes)
			{
				num3++;
			}
			num2 = GetFragmentationHeaderSize(group, totalBytes, num, num3);
		}
		while (num + num2 + 5 + 1 >= mtu);
		return num;
	}
}
