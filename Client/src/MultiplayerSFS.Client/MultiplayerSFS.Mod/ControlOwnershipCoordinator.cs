// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Mod;

public enum ControlRequestOrigin
{
    NativeSelection,
    TopologyCommit,
    UserAction,
}

public sealed class ControlOwnershipCoordinator
{
    public int ConfirmedRocketId { get; private set; } = -1;

    public int PendingRocketId { get; private set; } = -1;

    public ControlRequestOrigin? PendingOrigin { get; private set; }

    public bool Request(int rocketId, ControlRequestOrigin origin)
    {
        if (rocketId < 0 || rocketId == ConfirmedRocketId || rocketId == PendingRocketId)
        	return false;

        PendingRocketId = rocketId;
        PendingOrigin = origin;
        return true;
    }

    public bool ApplyServerConfirmation(int rocketId)
    {
        ConfirmedRocketId = rocketId;
        PendingRocketId = -1;
        PendingOrigin = null;
        return true;
    }

    public void Clear()
    {
        ConfirmedRocketId = -1;
        PendingRocketId = -1;
        PendingOrigin = null;
    }

    // 只清 Pending（超时/被拒绝时用）：保留 ConfirmedRocketId，玩家不至于被踢出当前火箭
    public void ClearPending()
    {
        PendingRocketId = -1;
        PendingOrigin = null;
    }

    public bool CanApplyNativeSelection(int selectedRocketId)
    {
        return selectedRocketId >= 0 && selectedRocketId == ConfirmedRocketId;
    }
}
