// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using HarmonyLib;
using SFS.Stats;

namespace MultiplayerSFS.Mod.Patches;

public class DisableStats
{
	[HarmonyPatch(typeof(StatsRecorder), "Record")]
	public class StatsRecorder_Record
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}

	[HarmonyPatch(typeof(StatsRecorder), "OnCrash")]
	public class StatsRecorder_OnCrash
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}

	[HarmonyPatch(typeof(StatsRecorder), "OnSplit")]
	public class StatsRecorder_OnSplit
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}

	[HarmonyPatch(typeof(StatsRecorder), "OnMerge")]
	public class StatsRecorder_OnMerge
	{
		public static bool Prefix()
		{
			return !ClientManager.multiplayerEnabled.Value;
		}
	}
}
