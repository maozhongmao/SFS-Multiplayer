// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using MultiplayerSFS.Common;
using SFS.Translations;
using SFS.UI;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod;

// 服务端下发的作弊策略持有类 + 应用逻辑。
//
// 设计要点：
//  * 默认「没有策略」（HasPolicy == false）：AllowsToggle/BlocksEntry 全部放行，
//    离线、单机、连到不发策略的老服务端时，行为与打补丁之前完全一致。
//  * 只有真正收到 Packet_UpdateCheatStatus 才 HasPolicy = true 并开始生效。
//  * 断线时保留最后一次策略（不重置），只打一行日志说明 —— 需求 3 的要求。
//  * 服务端是权威；这里是**客户端侧**约束，不是反作弊（改 DLL 即可绕过，见 docs/cheats-client.md）。
//
// 日志统一前缀 [SFS-MP][cheat]，内容保持纯 ASCII，方便服务端/玩家 grep 排查。
public static class CheatPolicy
{
	public const string LogPrefix = "[SFS-MP][cheat] ";

	// 是否已经收到过服务端策略。false = 未受控，一切照旧。
	public static bool HasPolicy { get; private set; }

	// 总开关：false = 作弊入口不可见/不可用。
	public static bool Enabled { get; private set; } = true;

	// 单项位图（CheatFlag）。
	public static CheatFlag IndividualFlags { get; private set; } = CheatFlag.None;

	// ---- 本地测试覆盖（F8 调试面板）----
	// 覆盖存在时，所有判定（AllowToggle / BlocksEntry / IsAllowed / ForcesOff）一律以覆盖为准，
	// 服务端策略被暂时忽略但仍保留；ClearLocalOverride 后立即回到服务端策略（从未下发则回到全放行）。
	// 默认不存在覆盖：行为与打补丁前完全一致。
	public static bool HasLocalOverride { get; private set; }
	public static bool LocalEnabled { get; private set; } = true;
	public static CheatFlag LocalFlags { get; private set; } = CheatFlag.None;

	// 当前实际生效的策略来源：本地覆盖 > 服务端策略 > 默认全放行。
	public static bool HasEffectivePolicy
	{
		get { return HasLocalOverride || HasPolicy; }
	}

	public static bool EffectiveEnabled
	{
		get { return HasLocalOverride ? LocalEnabled : Enabled; }
	}

	public static CheatFlag EffectiveFlags
	{
		get { return HasLocalOverride ? LocalFlags : IndividualFlags; }
	}

	// 总开关是否要求隐藏/禁用整个作弊入口。
	public static bool BlocksEntry
	{
		get { return HasEffectivePolicy && !EffectiveEnabled; }
	}

	public static void Log(string message)
	{
		Debug.Log(LogPrefix + message);
	}

	private static void LogWarning(string message)
	{
		Debug.LogWarning(LogPrefix + message);
	}

	private static bool IsAllowed(CheatFlag flag)
	{
		if (!HasEffectivePolicy) return true;
		if (!EffectiveEnabled) return false;
		return (EffectiveFlags & flag) != 0;
	}

	// 单项是否必须强制关闭：策略里这一位是 0。
	public static bool ForcesOff(CheatFlag flag)
	{
		return HasEffectivePolicy && (EffectiveFlags & flag) == 0;
	}

	// 供 Harmony Prefix 调用：允许则返回 true 放行，拦截则返回 false 并打日志。
	public static bool AllowToggle(CheatFlag flag, string action)
	{
		// 【诊断 2026-10-15】放行也记一行：以前只在拦下时打日志，于是"点击根本没到达游戏"
		// 和"到达了但被拦"这两种情况在日志里长得一模一样 ✗。现在点一下就有一条 PASS/BLOCK，
		// 没有这一条 ⇒ 那个开关在 UI 上就是不可交互的（另查 UI 状态）。
		if (!HasEffectivePolicy)
		{
			Log("PASS(no policy) " + action);
			return true;
		}
		if (!EffectiveEnabled)
		{
			Log($"BLOCK {action}: total cheat switch is OFF ({Origin()}); ignoring click");
			return false;
		}
		if ((EffectiveFlags & flag) == 0)
		{
			Log($"BLOCK {action}: item '{flag}' is disabled by {Origin()} (flags=0x{(ushort)EffectiveFlags:X4})");
			return false;
		}
		Log("PASS " + action + " (flags=0x" + ((ushort)EffectiveFlags).ToString("X4") + ", " + Origin() + ")");
		return true;
	}

