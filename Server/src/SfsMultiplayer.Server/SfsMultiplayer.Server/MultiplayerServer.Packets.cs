// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Server;

public sealed partial class MultiplayerServer
{
    private void HandlePacket(PacketType type, NetIncomingMessage message, ConnectedPlayer player)
    {
        switch (type)
        {
            case PacketType.UpdatePlayerControl:
                HandlePlayerControl(message, player);
                break;
            case PacketType.UpdatePlayerColor:
                HandlePlayerColor(message, player);
                break;
            case PacketType.SendChatMessage:
                HandleChat(message, player);
                break;
            case PacketType.CreateRocket:
                HandleCreateRocket(message, player);
                break;
            case PacketType.DestroyRocket:
                HandleDestroyRocket(message, player);
                break;
            case PacketType.UpdateRocketPrimary:
                HandleRocketPrimary(message, player);
                break;
            case PacketType.UpdateRocketSecondary:
                HandleRocketSecondary(message, player);
                break;
            case PacketType.DestroyPart:
                HandleDestroyPart(message, player);
                break;
            case PacketType.UpdateStaging:
                HandleStaging(message, player);
                break;
            case PacketType.UpdatePart_EngineModule:
                HandleEngine(message, player);
                break;
            case PacketType.UpdatePart_WheelModule:
                HandleWheel(message, player);
                break;
            case PacketType.UpdatePart_BoosterModule:
                HandleBooster(message, player);
                break;
            case PacketType.UpdatePart_ParachuteModule:
                HandleParachute(message, player);
                break;
            case PacketType.UpdatePart_MoveModule:
                HandleMove(message, player);
                break;
            case PacketType.UpdatePart_ResourceModule:
                HandleResource(message, player);
                break;
            case PacketType.JoinRequest:
            case PacketType.JoinResponse:
            case PacketType.PlayerConnected:
            case PacketType.PlayerDisconnected:
            case PacketType.UpdatePlayerAuthority:
            case PacketType.UpdateWorldTime:
                throw new InvalidDataException($"Packet {type} is server-only or invalid after joining.");
            default:
                throw new InvalidDataException($"Unhandled packet type: {type}.");
        }
    }

    private void HandlePlayerControl(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePlayerControlPacket>(message);
        if (packet.RocketId != -1)
        {
            if (!_world.Rockets.ContainsKey(packet.RocketId)) return;
            var alreadyControlled = _players.Values.Any(other =>
                other.Id != player.Id && other.ControlledRocket == packet.RocketId);
            if (alreadyControlled) return;
        }
        packet.PlayerId = player.Id;
        player.ControlledRocket = packet.RocketId;
        RefreshAuthorities();
        Broadcast(PacketType.UpdatePlayerControl, packet, message.SenderConnection);
    }

    private void HandlePlayerColor(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePlayerColorPacket>(message);
        packet.PlayerId = player.Id;
        packet.Color = ClampColor(packet.Color);
        player.Color = packet.Color;
        Broadcast(PacketType.UpdatePlayerColor, packet, message.SenderConnection);
    }

    private void HandleChat(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<SendChatMessagePacket>(message);
        var text = packet.Message.Trim();
        if (text.Length == 0 || text.Length > _settings.MaxChatMessageLength || text.Any(char.IsControl))
            return;
        var now = DateTime.UtcNow;
        if ((now - player.LastChatUtc).TotalSeconds < _settings.ChatMessageCooldown)
            return;
        player.LastChatUtc = now;
        packet.SenderId = player.Id;
        packet.Message = text;
        Broadcast(PacketType.SendChatMessage, packet, message.SenderConnection);
    }

    private void HandleCreateRocket(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<CreateRocketPacket>(message);
        ValidateRocket(packet.Rocket);
        if (packet.GlobalId >= 0 && _world.Rockets.ContainsKey(packet.GlobalId))
        {
            if (!CanUpdate(player, packet.GlobalId)) return;
            _world.Rockets[packet.GlobalId] = packet.Rocket;
            packet.WorldTime = WorldTime;
            Broadcast(PacketType.CreateRocket, packet, message.SenderConnection);
            return;
        }
        packet.GlobalId = NextRocketId();
        packet.WorldTime = WorldTime;
        _world.Rockets.Add(packet.GlobalId, packet.Rocket);
        player.UpdateAuthority.Add(packet.GlobalId);
        Broadcast(PacketType.CreateRocket, packet);
        if (!packet.ForLaunch) RefreshAuthorities();
    }

    private void HandleDestroyRocket(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<DestroyRocketPacket>(message);
        if (!CanUpdate(player, packet.RocketId) || !_world.Rockets.Remove(packet.RocketId)) return;
        packet.WorldTime = WorldTime;
        foreach (var connected in _players.Values)
        {
            if (connected.ControlledRocket == packet.RocketId) connected.ControlledRocket = -1;
            connected.UpdateAuthority.Remove(packet.RocketId);
        }
        Broadcast(PacketType.DestroyRocket, packet, message.SenderConnection);
        RefreshAuthorities();
    }

