// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Text;
using SFS.WorldBase;

namespace MultiplayerSFS.Common;

public static class SessionHandshakeCodec
{
	private const uint Magic = 0x31465353; // SFS1
	public const int Version = 2;
	private const int MaxStringBytes = 4096;

	public static byte[] EncodeHello(Packet_JoinRequest request)
	{
		using (var stream = new MemoryStream())
		using (var writer = new BinaryWriter(stream, Encoding.UTF8))
		{
			writer.Write(Magic);
			writer.Write(Version);
			WriteString(writer, request.Username);
			WriteString(writer, request.Password);
			WriteString(writer, request.SolarSystemName);
			writer.Write(request.ResumePlayerId);
			WriteString(writer, request.ResumeToken);
			return stream.ToArray();
		}
	}

	public static Packet_JoinResponse DecodeAck(byte[] payload)
	{
		using (var stream = new MemoryStream(payload ?? Array.Empty<byte>(), false))
		using (var reader = new BinaryReader(stream, Encoding.UTF8))
		{
			Require(reader.ReadUInt32() == Magic, "Invalid handshake magic.");
			Require(reader.ReadInt32() == Version, "Unsupported handshake version.");
			var response = new Packet_JoinResponse
			{
				PlayerId = reader.ReadInt32(),
				UpdateRocketsPeriod = reader.ReadDouble(),
				ChatMessageCooldown = reader.ReadDouble(),
				WorldTime = reader.ReadDouble(),
				SendTime = reader.ReadDouble(),
				Difficulty = (Difficulty.DifficultyType)reader.ReadByte(),
				SolarSystemName = ReadString(reader),
				UdpSessionToken = ReadString(reader),
				ResumeToken = ReadString(reader)
			};
			if (stream.Position != stream.Length)
				throw new InvalidDataException($"Handshake response has trailing data: position={stream.Position}, length={stream.Length}, remaining={stream.Length - stream.Position}.");
			return response;
		}
	}

	private static void WriteString(BinaryWriter writer, string value)
	{
		byte[] bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
		if (bytes.Length > MaxStringBytes) throw new InvalidDataException("Handshake string is too long.");
		writer.Write(bytes.Length);
		writer.Write(bytes);
	}

	private static string ReadString(BinaryReader reader)
	{
		int length = reader.ReadInt32();
		Require(length >= 0 && length <= MaxStringBytes, "Invalid handshake string length.");
		byte[] bytes = reader.ReadBytes(length);
		Require(bytes.Length == length, "Handshake response ended early.");
		return Encoding.UTF8.GetString(bytes);
	}

	private static void Require(bool condition, string message)
	{
		if (!condition) throw new InvalidDataException(message);
	}
}
