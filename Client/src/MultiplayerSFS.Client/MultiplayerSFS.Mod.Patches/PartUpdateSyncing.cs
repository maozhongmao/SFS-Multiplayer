// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Threading;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using HarmonyLib;
using Lidgren.Network;
using MultiplayerSFS.Common;
using SFS.Parts;
using SFS.Parts.Modules;
using SFS.World;

namespace MultiplayerSFS.Mod.Patches;

public class PartUpdateSyncing
{
	[HarmonyPatch(typeof(DockingPortModule), "Dock")]
	public static class DockingPortModule_Dock
	{
		private static DateTime lastRequestUtc;
	private static int nextDockTransactionId;

		public static bool Prefix(DockingPortModule __instance, DockingPortModule otherPort)
		{
			if ((bool)ClientManager.multiplayerEnabled)
			{
				int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.Rocket);
				int syncedRocketID2 = LocalManager.GetSyncedRocketID(otherPort.Rocket);
				if (syncedRocketID == -1 || syncedRocketID2 == -1)
				{
					return false;
				}
				if ((DateTime.UtcNow - lastRequestUtc).TotalMilliseconds < 250) return false;
				lastRequestUtc = DateTime.UtcNow;
				Part keepPart = __instance.GetComponentInParent<Part>();
				Part removePart = otherPort.GetComponentInParent<Part>();
				ClientManager.SendPacket(new Packet_DockTransaction
				{
					TransactionId = Interlocked.Increment(ref nextDockTransactionId),
					Operation = DockTransactionOperation.Dock,
					Committed = false,
					KeepRocketId = syncedRocketID,
					RemoveRocketId = syncedRocketID2,
					KeepPartId = LocalManager.GetLocalPartID(syncedRocketID, keepPart),
					RemovePartId = LocalManager.GetLocalPartID(syncedRocketID2, removePart),
					WorldTime = ClientManager.world.WorldTime
				});
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(DockingPortModule), "Draw")]
	public static class DockingPortModule_Draw
	{
		public static void Postfix(DockingPortModule __instance, StatsMenu drawer, PartDrawSettings settings)
		{
			if (!ClientManager.multiplayerEnabled || !settings.game || !__instance.isOccupied) return;
			drawer.DrawButton(72, "Multiplayer docking", "Undock", delegate
			{
				Part part = __instance.GetComponentInParent<Part>();
				int rocketId = LocalManager.GetSyncedRocketID(__instance.Rocket);
				if (rocketId == -1) return;
				PartJoint joint = __instance.Rocket.jointsGroup.GetConnectedJoints(part)
					.FirstOrDefault(candidate => candidate.GetOtherPart(part).GetModules<DockingPortModule>().Length > 0);
				if (joint == null) return;
				Part other = joint.GetOtherPart(part);
				ClientManager.SendPacket(new Packet_DockTransaction
				{
					TransactionId = System.Environment.TickCount,
					Operation = DockTransactionOperation.Undock,
					Committed = false,
					KeepRocketId = rocketId,
					RemoveRocketId = -1,
					KeepPartId = LocalManager.GetLocalPartID(rocketId, part),
					RemovePartId = LocalManager.GetLocalPartID(rocketId, other),
					WorldTime = ClientManager.world.WorldTime
				});
			}, true);
		}
	}

	[HarmonyPatch(typeof(RocketManager), "MergeRockets")]
	public class RocketManager_MergeRockets
	{
		public static void Postfix(Rocket rocket_A, Rocket rocket_B)
		{
			if (!ClientManager.multiplayerEnabled || rocket_A == rocket_B)
			{
				return;
			}
			// Multiplayer docking is committed by the server through Packet_DockTransaction.
		}
	}

