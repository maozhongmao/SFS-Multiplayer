// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace SfsMultiplayer.Protocol;

public sealed class ExperimentalAccessPacket : INetData
{
    public bool Request { get; set; }
    public string Passphrase { get; set; } = string.Empty;
    public bool Granted { get; set; }
    public string Message { get; set; } = string.Empty;

    public void Serialize(NetOutgoingMessage message)
    {
        message.Write(Request);
        message.Write(Passphrase ?? string.Empty);
        message.Write(Granted);
        message.Write(Message ?? string.Empty);
    }

    public void Deserialize(NetIncomingMessage message)
    {
        Request = message.ReadBoolean();
        Passphrase = message.ReadStringBounded();
        Granted = message.ReadBoolean();
        Message = message.ReadStringBounded();
    }
}
