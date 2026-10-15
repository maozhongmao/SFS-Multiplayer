using System;
using System.Collections.Generic;
using System.Linq;
using SFS.Cameras;
using SFS.World.PlanetModules;
using SFS.World.Terrain;
using SFS.WorldBase;
using UnityEngine;

namespace SFS.World;

public class WorldEnvironment : MonoBehaviour
{
	private static readonly int Fog = Shader.PropertyToID("_Fog");

	private static readonly int WaterOpacity = Shader.PropertyToID("_Opacity");

	private static readonly int WaterSurfaceOpacity = Shader.PropertyToID("_SurfaceOpacity");

	private static readonly int WaterDarkness = Shader.PropertyToID("_Darkness");

	private static readonly int WaterOffset = Shader.PropertyToID("_Offset");

	private static readonly int WaterWavesOn = Shader.PropertyToID("_WavesOn");

	private static readonly int WaterTime = Shader.PropertyToID("_WorldTime");

	private static readonly int WaveTime = Shader.PropertyToID("_WaveTime");

	public Transform chunkPrefab;

	public Transform[] rockPrefabs;

	public Transform atmospherePrefab;

	public Environment[] environments;

	private (double time, bool scaled, Double2 offset) lastState;

	public void Initialize(bool launchPlanetOnly)
	{
		environments = CreateEnvironments(launchPlanetOnly);
		WorldView.main.scaledSpace.OnChange += new Action(OnScaledSpaceChange);
		WorldView.main.viewDistance.OnChange += new Action(UpdateFog);
		WorldView.main.positionOffset.OnChange += new Action(UpdateViewPosition);
	}

	private void OnScaledSpaceChange()
	{
		string layer = (WorldView.main.scaledSpace ? "Scaled Space" : "Celestial Body");
		Environment[] array = environments;
		foreach (Environment obj in array)
		{
			obj.holder.transform.localScale = Vector3.one / (WorldView.main.scaledSpace ? 10000f : 1f);
			obj.terrain?.SetLayer(layer);
			obj.atmosphere?.SetLayer(layer);
			obj.frontClouds?.SetLayer(layer);
			obj.rings?.SetLayer(layer);
		}
		UpdateViewPosition();
	}

	private static void UpdateFog()
	{
		foreach (Planet value3 in Base.planetLoader.planets.Values)
		{
			if (value3.data.hasAtmosphereVisuals && value3.data.hasTerrain)
			{
				Color value = value3.data.atmosphereVisuals.FOG.Evaluate(WorldView.main.viewDistance);
				value3.terrainMaterial.SetColor(Fog, value);
				if (value3.data.hasWater)
				{
					value3.waterMaterial.SetColor(Fog, value);
				}
			}
			if (value3.data.hasFrontClouds)
			{
				float num = Mathf.Abs(value3.data.frontClouds.positionZ) + 1f;
				float value2 = Mathf.Clamp01(Mathf.InverseLerp(num, num * 1.5f, WorldView.main.viewDistance));
				value3.frontCloudsMaterial.SetFloat("_Alpha", value2);
			}
		}
	}

	private void UpdateAtmospherePosition()
	{
		Location viewLocation = WorldView.main.ViewLocation;
		float val = (viewLocation.planet.data.hasAtmosphereVisuals ? ((float)Math.Min(viewLocation.Height, viewLocation.planet.data.atmosphereVisuals.GRADIENT.height)) : 0f) * 0.7f / GetViewRangeNormal();
		Environment[] array = environments;
		foreach (Environment environment in array)
		{
			Planet planet = environment.planet;
			if (planet.data.hasAtmosphereVisuals)
			{
				float num = planet.data.atmosphereVisuals.GRADIENT.positionZ;
				float num2 = ((planet == viewLocation.planet) ? Math.Max(val, num) : num);
				num2 = Mathf.Max(num2 - WorldView.main.viewDistance.Value * 0.25f, 0f);
				environment.atmosphere.transform.localPosition = Vector3.forward * num2;
			}
		}
	}

