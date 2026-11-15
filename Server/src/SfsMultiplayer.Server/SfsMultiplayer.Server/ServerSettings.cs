// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net;
using System.Text.Json;
using System.Text.Json.Serialization;
using YamlDotNet.Serialization;
using YamlDotNet.Serialization.NamingConventions;

namespace SfsMultiplayer.Server;

public sealed class ServerSettings
{
    public string BindAddress { get; set; } = "0.0.0.0";
    public string WorldPath { get; set; } = string.Empty;
    public string StatePath { get; set; } = string.Empty;
    public int AutoSaveSeconds { get; set; } = 30;
    public int Port { get; set; } = 9806;
    public string Password { get; set; } = string.Empty;
    public int MaxConnections { get; set; } = 16;
    public bool BlockDuplicatePlayerNames { get; set; } = true;
    public double UpdateRocketsPeriod { get; set; } = 20;
    public double ChatMessageCooldown { get; set; } = 3;
    public int MaxUsernameLength { get; set; } = 32;
    public int MaxChatMessageLength { get; set; } = 512;
    public bool Debug { get; set; }

    [YamlMember(Alias = "p2p")]
    public P2PSettings P2P { get; set; } = new();

    public ExperimentalAccessSettings ExperimentalAccess { get; set; } = new();

    // 实验性功能开关：DLL 补丁（插件）系统。开启后服务器才生成 plugins/ 目录并加载其中的补丁 DLL。
    [YamlMember(Alias = "experimental_patches")]
    public bool ExperimentalPatches { get; set; }

    [JsonIgnore]
    [YamlIgnore]
    public IPAddress BindIpAddress => IPAddress.Parse(BindAddress);

    public void Validate(bool allowEphemeralPort = false)
    {
        var minimumPort = allowEphemeralPort ? 0 : 1;
        if (Port < minimumPort || Port > 65535)
            throw new InvalidDataException($"Port must be between {minimumPort} and 65535.");
        if (string.IsNullOrWhiteSpace(BindAddress) || !IPAddress.TryParse(BindAddress, out _))
            throw new InvalidDataException("BindAddress must be a valid IPv4 or IPv6 address. Quote IPv6 values in YAML, for example bind_address: \"::\".");
        if (MaxConnections < 1 || MaxConnections > 256)
            throw new InvalidDataException("MaxConnections must be between 1 and 256.");
        if (AutoSaveSeconds < 0 || AutoSaveSeconds > 86400)
            throw new InvalidDataException("AutoSaveSeconds must be between 0 and 86400; zero disables periodic saves.");
        if (!double.IsFinite(UpdateRocketsPeriod) || UpdateRocketsPeriod < 10 || UpdateRocketsPeriod > 1000)
            throw new InvalidDataException("UpdateRocketsPeriod must be between 10 and 1000 milliseconds.");
        if (!double.IsFinite(ChatMessageCooldown) || ChatMessageCooldown < 0 || ChatMessageCooldown > 3600)
            throw new InvalidDataException("ChatMessageCooldown must be between 0 and 3600 seconds.");
        ValidateP2P();
        if (MaxUsernameLength < 1 || MaxUsernameLength > 128)
            throw new InvalidDataException("MaxUsernameLength must be between 1 and 128.");
        if (MaxChatMessageLength < 1 || MaxChatMessageLength > 4096)
            throw new InvalidDataException("MaxChatMessageLength must be between 1 and 4096.");
        if (Password.Length > 256)
            throw new InvalidDataException("Password cannot exceed 256 characters.");
    }

