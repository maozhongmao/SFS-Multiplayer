// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

// 客户端 → 服务端：执行一条服务端控制台命令（需要实验性访问权限）。
// 服务端把命令输出写回 Output 字段（同一个包类型双向传递）。
public class Packet_ServerCommand : Packet
{
    public string Command { get; set; } = string.Empty;

    public string Output { get; set; } = string.Empty;

    public override PacketType Type => PacketType.ServerCommand;

    public override void Serialize(NetOutgoingMessage msg)
    {
        msg.Write(Command ?? string.Empty);
        msg.Write(Output ?? string.Empty);
    }

    public override void Deserialize(NetIncomingMessage msg)
    {
        Command = msg.ReadString();
        Output = msg.ReadString();
    }
}
