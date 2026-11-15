// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace SfsMultiplayer.Protocol;

public static class RocketLatencyCompensation
{
    public const double MaximumOneWaySeconds = 0.5;

    public static void Advance(UpdateRocketPrimaryPacket packet, double roundTripMilliseconds)
    {
        ArgumentNullException.ThrowIfNull(packet);
        if (!double.IsFinite(roundTripMilliseconds) || roundTripMilliseconds <= 0) return;

        var oneWaySeconds = Math.Min(roundTripMilliseconds / 2000.0, MaximumOneWaySeconds);
        var location = packet.Location;
        packet.Location = new NetLocation(
            location.X + location.Vx * oneWaySeconds,
            location.Y + location.Vy * oneWaySeconds,
            location.Vx,
            location.Vy,
            location.Address);
        packet.Rotation += packet.AngularVelocity * (float)oneWaySeconds;
        packet.WorldTime += oneWaySeconds;
    }
}
