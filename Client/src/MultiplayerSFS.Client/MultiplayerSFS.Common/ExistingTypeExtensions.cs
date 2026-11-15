// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;
using SFS.Parts.Modules;
using UnityEngine;

namespace MultiplayerSFS.Common;

public static class ExistingTypeExtensions
{
	public static void WriteCompressedDouble2(this NetOutgoingMessage msg, Double2 double2)
	{
		msg.WriteCompressedDouble(double2.x);
		msg.WriteCompressedDouble(double2.y);
	}

	public static Double2 ReadCompressedDouble2(this NetIncomingMessage msg)
	{
		return new Double2(msg.ReadCompressedDouble(), msg.ReadCompressedDouble());
	}

	public static void WriteCompressedVector2(this NetOutgoingMessage msg, Vector2 vector2)
	{
		msg.WriteCompressedFloat(vector2.x);
		msg.WriteCompressedFloat(vector2.y);
	}

	public static Vector2 ReadCompressedVector2(this NetIncomingMessage msg)
	{
		return new Vector2(msg.ReadCompressedFloat(), msg.ReadCompressedFloat());
	}

	public static void WriteCompressedColor(this NetOutgoingMessage msg, Color color)
	{
		msg.WriteCompressedFloat(color.r);
		msg.WriteCompressedFloat(color.g);
		msg.WriteCompressedFloat(color.b);
	}

	public static Color ReadCompressedColor(this NetIncomingMessage msg)
	{
		return new Color(msg.ReadCompressedFloat(), msg.ReadCompressedFloat(), msg.ReadCompressedFloat());
	}

	public static void WriteCompressedOrientation(this NetOutgoingMessage msg, Orientation orientation)
	{
		msg.WriteCompressedFloat(orientation.x);
		msg.WriteCompressedFloat(orientation.y);
		msg.WriteCompressedFloat(orientation.z);
	}

	public static Orientation ReadCompressedOrientation(this NetIncomingMessage msg)
	{
		return new Orientation(msg.ReadCompressedFloat(), msg.ReadCompressedFloat(), msg.ReadCompressedFloat());
	}

	public static void WriteCompressedBurnSave(this NetOutgoingMessage msg, BurnMark.BurnSave burnSave)
	{
		((NetBuffer)msg).Write(burnSave == null);
		if (burnSave != null)
		{
			msg.WriteCompressedFloat(burnSave.angle);
			msg.WriteCompressedFloat(burnSave.intensity);
			msg.WriteCompressedFloat(burnSave.x);
			msg.WriteCompressedString(burnSave.top);
			msg.WriteCompressedString(burnSave.bottom);
		}
	}

	public static BurnMark.BurnSave ReadCompressedBurnSave(this NetIncomingMessage msg)
	{
		if (((NetBuffer)msg).ReadBoolean())
		{
			return null;
		}
		return new BurnMark.BurnSave
		{
			angle = msg.ReadCompressedFloat(),
			intensity = msg.ReadCompressedFloat(),
			x = msg.ReadCompressedFloat(),
			top = msg.ReadCompressedString(),
			bottom = msg.ReadCompressedString()
		};
	}

	public static void WriteCompressedInt(this NetOutgoingMessage msg, int value)
	{
		((NetBuffer)msg).Write(value);
	}

	public static int ReadCompressedInt(this NetIncomingMessage msg)
	{
		return ((NetBuffer)msg).ReadInt32();
	}

	public static void WriteCompressedFloat(this NetOutgoingMessage msg, float value)
	{
		((NetBuffer)msg).Write(value);
	}

	public static float ReadCompressedFloat(this NetIncomingMessage msg)
	{
		return ((NetBuffer)msg).ReadFloat();
	}

	public static void WriteCompressedDouble(this NetOutgoingMessage msg, double value)
	{
		((NetBuffer)msg).Write(value);
	}

	public static double ReadCompressedDouble(this NetIncomingMessage msg)
	{
		return ((NetBuffer)msg).ReadDouble();
	}

	public static void WriteCompressedString(this NetOutgoingMessage msg, string value)
	{
		((NetBuffer)msg).Write(value ?? string.Empty);
	}

	public static string ReadCompressedString(this NetIncomingMessage msg)
	{
		return ((NetBuffer)msg).ReadString() ?? string.Empty;
	}
}
