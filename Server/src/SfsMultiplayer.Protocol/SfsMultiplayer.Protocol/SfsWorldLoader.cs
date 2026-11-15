// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Globalization;
using System.Text.Json;

namespace SfsMultiplayer.Protocol;

public static class SfsWorldLoader
{
    private const long MaxJsonBytes = 64L * 1024 * 1024;

    public static WorldSnapshot Load(string worldPath)
    {
        if (string.IsNullOrWhiteSpace(worldPath))
            throw new InvalidDataException("World path cannot be empty.");

        var root = Path.GetFullPath(worldPath);
        if (!Directory.Exists(root))
            throw new InvalidDataException($"World folder does not exist: {root}");

        var persistent = Path.Combine(root, "Persistent");
        if (!Directory.Exists(persistent))
            throw new InvalidDataException($"Persistent folder does not exist: {persistent}");

        using var settings = ReadDocument(Path.Combine(root, "WorldSettings.txt"));
        using var state = ReadDocument(Path.Combine(persistent, "WorldState.txt"));
        using var rockets = ReadDocument(Path.Combine(persistent, "Rockets.txt"));

        var result = new WorldSnapshot
        {
            Difficulty = ReadDifficulty(settings.RootElement),
            WorldTime = ReadRequiredDouble(state.RootElement, "worldTime"),
        };

        if (rockets.RootElement.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Rockets.txt root must be a JSON array.");
        if (rockets.RootElement.GetArrayLength() > NetMessageExtensions.MaxCollectionCount)
            throw new InvalidDataException("Rockets.txt contains too many rockets.");

        var rocketId = 0;
        foreach (var element in rockets.RootElement.EnumerateArray())
            result.Rockets.Add(rocketId++, ReadRocket(element));
        return result;
    }

    private static JsonDocument ReadDocument(string path)
    {
        if (!File.Exists(path))
            throw new InvalidDataException($"Required SFS save file is missing: {path}");
        var length = new FileInfo(path).Length;
        if (length <= 0 || length > MaxJsonBytes)
            throw new InvalidDataException($"Invalid SFS save file size ({length} bytes): {path}");
        try
        {
            return JsonDocument.Parse(File.ReadAllBytes(path), new JsonDocumentOptions
            {
                AllowTrailingCommas = false,
                CommentHandling = JsonCommentHandling.Disallow,
                MaxDepth = 128,
            });
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException($"Invalid JSON in SFS save file: {path}", ex);
        }
    }

    private static DifficultyType ReadDifficulty(JsonElement root)
    {
        if (!TryProperty(root, out var difficulty, "difficulty") ||
            !TryProperty(difficulty, out var value, "difficulty"))
            return DifficultyType.Normal;
        var number = ReadInt(value, "difficulty");
        return Enum.IsDefined(typeof(DifficultyType), (byte)number)
            ? (DifficultyType)(byte)number
            : DifficultyType.Normal;
    }

    private static RocketState ReadRocket(JsonElement value)
    {
        RequireObject(value, "rocket");
        var location = RequiredProperty(value, "location");
        var position = RequiredProperty(location, "position");
        var velocity = RequiredProperty(location, "velocity");
        var result = new RocketState
        {
            RocketName = ReadOptionalString(value, "rocketName"),
            Location = new NetLocation(
                ReadRequiredDouble(position, "x"), ReadRequiredDouble(position, "y"),
                ReadRequiredDouble(velocity, "x"), ReadRequiredDouble(velocity, "y"),
                ReadOptionalString(location, "address")),
            Rotation = ReadOptionalFloat(value, "rotation"),
            AngularVelocity = ReadOptionalFloat(value, "angularVelocity"),
            ThrottleOn = ReadOptionalBool(value, "throttleOn"),
            ThrottlePercent = ReadOptionalFloat(value, "throttlePercent"),
            Rcs = ReadOptionalBool(value, "RCS", "rcs"),
        };

        var parts = RequiredProperty(value, "parts");
        if (parts.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Rocket parts must be a JSON array.");
        if (parts.GetArrayLength() > NetMessageExtensions.MaxCollectionCount)
            throw new InvalidDataException("Rocket contains too many parts.");
        var partIds = new List<int>(parts.GetArrayLength());
        var partId = 0;
        foreach (var part in parts.EnumerateArray())
        {
            result.Parts.Add(partId, ReadPart(part));
            partIds.Add(partId++);
        }

        if (TryProperty(value, out var joints, "joints"))
            ReadJoints(joints, partIds, result.Joints);
        if (TryProperty(value, out var stages, "stages"))
            ReadStages(stages, partIds, result.Stages);
        return result;
    }

    private static PartState ReadPart(JsonElement value)
    {
        RequireObject(value, "part");
        var position = RequiredProperty(value, "p", "position");
        var orientation = RequiredProperty(value, "o", "orientation");
        var result = new PartState
        {
            Name = ReadOptionalString(value, "n", "name"),
            X = ReadRequiredFloat(position, "x"),
            Y = ReadRequiredFloat(position, "y"),
            OrientationX = ReadRequiredFloat(orientation, "x"),
            OrientationY = ReadRequiredFloat(orientation, "y"),
            OrientationZ = ReadRequiredFloat(orientation, "z"),
            Temperature = TryProperty(value, out var temperature, "t", "temperature")
                ? ReadFloat(temperature, "temperature")
                : float.NegativeInfinity,
        };
        ReadNumberMap(value, result.NumberVariables, "N", "NUMBER_VARIABLES");
        ReadBoolMap(value, result.ToggleVariables, "B", "TOGGLE_VARIABLES");
        ReadStringMap(value, result.TextVariables, "T", "TEXT_VARIABLES");
        if (TryProperty(value, out var burns, "burns") && burns.ValueKind == JsonValueKind.Object)
        {
            result.Burns = new BurnMarkState(
                ReadOptionalFloat(burns, "angle"), ReadOptionalFloat(burns, "intensity"),
                ReadOptionalFloat(burns, "x"), ReadOptionalString(burns, "top"),
                ReadOptionalString(burns, "bottom"));
        }
        return result;
    }

    private static void ReadJoints(JsonElement value, IReadOnlyList<int> partIds, List<JointState> output)
    {
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Rocket joints must be a JSON array.");
        foreach (var joint in value.EnumerateArray())
        {
            var a = ReadRequiredInt(joint, "a", "partIndex_A", "id_A");
            var b = ReadRequiredInt(joint, "b", "partIndex_B", "id_B");
            output.Add(new JointState(MapPartIndex(a, partIds), MapPartIndex(b, partIds)));
        }
    }

    private static void ReadStages(JsonElement value, IReadOnlyList<int> partIds, List<StageState> output)
    {
        if (value.ValueKind != JsonValueKind.Array)
            throw new InvalidDataException("Rocket stages must be a JSON array.");
        foreach (var stage in value.EnumerateArray())
        {
            var id = ReadRequiredInt(stage, "stageId", "stageID", "id");
            var indexes = RequiredProperty(stage, "partIndexes", "parts", "partIDs");
            if (indexes.ValueKind != JsonValueKind.Array)
                throw new InvalidDataException("Stage part indexes must be a JSON array.");
            output.Add(new StageState(id, indexes.EnumerateArray()
                .Select(item => MapPartIndex(ReadInt(item, "stage part index"), partIds))));
        }
    }

    private static int MapPartIndex(int index, IReadOnlyList<int> ids)
    {
        if (index == -1)
            return -1;
        if (index < 0 || index >= ids.Count)
            throw new InvalidDataException($"Part index {index} is outside the rocket part array.");
        return ids[index];
    }

    private static void ReadNumberMap(JsonElement parent, IDictionary<string, double> output, params string[] names)
    {
        if (!TryProperty(parent, out var map, names)) return;
        RequireObject(map, names[0]);
        foreach (var item in map.EnumerateObject()) output[item.Name] = ReadDouble(item.Value, item.Name);
    }

    private static void ReadBoolMap(JsonElement parent, IDictionary<string, bool> output, params string[] names)
    {
        if (!TryProperty(parent, out var map, names)) return;
        RequireObject(map, names[0]);
        foreach (var item in map.EnumerateObject()) output[item.Name] = ReadBool(item.Value, item.Name);
    }

    private static void ReadStringMap(JsonElement parent, IDictionary<string, string> output, params string[] names)
    {
        if (!TryProperty(parent, out var map, names)) return;
        RequireObject(map, names[0]);
        foreach (var item in map.EnumerateObject()) output[item.Name] = ReadString(item.Value, item.Name);
    }

    private static JsonElement RequiredProperty(JsonElement parent, params string[] names)
    {
        if (TryProperty(parent, out var value, names)) return value;
        throw new InvalidDataException($"Missing required JSON property: {string.Join("/", names)}");
    }

    private static bool TryProperty(JsonElement parent, out JsonElement value, params string[] names)
    {
        if (parent.ValueKind == JsonValueKind.Object)
            foreach (var name in names)
                if (parent.TryGetProperty(name, out value)) return true;
        value = default;
        return false;
    }

    private static void RequireObject(JsonElement value, string name)
    {
        if (value.ValueKind != JsonValueKind.Object)
            throw new InvalidDataException($"{name} must be a JSON object.");
    }

    private static string ReadOptionalString(JsonElement parent, params string[] names) =>
        TryProperty(parent, out var value, names) ? ReadString(value, names[0]) : string.Empty;
    private static float ReadOptionalFloat(JsonElement parent, params string[] names) =>
        TryProperty(parent, out var value, names) ? ReadFloat(value, names[0]) : 0;
    private static bool ReadOptionalBool(JsonElement parent, params string[] names) =>
        TryProperty(parent, out var value, names) && ReadBool(value, names[0]);
    private static int ReadRequiredInt(JsonElement parent, params string[] names) =>
        ReadInt(RequiredProperty(parent, names), names[0]);
    private static double ReadRequiredDouble(JsonElement parent, params string[] names) =>
        ReadDouble(RequiredProperty(parent, names), names[0]);
    private static float ReadRequiredFloat(JsonElement parent, params string[] names) =>
        ReadFloat(RequiredProperty(parent, names), names[0]);

    private static string ReadString(JsonElement value, string name) => value.ValueKind switch
    {
        JsonValueKind.String => value.GetString() ?? string.Empty,
        JsonValueKind.Null => string.Empty,
        _ => throw new InvalidDataException($"{name} must be a string."),
    };

    private static bool ReadBool(JsonElement value, string name) => value.ValueKind switch
    {
        JsonValueKind.True => true,
        JsonValueKind.False => false,
        _ => throw new InvalidDataException($"{name} must be a boolean."),
    };

    private static int ReadInt(JsonElement value, string name)
    {
        if (value.ValueKind == JsonValueKind.Number && value.TryGetInt32(out var result)) return result;
        throw new InvalidDataException($"{name} must be a 32-bit integer.");
    }

    private static float ReadFloat(JsonElement value, string name) => (float)ReadDouble(value, name);

    private static double ReadDouble(JsonElement value, string name)
    {
        if (value.ValueKind == JsonValueKind.Number && value.TryGetDouble(out var number)) return number;
        if (value.ValueKind == JsonValueKind.String)
        {
            var text = value.GetString();
            if (string.Equals(text, "Infinity", StringComparison.OrdinalIgnoreCase)) return double.PositiveInfinity;
            if (string.Equals(text, "-Infinity", StringComparison.OrdinalIgnoreCase)) return double.NegativeInfinity;
            if (string.Equals(text, "NaN", StringComparison.OrdinalIgnoreCase)) return double.NaN;
            if (double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out number)) return number;
        }
        throw new InvalidDataException($"{name} must be a number.");
    }
}
