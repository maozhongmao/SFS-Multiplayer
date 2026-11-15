// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net;

namespace MultiplayerSFS.Mod;

public class JoinInfo
{
	public IPAddress address = IPAddress.Parse("127.0.0.1");

	public int port = 9806;

	public string username = "DEFAULT_USERNAME";

	public string password = "";
}
