// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using Lidgren.Network;
using SFS.Parts;

namespace MultiplayerSFS.Common;

public class PartState : INetData
{
	public PartSave part;

	public PartState()
	{
	}

	public PartState(PartSave save)
	{
		part = save;
	}

	public void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedString(part.name);
		msg.WriteCompressedVector2(part.position);
		msg.WriteCompressedOrientation(part.orientation);
		msg.WriteCompressedFloat(part.temperature);
		msg.WriteCollection(part.NUMBER_VARIABLES, delegate(KeyValuePair<string, double> kvp)
		{
			msg.WriteCompressedString(kvp.Key);
			msg.WriteCompressedDouble(kvp.Value);
		});
		msg.WriteCollection(part.TOGGLE_VARIABLES, delegate(KeyValuePair<string, bool> kvp)
		{
			msg.WriteCompressedString(kvp.Key);
			((NetBuffer)msg).Write(kvp.Value);
		});
		msg.WriteCollection(part.TEXT_VARIABLES, delegate(KeyValuePair<string, string> kvp)
		{
			msg.WriteCompressedString(kvp.Key);
			msg.WriteCompressedString(kvp.Value);
		});
		msg.WriteCompressedBurnSave(part.burns);
	}

	public void Deserialize(NetIncomingMessage msg)
	{
		part = new PartSave
		{
			name = msg.ReadCompressedString(),
			position = msg.ReadCompressedVector2(),
			orientation = msg.ReadCompressedOrientation(),
			temperature = msg.ReadCompressedFloat(),
			NUMBER_VARIABLES = msg.ReadCollection((int count) => new Dictionary<string, double>(count), () => new KeyValuePair<string, double>(msg.ReadCompressedString(), msg.ReadCompressedDouble())),
			TOGGLE_VARIABLES = msg.ReadCollection((int count) => new Dictionary<string, bool>(count), () => new KeyValuePair<string, bool>(msg.ReadCompressedString(), ((NetBuffer)msg).ReadBoolean())),
			TEXT_VARIABLES = msg.ReadCollection((int count) => new Dictionary<string, string>(count), () => new KeyValuePair<string, string>(msg.ReadCompressedString(), msg.ReadCompressedString())),
			burns = msg.ReadCompressedBurnSave()
		};
	}
}
