// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public enum TimeWarpOperation : byte
{
    Request = 0,
    Vote = 1,
    Response = 2,
    Applied = 3,
    Cancelled = 4,
    Notice = 5,
}

public sealed class TimeWarpPacket : INetData
{
    public TimeWarpOperation Operation { get; set; }
    public int VoteId { get; set; }
    public int RequesterId { get; set; } = -1;
    public string RequesterName { get; set; } = string.Empty;
    public double Multiplier { get; set; } = 1;
    public bool Approved { get; set; }
    public double WorldTime { get; set; }
    public int TimeoutSeconds { get; set; }
    public string Message { get; set; } = string.Empty;

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write((byte)Operation);
        message.Write(VoteId);
        message.Write(RequesterId);
        message.Write(RequesterName ?? string.Empty);
        message.Write(Multiplier);
        message.Write(Approved);
        message.Write(WorldTime);
        message.Write(TimeoutSeconds);
        message.Write(Message ?? string.Empty);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        Operation = (TimeWarpOperation)message.ReadByte();
        VoteId = message.ReadInt32();
        RequesterId = message.ReadInt32();
        RequesterName = message.ReadStringBounded();
        Multiplier = message.ReadDouble();
        Approved = message.ReadBoolean();
        WorldTime = message.ReadDouble();
        TimeoutSeconds = message.ReadInt32();
        Message = message.ReadStringBounded();
    }
}
