// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Text.Json.Serialization;
using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public sealed record BurnMarkState(
    float Angle,
    float Intensity,
    float X,
    string Top,
    string Bottom);

public sealed class PartState : INetData
{
    public string Name { get; set; } = string.Empty;
    public float X { get; set; }
    public float Y { get; set; }
    public float OrientationX { get; set; } = 1;
    public float OrientationY { get; set; } = 1;
    public float OrientationZ { get; set; }
    public float Temperature { get; set; } = float.NegativeInfinity;
    [JsonInclude]
    public Dictionary<string, double> NumberVariables { get; private set; } = new(StringComparer.Ordinal);
    [JsonInclude]
    public Dictionary<string, bool> ToggleVariables { get; private set; } = new(StringComparer.Ordinal);
    [JsonInclude]
    public Dictionary<string, string> TextVariables { get; private set; } = new(StringComparer.Ordinal);
    public BurnMarkState? Burns { get; set; }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(Name);
        message.Write(X); message.Write(Y);
        message.Write(OrientationX); message.Write(OrientationY); message.Write(OrientationZ);
        message.Write(Temperature);
        message.WriteCollection(NumberVariables, pair =>
        { message.Write(pair.Key); message.Write(pair.Value); });
        message.WriteCollection(ToggleVariables, pair =>
        { message.Write(pair.Key); message.Write(pair.Value); });
        message.WriteCollection(TextVariables, pair =>
        { message.Write(pair.Key); message.Write(pair.Value); });
        message.Write(Burns is null);
        if (Burns is { } burn)
        {
            message.Write(burn.Angle); message.Write(burn.Intensity); message.Write(burn.X);
            message.Write(burn.Top); message.Write(burn.Bottom);
        }
    }

    public void Deserialize(NetIncomingMessage message)
    {
        Name = message.ReadStringBounded();
        X = message.ReadFloat(); Y = message.ReadFloat();
        OrientationX = message.ReadFloat(); OrientationY = message.ReadFloat(); OrientationZ = message.ReadFloat();
        Temperature = message.ReadFloat();
        NumberVariables.Clear();
        var numberVariableCount = message.ReadCount();
        for (var i = 0; i < numberVariableCount; i++)
            NumberVariables.Add(message.ReadStringBounded(), message.ReadDouble());
        ToggleVariables.Clear();
        var toggleVariableCount = message.ReadCount();
        for (var i = 0; i < toggleVariableCount; i++)
            ToggleVariables.Add(message.ReadStringBounded(), message.ReadBoolean());
        TextVariables.Clear();
        var textVariableCount = message.ReadCount();
        for (var i = 0; i < textVariableCount; i++)
            TextVariables.Add(message.ReadStringBounded(), message.ReadStringBounded());
        Burns = message.ReadBoolean()
            ? null
            : new BurnMarkState(message.ReadFloat(), message.ReadFloat(), message.ReadFloat(),
                message.ReadStringBounded(), message.ReadStringBounded());
    }
}

public sealed class JointState : INetData
{
    public int PartA { get; set; }
    public int PartB { get; set; }

    public JointState() { }
    public JointState(int partA, int partB) { PartA = partA; PartB = partB; }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(PartA); message.Write(PartB);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        PartA = message.ReadInt32(); PartB = message.ReadInt32();
    }
}

public sealed class StageState : INetData
{
    public int StageId { get; set; }
    public List<int> PartIds { get; set; } = new();

    public StageState() { }
    public StageState(int stageId, IEnumerable<int> partIds)
    { StageId = stageId; PartIds = partIds.ToList(); }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(StageId);
        message.WriteCollection(PartIds, message.Write);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        StageId = message.ReadInt32();
        PartIds = new List<int>();
        var partCount = message.ReadCount();
        for (var i = 0; i < partCount; i++)
            PartIds.Add(message.ReadInt32());
    }
}

public sealed class RocketState : INetData
{
    public string RocketName { get; set; } = string.Empty;
    public NetLocation Location { get; set; } = NetLocation.Empty;
    public float Rotation { get; set; }
    public float AngularVelocity { get; set; }
    public bool ThrottleOn { get; set; }
    public float ThrottlePercent { get; set; }
    public bool Rcs { get; set; }
    [JsonInclude]
    public Dictionary<int, PartState> Parts { get; private set; } = new();
    public List<JointState> Joints { get; set; } = new();
    public List<StageState> Stages { get; set; } = new();

    // Alias retained for callers that use the game's original capitalization.
    public bool RCS { get => Rcs; set => Rcs = value; }

    public void Apply(UpdateRocketPrimaryPacket packet)
    {
        Location = packet.Location;
        Rotation = packet.Rotation;
        AngularVelocity = packet.AngularVelocity;
    }

    public void Apply(UpdateRocketSecondaryPacket packet)
    {
        ThrottlePercent = packet.ThrottlePercent;
        ThrottleOn = packet.ThrottleOn;
        Rcs = packet.Rcs;
    }

    public bool RemovePart(int partId)
    {
        Joints.RemoveAll(j => j.PartA == partId || j.PartB == partId);
        foreach (var stage in Stages)
            stage.PartIds.RemoveAll(id => id == partId);
        return Parts.Remove(partId);
    }

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(RocketName);
        message.Write(Location);
        message.Write(Rotation); message.Write(AngularVelocity);
        message.Write(ThrottleOn); message.Write(ThrottlePercent); message.Write(Rcs);
        message.WriteCollection(Parts, pair =>
        { message.Write(pair.Key); message.Write(pair.Value); });
        message.WriteCollection(Joints, joint => message.Write(joint));
        message.WriteCollection(Stages, stage => message.Write(stage));
    }

    public void Deserialize(NetIncomingMessage message)
    {
        RocketName = message.ReadStringBounded();
        Location = message.ReadNetLocation();
        Rotation = message.ReadFloat(); AngularVelocity = message.ReadFloat();
        ThrottleOn = message.ReadBoolean(); ThrottlePercent = message.ReadFloat(); Rcs = message.ReadBoolean();
        Parts.Clear();
        var partCount = message.ReadCount();
        for (var i = 0; i < partCount; i++)
        {
            var id = message.ReadInt32();
            Parts.Add(id, ReadPart(message));
        }
        Joints = new List<JointState>();
        var jointCount = message.ReadCount();
        for (var i = 0; i < jointCount; i++)
            Joints.Add(ReadJoint(message));
        Stages = new List<StageState>();
        var stageCount = message.ReadCount();
        for (var i = 0; i < stageCount; i++)
            Stages.Add(ReadStage(message));
    }

    private static PartState ReadPart(NetIncomingMessage message)
    { var value = new PartState(); value.Deserialize(message); return value; }
    private static JointState ReadJoint(NetIncomingMessage message)
    { var value = new JointState(); value.Deserialize(message); return value; }
    private static StageState ReadStage(NetIncomingMessage message)
    { var value = new StageState(); value.Deserialize(message); return value; }
}

public sealed class WorldSnapshot
{
    public double WorldTime { get; set; }
    public DifficultyType Difficulty { get; set; } = DifficultyType.Normal;
    [JsonInclude]
    public Dictionary<int, RocketState> Rockets { get; private set; } = new();
}
