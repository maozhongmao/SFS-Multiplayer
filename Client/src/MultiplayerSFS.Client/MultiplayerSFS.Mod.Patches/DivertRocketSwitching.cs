// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using Lidgren.Network;
using MultiplayerSFS.Common;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

public class DivertRocketSwitching
{
	[HarmonyPatch(typeof(PlayerController), "SmoothChangePlayer")]
	public static class PlayerController_SmoothChangePlayer
	{
		public static bool Prefix(Player newPlayer)
		{
			if (TrySwitchPlayer(newPlayer))
			{
				if (PlayerController.main.player.Value == null)
				{
					PlayerController.main.player.Value = newPlayer;
				}
				return true;
			}
			return false;
		}
	}

	[HarmonyPatch(typeof(Rocket), "SetPlayerToBestControllable")]
	public static class Rocket_SetPlayerToBestControllable
	{
		public static bool ShouldRequestNativeSelection(bool multiplayerEnabled, int selectedRocketId, int currentRocketId)
		{
			return multiplayerEnabled && selectedRocketId >= 0 && selectedRocketId != currentRocketId;
		}

		public static void Postfix(Rocket[] rockets)
		{
			if (!(bool)ClientManager.multiplayerEnabled || rockets == null || PlayerController.main == null)
			{
				return;
			}
			Rocket selected = PlayerController.main.player.Value as Rocket;
			if (selected == null || Array.IndexOf(rockets, selected) < 0)
			{
				return;
			}
			int syncedId = LocalManager.GetSyncedRocketID(selected);
			if (syncedId >= 0)
			{
				int currentId = LocalManager.Player == null ? -1 : LocalManager.Player.controlledRocket.Value;
				if (ShouldRequestNativeSelection(true, syncedId, currentId))
				{
					ClientManager.RequestPlayerControl(syncedId, ControlRequestOrigin.NativeSelection);
				}
				return;
			}
			int localId = LocalManager.GetUnsyncedRocketID(selected);
			if (localId >= 0)
			{
				LocalManager.RequestControlForLocalRocket(localId);
			}
		}

	}

	[HarmonyPatch(typeof(GameSelector), "SwitchTo")]
	public static class GameSelector_SwitchTo
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions, ILGenerator generator)
		{
			bool found_ldsfld = false;
			Label label_CheckSwitch = generator.DefineLabel();
			foreach (CodeInstruction code in instructions)
			{
				if (!found_ldsfld && code.opcode == OpCodes.Ldsfld && code.operand is FieldInfo { Name: "view" })
				{
					found_ldsfld = true;
					yield return new CodeInstruction(OpCodes.Ldloc_0);
					yield return new CodeInstruction(OpCodes.Callvirt, AccessTools.PropertyGetter(typeof(MapPlayer), "Player"));
					yield return CodeInstruction.Call(typeof(DivertRocketSwitching), "TrySwitchPlayer");
					yield return new CodeInstruction(OpCodes.Brfalse, label_CheckSwitch);
				}
				if (found_ldsfld && code.opcode == OpCodes.Ret)
				{
					yield return code.WithLabels(label_CheckSwitch);
				}
				else
				{
					yield return code;
				}
			}
		}
	}

	public static bool TrySwitchPlayer(Player player)
	{
		if ((bool)ClientManager.multiplayerEnabled)
		{
			if (player is Rocket rocket)
			{
				int id = LocalManager.GetSyncedRocketID(rocket);
				if (id >= 0)
				{
					if (!Rocket_SetPlayerToBestControllable.ShouldRequestNativeSelection(
						(bool)ClientManager.multiplayerEnabled, id, LocalManager.Player != null ? LocalManager.Player.controlledRocket.Value : -1)) return true;
					ClientManager.RequestPlayerControl(id);
					return false;
				}
				id = LocalManager.GetUnsyncedRocketID(rocket);
				if (id >= 0)
				{
					LocalManager.RequestControlForLocalRocket(id);
					return false;
				}
				Debug.LogError("`TrySwitchPlayer`: `player` isn't registered!");
				return false;
			}
			if ((object)player == null)
			{
				Debug.LogError("`TrySwitchPlayer`: `player` is null!");
				return false;
			}
			Debug.LogError("`TrySwitchPlayer`: `player` is not a rocket!");
			return false;
		}
		return true;
	}
}
