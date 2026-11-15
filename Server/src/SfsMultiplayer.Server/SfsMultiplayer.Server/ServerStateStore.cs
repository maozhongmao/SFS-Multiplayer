// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Text.Json;
using System.Text.Json.Serialization;
using SfsMultiplayer.Protocol;

namespace SfsMultiplayer.Server;

public static class ServerStateStore
{
    private const int CurrentSchemaVersion = 1;
    private const long MaxStateBytes = 128L * 1024 * 1024;
    private static readonly object FileLock = new();

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = false,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals,
        MaxDepth = 128,
    };

    public static string BackupPath(string path) => Path.GetFullPath(path) + ".bak";

    public static void Save(string path, WorldSnapshot world)
    {
        ArgumentNullException.ThrowIfNull(world);
        Validate(world);
        var target = Path.GetFullPath(path);
        var directory = Path.GetDirectoryName(target)
            ?? throw new InvalidDataException("State path has no parent directory.");
        Directory.CreateDirectory(directory);
        var temporary = target + ".tmp-" + Guid.NewGuid().ToString("N");
        var backup = BackupPath(target);
        var envelope = new StateEnvelope
        {
            SchemaVersion = CurrentSchemaVersion,
            SavedAtUtc = DateTime.UtcNow,
            World = world,
        };

        lock (FileLock)
        {
            try
            {
                using (var stream = new FileStream(temporary, FileMode.CreateNew, FileAccess.Write,
                           FileShare.None, 64 * 1024, FileOptions.WriteThrough))
                {
                    JsonSerializer.Serialize(stream, envelope, JsonOptions);
                    stream.Flush(flushToDisk: true);
                    if (stream.Length <= 0 || stream.Length > MaxStateBytes)
                        throw new InvalidDataException($"Serialized server state has invalid size: {stream.Length} bytes.");
                }

                if (File.Exists(target))
                {
                    try
                    {
                        File.Replace(temporary, target, backup, ignoreMetadataErrors: true);
                    }
                    catch (PlatformNotSupportedException)
                    {
                        File.Copy(target, backup, overwrite: true);
                        File.Move(temporary, target, overwrite: true);
                    }
                }
                else
                {
                    File.Move(temporary, target);
                }
            }
            finally
            {
                if (File.Exists(temporary)) File.Delete(temporary);
            }
        }
    }

    public static WorldSnapshot Load(string path)
    {
        var primary = Path.GetFullPath(path);
        try
        {
            return LoadOne(primary);
        }
        catch (Exception primaryError) when (IsRecoverable(primaryError))
        {
            var backup = BackupPath(primary);
            try
            {
                return LoadOne(backup);
            }
            catch (Exception backupError) when (IsRecoverable(backupError))
            {
                throw new InvalidDataException(
                    $"Neither server state nor its backup could be loaded. Primary: {primaryError.Message} Backup: {backupError.Message}",
                    new AggregateException(primaryError, backupError));
            }
        }
    }

    public static bool TryLoad(string path, out WorldSnapshot? world)
    {
        if (!File.Exists(path) && !File.Exists(BackupPath(path)))
        {
            world = null;
            return false;
        }
        world = Load(path);
        return true;
    }

    private static WorldSnapshot LoadOne(string path)
    {
        if (!File.Exists(path)) throw new FileNotFoundException("State file was not found.", path);
        var size = new FileInfo(path).Length;
        if (size <= 0 || size > MaxStateBytes)
            throw new InvalidDataException($"State file has invalid size ({size} bytes): {path}");
        try
        {
            using var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.Read,
                64 * 1024, FileOptions.SequentialScan);
            var envelope = JsonSerializer.Deserialize<StateEnvelope>(stream, JsonOptions)
                ?? throw new InvalidDataException($"State file is empty: {path}");
            if (envelope.SchemaVersion != CurrentSchemaVersion)
                throw new InvalidDataException(
                    $"Unsupported state schema {envelope.SchemaVersion}; expected {CurrentSchemaVersion}.");
            if (envelope.World is null) throw new InvalidDataException("State file has no world snapshot.");
            Validate(envelope.World);
            return envelope.World;
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"State file contains invalid JSON: {path}", ex);
        }
    }

    private static void Validate(WorldSnapshot world)
    {
        if (!double.IsFinite(world.WorldTime)) throw new InvalidDataException("World time must be finite.");
        if (!Enum.IsDefined(world.Difficulty)) throw new InvalidDataException("World difficulty is invalid.");
        if (world.Rockets is null || world.Rockets.Count > NetMessageExtensions.MaxCollectionCount)
            throw new InvalidDataException("Rocket collection is invalid or too large.");
        foreach (var (rocketId, rocket) in world.Rockets)
        {
            if (rocket is null) throw new InvalidDataException($"Rocket {rocketId} is null.");
            if (rocket.Parts is null || rocket.Parts.Count > NetMessageExtensions.MaxCollectionCount)
                throw new InvalidDataException($"Rocket {rocketId} has an invalid part collection.");
            if (rocket.Joints is null || rocket.Stages is null)
                throw new InvalidDataException($"Rocket {rocketId} has null joint or stage data.");
            foreach (var (partId, part) in rocket.Parts)
            {
                if (part is null || part.NumberVariables is null || part.ToggleVariables is null || part.TextVariables is null)
                    throw new InvalidDataException($"Rocket {rocketId}, part {partId} has invalid module data.");
            }
        }
    }

    private static bool IsRecoverable(Exception error) =>
        error is IOException or UnauthorizedAccessException or InvalidDataException;

    private sealed class StateEnvelope
    {
        public int SchemaVersion { get; set; }
        public DateTime SavedAtUtc { get; set; }
        public WorldSnapshot? World { get; set; }
    }
}
