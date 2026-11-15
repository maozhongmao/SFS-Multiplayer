// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace SfsMultiplayer.Protocol;

public static class ServerInfoWire
{
    public const int ResponseBytes = 8;

    public static byte[] EncodeResponse(int currentPlayers, int maxPlayers)
    {
        if (currentPlayers < 0 || maxPlayers < 1 || currentPlayers > maxPlayers)
            throw new ArgumentOutOfRangeException(nameof(currentPlayers));

        var payload = new byte[ResponseBytes];
        WriteInt32LittleEndian(payload, 0, currentPlayers);
        WriteInt32LittleEndian(payload, 4, maxPlayers);
        return payload;
    }

    private static void WriteInt32LittleEndian(byte[] buffer, int offset, int value)
    {
        buffer[offset] = (byte)value;
        buffer[offset + 1] = (byte)(value >> 8);
        buffer[offset + 2] = (byte)(value >> 16);
        buffer[offset + 3] = (byte)(value >> 24);
    }
}
