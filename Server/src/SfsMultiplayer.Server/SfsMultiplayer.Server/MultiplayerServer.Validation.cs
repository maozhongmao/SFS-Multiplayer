// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Security.Cryptography;
using Lidgren.Network;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Server;

public sealed partial class MultiplayerServer
{
    private bool CanUpdate(ConnectedPlayer player, int rocketId) =>
        player.ControlledRocket == rocketId || player.UpdateAuthority.Contains(rocketId);

    private bool TryAuthorizedRocket(ConnectedPlayer player, int rocketId, out RocketState rocket)
    {
        if (CanUpdate(player, rocketId) && _world.Rockets.TryGetValue(rocketId, out rocket!)) return true;
        rocket = null!;
        return false;
    }

    private bool TryAuthorizedPart(ConnectedPlayer player, int rocketId, int partId, out PartState part)
    {
        if (TryAuthorizedRocket(player, rocketId, out var rocket) && rocket.Parts.TryGetValue(partId, out part!))
            return true;
        part = null!;
        return false;
    }

    private void RefreshAuthorities()
    {
        foreach (var player in _players.Values) player.UpdateAuthority.Clear();
        var connected = _players.Where(pair => pair.Key.Status == NetConnectionStatus.Connected)
            .OrderBy(pair => pair.Value.Id).ToList();
        if (connected.Count == 0) return;

        var roundRobin = 0;
        foreach (var rocketId in _world.Rockets.Keys.OrderBy(id => id))
        {
            var controller = connected.FirstOrDefault(pair => pair.Value.ControlledRocket == rocketId);
            var owner = controller.Value ?? connected[roundRobin++ % connected.Count].Value;
            owner.UpdateAuthority.Add(rocketId);
        }
        foreach (var pair in connected)
            Send(pair.Key, PacketType.UpdatePlayerAuthority,
                new UpdatePlayerAuthorityPacket { RocketIds = new HashSet<int>(pair.Value.UpdateAuthority) });
    }

    private int NextRocketId()
    {
        int id;
        do id = RandomNumberGenerator.GetInt32(1, int.MaxValue);
        while (_world.Rockets.ContainsKey(id));
        return id;
    }

    private static Color3 ClampColor(Color3 color)
    {
        if (!AllFinite(color.R, color.G, color.B)) throw new InvalidDataException("Color is non-finite.");
        return new Color3(Math.Clamp(color.R, 0, 1), Math.Clamp(color.G, 0, 1), Math.Clamp(color.B, 0, 1));
    }

    private static void ValidateFinite(NetLocation location, float rotation, float angularVelocity)
    {
        if (!double.IsFinite(location.X) || !double.IsFinite(location.Y) ||
            !double.IsFinite(location.Vx) || !double.IsFinite(location.Vy) ||
            !AllFinite(rotation, angularVelocity) ||
            string.IsNullOrWhiteSpace(location.Address) || location.Address.Length > 256 ||
            location.Address.Any(char.IsControl))
            throw new InvalidDataException("Rocket location contains invalid values.");
    }

    private static bool AllFinite(params float[] values) => values.All(float.IsFinite);

    private static void ValidateRocket(RocketState rocket)
    {
        if (rocket.RocketName.Length > 256 || rocket.RocketName.Any(char.IsControl))
            throw new InvalidDataException("Rocket name is invalid.");
        if (rocket.Parts.Count > NetMessageExtensions.MaxCollectionCount ||
            rocket.Joints.Count > NetMessageExtensions.MaxCollectionCount ||
            rocket.Stages.Count > NetMessageExtensions.MaxCollectionCount)
            throw new InvalidDataException("Rocket collections are too large.");
        ValidateFinite(rocket.Location, rocket.Rotation, rocket.AngularVelocity);
        if (!float.IsFinite(rocket.ThrottlePercent)) throw new InvalidDataException("Rocket throttle is non-finite.");
        foreach (var pair in rocket.Parts)
        {
            if (pair.Value.Name.Length > 256 || pair.Value.Name.Any(char.IsControl))
                throw new InvalidDataException("Part name is invalid.");
            if (!AllFinite(pair.Value.X, pair.Value.Y, pair.Value.OrientationX,
                    pair.Value.OrientationY, pair.Value.OrientationZ) ||
                (!float.IsFinite(pair.Value.Temperature) && !float.IsInfinity(pair.Value.Temperature)))
                throw new InvalidDataException("Part state contains invalid numbers.");
        }
        ValidateStages(rocket.Stages, rocket);
    }

    private static void ValidateStages(IEnumerable<StageState> stages, RocketState rocket)
    {
        foreach (var stage in stages)
        {
            if (stage.PartIds.Count > NetMessageExtensions.MaxCollectionCount)
                throw new InvalidDataException("Stage contains too many part IDs.");
            if (stage.PartIds.Any(id => !rocket.Parts.ContainsKey(id)))
                throw new InvalidDataException("Stage references an unknown part.");
        }
    }
}
