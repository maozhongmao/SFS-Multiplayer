// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using Lidgren.Network;

namespace MultiplayerSFS.Common;

// 服务端可控的作弊开关 —— 单项位图。
// 位号就是线上格式的一部分，两端必须一致，永远不要重排（重排会让老服务端/客户端的位错位）。
// 位号按【游戏内作弊面板的实际顺序】排列（与 SandboxSettings 的 Toggle*/字段一一对应）：
//   bit0 InfiniteBuildArea  面板 "Infinite Build Area"  -> data.infiniteBuildArea
//   bit1 PartClipping       面板 "Part Clipping"        -> data.partClipping
//   bit2 InfiniteFuel       面板 "Infinite Fuel"        -> data.infiniteFuel
//   bit3 NoAtmosphericDrag  面板 "No Atmospheric Drag"  -> data.noAtmosphericDrag
//   bit4 UnbreakableParts   面板 "No Collision Damage"  -> data.unbreakableParts
//   bit5 NoGravity          面板 "No Gravity"           -> data.noGravity
//   bit6 NoHeatDamage       面板 "No Heat Damage"       -> data.noHeatDamage
//   bit7 NoBurnMarks        面板 "No Burn Marks"        -> data.noBurnMarks
//   bit8 Teleport           面板 "Teleport"（按钮）      -> TeleportMenu::OpenFromCheats()
//   bit9 RefillFuelTanks    面板 "Refill Fuel Tanks"（按钮，一次性动作，无持久字段）
// 置 1 = 允许；置 0 = 禁止。未用到的 bit10..bit15 必须写 0，客户端忽略。
[Flags]
public enum CheatFlag : ushort
{
	None = 0,
	InfiniteBuildArea = 1 << 0,
	PartClipping = 1 << 1,
	InfiniteFuel = 1 << 2,
	NoAtmosphericDrag = 1 << 3,
	UnbreakableParts = 1 << 4,
	NoGravity = 1 << 5,
	NoHeatDamage = 1 << 6,
	NoBurnMarks = 1 << 7,
	Teleport = 1 << 8,
	RefillFuelTanks = 1 << 9,
	All = InfiniteBuildArea | PartClipping | InfiniteFuel | NoAtmosphericDrag | UnbreakableParts
		| NoGravity | NoHeatDamage | NoBurnMarks | Teleport | RefillFuelTanks
}

// PacketType.UpdateCheatStatus（编号 22，永不改动编号）原来定义的是"客户端上报 8 个 bool 作弊状态"，
// 但服务端一直把 22 当非法空位（归档 1.5 客户端留下的），客户端也从没有发送点 —— 该语义从未上线。
//
// 现在把它重定义为「服务端 -> 客户端」的作弊策略下发包：
//   bool Enabled           总开关。false = 客户端隐藏/禁用整个作弊入口（CheatsDrawer + 暂停菜单里的 Open Cheats 按钮）。
//   ushort IndividualFlags 10 位位图（见 CheatFlag），逐项控制能否打开。
//
// 线上布局（按顺序，无对齐/padding）：
//   [0]     bool   Enabled
//   [1..2]  ushort IndividualFlags（小端）
// 服务端是权威方；客户端只接收并应用，不主动上报（保留带参构造以便将来上报）。
public class Packet_UpdateCheatStatus : Packet
{
	public bool Enabled { get; set; }

	public CheatFlag IndividualFlags { get; set; }

	public override PacketType Type => PacketType.UpdateCheatStatus;

	// 无参构造：反序列化 / 服务端下发路径用。
	public Packet_UpdateCheatStatus()
	{
	}

	// 带参构造：保留给将来"客户端 -> 服务端"上报本端作弊状态时使用（本次不发送）。
	public Packet_UpdateCheatStatus(bool enabled, CheatFlag individualFlags)
	{
		Enabled = enabled;
		IndividualFlags = individualFlags;
	}

	// 单项是否被允许：总开关关闭时一律不允许。
	public bool Allows(CheatFlag flag)
	{
		return Enabled && (IndividualFlags & flag) == flag;
	}

	public override void Serialize(NetOutgoingMessage msg)
	{
		((NetBuffer)msg).Write(Enabled);
		((NetBuffer)msg).Write((ushort)IndividualFlags);
	}

	public override void Deserialize(NetIncomingMessage msg)
	{
		Enabled = ((NetBuffer)msg).ReadBoolean();
		IndividualFlags = (CheatFlag)((NetBuffer)msg).ReadUInt16();
	}
}
