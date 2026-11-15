// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public interface INetData
{
    void Serialize(NetOutgoingMessage message);
    void Deserialize(NetIncomingMessage message);
}

public readonly record struct NetLocation(
    double X,
    double Y,
    double Vx,
    double Vy,
    string Address)
{
    public static NetLocation Empty => new(0, 0, 0, 0, "Earth");
}

public readonly record struct Color3(float R, float G, float B)
{
    public static Color3 Default => new(0.2f, 0.8f, 1.0f);
}

public enum DifficultyType : byte
{
    Normal = 0,
    Easy = 1,
    Hard = 2,
}

// This order is part of the wire protocol used by the archived 1.5 client.
public enum PacketType : byte
{
    JoinRequest = 0,
    JoinResponse = 1,
    PlayerConnected = 2,
    PlayerDisconnected = 3,
    UpdatePlayerControl = 4,
    UpdatePlayerAuthority = 5,
    UpdateWorldTime = 6,
    UpdatePlayerColor = 7,
    SendChatMessage = 8,
    CreateRocket = 9,
    DestroyRocket = 10,
    UpdateRocketPrimary = 11,
    UpdateRocketSecondary = 12,
    DestroyPart = 13,
    UpdateStaging = 14,
    UpdatePart_EngineModule = 15,
    UpdatePart_WheelModule = 16,
    UpdatePart_BoosterModule = 17,
    UpdatePart_ParachuteModule = 18,
    UpdatePart_MoveModule = 19,
    UpdatePart_ResourceModule = 20,
    ShowToastMessage = 21,
    DockTransaction = 23,
    TimeWarp = 24,
    ExperimentalAccess = 25,
    P2PPeerOffer = 26,
}

public static class NetMessageExtensions
{
    public const int MaxCollectionCount = 100_000;

    public static void Write(this NetOutgoingMessage message, INetData value)
    {
        value.Serialize(message);
    }


    public const int MaxStringLength = 1_000_000;

    public static void Write(this NetOutgoingMessage message, NetLocation value)
    {
        message.Write(value.X);
        message.Write(value.Y);
        message.Write(value.Vx);
        message.Write(value.Vy);
        message.Write(value.Address ?? string.Empty);
    }

    public static NetLocation ReadNetLocation(this NetIncomingMessage message)
    {
        return new NetLocation(
            message.ReadDouble(), message.ReadDouble(),
            message.ReadDouble(), message.ReadDouble(),
            ReadStringBounded(message));
    }

    public static void Write(this NetOutgoingMessage message, Color3 value)
    {
        message.Write(value.R);
        message.Write(value.G);
        message.Write(value.B);
    }

    public static Color3 ReadColor3(this NetIncomingMessage message)
    {
        return new Color3(message.ReadFloat(), message.ReadFloat(), message.ReadFloat());
    }

    public static void WriteCollection<T>(this NetOutgoingMessage message,
        IEnumerable<T> values, Action<T> writeItem)
    {
        var list = values.ToList();
        if (list.Count > MaxCollectionCount)
            throw new InvalidDataException("Collection is too large.");
        message.Write(list.Count);
        foreach (var item in list)
            writeItem(item);
    }

    public static int ReadCount(this NetIncomingMessage message)
    {
        var count = message.ReadInt32();
        if (count < 0 || count > MaxCollectionCount)
            throw new InvalidDataException($"Invalid collection count: {count}.");
        return count;
    }

    public static string ReadStringBounded(this NetIncomingMessage message)
    {
        var value = message.ReadString() ?? string.Empty;
        if (value.Length > MaxStringLength)
            throw new InvalidDataException("String is too long.");
        return value;
    }
}
