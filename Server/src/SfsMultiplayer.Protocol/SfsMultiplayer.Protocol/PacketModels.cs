// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public sealed class JoinRequestPacket : INetData
{
    public string Username { get; set; } = string.Empty;
    public string Password { get; set; } = string.Empty;
    public string SolarSystemName { get; set; } = string.Empty;
    public int ResumePlayerId { get; set; } = -1;
    public string ResumeToken { get; set; } = string.Empty;
    public void Serialize(NetOutgoingMessage message)
    { message.Write(Username); message.Write(Password); message.Write(SolarSystemName); }
    public void Deserialize(NetIncomingMessage message)
    { Username = message.ReadStringBounded(); Password = message.ReadStringBounded(); SolarSystemName = message.ReadStringBounded(); }
}

public sealed class JoinResponsePacket : INetData
{
    public int PlayerId { get; set; } = -1;
    public double UpdateRocketsPeriod { get; set; } = 20;
    public double ChatMessageCooldown { get; set; } = 3;
    public double WorldTime { get; set; }
    public double SendTime { get; set; }
    public DifficultyType Difficulty { get; set; } = DifficultyType.Normal;
    public string SolarSystemName { get; set; } = string.Empty;
    public string UdpSessionToken { get; set; } = string.Empty;
    public string ResumeToken { get; set; } = string.Empty;
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(PlayerId); message.Write(UpdateRocketsPeriod);
        message.Write(ChatMessageCooldown); message.Write(WorldTime); message.Write(SendTime);
        message.Write((byte)Difficulty); message.Write(SolarSystemName); message.Write(UdpSessionToken);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        PlayerId = message.ReadInt32(); UpdateRocketsPeriod = message.ReadDouble();
        ChatMessageCooldown = message.ReadDouble(); WorldTime = message.ReadDouble(); SendTime = message.ReadDouble();
        Difficulty = (DifficultyType)message.ReadByte(); SolarSystemName = message.ReadStringBounded();
        UdpSessionToken = message.ReadStringBounded();
    }
}

public sealed class PlayerConnectedPacket : INetData
{
    public int PlayerId { get; set; } = -1;
    public string Username { get; set; } = string.Empty;
    public Color3 IconColor { get; set; } = Color3.Default;
    public bool PrintMessage { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(PlayerId); message.Write(Username); message.Write(IconColor); message.Write(PrintMessage);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        PlayerId = message.ReadInt32(); Username = message.ReadStringBounded();
        IconColor = message.ReadColor3(); PrintMessage = message.ReadBoolean();
    }
}

public sealed class PlayerDisconnectedPacket : INetData
{
    public int PlayerId { get; set; } = -1;
    public void Serialize(NetOutgoingMessage message) => message.Write(PlayerId);
    public void Deserialize(NetIncomingMessage message) => PlayerId = message.ReadInt32();
}

public sealed class UpdatePlayerControlPacket : INetData
{
    public int PlayerId { get; set; } = -1;
    public int RocketId { get; set; } = -1;
    public void Serialize(NetOutgoingMessage message)
    { message.Write(PlayerId); message.Write(RocketId); }
    public void Deserialize(NetIncomingMessage message)
    { PlayerId = message.ReadInt32(); RocketId = message.ReadInt32(); }
}

public sealed class UpdatePlayerAuthorityPacket : INetData
{
    public HashSet<int> RocketIds { get; set; } = new();
    public void Serialize(NetOutgoingMessage message) => message.WriteCollection(RocketIds, message.Write);
    public void Deserialize(NetIncomingMessage message)
    {
        RocketIds = new HashSet<int>();
        var rocketCount = message.ReadCount();
        for (var i = 0; i < rocketCount; i++) RocketIds.Add(message.ReadInt32());
    }
}

public sealed class UpdateWorldTimePacket : INetData
{
    public double WorldTime { get; set; }
    public void Serialize(NetOutgoingMessage message) => message.Write(WorldTime);
    public void Deserialize(NetIncomingMessage message) => WorldTime = message.ReadDouble();
}

