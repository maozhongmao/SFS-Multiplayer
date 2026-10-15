// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Linq;
using System.Threading;
using System.Reflection;
using System.Threading.Tasks;
using System.Collections.Generic;
using HarmonyLib;
using UnityEngine;
using SFS;
using SFS.UI;
using SFS.Stats;
using SFS.World;
using SFS.Input;
using SFS.Builds;
using SFS.Career;
using SFS.WorldBase;
using SFS.World.Maps;
using MultiplayerSFS.Common;
using Environment = SFS.World.Environment;

namespace MultiplayerSFS.Mod.Patches
{
    /// <summary>
    /// Diverts loading of the world when in multiplayer.
    /// </summary>
    // TODO: These patches should probably be transpilers instead of overriding prefixes.
    public class DivertLoading
    {
        [HarmonyPatch(typeof(SavingCache), nameof(SavingCache.Preload_WorldPersistent))]
        public class SavingCache_Preload_WorldPersistent
        {
            public static bool Prefix(SavingCache __instance, bool needsRocketsAndBranches)
            {
                if (ClientManager.multiplayerEnabled.Value)
                {
                    ref SavingCache.Data<WorldSave> worldPersistent = ref __instance.FieldRef<SavingCache.Data<WorldSave>>("worldPersistent");

                    if (worldPersistent == null || needsRocketsAndBranches && (worldPersistent.result.data.rockets == null || worldPersistent.result.data.branches == null))
                    {
                        worldPersistent = new SavingCache.Data<WorldSave>
                        {
                            thread = new Thread
                            (
                                (ThreadStart) delegate
                                {
                                    __instance.FieldRef<SavingCache.Data<WorldSave>>("worldPersistent").result = (true, WorldSave.CreateEmptyQuicksave(Application.version), null);
                                }
                            )
                        };
                        worldPersistent.thread.Start();
                    }
                    return false;
                }
                return true;
            }
        }

        [HarmonyPatch(typeof(GameManager), "LoadPersistentAndLaunch")]
        public class GameManager_LoadPersistentAndLaunch
        {
            public static bool Prefix(GameManager __instance)
            {
                if (ClientManager.multiplayerEnabled.Value)
                {
#if SFS15
                    UnityEngine.Debug.Log("[SFS-MP][1.5] step 0 enter prefix (Preload next)");
#endif
                    SavingCache.main.Preload_WorldPersistent(true);
                    SavingCache.main.FieldRef<SavingCache.Data<WorldSave>>("worldPersistent") = null;
                    
                    AccessTools.Method(typeof(GameManager), "ClearWorld").Invoke(__instance, null);
                    CareerState.main.SetState(new WorldSave.CareerState());

                    // // Branches, challenges, logs
                    // TODO: Fix up errors related to branches, etc
                    
                    WorldTime.main.worldTime = ClientManager.world.WorldTime;
                    WorldTime.main.SetTimewarpIndex_ForLoad(0);
                    WorldView.main.SetViewLocation(Base.planetLoader.spaceCenter.LaunchPadLocation);
                    WorldView.main.viewDistance.Value = 32f;

#if SFS15
                    UnityEngine.Debug.Log("[SFS-MP][1.5] step L OnLoadWorld");
#endif
                    LocalManager.OnLoadWorld();

#if SFS15
                    UnityEngine.Debug.Log("[SFS-MP][1.5] step M AstronautState");
#endif
                    AstronautState.main.state = new WorldSave.Astronauts();
                    // // Astronauts loading
                    
                    Map.manager.mapMode.Value = false;
                    Map.view.view.target.Value = Base.planetLoader.spaceCenter.Planet.mapPlanet;
                    Map.view.view.position.Value = Base.planetLoader.spaceCenter.LaunchPadLocation.position;
                    Map.view.view.distance.Value = Base.planetLoader.spaceCenter.LaunchPadLocation.position.y * 0.65;
                    Map.navigation.SetTarget(Map.view.view.target.Value);
                   
                    PlayerController.main.player.Value = null;
                    PlayerController.main.cameraDistance.Value = 32f;
                    
                    if (__instance.environment.environments != null)
                    {
                        Environment[] environments = __instance.environment.environments;
                        foreach (Environment environment in environments)
                        {
                            environment.terrain?.LoadFully();
                        }
                    }
#if SFS15
                    UnityEngine.Debug.Log("[SFS-MP][1.5] step N ClearBranches");
#endif
                    LogManager.main.ClearBranches();

                    if (SavingCache.main.TryLoadBuildPersistent(MsgDrawer.main, out Blueprint buildPersistent, eraseCache: false))
                    {
                        RocketManager.SpawnBlueprint(buildPersistent);
                    }
#if SFS15
                    UnityEngine.Debug.Log("[SFS-MP][1.5] step O InstantlyRotateCamera");
#endif
                    GameCamerasManager.main.InstantlyRotateCamera();

                    return false;
                }
                return true;
            }
        }

