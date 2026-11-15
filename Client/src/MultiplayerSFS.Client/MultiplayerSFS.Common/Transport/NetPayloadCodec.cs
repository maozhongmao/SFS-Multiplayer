// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Linq;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

public sealed class NetPayload
{
	public byte[] Data { get; }
	public int BitLength { get; }

	public NetPayload(byte[] data, int bitLength)
	{
		Data = data;
		BitLength = bitLength;
	}
}

public static class NetPayloadCodec
{
	public static NetPayload Serialize(INetData value)
	{
		NetOutgoingMessage outgoing = NewOutgoing();
		value.Serialize(outgoing);
		return Copy(outgoing);
	}

	public static NetPayload Serialize(Packet packet, bool includePacketType)
	{
		NetOutgoingMessage outgoing = NewOutgoing();
		if (includePacketType) outgoing.Write((byte)packet.Type);
		packet.Serialize(outgoing);
		return Copy(outgoing);
	}

	public static T Deserialize<T>(byte[] data, int bitLength) where T : INetData, new()
	{
		T value = new T();
		value.Deserialize(ToIncoming(data, bitLength));
		return value;
	}

	public static NetIncomingMessage ToIncoming(byte[] data, int bitLength)
	{
		NetIncomingMessage incoming = (NetIncomingMessage)Activator.CreateInstance(typeof(NetIncomingMessage), true);
		incoming.Data = data.ToArray();
		incoming.LengthBits = bitLength;
		incoming.Position = 0;
		return incoming;
	}

	private static NetOutgoingMessage NewOutgoing()
	{
		return (NetOutgoingMessage)Activator.CreateInstance(typeof(NetOutgoingMessage), true);
	}

	private static NetPayload Copy(NetOutgoingMessage outgoing)
	{
		byte[] data = new byte[outgoing.LengthBytes];
		Buffer.BlockCopy(outgoing.Data, 0, data, 0, data.Length);
		return new NetPayload(data, outgoing.LengthBits);
	}
}