    private void ValidateP2P()
    {
        if (P2P is null) throw new InvalidDataException("P2P settings cannot be null.");
        if (!double.IsFinite(P2P.ProximityMeters) || P2P.ProximityMeters < 0 || P2P.ProximityMeters > 1_000_000)
            throw new InvalidDataException("P2P.ProximityMeters must be between 0 and 1000000.");
        if (P2P.ValidationIntervalSeconds < 1 || P2P.ValidationIntervalSeconds > 3600)
            throw new InvalidDataException("P2P.ValidationIntervalSeconds must be between 1 and 3600.");
        if (P2P.PeerTimeoutSeconds < 1 || P2P.PeerTimeoutSeconds > 3600)
            throw new InvalidDataException("P2P.PeerTimeoutSeconds must be between 1 and 3600.");
        if (P2P.TransitionBufferSeconds < 0 || P2P.TransitionBufferSeconds > 3600)
            throw new InvalidDataException("P2P.TransitionBufferSeconds must be between 0 and 3600.");
        if (P2P.MaxGroupMembers < 1 || P2P.MaxGroupMembers > 256)
            throw new InvalidDataException("P2P.MaxGroupMembers must be between 1 and 256.");
        if (P2P.MaxDirectPeersPerClient < 1 || P2P.MaxDirectPeersPerClient > 256)
            throw new InvalidDataException("P2P.MaxDirectPeersPerClient must be between 1 and 256.");
        if (ExperimentalAccess is null) throw new InvalidDataException("ExperimentalAccess settings cannot be null.");
        if (ExperimentalAccess.Passphrase.Length > 256)
            throw new InvalidDataException("ExperimentalAccess.Passphrase cannot exceed 256 characters.");
    }

    public static ServerSettings Load(string path)
    {
        if (!File.Exists(path))
            throw new FileNotFoundException("Server config was not found.", path);
        if (new FileInfo(path).Length > 1024 * 1024)
            throw new InvalidDataException("Server config is too large.");

        try
        {
            if (Path.GetExtension(path).Equals(".yml", StringComparison.OrdinalIgnoreCase) ||
                Path.GetExtension(path).Equals(".yaml", StringComparison.OrdinalIgnoreCase))
            {
                return YamlDeserializer.Deserialize<ServerSettings>(File.ReadAllText(path))
                    ?? throw new InvalidDataException("Server YAML config is empty.");
            }

            return JsonSerializer.Deserialize<ServerSettings>(File.ReadAllText(path), JsonOptions)
                ?? throw new InvalidDataException("Server JSON config is empty.");
        }
        catch (YamlDotNet.Core.YamlException ex)
        {
            throw new InvalidDataException("Server config contains invalid YAML.", ex);
        }
        catch (JsonException ex)
        {
            throw new InvalidDataException("Server config contains invalid JSON.", ex);
        }
    }

    public static string EnsureDefaultYaml(string path)
    {
        if (File.Exists(path)) return path;
        var parent = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);
        File.WriteAllText(path, DefaultYamlTemplate);
        return path;
    }

    public void Save(string path)
    {
        var parent = Path.GetDirectoryName(Path.GetFullPath(path));
        if (!string.IsNullOrEmpty(parent)) Directory.CreateDirectory(parent);
        File.WriteAllText(path, JsonSerializer.Serialize(this, JsonOptions));
    }

    private const string DefaultYamlTemplate = """
# SFS Multiplayer Server configuration.
# IPv6 must stay quoted because ':' has YAML syntax meaning.
# IPv4 examples: "0.0.0.0" (all IPv4), "127.0.0.1" (local only).
# IPv6 examples: "::" (all IPv6), "::1" (local only).
bind_address: "0.0.0.0"
port: 9806
max_connections: 16

# Leave empty to create a new empty world. Relative paths use this file's folder.
world_path: ""
state_path: "data/server-state.json"
auto_save_seconds: 30

# Leave empty for no password. Prefer SFS_SERVER_PASSWORD for private passwords.
password: ""
block_duplicate_player_names: true

# Network and chat limits.
update_rockets_period: 20
chat_message_cooldown: 3
max_username_length: 32
max_chat_message_length: 512
debug: false

# Experimental features. Leave empty to disable their server-side unlock.
experimental_access:
  passphrase: ""

# 实验性功能：DLL 补丁（插件）系统。设为 true 时服务器生成 plugins/ 目录并加载其中的补丁 DLL。
experimental_patches: false

# Experimental P2P feasibility settings.
# IPv6 endpoints are always represented as quoted strings in YAML.
p2p:
  enabled: true
  proximity_meters: 10000
  validation_interval_seconds: 1
  peer_timeout_seconds: 3
  transition_buffer_seconds: 10
  max_group_members: 16
  max_direct_peers_per_client: 8
""";

    private static readonly IDeserializer YamlDeserializer = new DeserializerBuilder()
        .WithNamingConvention(UnderscoredNamingConvention.Instance)
        .Build();

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        PropertyNameCaseInsensitive = true,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
    };
}
