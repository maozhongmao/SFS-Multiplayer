// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Reflection.Emit;
using HarmonyLib;
using Lidgren.Network;
using MultiplayerSFS.Common;
using SFS.Parts;
using SFS.UI;
using SFS.Variables;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

public class WorldEventSyncing
{
	[HarmonyPatch(typeof(RocketManager), "SpawnBlueprint")]
	public class RocketManager_SpawnBlueprint
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions, ILGenerator generator)
		{
			Label label_multiplayer = generator.DefineLabel();
			bool found_ldloc = false;
			foreach (CodeInstruction code in instructions)
			{
				if (!found_ldloc && code.opcode == OpCodes.Ldloc_3)
				{
					found_ldloc = true;
					yield return CodeInstruction.Call(typeof(RocketManager_SpawnBlueprint), "GetInMultiplayer");
					yield return new CodeInstruction(OpCodes.Brfalse, label_multiplayer);
					yield return new CodeInstruction(OpCodes.Ldloc_3);
					yield return CodeInstruction.Call(typeof(RocketManager_SpawnBlueprint), "SyncLaunch");
					yield return new CodeInstruction(OpCodes.Ret);
					yield return code.WithLabels(label_multiplayer);
				}
				else
				{
					yield return code;
				}
			}
		}

		public static bool GetInMultiplayer()
		{
			return ClientManager.multiplayerEnabled;
		}

		public static void SyncLaunch(Rocket[] rockets)
		{
			Menu.loading.Open("Sending launch request to server...");
			// 记录发起时间：LocalManager 会在收到回执时关闭加载屏，超时也会兜底关闭
			LocalManager.NotifyLaunchRequestSent();
			UnityEngine.Debug.Log($"[SFS-MP] launch request: {rockets.Length} rocket(s) to report (worldTime={ClientManager.world.WorldTime:F1}）");
			Dictionary<Rocket, int> dictionary = new Dictionary<Rocket, int>(rockets.Length);
			foreach (Rocket rocket in rockets)
			{
				LocalRocket localRocket = new LocalRocket(rocket);
				int num = LocalManager.unsyncedRockets.InsertNew(localRocket);
				Packet_CreateRocket request = new Packet_CreateRocket
				{
					WorldTime = ClientManager.world.WorldTime,
					LocalId = num,
					ForLaunch = true,
					Rocket = localRocket.ToState()
				};
				ClientManager.SendPacket(request, (NetDeliveryMethod)67);
				// 生成新火箭之前，先清掉发射台上所有没人操控的火箭：它们在本端是带碰撞箱的实体，
				// 和新火箭叠在一起会把发射流程卡死（程序未响应）。原生单人行为也是新火箭顶掉旧火箭。
				LocalManager.ClearUnmannedRocketsOnLaunchpad();
				// 登记重发：没等到回执就自动重发（同一 LocalId，服务端去重）
				LocalManager.RegisterLaunchRequest(num, request);
				dictionary.Add(rocket, num);
			}
			Rocket rocket2 = rockets.FirstOrDefault((Rocket r) => r.hasControl.Value) ?? ((rockets.Length != 0) ? rockets[0] : null);
			if (rocket2 != null && dictionary.TryGetValue(rocket2, out var value))
			{
				LocalManager.RequestControlForLocalRocket(value);
			}
		}
	}

	[HarmonyPatch(typeof(Part), "DestroyPart")]
	public class Part_DestroyPart
	{
		public static bool Prefix(Part __instance, bool createExplosion, ref DestructionReason reason, out Rocket __state)
		{
			// 【任务 #9】先把"零件所属的火箭"存进 Harmony 的 __state：Postfix 运行时零件可能已被
			// Unity 延迟销毁，那时不能再依赖 __instance 去取 Rocket。
			__state = __instance != null ? __instance.Rocket : null;
			if ((bool)ClientManager.multiplayerEnabled && GameManager.main != null)
			{
				if (reason == (DestructionReason)4)
				{
					reason = LocalManager.TrueDestructionReason;
					return true;
				}
				int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.Rocket);
				if (syncedRocketID == -1)
				{
					return false;
				}
				int localPartID = LocalManager.GetLocalPartID(syncedRocketID, __instance);
				if (localPartID == -1)
				{
					// 【根因修复 2026-10-10】这里以前是 return false：查不到零件 ID 就把整次销毁**否决**掉。
					// 后果是一条闭环（实测日志：19 次重建 + 4 条本错误）：
					//   销毁被否决 -> 本地永久多留一个零件 -> 本地与权威模型分叉
					//   -> 只能靠"整枚重建"兜底 -> 重建既修状态(同步)又重置插值器(抖动)
					//   -> 于是用户看到"抖动与不同步同时出现"，且"要撞一下（零件炸）才同步"。
					// 现在：不再否决 —— 游戏要求销毁就让它销毁（否则场上物体与权威模型必然对不上），
					//      改为立刻向服务端要这枚火箭的权威状态，让权威模型说了算，而不是让本地瞎猜。
					Debug.LogWarning("[SFS-MP] destroy part not mapped on rocket " + syncedRocketID
						+ "; allowing the destruction and requesting an authoritative resync");
					ClientManager.RequestRocketSnapshot(syncedRocketID);
					return true;
				}
				LocalPlayer player = LocalManager.Player;
				if (player == null)
				{
					return false;
				}
				bool flag = LocalManager.updateAuthority.Contains(syncedRocketID);
				bool flag2 = player.controlledRocket.Value == syncedRocketID;
				if (!flag)
				{
					return false;
				}
				// 修复 2026-10-10：原来这里是 !flag || !flag2 —— 即"我们是权威、但当前没在操控"的火箭，
				// 零件被物理摧毁时【既不销毁也不上报】✗，对面因此永远多留一个零件
				// （实测签名：反复出现 live = incoming + 1）。权威端必须如实上报。
				if (flag)
				{
					ClientManager.world.rockets[syncedRocketID].RemovePart(localPartID);
					ClientManager.SendPacket(new Packet_DestroyPart
					{
						WorldTime = ClientManager.world.WorldTime,
						RocketId = syncedRocketID,
						PartId = localPartID,
						CreateExplosion = createExplosion,
						Reason = reason
					}, (NetDeliveryMethod)67);
					return true;
				}
				return false;
			}
			return true;
		}

		// 【任务 #9】零件摧毁动作已完成（本地权威路径里 DestroyPart 包在 Prefix 已发出；
		// 远端路径由 Interpolator 应用后调用本方法），此时该零件的 id 不再会被任何发包/映射使用，
		// 清掉它留下的 null 条目是安全的，且只删 null、不删活体。
		// 注意：Unity 的 Object.Destroy 延迟到帧末生效，当帧可能还看不到 null；
		// LocalManager.CleanupStaleEntries 每秒的兜底扫描保证空条目长期不会堆积。
		public static void Postfix(Rocket __state)
		{
			if (!(bool)ClientManager.multiplayerEnabled) return;
			LocalManager.PruneDestroyedPartsFor(__state);
		}
	}

	[HarmonyPatch(typeof(RocketManager), "DestroyRocket")]
	public static class RocketManager_DestroyRocket
	{
		public static bool Prefix(Rocket rocket, ref DestructionReason reason)
		{
			if ((bool)ClientManager.multiplayerEnabled)
			{
				if (reason == (DestructionReason)4)
				{
					reason = LocalManager.TrueDestructionReason;
					return true;
				}
				int syncedRocketID = LocalManager.GetSyncedRocketID(rocket);
				if (syncedRocketID < 0)
				{
					return false;
				}
				if (LocalManager.Player == null)
				{
					return false;
				}
				if (LocalManager.updateAuthority.Contains(syncedRocketID))
				{
					ClientManager.world.rockets.Remove(syncedRocketID);
					LocalManager.syncedRockets.Remove(syncedRocketID);
					LocalManager.updateAuthority.Remove(syncedRocketID);
					ClientManager.SendPacket(new Packet_DestroyRocket
					{
						WorldTime = ClientManager.world.WorldTime,
						RocketId = syncedRocketID,
						Reason = reason
					}, (NetDeliveryMethod)67);
					return true;
				}
				return false;
			}
			return true;
		}

		public static void Postfix(Rocket rocket)
		{
			if (!ClientManager.multiplayerEnabled || !(PlayerController.main.player.Value is Rocket rocket2) || !(rocket2 != rocket))
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(rocket2);
			if (syncedRocketID >= 0)
			{
				ClientManager.RequestPlayerControl(syncedRocketID, ControlRequestOrigin.TopologyCommit);
				return;
			}
			syncedRocketID = LocalManager.GetUnsyncedRocketID(rocket2);
			if (syncedRocketID >= 0)
			{
				LocalManager.RequestControlForLocalRocket(syncedRocketID);
			}
			else
			{
				Debug.LogWarning("`RocketManager_DestroyRocket`: Player is controlling unregistered rocket!");
			}
		}
	}

	[HarmonyPatch(typeof(JointGroup), "RecreateRockets")]
	public class JointGroup_RecreateRockets
	{
		public static void Postfix(Rocket rocket, List<Rocket> childRockets)
		{
			if (!ClientManager.multiplayerEnabled)
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(rocket);
			if (syncedRocketID == -1)
			{
				return;
			}
			if (!LocalManager.updateAuthority.Contains(syncedRocketID))
			{
				foreach (Rocket childRocket in childRockets)
				{
					RocketManager.DestroyRocket(childRocket, (DestructionReason)4);
				}
				return;
			}
			LocalRocket localRocket = LocalManager.syncedRockets[syncedRocketID];
			localRocket.parts.Clear();
			foreach (Part item in rocket.partHolder.partsSet)
			{
				// 【确定性 id】运行时新增的零件也用同一套编号
				int newPartId = IDExtensions.DeterministicPartID(new PartSave(item));
				while (localRocket.parts.ContainsKey(newPartId)) newPartId = newPartId == int.MaxValue ? 1 : newPartId + 1;
				localRocket.parts.Add(newPartId, item);
			}
			RocketState rocketState = localRocket.ToState();
			ClientManager.world.rockets[syncedRocketID] = rocketState;
			ClientManager.SendPacket(new Packet_CreateRocket
			{
				WorldTime = ClientManager.world.WorldTime,
				GlobalId = syncedRocketID,
				Rocket = rocketState
			}, (NetDeliveryMethod)67);
			// 【重做 2026-10-10】本地乐观改写之后立刻向服务端要一次权威快照：
			// 服务端可能否决这次改写（例如权威已易主），那样本地就会永久分叉。
			// 有了这一次对齐，客户端最多一个来回就收敛到权威结构。
			ClientManager.RequestRocketSnapshot(syncedRocketID);
			foreach (Rocket childRocket2 in childRockets)
			{
				LocalRocket localRocket2 = new LocalRocket(childRocket2);
				RocketState rocket2 = localRocket2.ToState();
				int localId = LocalManager.unsyncedRockets.InsertNew(localRocket2);
				Packet_CreateRocket childRequest = new Packet_CreateRocket
				{
					WorldTime = ClientManager.world.WorldTime,
					LocalId = localId,
					Rocket = rocket2
				};
				ClientManager.SendPacket(childRequest, (NetDeliveryMethod)67);
				// 分离出的子火箭同样登记重发（首包容易丢，重发由服务端按 LocalId 去重）
				LocalManager.RegisterLaunchRequest(localId, childRequest);
			}
		}
	}

	[HarmonyPatch]
	public class StagingUpdates
	{
		public static IEnumerable<MethodBase> TargetMethods()
		{
			yield return AccessTools.Method(typeof(StagingDrawer), "AddStage");
			yield return AccessTools.Method(typeof(StagingDrawer), "UseStage");
			yield return AccessTools.Method(typeof(StagingDrawer), "OnReorder");
			yield return AccessTools.Method(typeof(StagingDrawer), "RemoveStage");
			yield return AccessTools.Method(typeof(StagingDrawer), "TogglePartSelected");
		}

		public static void Postfix(StagingDrawer __instance)
		{
			if (!ClientManager.multiplayerEnabled)
			{
				return;
			}
			Staging value = __instance.FieldRef<Staging_Local>("staging").Value;
			int syncedRocketID = LocalManager.GetSyncedRocketID(value.rocket);
			LocalPlayer player = LocalManager.Player;
			if (player == null)
			{
				return;
			}
			bool flag = player.controlledRocket.Value == syncedRocketID;
			if (!LocalManager.updateAuthority.Contains(syncedRocketID) || !flag)
			{
				return;
			}
			if (GameManager.main != null && LocalManager.syncedRockets.TryGetValue(syncedRocketID, out var value2))
			{
				List<StageState> list = new List<StageState>(value.stages.Count);
				foreach (Stage stage in value.stages)
				{
					StageState item = new StageState
					{
						stageID = stage.stageId,
						partIDs = (from id in stage.parts.Select(value2.GetPartID)
							where id >= 0
							select id).ToList()
					};
					list.Add(item);
				}
				ClientManager.world.rockets[syncedRocketID].stages = list;
				ClientManager.SendPacket(new Packet_UpdateStaging
				{
					WorldTime = ClientManager.world.WorldTime,
					RocketId = syncedRocketID,
					Stages = list
				}, (NetDeliveryMethod)67);
			}
			else
			{
				Debug.LogError("Missing local rocket when trying to send staging update!");
			}
		}
	}

	[HarmonyPatch(typeof(WorldTime), "Update")]
	public static class WorldTime_Update
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions, ILGenerator generator)
		{
			Label label_multiplayerStart = generator.DefineLabel();
			Label label_multiplayerEnd = generator.DefineLabel();
			bool found_ldarg = false;
			bool found_stfld = false;
			foreach (CodeInstruction code in instructions)
			{
				if (found_ldarg && code.opcode == OpCodes.Ldarg_0)
				{
					yield return code.WithLabels(label_multiplayerStart);
					found_ldarg = false;
				}
				else if (found_stfld && code.opcode == OpCodes.Ldarg_0)
				{
					yield return code.WithLabels(label_multiplayerEnd);
					found_stfld = false;
				}
				else
				{
					yield return code;
				}
				if (code.opcode == OpCodes.Stloc_0)
				{
					yield return new CodeInstruction(OpCodes.Ldsfld, AccessTools.Field(typeof(ClientManager), "multiplayerEnabled"));
					yield return new CodeInstruction(OpCodes.Callvirt, AccessTools.PropertyGetter(typeof(Obs<bool>), "Value"));
					yield return new CodeInstruction(OpCodes.Brfalse, label_multiplayerStart);
					yield return new CodeInstruction(OpCodes.Call, AccessTools.Method(typeof(LocalManager), "Update"));
					// yield return new CodeInstruction(OpCodes.Call, AccessTools.Method(typeof(ChatWindow), "Update"));
					yield return new CodeInstruction(OpCodes.Ldsfld, AccessTools.Field(typeof(ClientManager), "world"));
					yield return new CodeInstruction(OpCodes.Callvirt, AccessTools.PropertyGetter(typeof(WorldState), "WorldTime"));
					yield return new CodeInstruction(OpCodes.Stloc_1);
					yield return new CodeInstruction(OpCodes.Ldarg_0);
					yield return new CodeInstruction(OpCodes.Ldloc_1);
					yield return new CodeInstruction(OpCodes.Stfld, AccessTools.Field(typeof(WorldTime), "worldTime"));
					yield return new CodeInstruction(OpCodes.Br, label_multiplayerEnd);
					found_ldarg = true;
				}
				if (code.opcode == OpCodes.Stfld)
				{
					found_stfld = true;
				}
			}
		}
	}

	[HarmonyPatch(typeof(Rocket), "SFS.World.I_Physics.OnFixedUpdate")]
	public static class Rocket_OnFixedUpdate
	{
		public static bool Prefix(Rocket __instance)
		{
			if (!ClientManager.multiplayerEnabled)
			{
				return true;
			}
			if (__instance.rb2d.bodyType != RigidbodyType2D.Dynamic)
			{
				return false;
			}
			if (LocalManager.Player == null)
			{
				return false;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(__instance);
			if (syncedRocketID == -1)
			{
				return false;
			}
			if (LocalManager.updateAuthority.Contains(syncedRocketID))
			{
				return true;
			}
			return false;
		}
	}
}
