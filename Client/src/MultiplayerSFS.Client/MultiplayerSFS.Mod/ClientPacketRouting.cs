// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using MultiplayerSFS.Common;

namespace MultiplayerSFS.Mod;

public static class ClientPacketRouting
{
	public static int GetRocketId(Packet packet)
	{
		if (packet is Packet_DestroyPart destroyPart)
		{
			return destroyPart.RocketId;
		}

		throw new ArgumentException("Packet does not have a supported rocket route.", nameof(packet));
	}
}