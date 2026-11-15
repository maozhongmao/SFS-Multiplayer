// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public sealed class DestroyPartPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public bool CreateExplosion { get; set; }
    public byte Reason { get; set; }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(PartId);
        message.Write(CreateExplosion); message.Write(Reason);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32();
        CreateExplosion = message.ReadBoolean(); Reason = message.ReadByte();
    }
}

public sealed class UpdateStagingPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public List<StageState> Stages { get; set; } = new();

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId);
        message.WriteCollection(Stages, stage => message.Write(stage));
    }

    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32();
        Stages = new List<StageState>();
        var stageCount = message.ReadCount();
        for (var i = 0; i < stageCount; i++)
        {
            var stage = new StageState(); stage.Deserialize(message); Stages.Add(stage);
        }
    }
}

public sealed class UpdatePartEnginePacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public bool EngineOn { get; set; }
    public void Serialize(NetOutgoingMessage message)
    { message.Write(WorldTime); message.Write(RocketId); message.Write(PartId); message.Write(EngineOn); }
    public void Deserialize(NetIncomingMessage message)
    { WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32(); EngineOn = message.ReadBoolean(); }
}

public sealed class UpdatePartWheelPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public bool WheelOn { get; set; }
    public void Serialize(NetOutgoingMessage message)
    { message.Write(WorldTime); message.Write(RocketId); message.Write(PartId); message.Write(WheelOn); }
    public void Deserialize(NetIncomingMessage message)
    { WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32(); WheelOn = message.ReadBoolean(); }
}

public sealed class UpdatePartBoosterPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public bool Primed { get; set; }
    public float Throttle { get; set; }
    public float FuelPercent { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(PartId);
        message.Write(Primed); message.Write(Throttle); message.Write(FuelPercent);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32();
        Primed = message.ReadBoolean(); Throttle = message.ReadFloat(); FuelPercent = message.ReadFloat();
    }
}

public sealed class UpdatePartParachutePacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public float State { get; set; }
    public float TargetState { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(PartId);
        message.Write(State); message.Write(TargetState);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32();
        State = message.ReadFloat(); TargetState = message.ReadFloat();
    }
}

public sealed class UpdatePartMovePacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public int PartId { get; set; } = -1;
    public float Time { get; set; }
    public float TargetTime { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(PartId);
        message.Write(Time); message.Write(TargetTime);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); PartId = message.ReadInt32();
        Time = message.ReadFloat(); TargetTime = message.ReadFloat();
    }
}

public sealed class UpdatePartResourcePacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public double ResourcePercent { get; set; }
    public HashSet<int> PartIds { get; set; } = new();
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(ResourcePercent);
        message.WriteCollection(PartIds, message.Write);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); ResourcePercent = message.ReadDouble();
        PartIds = new HashSet<int>();
        var partCount = message.ReadCount();
        for (var i = 0; i < partCount; i++) PartIds.Add(message.ReadInt32());
    }
}
