// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace SfsMultiplayer.Protocol;

public static class ControlOwnershipPolicy
{
    public static int ResolveUndockControl(int currentRocketId, int retainedRocketId)
    {
        return currentRocketId == retainedRocketId ? retainedRocketId : currentRocketId;
    }
}
