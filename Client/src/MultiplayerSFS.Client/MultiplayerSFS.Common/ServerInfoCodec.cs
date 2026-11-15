// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;

namespace MultiplayerSFS.Common;

public static class ServerInfoCodec
{
    public const int ResponseBytes = 8;

    public static byte[] EncodeRequest()
    {
        return Array.Empty<byte>();
    }

    public static (int CurrentPlayers, int MaxPlayers) DecodeResponse(byte[] payload)
    {
        if (payload == null || payload.Length != ResponseBytes)
            throw new InvalidDataException("Invalid server info payload.");

        int currentPlayers = ReadInt32LittleEndian(payload, 0);
        int maxPlayers = ReadInt32LittleEndian(payload, 4);
        if (currentPlayers < 0 || maxPlayers < 1 || currentPlayers > maxPlayers)
            throw new InvalidDataException("Invalid server player counts.");

        return (currentPlayers, maxPlayers);
    }

    private static int ReadInt32LittleEndian(byte[] buffer, int offset)
    {
        return buffer[offset]
            | (buffer[offset + 1] << 8)
            | (buffer[offset + 2] << 16)
            | (buffer[offset + 3] << 24);
    }
}
