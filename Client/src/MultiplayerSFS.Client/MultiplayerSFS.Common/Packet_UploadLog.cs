// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

// 客户端 → 服务端：上传日志（GZip 压缩后的字节流），服务端落盘到自己的 logs/ 目录。
public class Packet_UploadLog : Packet
{
    public string FileName { get; set; } = "Player.log";

    public string Comment { get; set; } = string.Empty;

    public byte[] Data { get; set; } = Array.Empty<byte>();

    public override PacketType Type => PacketType.UploadLog;

    public override void Serialize(NetOutgoingMessage msg)
    {
        msg.Write(FileName ?? string.Empty);
        msg.Write(Comment ?? string.Empty);
        byte[] data = Data ?? Array.Empty<byte>();
        msg.Write(data.Length);
        if (data.Length > 0)
        {
            ((NetBuffer)msg).Write(data, 0, data.Length);
        }
    }

    public override void Deserialize(NetIncomingMessage msg)
    {
        FileName = msg.ReadString();
        Comment = msg.ReadString();
        int length = msg.ReadInt32();
        Data = length > 0 ? msg.ReadBytes(length) : Array.Empty<byte>();
    }
}
