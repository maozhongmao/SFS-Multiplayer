// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Text;

namespace MultiplayerSFS.Common;

// 登录前的版本探测。旧客户端不会发这个请求，旧服务端不会回这个帧，
// 所以两边都只表现为"探测不到版本"，不影响进服。
public static class ServerVersionCodec
{
    public const int MaxVersionStringBytes = 64;

    // 尾部可选 flags 的位定义。bit0 = 服务端要求连接密码。
    // 服务端未设置密码时不发这一位，客户端据此决定是否显示密码行。
    public const int ServerPolicyFlagPasswordRequired = 1;

    // 尾部 flags 占用的字节数。旧服务端不发，旧客户端读到会忽略。
    public const int FlagsBytes = 4;

    public static byte[] EncodeRequest()
    {
        return Array.Empty<byte>();
    }

    public static (int HandshakeVersion, int ProtocolVersion, string ServerVersion, int Flags) DecodeResponse(byte[] payload)
    {
        if (payload == null || payload.Length < 12)
            throw new InvalidDataException("Invalid server version payload.");

        int handshakeVersion = ReadInt32LittleEndian(payload, 0);
        int protocolVersion = ReadInt32LittleEndian(payload, 4);
        int length = ReadInt32LittleEndian(payload, 8);
        if (length < 0 || length > MaxVersionStringBytes || payload.Length < 12 + length)
            throw new InvalidDataException("Invalid server version string.");

        string serverVersion = Encoding.UTF8.GetString(payload, 12, length);

        // 尾部 flags 是可选的：旧服务端整段不发，这里给 0，行为与扩展前完全一致。
        int flags = 0;
        int flagsOffset = 12 + length;
        if (payload.Length >= flagsOffset + FlagsBytes)
            flags = ReadInt32LittleEndian(payload, flagsOffset);

        return (handshakeVersion, protocolVersion, serverVersion, flags);
    }

    public static byte[] EncodeResponse(int handshakeVersion, int protocolVersion, string serverVersion, int flags = 0)
    {
        serverVersion ??= string.Empty;
        byte[] bytes = Encoding.UTF8.GetBytes(serverVersion);
        if (bytes.Length > MaxVersionStringBytes)
            throw new ArgumentException("Server version string is too long.", nameof(serverVersion));

        byte[] payload = new byte[12 + bytes.Length + FlagsBytes];
        WriteInt32LittleEndian(payload, 0, handshakeVersion);
        WriteInt32LittleEndian(payload, 4, protocolVersion);
        WriteInt32LittleEndian(payload, 8, bytes.Length);
        Buffer.BlockCopy(bytes, 0, payload, 12, bytes.Length);
        WriteInt32LittleEndian(payload, 12 + bytes.Length, flags);
        return payload;
    }

    // 握手版本是服务端在 Hello 里硬校验的那个数，所以它才是真门槛；
    // 版本字符串前两位（1.2）给人看，也用来挡住明显不同代的客户端。
    public static bool IsCompatible(int serverHandshakeVersion, string serverVersion, int clientHandshakeVersion, string clientVersion, out string reason)
    {
        reason = null;
        if (serverHandshakeVersion != clientHandshakeVersion)
        {
            reason = "Handshake mismatch: server=" + serverHandshakeVersion + ", client=" + clientHandshakeVersion + ".";
            return false;
        }
        if (!string.IsNullOrEmpty(serverVersion) && MajorMinor(serverVersion) != MajorMinor(clientVersion))
        {
            reason = "Version mismatch: server=" + serverVersion + ", client=" + clientVersion + ".";
            return false;
        }
        return true;
    }

    private static string MajorMinor(string version)
    {
        string[] parts = (version ?? string.Empty).Split('.');
        return parts.Length >= 2 ? parts[0] + "." + parts[1] : version;
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
