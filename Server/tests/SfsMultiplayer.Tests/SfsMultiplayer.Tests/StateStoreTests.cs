// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using SfsMultiplayer.Protocol;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class StateStoreTests
{
    [Fact]
    public void SavesAndLoadsRoundTripWithoutTouchingSfsFiles()
    {
        var root = TempDirectory();
        try
        {
            var path = Path.Combine(root, "server-state.json");
            var world = SampleWorld("First");

            ServerStateStore.Save(path, world);
            var loaded = ServerStateStore.Load(path);

            Assert.Equal(42.5, loaded.WorldTime);
            Assert.Equal(DifficultyType.Hard, loaded.Difficulty);
            var rocket = Assert.Single(loaded.Rockets.Values);
            Assert.Equal("First", rocket.RocketName);
            Assert.Equal(0.75, Assert.Single(rocket.Parts.Values).NumberVariables["fuel_percent"]);
            Assert.False(File.Exists(Path.Combine(root, "Rockets.txt")));
            Assert.False(File.Exists(Path.Combine(root, "WorldState.txt")));
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public void CorruptPrimaryFallsBackToPreviousBackup()
    {
        var root = TempDirectory();
        try
        {
            var path = Path.Combine(root, "server-state.json");
            ServerStateStore.Save(path, SampleWorld("Backup"));
            ServerStateStore.Save(path, SampleWorld("Current"));
            File.WriteAllText(path, "not-json");

            var loaded = ServerStateStore.Load(path);

            Assert.Equal("Backup", Assert.Single(loaded.Rockets.Values).RocketName);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    [Fact]
    public async Task CancellationFlushesServerState()
    {
        var root = TempDirectory();
        try
        {
            var path = Path.Combine(root, "server-state.json");
            var settings = new ServerSettings
            {
                Port = 0,
                StatePath = path,
                AutoSaveSeconds = 3600,
            };
            await using var server = new MultiplayerServer(settings, SampleWorld("Flush"));
            server.Start();
            using var stop = new CancellationTokenSource();
            var runTask = server.RunAsync(stop.Token);
            await Task.Delay(50);
            stop.Cancel();
            await runTask;

            Assert.True(File.Exists(path));
            var saved = ServerStateStore.Load(path);
            Assert.Equal("Flush", Assert.Single(saved.Rockets.Values).RocketName);
            Assert.True(saved.WorldTime >= 42.5);
        }
        finally { Directory.Delete(root, recursive: true); }
    }

    private static WorldSnapshot SampleWorld(string name)
    {
        var world = new WorldSnapshot { WorldTime = 42.5, Difficulty = DifficultyType.Hard };
        var rocket = new RocketState
        {
            RocketName = name,
            Location = new NetLocation(1, 2, 3, 4, "Earth"),
        };
        var part = new PartState { Name = "Capsule", Temperature = float.NegativeInfinity };
        part.NumberVariables["fuel_percent"] = 0.75;
        rocket.Parts.Add(3, part);
        world.Rockets.Add(9, rocket);
        return world;
    }

    private static string TempDirectory()
    {
        var path = Path.Combine(Path.GetTempPath(), "sfs-state-test-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(path);
        return path;
    }
}