public sealed class UpdatePlayerColorPacket : INetData
{
    public int PlayerId { get; set; } = -1;
    public Color3 Color { get; set; } = Color3.Default;
    public void Serialize(NetOutgoingMessage message)
    { message.Write(PlayerId); message.Write(Color); }
    public void Deserialize(NetIncomingMessage message)
    { PlayerId = message.ReadInt32(); Color = message.ReadColor3(); }
}

public sealed class SendChatMessagePacket : INetData
{
    public int SenderId { get; set; } = -1;
    public string Message { get; set; } = string.Empty;
    public void Serialize(NetOutgoingMessage message)
    { message.Write(SenderId); message.Write(Message); }
    public void Deserialize(NetIncomingMessage message)
    { SenderId = message.ReadInt32(); Message = message.ReadStringBounded(); }
}

public sealed class CreateRocketPacket : INetData
{
    public double WorldTime { get; set; }
    public int LocalId { get; set; } = -1;
    public int GlobalId { get; set; } = -1;
    public bool ForLaunch { get; set; }
    public RocketState Rocket { get; set; } = new();
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(LocalId); message.Write(GlobalId); message.Write(ForLaunch); message.Write(Rocket);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); LocalId = message.ReadInt32(); GlobalId = message.ReadInt32(); ForLaunch = message.ReadBoolean();
        Rocket = new RocketState(); Rocket.Deserialize(message);
    }
}

public sealed class DestroyRocketPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public byte Reason { get; set; }
    public void Serialize(NetOutgoingMessage message)
    { message.Write(WorldTime); message.Write(RocketId); message.Write(Reason); }
    public void Deserialize(NetIncomingMessage message)
    { WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); Reason = message.ReadByte(); }
}

public sealed class UpdateRocketPrimaryPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public NetLocation Location { get; set; } = NetLocation.Empty;
    public float Rotation { get; set; }
    public float AngularVelocity { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(Location); message.Write(Rotation); message.Write(AngularVelocity);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); Location = message.ReadNetLocation();
        Rotation = message.ReadFloat(); AngularVelocity = message.ReadFloat();
    }
}

public sealed class UpdateRocketSecondaryPacket : INetData
{
    public double WorldTime { get; set; }
    public int RocketId { get; set; } = -1;
    public float InputTurn { get; set; }
    public float RawX { get; set; }
    public float RawY { get; set; }
    public float HorizontalX { get; set; }
    public float HorizontalY { get; set; }
    public float VerticalX { get; set; }
    public float VerticalY { get; set; }
    public float ThrottlePercent { get; set; }
    public bool ThrottleOn { get; set; }
    public bool Rcs { get; set; }
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(WorldTime); message.Write(RocketId); message.Write(InputTurn);
        message.Write(RawX); message.Write(RawY); message.Write(HorizontalX); message.Write(HorizontalY);
        message.Write(VerticalX); message.Write(VerticalY); message.Write(ThrottlePercent);
        message.Write(ThrottleOn); message.Write(Rcs);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        WorldTime = message.ReadDouble(); RocketId = message.ReadInt32(); InputTurn = message.ReadFloat();
        RawX = message.ReadFloat(); RawY = message.ReadFloat(); HorizontalX = message.ReadFloat(); HorizontalY = message.ReadFloat();
        VerticalX = message.ReadFloat(); VerticalY = message.ReadFloat(); ThrottlePercent = message.ReadFloat();
        ThrottleOn = message.ReadBoolean(); Rcs = message.ReadBoolean();
    }
}

// Player event toast: reuses the client's existing ShowToastMessage (PacketType 21) channel.
// Serialization mirrors Packet_UpdateRocketPrimary so server code can read/write it uniformly.
public class PlayerEventToastPacket : INetData
{
    public string Message = string.Empty;
    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(Message ?? string.Empty);
    }
    public void Deserialize(NetIncomingMessage message)
    {
        Message = message.ReadString();
    }
}
