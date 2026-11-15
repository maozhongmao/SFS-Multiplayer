// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Net;

namespace MultiplayerSFS.Common;

public sealed class P2PRawDatagram
{
	public IPEndPoint RemoteEndPoint { get; }
	public byte[] Data { get; }

	public P2PRawDatagram(IPEndPoint remoteEndPoint, byte[] data)
	{
		RemoteEndPoint = remoteEndPoint;
		Data = data ?? Array.Empty<byte>();
	}
}
