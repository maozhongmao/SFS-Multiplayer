using System;
using SFS.WorldBase;

namespace SFS.World;

[Serializable]
public class Location
{
	public double time;

	public Planet planet;

	public Double2 position;

	public Double2 velocity;

	public double Radius => position.magnitude;

	public double Height => Radius - planet.Radius;

	public double VerticalVelocity => velocity.Rotate(0.0 - (position.AngleRadians - Math.PI / 2.0)).y;

	public double GetTerrainHeight(bool clampToWater)
	{
		return Height - planet.GetTerrainHeightAtAngle(position.AngleRadians, clampToWater);
	}

	public Double2 GetSolarSystemPosition(double time)
	{
		return position + planet.GetSolarSystemPosition(time);
	}

	public Location(double time, Planet planet, Double2 position, Double2 velocity)
	{
		this.time = time;
		this.planet = planet;
		this.position = new Double2(CheckNaN(position.x), CheckNaN(position.y));
		this.velocity = new Double2(CheckNaN(velocity.x), CheckNaN(velocity.y));
	}

	public Location(Planet planet, Double2 position, Double2 velocity = default(Double2))
	{
		time = -1.0;
		this.planet = planet;
		this.position = new Double2(CheckNaN(position.x), CheckNaN(position.y));
		this.velocity = new Double2(CheckNaN(velocity.x), CheckNaN(velocity.y));
	}

	private double CheckNaN(double a)
	{
		if (!double.IsNaN(a))
		{
			return a;
		}
		return 0.0;
	}

	public static Location operator +(Location a, Location b)
	{
		return new Location(b.time, b.planet, a.position + b.position, a.velocity + b.velocity);
	}
}
You are not using the latest version of the tool, please update.
Latest version is '10.1.1.8388' (yours is '9.1.0.7988')
