// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.Linq;
using System.Reflection.Emit;
using HarmonyLib;
using SFS.Input;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

public class DisablePausing
{
	[HarmonyPatch(typeof(ScreenManager), "Awake")]
	public class ScreenManager_Awake
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
		{
			List<CodeInstruction> list = instructions.ToList();
			for (int i = 0; i < list.Count; i++)
			{
				if (list[i].Calls(AccessTools.PropertySetter(typeof(Time), "timeScale")))
				{
					list[i - 1].opcode = OpCodes.Nop;
					list.RemoveAt(i);
					break;
				}
			}
			return list;
		}

		public static void Postfix(ScreenManager __instance)
		{
			if (!ClientManager.multiplayerEnabled.Value && !__instance.selfInitialize)
			{
				Time.timeScale = 0f;
			}
		}
	}

	[HarmonyPatch(typeof(ScreenManager), "OpenScreen")]
	public class ScreenManager_OpenScreen
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
		{
			List<CodeInstruction> list = instructions.ToList();
			for (int i = 0; i < list.Count; i++)
			{
				if (list[i].Calls(AccessTools.PropertySetter(typeof(Time), "timeScale")))
				{
					list.RemoveRange(i - 14, 15);
					break;
				}
			}
			return list;
		}

		public static void Postfix(ScreenManager __instance)
		{
			if (!ClientManager.multiplayerEnabled.Value)
			{
				Time.timeScale = (__instance.CurrentScreen.PauseWhileOpen ? 0f : ((WorldTime.main != null) ? WorldTime.main.TimeScale : 1f));
			}
		}
	}
}
