// SPDX-License-Identifier: MPL-2.0
using HarmonyLib;
using SFS.World;

namespace MultiplayerSFS.Mod.Patches
{
    // SFS 本体 EndMissionMenu 的 logText prefab 默认值是 "- Aaaaaaaaa..."。
    // Recover 打开窗口但没有任务日志时，这段占位文本会直接显示出来。
    [HarmonyPatch(typeof(EndMissionMenu), "OnOpen")]
    public static class EndMissionMenu_PlaceholderPatch
    {
        private static void Postfix(EndMissionMenu __instance)
        {
            if (__instance == null || __instance.logText == null) return;

            string text = __instance.logText.Text;
            if (string.IsNullOrEmpty(text)) return;

            if (text.Contains("Aaaaaaaaa"))
                __instance.logText.Text = string.Empty;
        }
    }
}