        /// <summary>
        /// Prevents launching a rocket in multiplayer if the launchpad is currently occupied by another player.
        /// Also allows a launching player to clear the launchpad if it's only occupied by non-player rockets.
        /// </summary>
        [HarmonyPatch(typeof(BuildManager), "<Launch>g__Launch_2|33_5")]
        public static class BuildManager_Launch_2
        {
            [HarmonyReversePatch]
            public static void OrginalMethod(bool forceVertical) => throw new NotImplementedException("Harmony Reverse Patch");

            public static bool Prefix(bool forceVertical)
            {
                if (ClientManager.multiplayerEnabled && BuildManager.main.buildGrid.activeGrid.partsHolder.parts.Count > 0)
                {
                    ReplacementMethod(forceVertical);
                    return false;
                }
                return true;
            }

            public static async void ReplacementMethod(bool forceVertical)
            {
                HashSet<int> rockets = new HashSet<int>();
                HashSet<int> players = new HashSet<int>();

                bool confirmationOpen = true;
                bool confirmationVisible = true;
                int stackCount = ScreenManager.main.GetStackCount();

                void OnOpen()
                {
                    confirmationVisible = true;
                }

                void OnClose()
                {
                    // * `OnClose` is technically also closed when another screen is opened after the confirmation pop-up (like the debug console).
                    // * Therefore we have to differentiate between the confirmation pop-up actually being closed, or it being hidden by another screen.
                    if (ScreenManager.main.GetStackCount() > stackCount)
                    {
                        confirmationVisible = false;
                    }
                    else
                    {
                        confirmationOpen = false;
                    }
                }

                void ClearLaunchpad()
                {
                    confirmationOpen = false;
                    foreach (int id in rockets)
                    {
                        // 【安全护栏，2026-10-08 用户明确要求】绝不能删掉别人正在操控的火箭。
                        // 当前调用路径上 rockets 已经只含"无人控制"的（见 UpdateLaunchpadStatus），
                        // 但这里**再独立判一次** —— 免得以后有人改判定逻辑就把别人的船删了。
                        // "靠构造安全"不等于"靠代码安全"。
                        if (LocalManager.IsRocketControlledByAnyone(id))
                        {
                            Debug.Log($"[SFS-MP] ClearLaunchpad: skipped rocket {id} (controlled by a player)");
                            continue;
                        }
                        LocalManager.syncedRockets.Remove(id);
                        LocalManager.updateAuthority.Remove(id);
                        ClientManager.world.rockets.Remove(id);
                        ClientManager.SendPacket
                        (
                            new Packet_DestroyRocket()
                            {
                                RocketId = id,
                            }
                        );
                    }
                    OrginalMethod(forceVertical);
                }

                while (confirmationOpen)
                {
                    if (UpdateLaunchpadStatus(ref rockets, ref players, out bool updateText))
                    {
                        OrginalMethod(forceVertical);
                        return;
                    }
                    else if (confirmationVisible && updateText)
                    {
                        ScreenManager.main.CloseStack();
                        confirmationOpen = true;
                        // 【2026-10-08】原来这里还有一条"等待这些玩家离开发射台"的分支。
                        // 已随"不再等别人离开发射台"一起删除：玩家控制的火箭不再计入堵塞，
                        // players 恒为空，那条分支永远走不到（留着就是死代码）。
                        {
                            string message;
                            if (rockets.Count == 1)
                                message = "There is currently 1 uncontrolled rocket blocking the launchpad...";
                            else
                                message = $"There are currently {rockets.Count} uncontrolled rockets blocking the launchpad...";

                            Func<Screen_Base> menu = MenuGenerator.CreateMenu
                            (
                                CancelButton.Close,
                                CloseMode.Current,
                                OnOpen,
                                OnClose,
                                TextBuilder.CreateText(() => message),
                                ElementGenerator.HorizontalGroup
                                (
                                    group =>
                                    {
                                        group.spacing = 10f;
                                        ((RectTransform) group.transform).pivot = new Vector2(0.5f, 0.5f);
                                    },
                                    true,
                                    true,
                                    ButtonBuilder.CreateButton
                                    (
                                        null,
                                        () => "Close",
                                        OnClose,
                                        CloseMode.Current
                                    ),
                                    ButtonBuilder.CreateButton
                                    (
                                        null,
                                        () => "Clear Launchpad",
                                        ClearLaunchpad,
                                        CloseMode.Current
                                    )
                                )
                            );
                            ScreenManager.main.OpenScreen(menu);
                        }
                    }
                    await Task.Delay(500);
                }
            }

