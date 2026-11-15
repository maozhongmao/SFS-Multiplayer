// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Reflection;
using System.Security;
using System.Security.Permissions;
using SFS.IO;
using SFS.Parsers.Json;
using SFS.World;
using SFS.WorldBase;

namespace MultiplayerSFS.Common;

public class WorldState
{
	public double initWorldTime;

	public Stopwatch worldTimer = Stopwatch.StartNew();

	public double timeScale = 1.0;

	public Difficulty.DifficultyType difficulty;

	public string solarSystemName = "";

	public Dictionary<int, RocketState> rockets;

	public double WorldTime
	{
		get
		{
			if (worldTimer.ElapsedTicks > 1000 * Stopwatch.Frequency)
			{
				initWorldTime += worldTimer.Elapsed.TotalSeconds * timeScale;
				worldTimer.Restart();
			}
			return initWorldTime + worldTimer.Elapsed.TotalSeconds * timeScale;
		}
		set
		{
			initWorldTime = value;
			worldTimer.Restart();
		}
	}

	public void SetTimeScale(double multiplier, double authoritativeWorldTime)
	{
		initWorldTime = authoritativeWorldTime;
		timeScale = multiplier;
		worldTimer.Restart();
	}

	public WorldState()
	{
		initWorldTime = 1000000.0;
		difficulty = Difficulty.DifficultyType.Normal;
		solarSystemName = "";
		rockets = new Dictionary<int, RocketState>();
	}

	public WorldState(string path)
	{
		new SecurityPermission(SecurityPermissionFlag.AllFlags).Assert();
		try
		{
			LoadWorldStateWithReflection(path);
		}
		catch (Exception ex)
		{
			Console.WriteLine("[WARNING] Failed to load world state: " + ex.Message);
			Console.WriteLine("[WARNING] Using default world state values.");
			InitializeWithDefaults();
		}
		finally
		{
			CodeAccessPermission.RevertAssert();
		}
	}

	private void LoadWorldStateWithReflection(string path)
	{
		FolderPath folderPath = new FolderPath(path);
		FolderPath folderPath2 = folderPath.CloneAndExtend("Persistent");
		if (!folderPath.FolderExists())
		{
			throw new Exception("Save folder cannot be found or does not exist.");
		}
		if (!folderPath2.FolderExists())
		{
			throw new Exception("'Persistent' folder cannot be found or does not exist.");
		}
		MethodInfo method = typeof(JsonWrapper).GetMethod("TryLoadJson", new Type[2]
		{
			typeof(FilePath),
			typeof(object).MakeByRefType()
		});
		if (method == null)
		{
			throw new Exception("JsonWrapper.TryLoadJson method not found");
		}
		object[] array = new object[2]
		{
			folderPath.ExtendToFile("WorldSettings.txt"),
			null
		};
		if (!(bool)method.MakeGenericMethod(typeof(WorldSettings)).Invoke(null, array))
		{
			throw new Exception("'WorldSettings.txt' file cannot be found or could not be loaded.");
		}
		WorldSettings worldSettings = (WorldSettings)array[1];
		solarSystemName = "";
		PropertyInfo property = typeof(WorldSettings).GetProperty("solarSystem");
		if (property != null)
		{
			object value = property.GetValue(worldSettings);
			if (value != null)
			{
				PropertyInfo property2 = value.GetType().GetProperty("name");
				if (property2 != null)
				{
					object value2 = property2.GetValue(value);
					if (value2 != null)
					{
						solarSystemName = value2.ToString();
					}
				}
			}
		}
		Console.WriteLine("[INFO] Loaded solar system: '" + solarSystemName + "'");
		object[] array2 = new object[2]
		{
			folderPath2.ExtendToFile("WorldState.txt"),
			null
		};
		if (!(bool)method.MakeGenericMethod(typeof(WorldSave.WorldState)).Invoke(null, array2))
		{
			throw new Exception("'WorldState.txt' file cannot be found or could not be loaded.");
		}
		WorldSave.WorldState worldState = (WorldSave.WorldState)array2[1];
		object[] array3 = new object[2]
		{
			folderPath2.ExtendToFile("Rockets.txt"),
			null
		};
		if (!(bool)method.MakeGenericMethod(typeof(List<RocketSave>)).Invoke(null, array3))
		{
			throw new Exception("'Rockets.txt' file cannot be found or could not be loaded.");
		}
		List<RocketSave> obj = (List<RocketSave>)array3[1];
		initWorldTime = worldState.worldTime;
		difficulty = worldSettings?.difficulty?.difficulty ?? Difficulty.DifficultyType.Normal;
		rockets = new Dictionary<int, RocketState>();
		foreach (RocketSave item in obj)
		{
			rockets.InsertNew(new RocketState(item));
		}
		Console.WriteLine("[INFO] Successfully loaded world state from " + path);
	}

	private void InitializeWithDefaults()
	{
		initWorldTime = 0.0;
		difficulty = Difficulty.DifficultyType.Normal;
		solarSystemName = "";
		rockets = new Dictionary<int, RocketState>();
	}
}
