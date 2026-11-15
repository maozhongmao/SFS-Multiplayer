// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public enum DockTransactionOperation : byte
{
    Dock = 0,
    Undock = 1,
}

public sealed class DockTransactionPacket : INetData
{
    public int TransactionId { get; set; }
    public DockTransactionOperation Operation { get; set; }
    public bool Committed { get; set; }
    public int KeepRocketId { get; set; } = -1;
    public int RemoveRocketId { get; set; } = -1;
    public int KeepPartId { get; set; } = -1;
    public int RemovePartId { get; set; } = -1;
    public double WorldTime { get; set; }
    public RocketState? MergedRocket { get; set; }
    public int SecondRocketId { get; set; } = -1;
    public RocketState? SecondRocket { get; set; }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(TransactionId);
        message.Write((byte)Operation);
        message.Write(Committed);
        message.Write(KeepRocketId);
        message.Write(RemoveRocketId);
        message.Write(KeepPartId);
        message.Write(RemovePartId);
        message.Write(WorldTime);
        if (Committed)
        {
            message.Write(MergedRocket ?? throw new InvalidDataException("Committed docking transaction requires a rocket."));
            message.Write(SecondRocketId);
            message.Write(SecondRocket is not null);
            if (SecondRocket is not null) message.Write(SecondRocket);
        }
    }

    public void Deserialize(NetIncomingMessage message)
    {
        TransactionId = message.ReadInt32();
        Operation = (DockTransactionOperation)message.ReadByte();
        Committed = message.ReadBoolean();
        KeepRocketId = message.ReadInt32();
        RemoveRocketId = message.ReadInt32();
        KeepPartId = message.ReadInt32();
        RemovePartId = message.ReadInt32();
        WorldTime = message.ReadDouble();
        if (Committed)
        {
            MergedRocket = new RocketState();
            MergedRocket.Deserialize(message);
            SecondRocketId = message.ReadInt32();
            if (message.ReadBoolean())
            {
                SecondRocket = new RocketState();
                SecondRocket.Deserialize(message);
            }
        }
    }
}
