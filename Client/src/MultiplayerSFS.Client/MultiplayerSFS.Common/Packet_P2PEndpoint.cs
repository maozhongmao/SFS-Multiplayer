// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

/// <summary>
/// 客户端 -> 服务端：广告本端 P2P socket 实际绑定的 UDP 端口。
///
/// 为什么必须有这条：P2P socket 用 Bind(..., 0) 让系统分配一个空闲端口，
/// 而服务端撮合时填的 peer_port 取自主传输会话观察到的端点 —— 两者不是同一个端口。
/// 对方按报价打过来打的是主传输 socket，KCP conv 不匹配直接丢包，
/// 现象就是探测发了 39 个、收到 0 个、10 秒后握手超时，与网络/NAT/防火墙无关。
/// </summary>
public class Packet_P2PEndpoint : Packet
{
	public int Port { get; set; } = -1;

	public override PacketType Type => PacketType.P2PEndpoint;

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(Port);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		Port = ((NetBuffer)msg).ReadInt32();
	}
}
