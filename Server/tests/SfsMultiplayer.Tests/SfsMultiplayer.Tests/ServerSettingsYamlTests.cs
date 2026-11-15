// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net.Sockets;
using SfsMultiplayer.Server;

namespace SfsMultiplayer.Tests;

public sealed class ServerSettingsYamlTests
{
    [Fact]
    public void LoadsQuotedIpv6YamlAddress()
    {
        var path = Path.Combine(Path.GetTempPath(), $"sfs-settings-{Guid.NewGuid():N}.yml");
        try
        {
            File.WriteAllText(path, "bind_address: \"::1\"\nport: 9806\n");

            var settings = ServerSettings.Load(path);
            settings.Validate();

            Assert.Equal("::1", settings.BindAddress);
            Assert.Equal(AddressFamily.InterNetworkV6, settings.BindIpAddress.AddressFamily);
        }
        finally
        {
            if (File.Exists(path)) File.Delete(path);
        }
    }

    [Fact]
    public void InvalidBindAddressFailsValidation()
    {
        var settings = new ServerSettings { BindAddress = "not-an-address" };

        var error = Assert.Throws<InvalidDataException>(() => settings.Validate());

        Assert.Contains("BindAddress", error.Message);
    }

    [Fact]
    public void P2PDefaultsUseConfiguredValidationValues()
    {
        var settings = new ServerSettings();

        // 直连链路尚不稳定，默认关；要开得靠 server.yml 的 p2p.enabled: true（见 LoadsP2PYamlSection）
        Assert.False(settings.P2P.Enabled);
        Assert.Equal(10000, settings.P2P.ProximityMeters);
        Assert.Equal(1, settings.P2P.ValidationIntervalSeconds);
        Assert.Equal(3, settings.P2P.PeerTimeoutSeconds);
        Assert.Equal(10, settings.P2P.TransitionBufferSeconds);
    }

    [Fact]
    public void LoadsP2PYamlSection()
    {
        var path = Path.Combine(Path.GetTempPath(), $"sfs-p2p-{Guid.NewGuid():N}.yml");
        try
        {
            File.WriteAllText(path, "p2p:\n  enabled: true\n  proximity_meters: 5000\n");
            var settings = ServerSettings.Load(path);
            Assert.True(settings.P2P.Enabled);
            Assert.Equal(5000, settings.P2P.ProximityMeters);
        }
        finally
        {
            if (File.Exists(path)) File.Delete(path);
        }
    }

    [Fact]
    public void ExperimentalAccessDefaultsToDisabledWithoutPassphrase()
    {
        var settings = new ServerSettings();

        Assert.False(settings.ExperimentalAccess.IsConfigured);
        Assert.False(settings.ExperimentalAccess.Accepts("anything"));
    }

    [Fact]
    public void ExperimentalAccessUsesConfiguredPassphrase()
    {
        var settings = new ServerSettings();
        settings.ExperimentalAccess.Passphrase = "server-only-test";

        Assert.True(settings.ExperimentalAccess.IsConfigured);
        Assert.True(settings.ExperimentalAccess.Accepts("server-only-test"));
        Assert.False(settings.ExperimentalAccess.Accepts("wrong"));
    }
}
