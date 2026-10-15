// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using HarmonyLib;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

// ResourceDrawer（火箭资源条）在 LateUpdate 里用"缓存的部件/资源数组"和当前部件逐个比较，
// 联机动态生成/销毁部件后，游戏侧缓存可能和实际部件数不一致 -> 每帧抛 IndexOutOfRangeException。
//
// 实测：单局 2905 次。每个异常都会写完整调用栈并触发一次"上传崩溃报告"（Unity 崩溃上报），
// 主线程被拖死 —— 表现为碰撞/战斗时明显卡顿，长时间累积还可能直接崩溃。
//
// 这里把这类越界异常吞掉（只提示一次），资源条照常重绘；根因在游戏侧缓存，模组无法从源头修。
[HarmonyPatch(typeof(ResourceDrawer), "CheckRedraw")]
public static class ResourceDrawer_CheckRedraw
{
    private static bool warned;

    public static Exception Finalizer(Exception __exception)
    {
        if (__exception is IndexOutOfRangeException || __exception is ArgumentOutOfRangeException)
        {
            if (!warned)
            {
                warned = true;
                Debug.LogWarning("[SFS-MP] suppressed a ResourceDrawer resource-bar index out-of-range exception"
                                 + " (the game-side resource bar cache disagrees with multiplayer part changes); further occurrences are not logged.");
            }
            return null;
        }
        return __exception;
    }
}