	// 本地覆盖来源描述，仅用于日志；无覆盖时逐字保持旧的 server policy 文案。
	private static string Origin()
	{
		return HasLocalOverride ? "local override" : "server policy";
	}

	// ---- F8 本地测试入口 ----
	// 打开/更新本地覆盖：立即生效，不需要重连、不需要重新进世界。
	public static void SetLocalOverride(bool enabled, CheatFlag flags)
	{
		HasLocalOverride = true;
		LocalEnabled = enabled;
		LocalFlags = flags;
		Log($"LOCAL OVERRIDE enabled={(enabled ? "true" : "false")} flags=0x{(ushort)flags:X4} [{Describe(flags)}]");
		ApplyToLiveWorld("local override");
	}

	// 清除本地覆盖：回到服务端策略；服务端从未下发时回到"全放行"。
	public static void ClearLocalOverride()
	{
		bool wasActive = HasLocalOverride;
		HasLocalOverride = false;
		LocalEnabled = true;
		LocalFlags = CheatFlag.None;
		Log(wasActive ? "LOCAL OVERRIDE cleared" : "LOCAL OVERRIDE cleared (none was active)");
		ApplyToLiveWorld("local override cleared");
	}

	// 收到服务端策略。必须在 Unity 主线程调用（ClientManager.HandlePacket 走主线程 UpdateNetwork）。
	public static void Receive(Packet_UpdateCheatStatus packet)
	{
		if (packet == null) return;
		bool changed = !HasPolicy
			|| Enabled != packet.Enabled
			|| IndividualFlags != packet.IndividualFlags;
		Enabled = packet.Enabled;
		IndividualFlags = packet.IndividualFlags;
		HasPolicy = true;
		if (!changed) return;
		Log($"policy <- server: enabled={(Enabled ? "true" : "false")} individualFlags=0x{(ushort)IndividualFlags:X4} [{Describe(IndividualFlags)}]");
		// 有本地覆盖时服务端策略只存不用，等清除覆盖后立即恢复生效。
		if (HasLocalOverride) Log("server policy stored but LOCAL OVERRIDE is active; effective policy follows the override");
		ApplyToLiveWorld("policy update");
	}

	// 断线：保留最后一次策略（需求 3 明确允许），但必须留下可排查的日志。
	public static void OnDisconnected()
	{
		if (!HasPolicy) return;
		Log($"disconnected from server; RETAINING last cheat policy (enabled={(Enabled ? "true" : "false")} individualFlags=0x{(ushort)IndividualFlags:X4}); offline behaviour keeps the server's last words");
	}

	private static string Describe(CheatFlag flags)
	{
		if (flags == CheatFlag.None) return "none";
		string result = string.Empty;
		Append(ref result, flags, CheatFlag.InfiniteBuildArea, "InfiniteBuildArea");
		Append(ref result, flags, CheatFlag.PartClipping, "PartClipping");
		Append(ref result, flags, CheatFlag.InfiniteFuel, "InfiniteFuel");
		Append(ref result, flags, CheatFlag.NoAtmosphericDrag, "NoAtmosphericDrag");
		Append(ref result, flags, CheatFlag.UnbreakableParts, "UnbreakableParts(NoCollisionDamage)");
		Append(ref result, flags, CheatFlag.NoGravity, "NoGravity");
		Append(ref result, flags, CheatFlag.NoHeatDamage, "NoHeatDamage");
		Append(ref result, flags, CheatFlag.NoBurnMarks, "NoBurnMarks");
		Append(ref result, flags, CheatFlag.Teleport, "Teleport");
		Append(ref result, flags, CheatFlag.RefillFuelTanks, "RefillFuelTanks");
		return result;
	}

	private static void Append(ref string text, CheatFlag flags, CheatFlag bit, string name)
	{
		if ((flags & bit) == 0) return;
		if (text.Length > 0) text += ",";
		text += name;
	}

