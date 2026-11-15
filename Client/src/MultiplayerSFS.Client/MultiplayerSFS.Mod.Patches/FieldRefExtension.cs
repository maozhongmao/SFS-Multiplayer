// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using HarmonyLib;

namespace MultiplayerSFS.Mod.Patches;

public static class FieldRefExtension
{
	public static ref F FieldRef<F>(this object instance, string field)
	{
		return ref AccessTools.FieldRefAccess<F>(instance.GetType(), field)(instance);
	}
}
