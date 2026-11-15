// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;

namespace MultiplayerSFS.Common;

public enum TcpFrameKind : byte
{
	Hello = 1,
	HelloAck = 2,
	Packet = 3,
	Ping = 4,
	Pong = 5,
	Disconnect = 6,
	RequestWorldSnapshot = 7,
	RequestRocketSnapshot = 8,
	ServerInfoRequest = 9,
	ServerInfoResponse = 10,
	ServerVersionRequest = 11,
	ServerVersionResponse = 12
}

public sealed class TcpFrame
{
	public TcpFrameKind Kind { get; }
	public int Sequence { get; }
	public byte[] Payload { get; }
	public int PayloadBits { get; }

	public TcpFrame(TcpFrameKind kind, int sequence, byte[] payload, int payloadBits)
	{
		Kind = kind;
		Sequence = sequence;
		Payload = payload ?? Array.Empty<byte>();
		PayloadBits = payloadBits;
		if (payloadBits < 0 || payloadBits > Payload.Length * 8)
		{
			throw new ArgumentOutOfRangeException(nameof(payloadBits));
		}
	}
}

public static class TcpFrameCodec
{
	public const int ProtocolVersion = 32;
	public const int MaxPayloadBytes = 8 * 1024 * 1024;
	private const int BodyHeaderBytes = 9;

	public static byte[] Encode(TcpFrame frame)
	{
		if (frame == null) throw new ArgumentNullException(nameof(frame));
		if (frame.Payload.Length > MaxPayloadBytes)
		{
			throw new InvalidDataException("TCP frame payload is too large.");
		}
		int bodyLength = BodyHeaderBytes + frame.Payload.Length;
		byte[] bytes = new byte[4 + bodyLength];
		WriteInt32(bytes, 0, bodyLength);
		bytes[4] = (byte)frame.Kind;
		WriteInt32(bytes, 5, frame.Sequence);
		WriteInt32(bytes, 9, frame.PayloadBits);
		Buffer.BlockCopy(frame.Payload, 0, bytes, 13, frame.Payload.Length);
		return bytes;
	}

	public static TcpFrame Read(Stream stream)
	{
		byte[] lengthBytes = ReadExactly(stream, 4);
		int bodyLength = ReadInt32(lengthBytes, 0);
		ValidateBodyLength(bodyLength);
		return DecodeBody(ReadExactly(stream, bodyLength));
	}

	public static async Task<TcpFrame> ReadAsync(Stream stream, CancellationToken cancellationToken)
	{
		byte[] lengthBytes = await ReadExactlyAsync(stream, 4, cancellationToken).ConfigureAwait(false);
		int bodyLength = ReadInt32(lengthBytes, 0);
		ValidateBodyLength(bodyLength);
		return DecodeBody(await ReadExactlyAsync(stream, bodyLength, cancellationToken).ConfigureAwait(false));
	}

	public static async Task WriteAsync(Stream stream, TcpFrame frame, CancellationToken cancellationToken)
	{
		byte[] bytes = Encode(frame);
		await stream.WriteAsync(bytes, 0, bytes.Length, cancellationToken).ConfigureAwait(false);
	}

	public static TcpFrame DecodeBody(byte[] body)
	{
		if (body == null) throw new InvalidDataException("TCP frame body is null.");
		return DecodeBody(body, body.Length);
	}

	// 复用缓冲区版本：length 为本次有效字节数（调用方复用大缓冲时 body.Length 会远大于实际长度）
	public static TcpFrame DecodeBody(byte[] body, int length)
	{
		if (body == null || length < BodyHeaderBytes || length > body.Length)
		{
			throw new InvalidDataException("TCP frame body is truncated.");
		}
		TcpFrameKind kind = (TcpFrameKind)body[0];
		if (!Enum.IsDefined(typeof(TcpFrameKind), kind))
		{
			throw new InvalidDataException("Invalid TCP frame kind: " + body[0] + ".");
		}
		int sequence = ReadInt32(body, 1);
		int payloadBits = ReadInt32(body, 5);
		int payloadLength = length - BodyHeaderBytes;
		if (payloadBits < 0 || payloadBits > payloadLength * 8)
		{
			throw new InvalidDataException("Invalid TCP frame bit length: " + payloadBits + ".");
		}
		byte[] payload = new byte[payloadLength];
		Buffer.BlockCopy(body, BodyHeaderBytes, payload, 0, payloadLength);
		return new TcpFrame(kind, sequence, payload, payloadBits);
	}

	private static void ValidateBodyLength(int bodyLength)
	{
		if (bodyLength < BodyHeaderBytes || bodyLength > BodyHeaderBytes + MaxPayloadBytes)
		{
			throw new InvalidDataException("Invalid TCP frame length: " + bodyLength + ".");
		}
	}

	private static byte[] ReadExactly(Stream stream, int count)
	{
		byte[] buffer = new byte[count];
		int offset = 0;
		while (offset < count)
		{
			int read = stream.Read(buffer, offset, count - offset);
			if (read <= 0) throw new EndOfStreamException("TCP connection closed while reading a frame.");
			offset += read;
		}
		return buffer;
	}

	private static async Task<byte[]> ReadExactlyAsync(Stream stream, int count, CancellationToken cancellationToken)
	{
		byte[] buffer = new byte[count];
		int offset = 0;
		while (offset < count)
		{
			int read = await stream.ReadAsync(buffer, offset, count - offset, cancellationToken).ConfigureAwait(false);
			if (read <= 0) throw new EndOfStreamException("TCP connection closed while reading a frame.");
			offset += read;
		}
		return buffer;
	}

	private static void WriteInt32(byte[] buffer, int offset, int value)
	{
		buffer[offset] = (byte)value;
		buffer[offset + 1] = (byte)(value >> 8);
		buffer[offset + 2] = (byte)(value >> 16);
		buffer[offset + 3] = (byte)(value >> 24);
	}

	private static int ReadInt32(byte[] buffer, int offset)
	{
		return buffer[offset] |
			(buffer[offset + 1] << 8) |
			(buffer[offset + 2] << 16) |
			(buffer[offset + 3] << 24);
	}
}
