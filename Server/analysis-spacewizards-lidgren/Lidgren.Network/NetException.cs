using System;
using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;

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
	public static void Assert([DoesNotReturnIf(false)] bool isOk, string message)
	{
		if (!isOk)
		{
			throw new NetException(message);
		}
	}

	[Conditional("DEBUG")]
	public static void Assert([DoesNotReturnIf(false)] bool isOk)
	{
		if (!isOk)
		{
			throw new NetException();
		}
	}
}
