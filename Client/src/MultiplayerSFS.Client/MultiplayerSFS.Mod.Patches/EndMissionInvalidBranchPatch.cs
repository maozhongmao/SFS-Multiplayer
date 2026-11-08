// SPDX-License-Identifier: MPL-2.0
using System.Collections.Generic;
using HarmonyLib;
using SFS.Logs;
using SFS.Stats;
using SFS.World;

namespace MultiplayerSFS.Mod.Patches
{
    // EndMissionMenu.ReplayMission 直接访问 LogManager.main.branches[startBranch]。
    // 某些联机/回放火箭的 stats.branch 是 -1，点击 Destroy 时抛 KeyNotFoundException，
    // 结果就是弹窗内容空白、按钮回调中断、窗口关不掉。
    [HarmonyPatch(typeof(EndMissionMenu), "ReplayMission")]
    public static class EndMissionMenu_InvalidBranchPatch
    {
        private static bool Prefix(
            int startBranch,
            ref List<(string, double, LogId)> __result,
            out bool space,
            out bool orbit,
            out bool moonLand)
        {
            space = false;
            orbit = false;
            moonLand = false;

            if (startBranch >= 0 &&
                LogManager.main != null &&
                LogManager.main.branches != null &&
                LogManager.main.branches.ContainsKey(startBranch))
            {
                return true;
            }

            __result = new List<(string, double, LogId)>();
            return false;
        }
    }
}
