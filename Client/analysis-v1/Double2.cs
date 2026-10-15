using System;
using UnityEngine;

[Serializable]
public struct Double2
{
	public double x;

	public double y;

	public static Double2 zero => default(Double2);

	public static Double2 right => new Double2(1.0, 0.0);

	public static Double2 up => new Double2(0.0, 1.0);

	public static Double2 down => new Double2(0.0, -1.0);

	public double AngleRadians => Math.Atan2(y, x);

	public double AngleDegrees => Math.Atan2(y, x) / (Math.PI * 2.0) * 360.0;

	public double magnitude => Math.Sqrt(x * x + y * y);

	public double sqrMagnitude => x * x + y * y;

	public Double2 normalized
	{
		get
		{
			double num = magnitude;
			if (num > 9.99999974737875E-06)
			{
				return this / num;
			}
			return zero;
		}
	}

	public Vector2 ToVector2 => new Vector2((float)x, (float)y);

	public Vector3 ToVector3 => new Vector3((float)x, (float)y, 0f);

	public Double2(double x, double y)
	{
		this.x = x;
		this.y = y;
	}

	public static double Dot(Double2 a, Double2 b)
	{
		return a.x * b.x + a.y * b.y;
	}

	public static double Angle(Double2 from, Double2 to)
	{
		double num = Math.Sqrt(from.sqrMagnitude * to.sqrMagnitude);
		if (!(num < 1.00000000362749E-15))
		{
			return Math.Acos(Math.Clamp(Dot(from, to) / num, -1.0, 1.0)) * 57.29578;
		}
		return 0.0;
	}

	public static double SignedAngle(Double2 from, Double2 to)
	{
		return Angle(from, to) * (double)Math.Sign(from.x * to.y - from.y * to.x);
	}

	public bool Mag_MoreThan(double a)
	{
		return sqrMagnitude > a * a;
	}

	public bool Mag_LessThan(double a)
	{
		return sqrMagnitude < a * a;
	}

	public Double2 Rotate(double angleRadians)
	{
		double num = Math.Cos(angleRadians);
		double num2 = Math.Sin(angleRadians);
		return new Double2(x * num - y * num2, x * num2 + y * num);
	}

	public string ToParsableString()
	{
		return $"{x}:{y}";
	}

	public static Double2 operator +(Double2 a, Double2 b)
	{
		return new Double2(a.x + b.x, a.y + b.y);
	}

	public static Double2 operator +(Double2 a, Vector3 b)
	{
		return new Double2(a.x + (double)b.x, a.y + (double)b.y);
	}

	public static Double2 operator +(Double2 a, Vector2 b)
	{
		return new Double2(a.x + (double)b.x, a.y + (double)b.y);
	}

	public static Double2 operator -(Double2 a, Double2 b)
	{
		return new Double2(a.x - b.x, a.y - b.y);
	}

	public static Double2 operator -(Double2 a, Vector3 b)
	{
		return new Double2(a.x - (double)b.x, a.y - (double)b.y);
	}

	public static Double2 operator -(Double2 a, Vector2 b)
	{
		return new Double2(a.x - (double)b.x, a.y - (double)b.y);
	}

	public static Double2 operator -(Double2 a)
	{
		return new Double2(0.0 - a.x, 0.0 - a.y);
	}

	public static Double2 operator *(Double2 a, Double2 b)
	{
		return new Double2(a.x * b.x, a.y * b.y);
	}

	public static Double2 operator *(Double2 a, Vector2 b)
	{
		return new Double2(a.x * (double)b.x, a.y * (double)b.y);
	}

	public static Double2 operator *(Double2 a, double b)
	{
		return new Double2(a.x * b, a.y * b);
	}

	public static Double2 operator *(double b, Double2 a)
	{
		return new Double2(a.x * b, a.y * b);
	}

	public static Double2 operator /(Double2 a, double b)
	{
		return new Double2(a.x / b, a.y / b);
	}

	public static bool operator ==(Double2 a, Double2 b)
	{
		if (a.x == b.x)
		{
			return a.y == b.y;
		}
		return false;
	}

	public static bool operator !=(Double2 a, Double2 b)
	{
		if (a.x == b.x)
		{
			return a.y != b.y;
		}
		return true;
	}

	public static implicit operator Vector2(Double2 a)
	{
		return new Vector2((float)a.x, (float)a.y);
	}

	public static implicit operator Vector3(Double2 a)
	{
		return new Vector3((float)a.x, (float)a.y);
	}

	public override int GetHashCode()
	{
		return x.GetHashCode() ^ (y.GetHashCode() << 2);
	}

	public override bool Equals(object other)
	{
		if (!(other is Double2))
		{
			return false;
		}
		return Equals((Double2)other);
	}

	public bool Equals(Double2 other)
	{
		if (x.Equals(other.x))
		{
			return y.Equals(other.y);
		}
		return false;
	}

	public static Double2 ToDouble2(Vector2 a)
	{
		return new Double2(a.x, a.y);
	}

	public static Double2 ToDouble2(Vector3 a)
	{
		return new Double2(a.x, a.y);
	}

	public override string ToString()
	{
		return "(" + x + ", " + y + ")";
	}

	public static Double2 CosSin(double angleRadians)
	{
		return new Double2(Math.Cos(angleRadians), Math.Sin(angleRadians));
	}

	public static Double2 CosSin(double angleRadians, double radius)
	{
		return new Double2(Math.Cos(angleRadians) * radius, Math.Sin(angleRadians) * radius);
	}

	public static Double2 Reflect(Double2 inDirection, Double2 inNormal)
	{
		double num = -2.0 * Dot(inNormal, inDirection);
		return new Double2(num * inNormal.x + inDirection.x, num * inNormal.y + inDirection.y);
	}

	public static explicit operator Double2(Vector2 a)
	{
		return new Double2(a.x, a.y);
	}

	public static explicit operator Double2(Double3 a)
	{
		return new Double2(a.x, a.y);
	}

	public static Double2 Lerp(Double2 a, Double2 b, double t)
	{
		return (1.0 - t) * a + t * b;
	}

	public static Double2 Parse(string text)
	{
		double num = double.Parse(text.Split(':')[1]);
		double num2 = double.Parse(text.Split(':')[2]);
		return new Double2(num, num2);
	}
}
You are not using the latest version of the tool, please update.
Latest version is '10.1.1.8388' (yours is '9.1.0.7988')
