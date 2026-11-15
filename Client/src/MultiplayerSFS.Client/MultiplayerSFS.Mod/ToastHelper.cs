// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using SFS.UI;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public static class ToastHelper
{
	public static string ShowToast(string toast)
	{
		if (MsgDrawer.main == null)
		{
			MsgDrawer.main = Object.FindObjectOfType<MsgDrawer>();
		}
		if (MsgDrawer.main != null)
		{
			MsgDrawer.main.Log(toast, big: false);
			return "Success";
		}
		return "Error: MsgDrawer not available";
	}
}