	// 策略变化 / 菜单打开时，把当前世界设置里被禁用的项强制拨回 false。
	// 兼容世界设置 AllowsCheats=false：整个函数对 null 都做了保护，不会因此抛异常。
	public static void ApplyToLiveWorld(string source)
	{
		if (!HasEffectivePolicy) return;
		SandboxSettings main = SandboxSettings.main;

		// 逐项强制：被禁的项拨回 false（允许的项不动，让玩家自己决定）。
		EnforceSettings(main, source);

		// 【2026-10-15 补漏】这里以前漏了"总闸 / 遮罩"那一路：
		//   - 允许作弊时：必须主动把游戏自带的"禁止使用作弊"遮罩关掉；
		//   - 关闭入口时：强制显示该遮罩。
		// 为什么必须在这里做：联机世界里 SandboxSettings.UpdatePreventUse 可能根本不被调用，
		// 实测日志里 "entry allowed / entry disabled" 一次都没出现过 —— 也就是说光靠那个 Postfix
		// 永远等不到。策略到手立刻执行一次，才不依赖游戏自己的回调时机。
		EnforcePreventUse(main);
		DumpLive(source);

		if (BlocksEntry) HideDrawerIfNeeded(source);
		else EnforceMenuOpen(main);
	}

	// 只读诊断：把"游戏里的真实数据"和"策略认为的状态"打在一起。
	// 用于定位两类症状：① 开关点不动（看 overlay 是否还活着）② 点完生效但显示不同步（看 data 的真实值）。
	public static void DumpLive(string where)
	{
		try
		{
			SandboxSettings main = SandboxSettings.main;
			if (main == null) { Log("dump(" + where + "): SandboxSettings.main = null"); return; }
			SandboxSettings.Data data = main.settings;
			string values = data == null
				? "data=null"
				: ("fuel=" + data.infiniteFuel + " build=" + data.infiniteBuildArea + " clip=" + data.partClipping
					+ " drag=" + data.noAtmosphericDrag + " unbreak=" + data.unbreakableParts
					+ " grav=" + data.noGravity + " heat=" + data.noHeatDamage + " burn=" + data.noBurnMarks);
			Log("dump(" + where + "): hasPolicy=" + HasPolicy
				+ " enabled=" + (HasPolicy ? Enabled.ToString() : "-")
				+ " flags=0x" + ((ushort)EffectiveFlags).ToString("X4")
				+ " overlay=" + (main.preventUse_Cheats != null && main.preventUse_Cheats.gameObject != null
					&& main.preventUse_Cheats.gameObject.activeSelf)
				+ " | " + values);
		}
		catch (Exception e)
		{
			LogWarning("dump failed (" + where + "): " + e.GetType().Name + ": " + e.Message);
		}
	}

	// 只读诊断：把面板里所有开关/按钮的"可见 + 可交互"状态打出来。
	// 用途：日志已经证明 preventUse_Cheats 遮罩是关的（overlay=False），但开关在画面上是灰的，
	// 说明是控件自身 interactable=false（Unity 会把禁用项画灰）。这里直接点名是哪一个控件、
	// 它处于 active 还是 interactable 状态，不再靠猜。
	public static void DumpWidgets(SandboxSettings settings)
	{
		try
		{
			if (settings == null) return;
			System.Text.StringBuilder sb = new System.Text.StringBuilder("widgets:");
			foreach (UnityEngine.UI.Toggle t in settings.GetComponentsInChildren<UnityEngine.UI.Toggle>(true))
			{
				sb.Append(" [T:").Append(t.gameObject.name)
					.Append(" active=").Append(t.gameObject.activeInHierarchy)
					.Append(" interactable=").Append(t.interactable).Append(']');
			}
			foreach (SFS.UI.Button b in settings.GetComponentsInChildren<SFS.UI.Button>(true))
			{
				// 把内层真实的 Unity 控件状态也打出来：画面上"灰"通常是
				// UnityEngine.UI.Button/Toggle 的 interactable=false，或者 Behaviour.enabled=false，
				// 而 SFS.UI.Button 只是外壳，它的 activeInHierarchy 一直是 true（实测如此）。
				UnityEngine.UI.Button inner = b.GetComponent<UnityEngine.UI.Button>();
				UnityEngine.UI.Toggle innerToggle = b.GetComponent<UnityEngine.UI.Toggle>();
				sb.Append(" [B:").Append(b.gameObject.name)
					.Append(" active=").Append(b.gameObject.activeInHierarchy)
					.Append(" behEnabled=").Append(b.enabled)
					.Append(" uiBtn=").Append(inner == null ? "none" : inner.interactable.ToString())
					.Append(" uiToggle=").Append(innerToggle == null ? "none" : (innerToggle.interactable + "/" + innerToggle.isOn))
					.Append(']');
			}
			Log(sb.ToString());
		}
		catch (Exception e)
		{
			LogWarning("widget dump failed: " + e.GetType().Name + ": " + e.Message);
		}
	}

