// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Text;

namespace SfsMultiplayer.Protocol;

public static class SessionHandshakeCodec
{
    private const uint Magic = 0x31465353; // SFS1
    public const int Version = 2;
    private const int MaxStringBytes = 4096;

    public static byte[] EncodeHello(JoinRequestPacket request)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8);
        writer.Write(Magic);
        writer.Write(Version);
        WriteString(writer, request.Username);
        WriteString(writer, request.Password);
        WriteString(writer, request.SolarSystemName);
        writer.Write(request.ResumePlayerId);
        WriteString(writer, request.ResumeToken);
        return stream.ToArray();
    }

    public static JoinRequestPacket DecodeHello(byte[] payload)
    {
        using var stream = new MemoryStream(payload ?? Array.Empty<byte>(), false);
        using var reader = new BinaryReader(stream, Encoding.UTF8);
        Require(reader.ReadUInt32() == Magic, "Invalid handshake magic.");
        Require(reader.ReadInt32() == Version, "Unsupported handshake version.");
        var request = new JoinRequestPacket
        {
            Username = ReadString(reader),
            Password = ReadString(reader),
            SolarSystemName = ReadString(reader),
            ResumePlayerId = reader.ReadInt32(),
            ResumeToken = ReadString(reader),
        };
        Require(stream.Position == stream.Length, "Handshake request has trailing data.");
        return request;
    }

    public static byte[] EncodeAck(JoinResponsePacket response)
    {
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8);
        writer.Write(Magic);
        writer.Write(Version);
        writer.Write(response.PlayerId);
        writer.Write(response.UpdateRocketsPeriod);
        writer.Write(response.ChatMessageCooldown);
        writer.Write(response.WorldTime);
        writer.Write(response.SendTime);
        writer.Write((byte)response.Difficulty);
        WriteString(writer, response.SolarSystemName);
        WriteString(writer, response.UdpSessionToken);
        WriteString(writer, response.ResumeToken);
        return stream.ToArray();
    }

    public static JoinResponsePacket DecodeAck(byte[] payload)
    {
        using var stream = new MemoryStream(payload ?? Array.Empty<byte>(), false);
        using var reader = new BinaryReader(stream, Encoding.UTF8);
        Require(reader.ReadUInt32() == Magic, "Invalid handshake magic.");
        Require(reader.ReadInt32() == Version, "Unsupported handshake version.");
        var response = new JoinResponsePacket
        {
            PlayerId = reader.ReadInt32(),
            UpdateRocketsPeriod = reader.ReadDouble(),
            ChatMessageCooldown = reader.ReadDouble(),
            WorldTime = reader.ReadDouble(),
            SendTime = reader.ReadDouble(),
            Difficulty = (DifficultyType)reader.ReadByte(),
            SolarSystemName = ReadString(reader),
            UdpSessionToken = ReadString(reader),
            ResumeToken = ReadString(reader),
        };
        Require(stream.Position == stream.Length, "Handshake response has trailing data.");
        return response;
    }

    private static void WriteString(BinaryWriter writer, string? value)
    {
        var bytes = Encoding.UTF8.GetBytes(value ?? string.Empty);
        if (bytes.Length > MaxStringBytes) throw new InvalidDataException("Handshake string is too long.");
        writer.Write(bytes.Length);
        writer.Write(bytes);
    }

    private static string ReadString(BinaryReader reader)
    {
        var length = reader.ReadInt32();
        Require(length >= 0 && length <= MaxStringBytes, "Invalid handshake string length.");
        var bytes = reader.ReadBytes(length);
        Require(bytes.Length == length, "Handshake request ended early.");
        return Encoding.UTF8.GetString(bytes);
    }

    private static void Require(bool condition, string message)
    {
        if (!condition) throw new InvalidDataException(message);
    }
}
