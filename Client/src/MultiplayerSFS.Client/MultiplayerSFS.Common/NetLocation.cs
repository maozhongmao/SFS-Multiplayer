// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using Lidgren.Network;

namespace MultiplayerSFS.Common;

public class NetLocation : INetData
{
	public Double2 position;

	public Double2 velocity;

	public string address;

	public NetLocation()
	{
	}

	public NetLocation(Double2 pos, Double2 vel, string planetName)
	{
		position = pos;
		velocity = vel;
		address = planetName;
	}

	public void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedDouble2(position);
		msg.WriteCompressedDouble2(velocity);
		msg.WriteCompressedString(address);
	}

	public void Deserialize(NetIncomingMessage msg)
	{
		position = msg.ReadCompressedDouble2();
		velocity = msg.ReadCompressedDouble2();
		address = msg.ReadCompressedString();
	}
}
