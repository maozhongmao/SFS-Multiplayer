// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using HarmonyLib;
using MultiplayerSFS.Common;
using MultiplayerSFS.Mod;
using SFS;
using SFS.Builds;
using SFS.UI;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

// 服务端作弊策略的 Harmony 落点。所有判断都走 CheatPolicy：
// 没收到策略（HasPolicy == false）时一律放行，因此离线/单机行为与打补丁前一致。
//
// 世界总闸（AllowsCheats / preventUse_Cheats / Cannot_Use_Cheats_In_Career）由游戏自己处理；
// 本文件只在策略要求时**加强**限制，从不放宽，也不会因为世界本来就不允许作弊而崩溃
// （所有字段访问都有 null 保护）。

[HarmonyPatch(typeof(SandboxSettings))]
internal static class CheatPolicy_SandboxSettings
{
	// 菜单打开：按策略把被禁单项拨回 false、隐藏解锁/购买按钮、显示游戏自带的禁止遮罩。
	[HarmonyPostfix]
	[HarmonyPatch("OnOpen")]
	public static void OnOpen_Postfix(SandboxSettings __instance)
	{
		CheatPolicy.EnforceMenuOpen(__instance);
	}

	// 游戏根据 AllowsCheats 算完 preventUse_Cheats 的显隐后再覆盖一次。
	[HarmonyPostfix]
	[HarmonyPatch("UpdatePreventUse")]
	public static void UpdatePreventUse_Postfix(SandboxSettings __instance)
	{
		CheatPolicy.EnforcePreventUse(__instance);
	}

	// 总开关关闭时，连"解锁作弊"按钮也不该出现。
	[HarmonyPostfix]
	[HarmonyPatch("ShowUnlockCheats")]
	public static void ShowUnlockCheats_Postfix(ref bool __result)
	{
		if (CheatPolicy.BlocksEntry) __result = false;
	}

	// 【2026-10-15 修复】点了开关数据变了、画面却不刷新（开关一直灰着，像不能用）。
	//
	// 症状：日志能打出 PASS ToggleXxx（点击确实到了），但开关视觉不变。
	// 原因：联机流程用反射调 WorldBaseManager.EnterWorld(WorldReference, ...) 时第一个参数
	//       传了 null（见 ClientManager.EnterWorldScene），于是 Base.worldBase.paths 为 null；
	//       而游戏 SandboxSettings::OnToggle 的第一句就是 worldBase.paths.SaveWorldSettings(...)，
	//       它抛 NullReferenceException，把后面的 UpdateUI(false) / onToggleCheat.Invoke() 全带崩，
	//       ToggleButton::OnClick 里紧随其后的 UpdateUI(false) 也执行不到 —— 数据变了，开关没变。
	//
	// 修法：只在“路径为空”时接管 OnToggle：跳过会崩的本地存档那一步，把原方法剩下的
	//       刷新步骤（UpdateUI、建造区大小、onToggleCheat 广播）照原样补上。
	//       单机 / 正常进世界（paths != null）返回 true，游戏原逻辑完全不变。
	// IL 依据（Assembly-CSharp.full.il 170583-170618）：SaveWorldSettings 170591-170595、
	//   UpdateUI(false) 170596-170598、BuildManager.UpdateBuildSpaceSize 170599-170606、
	//   onToggleCheat.Invoke 170608-170616。
	[HarmonyPrefix]
	[HarmonyPatch("OnToggle")]
	public static bool OnToggle_Prefix(SandboxSettings __instance)
	{
		if (__instance == null) return false;
		// 路径正常：交还给游戏，行为与打补丁前一致（单机绝不受影响）。
		if (Base.worldBase != null && Base.worldBase.paths != null) return true;
		// 联机（paths == null）：跳过会抛 NRE 的 SaveWorldSettings，自己完成后续刷新。
		try
		{
			__instance.UpdateUI(false);
			if (BuildManager.main != null)
			{
				BuildManager.main.buildGridSize.UpdateBuildSpaceSize();
			}
			__instance.onToggleCheat?.Invoke();
		}
		catch (System.Exception e)
		{
			Debug.LogWarning(CheatPolicy.LogPrefix + "OnToggle fallback failed: " + e.GetType().Name + ": " + e.Message);
		}
		return false;
	}

	// 每次任意作弊项被切换后（onToggleCheat 的汇聚点），把被禁项强制拨回 false。
	[HarmonyPostfix]
	[HarmonyPatch("OnToggle")]
	public static void OnToggle_Postfix(SandboxSettings __instance)
	{
		CheatPolicy.EnforceSettings(__instance, "toggle");
	}

