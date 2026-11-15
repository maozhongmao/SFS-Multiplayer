// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using SFS.Variables;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public class LocalPlayer
{
	public string username;

	public Int_Local controlledRocket;

	public Color iconColor;

	public LocalPlayer(string username, Color color)
	{
		this.username = username;
		controlledRocket = new Int_Local
		{
			Value = -1
		};
		controlledRocket.OnChange += new Action<int, int>(OnControlledRocketChange);
		iconColor = color;
	}

	public void OnControlledRocketChange(int oldId, int newId)
	{
	}
}