	public static void EnforceSettings(SandboxSettings settings, string source)
	{
		if (!HasEffectivePolicy || settings == null) return;
		try
		{
			SandboxSettings.Data data = settings.settings;
			if (data == null) return;
			bool changed = false;
			changed |= ForceOff(ref data.infiniteFuel, CheatFlag.InfiniteFuel, "InfiniteFuel", source);
			changed |= ForceOff(ref data.infiniteBuildArea, CheatFlag.InfiniteBuildArea, "InfiniteBuildArea", source);
			changed |= ForceOff(ref data.noGravity, CheatFlag.NoGravity, "NoGravity", source);
			changed |= ForceOff(ref data.noHeatDamage, CheatFlag.NoHeatDamage, "NoHeatDamage", source);
			changed |= ForceOff(ref data.unbreakableParts, CheatFlag.UnbreakableParts, "UnbreakableParts", source);
		changed |= ForceOff(ref data.partClipping, CheatFlag.PartClipping, "PartClipping", source);
		changed |= ForceOff(ref data.noAtmosphericDrag, CheatFlag.NoAtmosphericDrag, "NoAtmosphericDrag", source);
		changed |= ForceOff(ref data.noBurnMarks, CheatFlag.NoBurnMarks, "NoBurnMarks", source);
			if (changed) settings.UpdateUI(false);
		}
		catch (Exception e)
		{
			LogWarning($"enforce settings failed ({source}): {e.GetType().Name}: {e.Message}");
		}
	}

	private static bool ForceOff(ref bool value, CheatFlag flag, string name, string source)
	{
		if (!value) return false;
		if (!ForcesOff(flag)) return false;
		value = false;
		Log($"FORCE OFF '{name}': disabled by {Origin()} (flags=0x{(ushort)EffectiveFlags:X4}) at {source}");
		return true;
	}

	// 打开作弊菜单时：隐藏解锁/购买按钮、强制显示游戏自带的"禁止使用作弊"遮罩，
	// 并在 Teleport 被禁时禁用传送按钮（传送是动作而不是一个 bool 配置）。
	public static void EnforceMenuOpen(SandboxSettings settings)
	{
		if (!HasEffectivePolicy || settings == null) return;
		DumpLive("menu open");
		DumpWidgets(settings);
		EnforceSettings(settings, "menu open");
		if (BlocksEntry)
		{
			SetActive(settings.preventUse_Cheats, true, "preventUse_Cheats");
			// 1.5 的 SandboxSettings 没有 unlockCheatsButton / buyCheatsButton 之外的这些按钮，
			// 解锁入口在 1.5 上不存在，因此这两处用条件编译排除（1.6 照常）。
#if !SFS15
			SetActive(settings.unlockCheatsButton, false, "UnlockCheats");
			SetActive(settings.buyCheatsButton, false, "BuyCheats");
			Log("entry disabled: forced the game's own 'cheats not allowed' overlay, hid unlock/buy buttons");
#else
			Log("entry disabled: forced the game's own 'cheats not allowed' overlay");
#endif
		}
		else
		{
			// 允许作弊时：每次打开菜单都把游戏自带的"禁止使用作弊"遮罩清掉。
			// 必须在这里做：游戏在 SandboxSettings::OnOpen / UpdatePreventUse 里会按
			// !(insideWorld && AllowsCheats) 重新 SetActive 一次，时机在我们后面，
			// 光靠策略到手时清一次会被它覆盖回去 —— 表现就是"上面的开关点不动、下面按钮能点"。
			EnforcePreventUse(settings);
		}
		// teleportButton 同样是 1.6 才有的字段；1.5 面板没有传送项。
#if !SFS15
		if (!IsAllowed(CheatFlag.Teleport) && settings.teleportButton != null)
		{
			settings.teleportButton.SetEnabled(false);
			Log($"DISABLE Teleport button: disabled by {Origin()}");
		}
#endif
	}

	// 游戏自己算完 preventUse 后，再由策略覆盖一次。
	//
	// 这里同时处理【两个方向】，因为实测症状是"面板看得见、但开关按不动"：
	//   游戏渲染了作弊面板，却因为世界设置 AllowsCheats 为假，用自带遮罩 preventUse_Cheats
	//   盖住整片开关（灰色半透明那一层），于是点不动。
	//   - 策略关闭入口 ⇒ 强制显示遮罩（与游戏一致，双保险）。
	//   - 策略允许作弊 ⇒ 主动把遮罩关掉（清障）。这一条是"按不动"的正解：
	//     只在收到策略 / 打开本地覆盖后执行，单机与未连服时一律不碰，行为不变。
	public static void EnforcePreventUse(SandboxSettings settings)
	{
		if (!HasEffectivePolicy || settings == null) return;
		if (BlocksEntry)
		{
			SetActive(settings.preventUse_Cheats, true, "preventUse_Cheats");
			return;
		}
		SetActive(settings.preventUse_Cheats, false, "preventUse_Cheats(cleared)");
		Log("entry allowed: cleared the game's own 'cheats not allowed' overlay");
	}