	[HarmonyPatch(typeof(EngineModule), "Start")]
	public class EngineModule_Start
	{
		public static void Postfix(EngineModule __instance)
		{
			if (GameManager.main != null && (bool)ClientManager.multiplayerEnabled)
			{
				__instance.engineOn.OnChange += new Action<bool, bool>(OnToggle);
			}
			void OnToggle(bool engineOn_old, bool engineOn_new)
			{
				if (engineOn_old != engineOn_new)
				{
					int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponentInParentTree<Rocket>());
					if (syncedRocketID >= 0)
					{
						LocalPlayer player = LocalManager.Player;
						if (player != null)
						{
							bool flag = player.controlledRocket.Value == syncedRocketID;
							if (LocalManager.updateAuthority.Contains(syncedRocketID) && flag)
							{
								Part componentInParent = __instance.GetComponentInParent<Part>();
								int localPartID = LocalManager.GetLocalPartID(syncedRocketID, componentInParent);
								ClientManager.SendPacket(new Packet_UpdatePart_EngineModule
								{
									WorldTime = ClientManager.world.WorldTime,
									RocketId = syncedRocketID,
									PartId = localPartID,
									EngineOn = engineOn_new
								}, (NetDeliveryMethod)67);
							}
						}
					}
				}
			}
		}
	}

	[HarmonyPatch]
	public class BoosterModuleUpdates
	{
		public static IEnumerable<MethodBase> TargetMethods()
		{
			yield return AccessTools.Method(typeof(BoosterModule), "FixedUpdate");
			yield return AccessTools.Method(typeof(BoosterModule), "Fire");
			yield return AccessTools.Method(typeof(BoosterModule), "Fire_Instantly");
		}

		public static void Prefix(BoosterModule __instance, out (bool primed, float throttle) __state)
		{
			__state.primed = __instance.boosterPrimed.Value;
			__state.throttle = __instance.throttle_Out.Value;
		}

		public static void Postfix(BoosterModule __instance, (bool primed, float throttle) __state, MethodBase __originalMethod)
		{
			bool value = __instance.boosterPrimed.Value;
			float value2 = __instance.throttle_Out.Value;
			if (!ClientManager.multiplayerEnabled || !(GameManager.main != null) || (value == __state.primed && value2 == __state.throttle))
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponentInParentTree<Rocket>());
			if (syncedRocketID < 0)
			{
				return;
			}
			LocalPlayer player = LocalManager.Player;
			if (player != null)
			{
				bool flag = player.controlledRocket.Value == syncedRocketID;
				if (LocalManager.updateAuthority.Contains(syncedRocketID) && flag)
				{
					Part componentInParent = __instance.GetComponentInParent<Part>();
					int localPartID = LocalManager.GetLocalPartID(syncedRocketID, componentInParent);
					ClientManager.SendPacket(new Packet_UpdatePart_BoosterModule
					{
						WorldTime = ClientManager.world.WorldTime,
						RocketId = syncedRocketID,
						PartId = localPartID,
						Primed = value,
						Throttle = value2,
						FuelPercent = __instance.fuelPercent.Value
					}, (NetDeliveryMethod)67);
				}
			}
		}
	}

	[HarmonyPatch(typeof(WheelModule), "ToggleEnabled")]
	public static class WheelModule_ToggleEnabled
	{
		public static void Postfix(WheelModule __instance)
		{
			if (!(GameManager.main != null) || !ClientManager.multiplayerEnabled)
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponentInParentTree<Rocket>());
			if (syncedRocketID < 0)
			{
				return;
			}
			LocalPlayer player = LocalManager.Player;
			if (player != null)
			{
				bool flag = player.controlledRocket.Value == syncedRocketID;
				if (LocalManager.updateAuthority.Contains(syncedRocketID) && flag)
				{
					Part componentInParent = __instance.GetComponentInParent<Part>();
					int localPartID = LocalManager.GetLocalPartID(syncedRocketID, componentInParent);
					ClientManager.SendPacket(new Packet_UpdatePart_WheelModule
					{
						WorldTime = ClientManager.world.WorldTime,
						RocketId = syncedRocketID,
						PartId = localPartID,
						WheelOn = __instance.on.Value
					}, (NetDeliveryMethod)67);
				}
			}
		}
	}

	[HarmonyPatch(typeof(ParachuteModule), "Start")]
	public class ParachuteModule_Start
	{
		public static void Postfix(ParachuteModule __instance)
		{
			if (GameManager.main != null && (bool)ClientManager.multiplayerEnabled)
			{
				__instance.targetState.OnChange += new Action(UpdateState);
			}
			void UpdateState()
			{
				int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponentInParentTree<Rocket>());
				if (syncedRocketID >= 0)
				{
					LocalPlayer player = LocalManager.Player;
					if (player != null)
					{
						bool flag = player.controlledRocket.Value == syncedRocketID;
						if (LocalManager.updateAuthority.Contains(syncedRocketID) && flag)
						{
							Part componentInParent = __instance.GetComponentInParent<Part>();
							int localPartID = LocalManager.GetLocalPartID(syncedRocketID, componentInParent);
							ClientManager.SendPacket(new Packet_UpdatePart_ParachuteModule
							{
								WorldTime = ClientManager.world.WorldTime,
								RocketId = syncedRocketID,
								PartId = localPartID,
								State = __instance.state.Value,
								TargetState = __instance.targetState.Value
							}, (NetDeliveryMethod)67);
						}
					}
				}
			}
		}
	}

	[HarmonyPatch(typeof(MoveModule), "Toggle")]
	public static class MoveModule_Toggle
	{
		public static void Postfix(MoveModule __instance)
		{
			if (!(GameManager.main != null) || !ClientManager.multiplayerEnabled)
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponentInParentTree<Rocket>());
			if (syncedRocketID < 0)
			{
				return;
			}
			LocalPlayer player = LocalManager.Player;
			if (player != null)
			{
				bool flag = player.controlledRocket.Value == syncedRocketID;
				if (LocalManager.updateAuthority.Contains(syncedRocketID) && flag)
				{
					Part componentInParent = __instance.GetComponentInParent<Part>();
					int localPartID = LocalManager.GetLocalPartID(syncedRocketID, componentInParent);
					ClientManager.SendPacket(new Packet_UpdatePart_MoveModule
					{
						WorldTime = ClientManager.world.WorldTime,
						RocketId = syncedRocketID,
						PartId = localPartID,
						Time = __instance.time.Value,
						TargetTime = __instance.targetTime.Value
					}, (NetDeliveryMethod)67);
				}
			}
		}
	}
}
