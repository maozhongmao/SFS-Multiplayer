// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace SfsMultiplayer.Protocol;

public sealed record AuthorityParticipant(int PlayerId, int ControlledRocketId);

public static class AuthorityAllocationPolicy
{
    public static Dictionary<int, int> Allocate(
        IEnumerable<int> rocketIds,
        IEnumerable<AuthorityParticipant> participants)
    {
        var orderedParticipants = participants
            .OrderBy(participant => participant.PlayerId)
            .ToArray();
        var assignments = new Dictionary<int, int>();
        if (orderedParticipants.Length == 0)
            return assignments;

        var roundRobin = 0;
        foreach (var rocketId in rocketIds.OrderBy(id => id))
        {
            var owner = orderedParticipants.FirstOrDefault(
                participant => participant.ControlledRocketId == rocketId)
                ?? orderedParticipants[roundRobin++ % orderedParticipants.Length];
            assignments[rocketId] = owner.PlayerId;
        }
        return assignments;
    }
}
