// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Threading;
using HarmonyLib;
using SFS;
using SFS.Builds;
using SFS.IO;
using SFS.WorldBase;
using UnityEngine;

namespace MultiplayerSFS.Mod.Patches;

public class DivertBuildSavingAndLoading
{
	[HarmonyPatch(typeof(SavingCache), "SaveBuildPersistent")]
	public class SavingCache_SaveBuildPersistent
	{
		public static bool Prefix(SavingCache __instance, Blueprint new_BuildPersistent, bool cache)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				__instance.FieldRef<SavingCache.Data<Blueprint>>("buildPersistent") = SavingCache.Data<Blueprint>.Cache(new_BuildPersistent, cache);
				SavingCache.SaveAsync(delegate
				{
					Blueprint.Save(Main.buildPersistentFolder, new_BuildPersistent, Application.version);
				});
				return false;
			}
			return true;
		}
	}

	[HarmonyPatch(typeof(SavingCache), "Preload_BlueprintPersistent")]
	public class SavingCache_Preload_BlueprintPersistent
	{
		public static bool Prefix(SavingCache __instance)
		{
			if (ClientManager.multiplayerEnabled.Value)
			{
				ref SavingCache.Data<Blueprint> reference = ref __instance.FieldRef<SavingCache.Data<Blueprint>>("buildPersistent");
				if (reference == null)
				{
#if SFS15
					FolderPath path = Main.buildPersistentFolder;
#else
					IFolder path = Main.buildPersistentFolder;
#endif
					MsgCollector logger = new MsgCollector();
					reference = new SavingCache.Data<Blueprint>
					{
						thread = new Thread((ThreadStart)delegate
												{
												try
												{
													ref SavingCache.Data<Blueprint> reference2 = ref __instance.FieldRef<SavingCache.Data<Blueprint>>("buildPersistent");
#if SFS15
							// FolderPath 没有 Exists()，对应的是 FolderExists()。
							if (path.FolderExists() && Blueprint.TryLoad(path, logger, out var blueprint))
#else
							if (path.Exists() && Blueprint.TryLoad(path, logger, out var blueprint))
#endif
							{
								reference2.result = (success: true, data: blueprint, log: (logger.msg.Length > 0) ? logger.msg.ToString() : null);
							}
							else
							{
								reference2.result = (success: false, data: null, log: null);
							}
						}
						catch (System.Exception ex)
						{
							System.Console.Error.WriteLine("[SFS-MP] background thread exception (blueprint save): " + ex);
							// 同理：不回填结果，主线程会一直等这个 Data.result。
							try
							{
								__instance.FieldRef<SavingCache.Data<Blueprint>>("buildPersistent").result = (success: false, data: null, log: null);
							}
							catch { }
						}
						})
					};
					reference.thread.Start();
				}
				return false;
			}
			return true;
		}
	}
}