            static bool UpdateLaunchpadStatus(ref HashSet<int> rockets, ref HashSet<int> players, out bool updateText)
            {
                HashSet<int> newRockets = new HashSet<int>();
                HashSet<int> newPlayers = new HashSet<int>();

                // 【2026-10-08 用户要求：不再"等别人离开发射台"】
                // 原来只要别人把火箭停在发射台上，后来者就发不出去，并弹
                // "Waiting for the following players to leave the launchpad"。
                // 拆掉的理由（用户原话）：发射台 200 米半径内火箭之间本来就不碰撞
                //（见 LocalManager 的碰撞豁免），"被占着"没有任何技术后果；
                // 反而让一个人可以永久占着发射台把别人全堵死。
                //
                // **仍然保留**：无人控制的幽灵火箭依旧算堵塞（会弹 Clear Launchpad 确认）。
                // 那不是礼仪问题 —— LocalManager 里写明它会让发射流程/物理卡死主线程，
                // 而且原生单人的行为也是"新火箭顶掉发射台上的旧火箭"。
                foreach (KeyValuePair<int, RocketState> rocket in ClientManager.world.rockets)
                {
                    if (!GameManager_IsOnLaunchpad.IsOnLaunchpad(rocket.Value.location.address, rocket.Value.location.position))
                        continue;

                    // 有玩家在操控的火箭（含自己）不算堵塞
                    bool controlledBySomeone = false;
                    foreach (KeyValuePair<int, LocalPlayer> player in LocalManager.players)
                    {
                        if (player.Value.controlledRocket == rocket.Key)
                        {
                            controlledBySomeone = true;
                            break;
                        }
                    }
                    if (controlledBySomeone) continue;

                    Debug.Log(rocket.Value.location.position);
                    newRockets.Add(rocket.Key);
                }
                if (newPlayers.Count == 0 && newRockets.Count == 0)
                {
                    updateText = false;
                    return true;
                }
                else
                {
                    updateText = !newPlayers.SetEquals(players) || !newRockets.SetEquals(rockets);
                    rockets = newRockets;
                    players = newPlayers;
                    return false;
                }
            }
        }

        /// <summary>
        /// Reverse patch for use in `BuildManager_Launch_2.UpdateLaunchpadStatus`.
        /// </summary>
        [HarmonyPatch(typeof(GameManager), "IsOnLaunchpad")]
        public static class GameManager_IsOnLaunchpad
        {
            [HarmonyReversePatch]
            public static bool IsOnLaunchpad(string planet, Double2 postion) => throw new NotImplementedException("Harmony Reverse Patch");
        }
    }


public static class LaunchpadReference
{
    public static bool TryGetPosition(out double x, out double y, out string address)
    {
        x = 0.0;
        y = 0.0;
        address = null;
        try
        {
            var loader = Base.planetLoader;
            var center = loader == null ? null : loader.spaceCenter;
            if (center == null) return false;
            x = center.LaunchPadLocation.position.x;
            y = center.LaunchPadLocation.position.y;
            address = center.address;
            return true;
        }
        catch (Exception)
        {
            return false;
        }
    }
}
}