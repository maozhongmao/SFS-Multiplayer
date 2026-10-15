using System;
using System.Diagnostics;

namespace Lidgren.Network;

[DebuggerDisplay("LengthBits={LengthBits}")]
public sealed class NetOutgoingMessage : NetBuffer
{
	internal NetMessageType m_messageType;

	internal bool m_isSent;

	internal int m_recyclingCount;

	internal int m_fragmentGroup;

	internal int m_fragmentGroupTotalBits;

	internal int m_fragmentChunkByteSize;

	internal int m_fragmentChunkNumber;

	internal NetOutgoingMessage()
	{
	}

	internal void Reset()
	{
		m_messageType = NetMessageType.LibraryError;
		m_bitLength = 0;
		m_isSent = false;
		m_fragmentGroup = 0;
	}

	internal int Encode(byte[] intoBuffer, int ptr, int sequenceNumber)
	{
		intoBuffer[ptr++] = (byte)m_messageType;
		byte b = (byte)((sequenceNumber << 1) | ((m_fragmentGroup != 0) ? 1 : 0));
		intoBuffer[ptr++] = b;
		intoBuffer[ptr++] = (byte)(sequenceNumber >> 7);
		if (m_fragmentGroup == 0)
		{
			intoBuffer[ptr++] = (byte)m_bitLength;
			intoBuffer[ptr++] = (byte)(m_bitLength >> 8);
			int num = NetUtility.BytesToHoldBits(m_bitLength);
			if (num > 0)
			{
				Buffer.BlockCopy(m_data, 0, intoBuffer, ptr, num);
				ptr += num;
			}
		}
		else
		{
			int num2 = ptr;
			intoBuffer[ptr++] = (byte)m_bitLength;
			intoBuffer[ptr++] = (byte)(m_bitLength >> 8);
			ptr = NetFragmentationHelper.WriteHeader(intoBuffer, ptr, m_fragmentGroup, m_fragmentGroupTotalBits, m_fragmentChunkByteSize, m_fragmentChunkNumber);
			int num3 = ptr - num2 - 2;
			int num4 = m_bitLength + num3 * 8;
			intoBuffer[num2] = (byte)num4;
			intoBuffer[num2 + 1] = (byte)(num4 >> 8);
			int num5 = NetUtility.BytesToHoldBits(m_bitLength);
			if (num5 > 0)
			{
				Buffer.BlockCopy(m_data, m_fragmentChunkNumber * m_fragmentChunkByteSize, intoBuffer, ptr, num5);
				ptr += num5;
			}
		}
		return ptr;
	}

	internal int GetEncodedSize()
	{
		int num = 5;
		if (m_fragmentGroup != 0)
		{
			num += NetFragmentationHelper.GetFragmentationHeaderSize(m_fragmentGroup, m_fragmentGroupTotalBits / 8, m_fragmentChunkByteSize, m_fragmentChunkNumber);
		}
		return num + base.LengthBytes;
	}

	public bool Encrypt(NetEncryption encryption)
	{
		return encryption.Encrypt(this);
	}

	public override string ToString()
	{
		if (m_isSent)
		{
			return string.Concat("[NetOutgoingMessage ", m_messageType, " ", base.LengthBytes, " bytes]");
		}
		return "[NetOutgoingMessage " + base.LengthBytes + " bytes]";
	}
}