	private static void SetActive(Component component, bool active, string name)
	{
		if (component == null) return;
		GameObject go = component.gameObject;
		if (go == null) return;
		if (go.activeSelf == active) return;
		go.SetActive(active);
		Log($"{(active ? "show" : "hide")} '{name}' (entry disabled by {Origin()})");
	}

	// CheatsDrawer.UpdateText 每次刷新后调用：入口被关闭时强制整条抽屉不可见。
	public static void EnforceDrawerVisibility(CheatsDrawer drawer)
	{
		if (drawer == null || !BlocksEntry) return;
		GameObject go = drawer.gameObject;
		if (go == null || !go.activeSelf) return;
		go.SetActive(false);
		Log($"hide CheatsDrawer: entry disabled by {Origin()}");
	}

	private static void HideDrawerIfNeeded(string source)
	{
		if (!BlocksEntry) return;
		try
		{
			CheatsDrawer drawer = UnityEngine.Object.FindObjectOfType<CheatsDrawer>();
			if (drawer != null) EnforceDrawerVisibility(drawer);
		}
		catch (Exception e)
		{
			LogWarning($"hide drawer failed ({source}): {e.GetType().Name}: {e.Message}");
		}
	}

	// 暂停菜单里的 "Open Cheats" 按钮由 ButtonBuilder 动态生成，没有稳定引用。
	// 这里在按钮创建完成（文本已写好）之后按本地化文案识别并隐藏它。
	// 诊断用：已经记录过的"作弊相关按钮文案"，避免每帧刷屏。
	private static readonly System.Collections.Generic.HashSet<string> loggedCheatButtons =
		new System.Collections.Generic.HashSet<string>();

	public static void GuardEntryButton(NewElement element)
	{
		if (element == null) return;

		// 【诊断 2026-10-15】把每个按钮文案里含 "cheat" 的都记一次（不论策略开关）。
		// 为什么需要：这段代码以前在 !BlocksEntry 时直接 return，于是"策略允许作弊"的情况下
		// 完全无法判断游戏到底有没有创建作弊入口按钮、它的文案是什么 —— 只能靠猜 ✗。
		// 现在把候选按钮文案打进日志，一眼就能看出入口在哪个菜单、叫什么名字。
		try
		{
			TextAdapter probe = element.GetComponentInChildren<TextAdapter>();
			string probeLabel = probe != null ? probe.Text : null;
			if (!string.IsNullOrEmpty(probeLabel)
				&& probeLabel.IndexOf("cheat", StringComparison.OrdinalIgnoreCase) >= 0
				&& loggedCheatButtons.Add(probeLabel.Trim()))
			{
				Log("cheat-entry candidate button: '" + probeLabel.Trim() + "' (BlocksEntry=" + BlocksEntry + ")");
			}
		}
		catch { }

		if (!BlocksEntry) return;
		try
		{
			TextAdapter text = element.GetComponentInChildren<TextAdapter>();
			if (text == null) return;
			string label = text.Text;
			if (string.IsNullOrEmpty(label)) return;
			string cheatsLabel = CheatEntryLabel();
			if (string.IsNullOrEmpty(cheatsLabel)) return;
			if (!string.Equals(label.Trim(), cheatsLabel.Trim(), StringComparison.Ordinal)) return;
			Button button = element.GetComponent<Button>();
			if (button == null) return;
			if (!button.gameObject.activeSelf) return;
			button.gameObject.SetActive(false);
			Log($"hide 'Open Cheats' menu entry button: entry disabled by {Origin()}");
		}
		catch (Exception e)
		{
			LogWarning($"cheat entry button guard failed: {e.GetType().Name}: {e.Message}");
		}
	}

	private static string CheatEntryLabel()
	{
		try
		{
			var translation = Loc.main;
			if (translation == null) return null;
			return (string)translation.Open_Cheats_Button;
		}
		catch (Exception)
		{
			return null;
		}
	}
}
