using System;
using System.Diagnostics;

namespace Lidgren.Network;

public sealed class NetException : Exception
{
	public NetException()
	{
	}

	public NetException(string message)
		: base(message)
	{
	}

	public NetException(string message, Exception inner)
		: base(message, inner)
	{
	}

	[Conditional("DEBUG")]
	public static void Assert(bool isOk, string message)
	{
		if (!isOk)
		{
			throw new NetException(message);
		}
	}

	[Conditional("DEBUG")]
	public static void Assert(bool isOk)
	{
		if (!isOk)
		{
			throw new NetException();
		}
	}
}
