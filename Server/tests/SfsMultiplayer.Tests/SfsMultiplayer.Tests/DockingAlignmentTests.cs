// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Reflection;
using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class DockingAlignmentTests
{
    [Fact]
    public void DockingUsesOriginalRemovePortAsPivotForEveryPart()
    {
        var keep = RocketWithPart(10, new PartState
        {
            Name = "keep-port", X = 10, Y = 0,
            OrientationX = 1, OrientationY = 1, OrientationZ = 0,
        });
        var remove = RocketWithPart(20, new PartState
        {
            Name = "remove-port", X = 0, Y = 0,
            OrientationX = 1, OrientationY = 1, OrientationZ = 0,
        });
        remove.Parts.Add(21, new PartState
        {
            Name = "remove-body", X = 0, Y = 5,
            OrientationX = 1, OrientationY = 1, OrientationZ = 0,
        });
        remove.Joints.Add(new JointState(20, 21));

        var method = typeof(TcpMultiplayerServer).GetMethod(
            "MergeDockedRockets", BindingFlags.NonPublic | BindingFlags.Static)!;
        var merged = (RocketState)method.Invoke(null, new object[] { keep, remove, 10, 20 })!;
        var body = merged.Parts.Values.Single(part => part.Name == "remove-body");

        Assert.Equal(10f, body.X, 3);
        Assert.Equal(-5f, body.Y, 3);
    }

    [Fact]
    public void DockingResultDoesNotDependOnPartDictionaryOrder()
    {
        var first = MergeWithOrder(portFirst: true);
        var second = MergeWithOrder(portFirst: false);

        Assert.Equal(first.X, second.X, 3);
        Assert.Equal(first.Y, second.Y, 3);
        Assert.Equal(first.OrientationZ, second.OrientationZ, 3);
    }

    [Fact]
    public void DockingPreservesRemoveRocketRigidGeometry()
    {
        var keep = RocketWithPart(10, Part("keep-port", 8, -3, 90));
        var remove = RocketWithPart(20, Part("remove-port", 2, 4, 0));
        remove.Parts.Add(21, Part("body-a", 5, 4, 0));
        remove.Parts.Add(22, Part("body-b", 5, 8, 90));
        remove.Joints.Add(new JointState(20, 21));
        remove.Joints.Add(new JointState(21, 22));
        var beforeA = remove.Parts[21];
        var beforeB = remove.Parts[22];
        var beforeDistance = Distance(beforeA, beforeB);

        var merged = Merge(keep, remove, 10, 20);
        var afterA = merged.Parts.Values.Single(part => part.Name == "body-a");
        var afterB = merged.Parts.Values.Single(part => part.Name == "body-b");

        Assert.Equal(beforeDistance, Distance(afterA, afterB), 3);
        Assert.Equal(90f, Normalize(afterB.OrientationZ - afterA.OrientationZ), 3);
    }

    private static PartState MergeWithOrder(bool portFirst)
    {
        var keep = RocketWithPart(10, Part("keep-port", 10, 0, 0));
        var remove = new RocketState();
        if (portFirst) remove.Parts.Add(20, Part("remove-port", 0, 0, 0));
        remove.Parts.Add(21, Part("remove-body", 0, 5, 0));
        if (!portFirst) remove.Parts.Add(20, Part("remove-port", 0, 0, 0));
        remove.Joints.Add(new JointState(20, 21));
        return Merge(keep, remove, 10, 20).Parts.Values.Single(part => part.Name == "remove-body");
    }

    private static RocketState Merge(RocketState keep, RocketState remove, int keepPart, int removePart)
    {
        var method = typeof(TcpMultiplayerServer).GetMethod(
            "MergeDockedRockets", BindingFlags.NonPublic | BindingFlags.Static)!;
        return (RocketState)method.Invoke(null, new object[] { keep, remove, keepPart, removePart })!;
    }

    private static PartState Part(string name, float x, float y, float z) => new()
    {
        Name = name, X = x, Y = y,
        OrientationX = 1, OrientationY = 1, OrientationZ = z,
    };

    private static float Distance(PartState a, PartState b)
    {
        var dx = a.X - b.X;
        var dy = a.Y - b.Y;
        return MathF.Sqrt(dx * dx + dy * dy);
    }

    private static float Normalize(float angle)
    {
        angle %= 360f;
        return angle < 0 ? angle + 360f : angle;
    }

    private static RocketState RocketWithPart(int id, PartState part)
    {
        var rocket = new RocketState { Rotation = 0 };
        rocket.Parts.Add(id, part);
        return rocket;
    }
}
