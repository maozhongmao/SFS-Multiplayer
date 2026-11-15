// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Text.Json;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Tests;

public sealed class WorldLoaderTests
{
    [Fact]
    public void LoadsSfsWorldSettingsAndRocketState()
    {
        var root = Path.Combine(Path.GetTempPath(), "sfs-server-test-" + Guid.NewGuid().ToString("N"));
        var persistent = Path.Combine(root, "Persistent");
        Directory.CreateDirectory(persistent);
        try
        {
            File.WriteAllText(Path.Combine(root, "WorldSettings.txt"),
                "{\"difficulty\":{\"difficulty\":0},\"mode\":{\"mode\":0}}");
            File.WriteAllText(Path.Combine(persistent, "WorldState.txt"),
                "{\"worldTime\":123.5}");
            File.WriteAllText(Path.Combine(persistent, "Rockets.txt"),
                "[{\"rocketName\":\"Test\",\"location\":{\"address\":\"Earth\",\"position\":{\"x\":1.0,\"y\":2.0},\"velocity\":{\"x\":3.0,\"y\":4.0}},\"rotation\":5.0,\"angularVelocity\":6.0,\"throttleOn\":true,\"throttlePercent\":0.25,\"RCS\":false,\"parts\":[{\"n\":\"Capsule\",\"p\":{\"x\":0,\"y\":0},\"o\":{\"x\":1,\"y\":1,\"z\":0},\"t\":\"-Infinity\",\"N\":{\"temperature\":\"-Infinity\"}}],\"joints\":[],\"stages\":[],\"staging_EditMode\":false,\"branch\":0}]");

            var world = SfsWorldLoader.Load(root);

            Assert.Equal(123.5, world.WorldTime, 5);
            Assert.Equal(DifficultyType.Normal, world.Difficulty);
            var rocket = Assert.Single(world.Rockets.Values);
            Assert.Equal("Test", rocket.RocketName);
            Assert.Equal("Earth", rocket.Location.Address);
            var part = Assert.Single(rocket.Parts.Values);
            Assert.Equal("Capsule", part.Name);
            Assert.True(float.IsNegativeInfinity(part.Temperature));
        }
        finally
        {
            if (Directory.Exists(root))
                Directory.Delete(root, recursive: true);
        }
    }

    [Fact]
    public void RejectsMissingPersistentFiles()
    {
        var root = Path.Combine(Path.GetTempPath(), "sfs-server-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(root);
        try
        {
            var error = Assert.Throws<InvalidDataException>(() => SfsWorldLoader.Load(root));
            Assert.Contains("Persistent", error.Message, StringComparison.OrdinalIgnoreCase);
        }
        finally
        {
            Directory.Delete(root, recursive: true);
        }
    }
}

public sealed class ProtocolCompatibilityTests
{
    [Fact]
    public void PacketTypeValuesMatchArchivedClientProtocol()
    {
        Assert.Equal(0, (byte)PacketType.JoinRequest);
        Assert.Equal(1, (byte)PacketType.JoinResponse);
        Assert.Equal(2, (byte)PacketType.PlayerConnected);
        Assert.Equal(9, (byte)PacketType.CreateRocket);
        Assert.Equal(12, (byte)PacketType.UpdateRocketSecondary);
        Assert.Equal(20, (byte)PacketType.UpdatePart_ResourceModule);
    }

    [Fact]
    public void RocketPrimaryLatencyCompensationAdvancesPositionAndRotation()
    {
        var packet = new UpdateRocketPrimaryPacket
        {
            RocketId = 4,
            Location = new NetLocation(10, 20, 1000, -200, "Earth"),
            Rotation = 30,
            AngularVelocity = 40,
        };

        RocketLatencyCompensation.Advance(packet, 200);

        Assert.Equal(110, packet.Location.X, 6);
        Assert.Equal(0, packet.Location.Y, 6);
        Assert.Equal(34, packet.Rotation, 6);
    }

    [Fact]
    public void RocketStateUpdatesOnlyMutableFields()
    {
        var rocket = new RocketState
        {
            RocketName = "r",
            Location = new NetLocation(1, 2, 3, 4, "Earth"),
            Rotation = 10,
            AngularVelocity = 11,
            ThrottleOn = false,
            ThrottlePercent = 0.1f,
            Rcs = false
        };
        var update = new UpdateRocketPrimaryPacket
        {
            RocketId = 4,
            Location = new NetLocation(5, 6, 7, 8, "Moon"),
            Rotation = 20,
            AngularVelocity = 21
        };

        rocket.Apply(update);

        Assert.Equal("r", rocket.RocketName);
        Assert.Equal("Moon", rocket.Location.Address);
        Assert.Equal(20, rocket.Rotation);
        Assert.Equal(21, rocket.AngularVelocity);
        Assert.False(rocket.ThrottleOn);
    }
}