	private static float GetViewRangeNormal()
	{
		CameraManager worldCamera = ((!(GameManager.main != null)) ? WorldView.main.worldCamera : (WorldView.main.scaledSpace.Value ? GameCamerasManager.main.scaledWorld_Camera : GameCamerasManager.main.world_Camera));
		Double2 position = WorldView.main.ViewLocation.position;
		Vector2 size = new Vector2(Screen.width, Screen.height);
		Vector3 center = worldCamera.camera.ScreenToWorldPoint((size / 2f).ToVector3(1f));
		return Mathf.Lerp(new Vector2[4]
		{
			new Vector2(0f, 0f),
			new Vector2(0f, 1f),
			new Vector2(1f, 1f),
			new Vector2(1f, 0f)
		}.Select(GetDiff_Rotated).ToArray().Max((Vector2 a) => a.y), worldCamera.ViewSizeNormal, 0.1f);
		Vector2 GetDiff(Vector2 percent)
		{
			return worldCamera.camera.ScreenToWorldPoint((size * percent).ToVector3(1f)) - center;
		}
		Vector2 GetDiff_Rotated(Vector2 percent)
		{
			return GetDiff(percent).Rotate_Radians(0f - (float)position.AngleRadians).Rotate_90();
		}
	}

	private void LateUpdate()
	{
		UpdateAtmospherePosition();
		UpdateViewPosition();
	}

	private void UpdateViewPosition()
	{
		Planet planet = WorldView.main.ViewLocation.planet;
		Double2 position = WorldView.main.ViewLocation.position;
		if (planet == null)
		{
			return;
		}
		double num = ((WorldTime.main != null) ? WorldTime.main.worldTime : 0.0);
		(double, bool, Double2) tuple = (num, WorldView.main.scaledSpace, WorldView.main.positionOffset);
		(double, bool, Double2) tuple2 = tuple;
		(double, bool, Double2) tuple3 = lastState;
		if (tuple2.Item1 == tuple3.Item1 && tuple2.Item2 == tuple3.Item2 && tuple2.Item3 == tuple3.Item3)
		{
			return;
		}
		lastState = tuple;
		Double2 solarSystemPosition = WorldView.main.ViewLocation.GetSolarSystemPosition(num);
		Environment[] array;
		if (!WorldView.main.scaledSpace)
		{
			Double2 solarSystemPosition2 = planet.GetSolarSystemPosition(num);
			array = environments;
			foreach (Environment environment in array)
			{
				Double2 solarSystemPosition3 = environment.planet.GetSolarSystemPosition(num);
				environment.holder.position = WorldView.ToLocalPosition(solarSystemPosition3 - solarSystemPosition2);
				if (environment.terrain != null)
				{
					environment.terrain.SetViewPosition(new Double3(solarSystemPosition.x - solarSystemPosition3.x, solarSystemPosition.y - solarSystemPosition3.y, (float)WorldView.main.viewDistance));
				}
			}
		}
		else
		{
			array = environments;
			foreach (Environment environment2 in array)
			{
				Double2 @double = environment2.planet.GetSolarSystemPosition() - solarSystemPosition;
				environment2.holder.position = new Vector3((float)@double.x / 10000f, (float)@double.y / 10000f, (float)WorldView.main.viewDistance / 10000f);
				if (environment2.terrain != null)
				{
					environment2.terrain.SetViewPosition(new Double3(0.0 - @double.x, 0.0 - @double.y, (float)WorldView.main.viewDistance));
				}
			}
		}
		array = environments;
		foreach (Environment environment3 in array)
		{
			if (environment3.planet.data.hasWater)
			{
				Planet planet2 = environment3.planet;
				WaterModule water = planet2.data.water;
				float num2 = ((planet2 == planet) ? Mathf.Max(0f - (float)WorldView.main.ViewLocation.Height, 0f) : 0f);
				float num3 = Mathf.InverseLerp(water.fullDarknessDepth * 0.1f, water.fullDarknessDepth, num2);
				float a = Mathf.Lerp(water.surfaceVisibilityDistance, water.fullDarknessVisibilityDistance, num3);
				float b = Mathf.Lerp(water.opacity_Surface, water.opacity_FullDarkness, num3);
				float value = Mathf.Lerp(water.opacity_Far, b, Mathf.Pow(Mathf.InverseLerp(a, 0f, WorldView.main.viewDistance), 3f));
				planet2.waterMaterial.SetFloat(WaterOpacity, value);
				float num4 = Mathf.InverseLerp(num2 * 0.8f, 1f + num2 * 1.1f, WorldView.main.viewDistance);
				planet2.waterMaterial.SetFloat(WaterDarkness, 1f - num3 * (1f - num4) * 0.8f);
				float value2 = Mathf.InverseLerp(num2 * 1f, 1f + num2 * 1.1f, WorldView.main.viewDistance);
				planet2.waterMaterial.SetFloat(WaterSurfaceOpacity, value2);
				if (planet2 == WorldView.main.ViewLocation.planet && !WorldView.main.scaledSpace)
				{
					Vector2 vector = position / planet2.Radius * 0.5 * planet2.data.terrain.TERRAIN_TEXTURE_DATA.planetTextureCutout + Vector2.one * 0.5f;
					Vector4 value3 = new Vector2(Mathf.Round(vector.x * (float)planet2.waterTexture.width) / (float)planet2.waterTexture.width, Mathf.Round(vector.y * (float)planet2.waterTexture.height) / (float)planet2.waterTexture.height);
					planet2.waterMaterial.SetVector(WaterOffset, value3);
					planet2.terrainMaterial.SetVector(WaterOffset, value3);
				}
				planet2.waterMaterial.SetFloat(WaterTime, (float)(num / 20.0 % 100.0));
				planet2.waterMaterial.SetFloat(WaveTime, (float)(num % (Math.PI * 2.0)));
				planet2.waterMaterial.SetFloat(WaterWavesOn, (!WorldView.main.scaledSpace) ? 1 : 0);
			}
		}
	}

