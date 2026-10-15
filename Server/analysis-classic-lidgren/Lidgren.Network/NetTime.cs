using System;
using System.Diagnostics;

namespace Lidgren.Network;

public static class NetTime
{
	private static readonly long s_timeInitialized = Stopwatch.GetTimestamp();

	private static readonly double s_dInvFreq = 1.0 / (double)Stopwatch.Frequency;

	public static double Now => (double)(Stopwatch.GetTimestamp() - s_timeInitialized) * s_dInvFreq;

	public static string ToReadable(double seconds)
	{
		if (seconds > 60.0)
		{
			return TimeSpan.FromSeconds(seconds).ToString();
		}
		return (seconds * 1000.0).ToString("N2") + " ms";
	}
}
