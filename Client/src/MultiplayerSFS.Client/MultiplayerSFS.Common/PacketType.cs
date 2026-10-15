// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace MultiplayerSFS.Common;

public enum PacketType
{
	JoinRequest = 0,
	JoinResponse = 1,
	PlayerConnected = 2,
	PlayerDisconnected = 3,
	UpdatePlayerControl = 4,
	UpdatePlayerAuthority = 5,
	UpdateWorldTime = 6,
	UpdatePlayerColor = 7,
	SendChatMessage = 8,
	CreateRocket = 9,
	DestroyRocket = 10,
	UpdateRocketPrimary = 11,
	UpdateRocketSecondary = 12,
	DestroyPart = 13,
	UpdateStaging = 14,
	UpdatePart_EngineModule = 15,
	UpdatePart_WheelModule = 16,
	UpdatePart_BoosterModule = 17,
	UpdatePart_ParachuteModule = 18,
	UpdatePart_MoveModule = 19,
	UpdatePart_ResourceModule = 20,

	// Client-only extension IDs. The current server uses 0-20.
	ShowToastMessage = 21,
	// Client-only extension IDs. 服务端实际使用 0-20 以及 21/23/24/25/27-31；
	// 22（UpdateCheatStatus）在服务端被显式视为非法空位（归档 1.5 客户端留下的），
	// 发过去会被丢弃并只留一行服务端日志 —— 客户端目前没有发送点，保留定义仅作兼容。
	UpdateCheatStatus = 22,
	DockTransaction = 23,
	TimeWarp = 24,
	// 【已停用 2026-10-06】原因：权限分组以后并进密码系统，不再单设"实验性访问"口令门。
	// 消息号保留占位 —— 消息号永不改，重编号会让老客户端错位解析。
	ExperimentalAccess = 25,
	P2PPeerOffer = 26,
	RenameRocket = 27,
	UpdatePartTemperature = 28,
	UpdateDestructionReason = 29,
	ServerCommand = 30,
	UploadLog = 31,

	// 新增 2026-10-10：客户端 -> 服务端，广告本端 P2P socket 实际绑定的 UDP 端口。
	// 不新增这一条就会打错门：服务端撮合用的端口取自主传输会话（观察到的端点），
	// 而 P2P socket 绑的是另一个随机空闲端口 —— 实测探测发 39 个、收到 0 个、永远握手超时。
	P2PEndpoint = 32
}
