// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Tests;

public sealed class TimeWarpControlRuleTests
{
    [Fact]
    public void AllowsFiniteMultiplierInsideRangeWhenExactlyOnePlayerControls()
    {
        Assert.True(TimeWarpControlRules.CanSet(1, 12.5));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2)]
    public void RejectsAccelerationUnlessExactlyOnePlayerControls(int controllers)
    {
        Assert.False(TimeWarpControlRules.CanSet(controllers, 12.5));
    }

    [Theory]
    [InlineData(1)]
    [InlineData(0)]
    [InlineData(2)]
    public void AllowsOneXRegardlessOfControllerCount(int controllers)
    {
        Assert.True(TimeWarpControlRules.CanSet(controllers, 1));
    }

    [Theory]
    [InlineData(0)]
    [InlineData(2500.01)]
    [InlineData(double.PositiveInfinity)]
    public void RejectsMultiplierOutsideRange(double multiplier)
    {
        Assert.False(TimeWarpControlRules.CanSet(1, multiplier));
    }

    [Theory]
    [InlineData(2, 1)]
    [InlineData(2, 3)]
    [InlineData(8, 5)]
    public void AllowsPersonalMultiplierOnlyForMultiplePlayers(int onlinePlayers, double multiplier)
    {
        Assert.True(TimeWarpControlRules.CanSetPersonal(onlinePlayers, multiplier));
    }

    [Theory]
    [InlineData(1, 3)]
    [InlineData(2, 5.01)]
    [InlineData(2, 0.5)]
    public void RejectsPersonalMultiplierOutsideMultiplayerRange(int onlinePlayers, double multiplier)
    {
        Assert.False(TimeWarpControlRules.CanSetPersonal(onlinePlayers, multiplier));
    }
}
