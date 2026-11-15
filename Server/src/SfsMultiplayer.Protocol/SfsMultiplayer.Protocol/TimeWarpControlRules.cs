// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace SfsMultiplayer.Protocol;

public static class TimeWarpControlRules
{
    public const double MinimumMultiplier = 1.0;
    public const double MaximumMultiplier = 2500.0;
    public const double MultiplayerMinimumMultiplier = 1.0;
    public const double MultiplayerMaximumMultiplier = 5.0;

    public static bool CanSet(int controllingPlayers, double multiplier)
    {
        if (!double.IsFinite(multiplier) || multiplier < MinimumMultiplier || multiplier > MaximumMultiplier)
            return false;
        return multiplier == 1.0 || controllingPlayers == 1;
    }

    public static bool CanSetPersonal(int onlinePlayers, double multiplier)
    {
        return onlinePlayers > 1 && double.IsFinite(multiplier) &&
            multiplier >= MultiplayerMinimumMultiplier && multiplier <= MultiplayerMaximumMultiplier;
    }
}