	private Environment[] CreateEnvironments(bool launchPlanetOnly)
	{
		double worldTime = ((WorldTime.main != null) ? WorldTime.main.worldTime : 0.0);
		Double2 viewSolarSystemPosition = WorldView.main.ViewLocation.GetSolarSystemPosition(worldTime);
		List<Environment> output = new List<Environment>();
		if (launchPlanetOnly)
		{
			Create(Base.planetLoader.spaceCenter.address.GetPlanet());
		}
		else
		{
			foreach (Planet value in Base.planetLoader.planets.Values)
			{
				Create(value);
			}
		}
		return output.ToArray();
		void Create(Planet planet)
		{
			Double2 solarSystemPosition = planet.GetSolarSystemPosition(worldTime);
			output.Add(CreateEnvironment(planet, new Double3(viewSolarSystemPosition.x - solarSystemPosition.x, viewSolarSystemPosition.y - solarSystemPosition.y, (float)WorldView.main.viewDistance)));
		}
	}

	private Environment CreateEnvironment(Planet planet, Double3 viewPosition)
	{
		Environment environment = new Environment
		{
			planet = planet,
			holder = new GameObject(planet.codeName + " Environment").transform
		};
		environment.holder.parent = base.transform;
		if (planet.data.hasTerrain)
		{
			TerrainModule.RockData rockData = planet.data.terrain.rocks;
			Transform transform = ((rockData != null) ? rockPrefabs.FirstOrDefault((Transform a) => a.name == rockData.rockType) : null);
			environment.terrain = DynamicTerrain.Create(planet, viewPosition, null, 1f, 1f, "Default", (planet.terrainMaterial, planet.waterMaterial), useTerrainUV: true, chunkPrefab, (transform != null) ? rockData : null, transform);
			environment.terrain.transform.parent = environment.holder;
		}
		if (planet.HasAtmosphereVisuals)
		{
			environment.atmosphere = Atmosphere.Create(planet, environment.holder, atmospherePrefab);
		}
		if (planet.HasFrontClouds)
		{
			environment.frontClouds = FrontClouds.Create(planet, environment.holder, atmospherePrefab, planet.frontCloudsMaterial);
		}
		if (planet.HasRings)
		{
			environment.rings = Rings.Create(planet, environment.holder, atmospherePrefab, planet.ringsMaterial);
		}
		return environment;
	}
}
You are not using the latest version of the tool, please update.
Latest version is '10.1.1.8388' (yours is '9.1.0.7988')