	// 单项拦截：Prefix 返回 false 时原方法不执行，玩家点了也打不开。
	// 单项为 true 时返回 true —— 允许，但不主动帮玩家打开。
	[HarmonyPrefix]
	[HarmonyPatch("ToggleInfiniteFuel")]
	public static bool ToggleInfiniteFuel_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.InfiniteFuel, "ToggleInfiniteFuel");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleInfiniteBuildArea")]
	public static bool ToggleInfiniteBuildArea_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.InfiniteBuildArea, "ToggleInfiniteBuildArea");
	}

	[HarmonyPrefix]
	[HarmonyPatch("TogglePartClipping")]
	public static bool TogglePartClipping_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.PartClipping, "TogglePartClipping");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleNoAtmosphericDrag")]
	public static bool ToggleNoAtmosphericDrag_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.NoAtmosphericDrag, "ToggleNoAtmosphericDrag");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleNoBurnMarks")]
	public static bool ToggleNoBurnMarks_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.NoBurnMarks, "ToggleNoBurnMarks");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleNoGravity")]
	public static bool ToggleNoGravity_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.NoGravity, "ToggleNoGravity");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleNoHeatDamage")]
	public static bool ToggleNoHeatDamage_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.NoHeatDamage, "ToggleNoHeatDamage");
	}

	[HarmonyPrefix]
	[HarmonyPatch("ToggleUnbreakableParts")]
	public static bool ToggleUnbreakableParts_Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.UnbreakableParts, "ToggleUnbreakableParts");
	}

	// 【通用修复 2026-10-15（与策略无关，联机专用）】
	// 联机世界里 WorldBaseManager.paths 是 null —— mod 反射调用 EnterWorld 时第一个参数传的是 null。
	// 于是 SandboxSettings::OnToggle 的第一句 worldBase.paths.SaveWorldSettings(...) 直接抛
	// NullReferenceException，导致它后面的 UpdateUI(false) 与 onToggleCheat 全部被跳过。
	// 表现（实测）：点开关后【数值生效了，但开关的显示一直停在关/灰】——玩家以为没点动。
	// 这里在 paths 为空时自己把那条刷新链走完，并跳过会抛异常的原方法；
	// 单机（paths 正常）一律走原路，行为完全不变。
	[HarmonyPrefix]
	[HarmonyPatch("OnToggle")]
	public static bool OnToggle_NullPaths_Prefix(SandboxSettings __instance)
	{
		if (SFS.Base.worldBase == null || SFS.Base.worldBase.paths != null) return true;
		try
		{
			// IL 170620：让 8 个 ToggleButton 各自 UpdateUI(false)（动画即时到位）
			__instance.UpdateUI(false);

			// IL 170599-170606：建造区大小跟随刷新
			if (SFS.Builds.BuildManager.main != null)
			{
				SFS.Builds.BuildManager.main.buildGridSize.UpdateBuildSpaceSize();
			}

			// IL 170608-170616：游戏自己的"作弊项变化"回调（私有字段，用反射拿）
			System.Reflection.FieldInfo field = AccessTools.Field(typeof(SandboxSettings), "onToggleCheat");
			System.Action callback = (field != null) ? (field.GetValue(__instance) as System.Action) : null;
			if (callback != null) callback.Invoke();

			CheatPolicy.Log("OnToggle: worldBase.paths is null (multiplayer) -> refreshed UI manually, skipped SaveWorldSettings");
		}
		catch (System.Exception e)
		{
			Debug.LogWarning("[SFS-MP] manual OnToggle refresh failed: " + e.Message);
		}
		return false;   // 跳过会抛 NullReferenceException 的原方法
	}
}

// 世界总闸 AllowsCheats —— 这是【作弊面板能不能出现】的第一道门。
//
// 为什么必须单独补这一块（血泪教训）：
//   客户端此前只会"隐藏"（策略关时藏入口），却从来不会"打开" —— 而联机世界的
//   AllowsCheats 来自本地存档/世界设置，默认是 false，于是无论策略怎么配，
//   游戏自带的 Cheat 入口根本不会出现。这就是"装了却没作弊"的真正原因。
//
// 实现方式：不写死声明类型（不同游戏版本可能变），而是在启动时扫描 Assembly-CSharp 里的
// 属性 AllowsCheats，拿到它的 getter 再挂 Prefix。
//   - 没有策略（单机/未连服）→ 返回 true，游戏原样返回，行为与打补丁前完全一致 ✓
//   - 有策略 → 直接把结果覆写成策略值：enabled=true 放行，false 关门 ✓
internal static class CheatPolicy_AllowsCheats
{
	private static bool installed;

