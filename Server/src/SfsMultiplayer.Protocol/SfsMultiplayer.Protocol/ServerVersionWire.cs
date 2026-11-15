// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Text;

namespace SfsMultiplayer.Protocol;

// 登录前的版本探测载荷，与客户端 MultiplayerSFS.Common.ServerVersionCodec、
// C++ 服务端 protocol/server_version.hpp 三者必须逐字节一致。
//
// 载荷：[握手版本 int32][协议版本 int32][版本字符串字节数 int32][版本字符串 UTF-8]，全小端。
// 握手版本就是 Hello 帧里服务端硬校验的那个数，所以它才是真正的兼容门槛；
// 版本字符串给人看，客户端拿它比前两位（1.2）。
public static class ServerVersionWire
{
    public const int MaxVersionStringBytes = 64;

    public static byte[] EncodeResponse(int handshakeVersion, int protocolVersion, string serverVersion)
    {
        serverVersion ??= string.Empty;
        var bytes = Encoding.UTF8.GetBytes(serverVersion);
        if (bytes.Length > MaxVersionStringBytes)
            throw new ArgumentOutOfRangeException(nameof(serverVersion), "Server version string is too long.");

        var payload = new byte[12 + bytes.Length];
        WriteInt32LittleEndian(payload, 0, handshakeVersion);
        WriteInt32LittleEndian(payload, 4, protocolVersion);
        WriteInt32LittleEndian(payload, 8, bytes.Length);
        Buffer.BlockCopy(bytes, 0, payload, 12, bytes.Length);
        return payload;
    }

    public static (int HandshakeVersion, int ProtocolVersion, string ServerVersion) DecodeResponse(byte[] payload)
    {
        if (payload == null || payload.Length < 12)
            throw new InvalidDataException("Invalid server version payload.");

        int handshakeVersion = ReadInt32LittleEndian(payload, 0);
        int protocolVersion = ReadInt32LittleEndian(payload, 4);
        int length = ReadInt32LittleEndian(payload, 8);
        if (length < 0 || length > MaxVersionStringBytes || payload.Length < 12 + length)
            throw new InvalidDataException("Invalid server version string.");

        return (handshakeVersion, protocolVersion, Encoding.UTF8.GetString(payload, 12, length));
    }

    private static int ReadInt32LittleEndian(byte[] buffer, int offset)
    {
        return buffer[offset]
            | (buffer[offset + 1] << 8)
            | (buffer[offset + 2] << 16)
            | (buffer[offset + 3] << 24);
    }

    private static void WriteInt32LittleEndian(byte[] buffer, int offset, int value)
    {
        buffer[offset] = (byte)value;
        buffer[offset + 1] = (byte)(value >> 8);
        buffer[offset + 2] = (byte)(value >> 16);
        buffer[offset + 3] = (byte)(value >> 24);
    }
}
