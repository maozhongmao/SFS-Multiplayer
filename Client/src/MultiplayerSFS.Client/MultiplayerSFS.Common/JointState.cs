// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Collections.Generic;
using Lidgren.Network;
using SFS.World;

namespace MultiplayerSFS.Common;

public class JointState : INetData
{
	public int id_A;

	public int id_B;

	public JointState()
	{
	}

	public JointState(int id_A, int id_B)
	{
		this.id_A = id_A;
		this.id_B = id_B;
	}

	public JointState(JointSave save, Dictionary<int, int> partIndexToID)
	{
		id_A = partIndexToID.TryGetValue(save.partIndex_A, out var a) ? a : -1;
		id_B = partIndexToID.TryGetValue(save.partIndex_B, out var b) ? b : -1;
	}

	public void Serialize(NetOutgoingMessage msg)
	{
		msg.WriteCompressedInt(id_A);
		msg.WriteCompressedInt(id_B);
	}

	public void Deserialize(NetIncomingMessage msg)
	{
		id_A = msg.ReadCompressedInt();
		id_B = msg.ReadCompressedInt();
	}
}
