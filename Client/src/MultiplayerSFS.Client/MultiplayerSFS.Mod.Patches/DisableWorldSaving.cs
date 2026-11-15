// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using HarmonyLib;
using SFS.World;
using SFS.WorldBase;

namespace MultiplayerSFS.Mod.Patches;

public class DisableWorldSaving
{
	[HarmonyPatch(typeof(SavingCache), "UpdateWorldPersistent")]
	public class SavingCache_UpdateWorldPersistent
	{
		public static bool Prefix()
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				LocalManager.syncedRockets.Clear();
				LocalManager.unsyncedRockets.Clear();
				LocalManager.unsyncedToControl = -1;
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(SavingCache), "SaveWorldPersistent")]
	public class SavingCache_SaveWorldPersistent
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}

	[HarmonyPatch(typeof(GameManager), "Update")]
	public class GameManager_Update
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}

	[HarmonyPatch(typeof(WorldBaseManager), "UpdateWorldPlaytime")]
	public class WorldBaseManager_UpdateWorldPlaytime
	{
		public static bool Prefix(WorldBaseManager __instance)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				__instance.settings.playtime.lastPlayedTime_Ticks = DateTime.Now.Ticks;
				__instance.settings.playtime.totalPlayTime_Seconds += 10.0;
				return false;
			}
			return true;
		}
	}
}
