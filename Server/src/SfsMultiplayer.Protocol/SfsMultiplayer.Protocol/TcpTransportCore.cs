// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Concurrent;
using System.Reflection;
using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

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
    // Optional pre-login discovery frames. They do not create a player session.
    ServerInfoRequest = 9,
    ServerInfoResponse = 10,
    // 登录页头部那行"服务端 V.. / 客户端 V.."靠它。旧客户端不发、旧服务端不回，
    // 两边都只是探测不到版本，不影响进服。
    ServerVersionRequest = 11,
    ServerVersionResponse = 12,
}

public sealed record TcpFrame(TcpFrameKind Kind, int Sequence, byte[] Payload, int PayloadBits);

public static class TcpFrameCodec
{
    public const int ProtocolVersion = 32;
    public const int MaxPayloadBytes = 8 * 1024 * 1024;
    private const int BodyHeaderBytes = 9;

    public static byte[] Encode(TcpFrame frame)
    {
        ArgumentNullException.ThrowIfNull(frame);
        var payload = frame.Payload ?? Array.Empty<byte>();
        if (payload.Length > MaxPayloadBytes) throw new InvalidDataException("TCP frame payload is too large.");
        if (frame.PayloadBits < 0 || frame.PayloadBits > payload.Length * 8)
            throw new InvalidDataException("TCP frame bit length is invalid.");
        var bodyLength = BodyHeaderBytes + payload.Length;
        var bytes = new byte[4 + bodyLength];
        WriteInt32(bytes, 0, bodyLength);
        bytes[4] = (byte)frame.Kind;
        WriteInt32(bytes, 5, frame.Sequence);
        WriteInt32(bytes, 9, frame.PayloadBits);
        Buffer.BlockCopy(payload, 0, bytes, 13, payload.Length);
        return bytes;
    }

    public static async ValueTask WriteAsync(Stream stream, TcpFrame frame, CancellationToken cancellationToken)
    {
        var bytes = Encode(frame);
        await stream.WriteAsync(bytes, cancellationToken).ConfigureAwait(false);
    }

    public static async ValueTask<TcpFrame> ReadAsync(Stream stream, CancellationToken cancellationToken)
    {
        var lengthBytes = await ReadExactlyAsync(stream, 4, cancellationToken).ConfigureAwait(false);
        var bodyLength = ReadInt32(lengthBytes, 0);
        if (bodyLength < BodyHeaderBytes || bodyLength > BodyHeaderBytes + MaxPayloadBytes)
            throw new InvalidDataException($"Invalid TCP frame length: {bodyLength}.");
        var body = await ReadExactlyAsync(stream, bodyLength, cancellationToken).ConfigureAwait(false);
        var kind = (TcpFrameKind)body[0];
        if (!Enum.IsDefined(kind)) throw new InvalidDataException($"Invalid TCP frame kind: {body[0]}.");
        var sequence = ReadInt32(body, 1);
        var payloadBits = ReadInt32(body, 5);
        var payloadLength = bodyLength - BodyHeaderBytes;
        if (payloadBits < 0 || payloadBits > payloadLength * 8)
            throw new InvalidDataException($"Invalid TCP frame bit length: {payloadBits}.");
        var payload = new byte[payloadLength];
        Buffer.BlockCopy(body, BodyHeaderBytes, payload, 0, payloadLength);
        return new TcpFrame(kind, sequence, payload, payloadBits);
    }

    private static async ValueTask<byte[]> ReadExactlyAsync(Stream stream, int count, CancellationToken cancellationToken)
    {
        var buffer = new byte[count];
        var offset = 0;
        while (offset < count)
        {
            var read = await stream.ReadAsync(buffer.AsMemory(offset, count - offset), cancellationToken).ConfigureAwait(false);
            if (read == 0) throw new EndOfStreamException("TCP connection closed while reading a frame.");
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

    private static int ReadInt32(byte[] buffer, int offset) =>
        buffer[offset] | buffer[offset + 1] << 8 | buffer[offset + 2] << 16 | buffer[offset + 3] << 24;
}

public readonly record struct NetPayload(byte[] Data, int BitLength);

public static class NetPayloadCodec
{
    public static NetPayload Serialize(INetData value)
    {
        var message = NewOutgoing();
        value.Serialize(message);
        return Copy(message);
    }

    public static NetPayload Serialize(PacketType type, INetData value)
    {
        var message = NewOutgoing();
        message.Write((byte)type);
        value.Serialize(message);
        return Copy(message);
    }

    public static T Deserialize<T>(byte[] data, int bitLength) where T : INetData, new()
    {
        var value = new T();
        value.Deserialize(ToIncoming(data, bitLength));
        return value;
    }

    public static NetIncomingMessage ToIncoming(byte[] data, int bitLength)
    {
        var message = (NetIncomingMessage?)Activator.CreateInstance(typeof(NetIncomingMessage), nonPublic: true)
            ?? throw new InvalidOperationException("Cannot create a Lidgren incoming message.");
        message.Data = data.ToArray();
        message.LengthBits = bitLength;
        message.Position = 0;
        return message;
    }

    private static NetOutgoingMessage NewOutgoing() =>
        (NetOutgoingMessage?)Activator.CreateInstance(typeof(NetOutgoingMessage), nonPublic: true)
        ?? throw new InvalidOperationException("Cannot create a Lidgren outgoing message.");

    private static NetPayload Copy(NetOutgoingMessage message) =>
        new(message.Data.AsSpan(0, message.LengthBytes).ToArray(), message.LengthBits);
}

public sealed class TcpSendQueue
{
    private readonly ConcurrentQueue<TcpFrame> _critical = new();
    private readonly ConcurrentDictionary<long, TcpFrame> _latest = new();
    private long _overwritten;

    public long OverwrittenStates => Interlocked.Read(ref _overwritten);
    public int Count => _critical.Count + _latest.Count;

    public void EnqueueCritical(TcpFrame frame) => _critical.Enqueue(frame);

    public void EnqueueLatest(long key, TcpFrame frame)
    {
        if (_latest.ContainsKey(key)) Interlocked.Increment(ref _overwritten);
        _latest[key] = frame;
    }

    public bool TryDequeue(out TcpFrame? frame)
    {
        if (_critical.TryDequeue(out frame)) return true;
        foreach (var pair in _latest)
            if (_latest.TryRemove(pair.Key, out frame)) return true;
        frame = null;
        return false;
    }
}