	public static void Install(Harmony harmony)
	{
		if (installed) return;
		try
		{
			// 反编译 IL 证据（Assembly-CSharp.full.il）：
			//   WorldBaseManager.get_AllowsCheats() → callvirt WorldMode::get_AllowsCheats()
			//   WorldMode.get_AllowsCheats()       → return (mode == 0)        ← 真正判断在这里
			// 只挂 WorldBaseManager 那层不够：任何直接读 WorldMode 的 UI 代码会绕过它，
			// 于是"策略允许了，入口依然不出现"。所以把声明了 AllowsCheats 的类型全部挂上。
			System.Collections.Generic.List<System.Reflection.MethodInfo> getters = FindGetters();
			if (getters.Count == 0)
			{
				Debug.LogWarning("[SFS-MP][cheat] AllowsCheats property not found; world cheat gate left untouched");
				return;
			}
			// 注意：不能写 GetMethod(nameof(Prefix)) —— 它默认只搜 public，而 Prefix 是 private static，
			// 返回 null 后 HarmonyMethod 会抛 ArgumentNullException，外层 catch 就把它吞成
			// "failed to hook AllowsCheats: Value cannot be null."（实测就是这样挂不上的）。
			// 显式给 BindingFlags；Prefix 本身也一并改成 public 做双保险。
			System.Reflection.MethodInfo prefix = typeof(CheatPolicy_AllowsCheats).GetMethod(
				nameof(Prefix),
				System.Reflection.BindingFlags.Static | System.Reflection.BindingFlags.Public
				| System.Reflection.BindingFlags.NonPublic);
			if (prefix == null) throw new System.InvalidOperationException("prefix method not found");
			foreach (System.Reflection.MethodInfo getter in getters)
			{
				harmony.Patch(getter, prefix: new HarmonyMethod(prefix));
				Debug.Log("[SFS-MP][cheat] hooked AllowsCheats getter on " + getter.DeclaringType.FullName);
			}
			installed = true;
		}
		catch (System.Exception e)
		{
			Debug.LogWarning("[SFS-MP][cheat] failed to hook AllowsCheats: " + e.Message);
		}
	}

	private static System.Collections.Generic.List<System.Reflection.MethodInfo> FindGetters()
	{
		// 只在游戏主程序集里找，避免把所有程序集都扫一遍拖慢启动。
		// 同一版本里有多个类型声明 AllowsCheats（WorldBaseManager 转发、WorldMode 真正判断），
		// 全部收集起来逐个挂，才能堵住"直接读 WorldMode"的旁路。
		System.Collections.Generic.List<System.Reflection.MethodInfo> found =
			new System.Collections.Generic.List<System.Reflection.MethodInfo>();
		System.Reflection.Assembly asm = typeof(SandboxSettings).Assembly;
		foreach (System.Type t in asm.GetTypes())
		{
			System.Reflection.PropertyInfo p = t.GetProperty("AllowsCheats",
				System.Reflection.BindingFlags.Public | System.Reflection.BindingFlags.NonPublic
				| System.Reflection.BindingFlags.Instance | System.Reflection.BindingFlags.Static);
			if (p == null) continue;
			System.Reflection.MethodInfo g = p.GetGetMethod(true);
			if (g != null && g.ReturnType == typeof(bool)) found.Add(g);
		}
		return found;
	}

	// 没有策略时不干预（单机行为不变）；有策略时直接给答案。
	// public：让 GetMethod 默认搜索就能拿到（见上面 Install 里的注释）。
	public static bool Prefix(ref bool __result)
	{
		if (!CheatPolicy.HasPolicy && !CheatPolicy.HasLocalOverride) return true;
		__result = CheatPolicy.EffectiveEnabled;
		return false;
	}
}

// CheatsDrawer：屏幕上"当前生效的作弊"列表。策略关闭入口时整条隐藏。
[HarmonyPatch(typeof(CheatsDrawer), "UpdateText")]
internal static class CheatPolicy_CheatsDrawer
{
	[HarmonyPostfix]
	public static void Postfix(CheatsDrawer __instance)
	{
		CheatPolicy.EnforceDrawerVisibility(__instance);
	}
}

// Teleport 是一个动作（打开传送菜单），不是世界设置里的 bool，所以在入口处拦截。
//
// ⚠️ TeleportMenu 是 SFS 1.6 才有的类型：1.5 包里不存在这个类型，直接引用会导致
// 1.5 包编译失败（build-release.ps1 在 1.5 那一步整批中断）。1.5 面板本来也没有 Teleport 项，
// 所以这里用条件编译排除 —— 位图里的 bit8 在 1.5 上不生效，其余九项照常。
#if !SFS15
[HarmonyPatch(typeof(TeleportMenu), "OpenFromCheats")]
internal static class CheatPolicy_TeleportMenu
{
	[HarmonyPrefix]
	public static bool Prefix()
	{
		return CheatPolicy.AllowToggle(CheatFlag.Teleport, "TeleportMenu.OpenFromCheats");
	}
}
#endif

// 暂停菜单里动态生成的 "Open Cheats" 按钮：创建完成后按本地化文案识别并隐藏。
[HarmonyPatch(typeof(ButtonBuilder), "OnCreated")]
internal static class CheatPolicy_ButtonBuilder
{
	[HarmonyPostfix]
	public static void Postfix(NewElement element)
	{
		CheatPolicy.GuardEntryButton(element);
	}
}