    private void HandleRocketPrimary(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdateRocketPrimaryPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        ValidateFinite(packet.Location, packet.Rotation, packet.AngularVelocity);
        packet.WorldTime = WorldTime;
        rocket.Apply(packet);
        Broadcast(PacketType.UpdateRocketPrimary, packet, message.SenderConnection,
            NetDeliveryMethod.UnreliableSequenced);
    }

    private void HandleRocketSecondary(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdateRocketSecondaryPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        if (!AllFinite(packet.InputTurn, packet.RawX, packet.RawY, packet.HorizontalX,
                packet.HorizontalY, packet.VerticalX, packet.VerticalY, packet.ThrottlePercent))
            throw new InvalidDataException("Rocket input contains a non-finite value.");
        packet.ThrottlePercent = Math.Clamp(packet.ThrottlePercent, 0, 1);
        packet.WorldTime = WorldTime;
        rocket.Apply(packet);
        Broadcast(PacketType.UpdateRocketSecondary, packet, message.SenderConnection,
            NetDeliveryMethod.UnreliableSequenced);
    }

    private void HandleDestroyPart(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<DestroyPartPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket) || !rocket.RemovePart(packet.PartId)) return;
        packet.WorldTime = WorldTime;
        Broadcast(PacketType.DestroyPart, packet, message.SenderConnection);
    }

    private void HandleStaging(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdateStagingPacket>(message);
        if (!TryAuthorizedRocket(player, packet.RocketId, out var rocket)) return;
        ValidateStages(packet.Stages, rocket);
        packet.WorldTime = WorldTime;
        rocket.Stages = packet.Stages;
        Broadcast(PacketType.UpdateStaging, packet, message.SenderConnection);
    }

    private void HandleEngine(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartEnginePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        packet.WorldTime = WorldTime; part.ToggleVariables["engine_on"] = packet.EngineOn;
        Broadcast(PacketType.UpdatePart_EngineModule, packet, message.SenderConnection);
    }

    private void HandleWheel(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartWheelPacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        packet.WorldTime = WorldTime; part.ToggleVariables["wheel_on"] = packet.WheelOn;
        Broadcast(PacketType.UpdatePart_WheelModule, packet, message.SenderConnection);
    }

    private void HandleBooster(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartBoosterPacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.Throttle, packet.FuelPercent)) throw new InvalidDataException("Booster state is non-finite.");
        packet.Throttle = Math.Clamp(packet.Throttle, 0, 1); packet.FuelPercent = Math.Clamp(packet.FuelPercent, 0, 1);
        packet.WorldTime = WorldTime; part.NumberVariables["fuel_percent"] = packet.FuelPercent;
        Broadcast(PacketType.UpdatePart_BoosterModule, packet, message.SenderConnection);
    }

    private void HandleParachute(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartParachutePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.State, packet.TargetState)) throw new InvalidDataException("Parachute state is non-finite.");
        packet.WorldTime = WorldTime; part.NumberVariables["animation_state"] = packet.State;
        part.NumberVariables["deploy_state"] = packet.TargetState;
        Broadcast(PacketType.UpdatePart_ParachuteModule, packet, message.SenderConnection);
    }

    private void HandleMove(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartMovePacket>(message);
        if (!TryAuthorizedPart(player, packet.RocketId, packet.PartId, out var part)) return;
        if (!AllFinite(packet.Time, packet.TargetTime)) throw new InvalidDataException("Move state is non-finite.");
        packet.WorldTime = WorldTime; part.NumberVariables["state"] = packet.Time;
        part.NumberVariables["state_target"] = packet.TargetTime;
        Broadcast(PacketType.UpdatePart_MoveModule, packet, message.SenderConnection);
    }

    private void HandleResource(NetIncomingMessage message, ConnectedPlayer player)
    {
        var packet = Read<UpdatePartResourcePacket>(message);
        if (!CanUpdate(player, packet.RocketId) || !double.IsFinite(packet.ResourcePercent)) return;
        if (!_world.Rockets.TryGetValue(packet.RocketId, out var rocket)) return;
        packet.ResourcePercent = Math.Clamp(packet.ResourcePercent, 0, 1);
        var found = false;
        foreach (var id in packet.PartIds)
            if (rocket.Parts.TryGetValue(id, out var part))
            { part.NumberVariables["fuel_percent"] = packet.ResourcePercent; found = true; }
        if (!found) return;
        packet.WorldTime = WorldTime;
        Broadcast(PacketType.UpdatePart_ResourceModule, packet, message.SenderConnection,
            NetDeliveryMethod.UnreliableSequenced);
    }

    private static T Read<T>(NetIncomingMessage message) where T : INetData, new()
    { var packet = new T(); packet.Deserialize(message); return packet; }
}
