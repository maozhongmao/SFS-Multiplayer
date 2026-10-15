// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using System.Linq;
using HarmonyLib;
using Lidgren.Network;
using SFS;
using SFS.Builds;
using SFS.Career;
using SFS.Input;
using SFS.Translations;
using SFS.UI;
using SFS.World;
using SFS.World.Maps;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

public class EditUI
{
	[HarmonyPatch(typeof(HubManager), "Start")]
	public class HubManager_Start
	{
		public static IEnumerable<CodeInstruction> Transpiler(IEnumerable<CodeInstruction> instructions)
		{
			List<CodeInstruction> list = instructions.ToList();
			for (int i = 0; i < list.Count; i++)
			{
				if (list[i].LoadsField(typeof(HubManager).GetField("resumeGameButton")))
				{
					list.RemoveRange(i - 1, 6);
					break;
				}
			}
			return list;
		}

		public static void Postfix(HubManager __instance)
		{
			__instance.FieldRef<Button>("resumeGameButton").SetEnabled(!ClientManager.multiplayerEnabled.Value && Base.worldBase.paths.CanResumeGame());
		}
	}

	[HarmonyPatch(typeof(HubManager), "OpenMenu")]
	public class HubManager_OpenMenu
	{
		public static bool Prefix(HubManager __instance)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ResourcesLoader.ButtonIcons buttonIcons = ResourcesLoader.main.buttonIcons;
				MenuGenerator.OpenMenu(CancelButton.Close, CloseMode.Current, new SizeSyncerBuilder(out var carrier).HorizontalMode(SizeMode.MaxChildSize), ButtonBuilder.CreateIconButton(carrier, buttonIcons.settings, () => Loc.main.Open_Settings_Button, Menu.settings.Open, CloseMode.None), ElementGenerator.VerticalSpace(10), ButtonBuilder.CreateIconButton(carrier, buttonIcons.exit, () => Loc.main.Exit_To_Main_Menu, delegate
				{
					ClientManager.Disconnect("Left world");
					__instance.ExitToMainMenu();
				}, CloseMode.None));
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(BuildManager), "OpenMenu")]
	public class BuildManager_OpenMenu
	{
		public static bool Prefix(BuildManager __instance)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ResourcesLoader.ButtonIcons buttonIcons = ResourcesLoader.main.buttonIcons;
				List<MenuElement> list = new List<MenuElement>();
				list.Add(new SizeSyncerBuilder(out var carrier).HorizontalMode(SizeMode.MaxChildSize));
				list.Add(new SizeSyncerBuilder(out var carrier2).HorizontalMode(SizeMode.MaxChildSize));
				list.Add(ElementGenerator.VerticalSpace(10));
				list.Add(ElementGenerator.DefaultHorizontalGroup(ButtonBuilder.CreateIconButton(carrier, buttonIcons.save, () => Loc.main.Save_Blueprint, __instance.OpenSaveMenu, CloseMode.None), ButtonBuilder.CreateIconButton(carrier, buttonIcons.load, () => Loc.main.Load_Blueprint, __instance.OpenLoadMenu, CloseMode.None)));
				list.Add(ElementGenerator.VerticalSpace(10));
				list.Add(ElementGenerator.DefaultHorizontalGroup(ButtonBuilder.CreateIconButton(carrier, buttonIcons.moveRocket, () => Loc.main.Move_Rocket_Button, MoveRocket, CloseMode.Current), ButtonBuilder.CreateIconButton(carrier, buttonIcons.clear, () => Loc.main.Clear_Confirm, __instance.AskClear, CloseMode.Current)));
				List<MenuElement> list2 = list;
				if (RemoteSettings.GetBool("Example_Rockets", defaultValue: true) || RemoteSettings.GetBool("Video_Tutorials", defaultValue: true))
				{
					list2.Add(ElementGenerator.VerticalSpace(25));
				}
				if (RemoteSettings.GetBool("Example_Rockets", defaultValue: true))
				{
					list2.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.exampleRockets, () => Loc.main.Example_Rockets_OpenMenu, OpenExampleRocketsMenu, CloseMode.None));
				}
				if (RemoteSettings.GetBool("Video_Tutorials", defaultValue: true))
				{
					list2.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.videoTutorials, () => Loc.main.Video_Tutorials_OpenButton, HomeManager.OpenTutorials_Static, CloseMode.None));
				}
				list2.Add(ElementGenerator.VerticalSpace(10));
				list2.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.shareRocket, () => Loc.main.Share_Button, __instance.UploadPC, CloseMode.Current));
				list2.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.settings, () => Loc.main.Open_Settings_Button, Menu.settings.Open, CloseMode.None));
				list2.Add(ElementGenerator.VerticalSpace(10));
				list2.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.exit, () => Loc.main.Exit_To_Space_Center, ExitToHub, CloseMode.None));
				MenuGenerator.OpenMenu(CancelButton.Close, CloseMode.Current, list2.ToArray());
				return false;
			}
			return true;
			static void ExitToHub()
			{
				Base.sceneLoader.LoadHubScene();
			}
			void MoveRocket()
			{
				__instance.selector.Select(__instance.buildGrid.activeGrid.partsHolder.GetArray());
			}
			void OpenExampleRocketsMenu()
			{
				AccessTools.Method(typeof(BuildManager), "OpenExampleRocketsMenu").Invoke(__instance, null);
			}
		}
	}

	[HarmonyPatch(typeof(GameManager), "OpenMenu")]
	public class GameManager_OpenMenu
	{
		public static bool Prefix(GameManager __instance)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ResourcesLoader.ButtonIcons buttonIcons = ResourcesLoader.main.buttonIcons;
				SizeSyncerBuilder.Carrier carrier;
				SizeSyncerBuilder.Carrier carrier2;
				List<MenuElement> list = new List<MenuElement>
				{
					new SizeSyncerBuilder(out carrier).HorizontalMode(SizeMode.MaxChildSize),
					new SizeSyncerBuilder(out carrier2).HorizontalMode(SizeMode.MaxChildSize),
					ButtonBuilder.CreateIconButton(carrier2, buttonIcons.newRocket, () => Loc.main.Build_New_Rocket, __instance.ExitToBuild, CloseMode.None),
					ElementGenerator.VerticalSpace(10),
					ButtonBuilder.CreateIconButton(carrier2, buttonIcons.settings, () => Loc.main.Open_Settings_Button, Menu.settings.Open, CloseMode.None),
					ElementGenerator.VerticalSpace(10),
					ButtonBuilder.CreateIconButton(carrier2, buttonIcons.exit, () => Loc.main.Exit_To_Space_Center, __instance.ExitToHub, CloseMode.None)
				};

				// 【作弊入口 2026-10-15】根因修复：这个 Prefix 直接 return false 接管了暂停菜单，
				// 于是原方法（反编译 IL 196910-196932）里"无条件创建 Open Cheats 入口"那几行
				// 永远不会执行 —— 入口按钮根本没被创建，跟世界设置 AllowsCheats 一点关系都没有
				// （IL 已证实联机世界里 mode=Sandbox、AllowsCheats=true、FullVersion 恒 true）。
				// 这里按原版完全相同的方式补一行：同一个 buttonIcons.cheats、同一个翻译键、
				// 同一个 onClick = SandboxSettings.main.Open（原版 IL 196925-196930）。
				// 服务端策略要求隐藏入口时（CheatPolicy.BlocksEntry）不加这一行。
				if (!CheatPolicy.BlocksEntry)
				{
					SandboxSettings sandbox = SandboxSettings.main;
					if (sandbox != null)
					{
						list.Add(ElementGenerator.VerticalSpace(10));
						list.Add(ButtonBuilder.CreateIconButton(carrier2, buttonIcons.cheats,
							() => Loc.main.Open_Cheats_Button, sandbox.Open, CloseMode.None));
					}
				}

				MenuGenerator.OpenMenu(CancelButton.Close, CloseMode.Current, list.ToArray());
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(MapIcon), "UpdateAlpha")]
	public static class MapIcon_UpdateAlpha
	{
		public static void Postfix(MapIcon __instance)
		{
			if (!ClientManager.multiplayerEnabled)
			{
				return;
			}
			int syncedRocketID = LocalManager.GetSyncedRocketID(__instance.GetComponent<Rocket>());
			foreach (LocalPlayer value in LocalManager.players.Values)
			{
				if (value.controlledRocket.Value == syncedRocketID)
				{
					SpriteRenderer componentInChildren = __instance.mapIcon.GetComponentInChildren<SpriteRenderer>();
						if (componentInChildren != null)
							componentInChildren.color = new Color(componentInChildren.color.r * value.iconColor.r, componentInChildren.color.g * value.iconColor.g, componentInChildren.color.b * value.iconColor.b, componentInChildren.color.a);
					break;
				}
			}
		}
	}

	[HarmonyPatch(typeof(Screen_Game), "ProcessInput")]
	private class Screen_Game_ProcessInput
	{
		private static bool Prefix()
		{
			// return !ChatWindow.InputSelected;
			return true;
		}
	}
}
