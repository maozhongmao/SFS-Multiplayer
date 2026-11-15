// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using MultiplayerSFS.Mod.Patches;
using SFS.UI.ModGUI;
using TMPro;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public class ChatMessage
{
	public string message;

	public int senderId;

	public Color color;

	public Label label_playerName;

	public Label label_message;

	private static TMP_FontAsset cachedChineseFont;

	public ChatMessage(string message, int senderId = -1, Color color = default(Color))
	{
		this.message = message;
		this.senderId = senderId;
		this.color = ((color == default(Color)) ? Color.white : color);
	}

	public static TMP_FontAsset GetChineseFont()
	{
		if (cachedChineseFont != null)
		{
			return cachedChineseFont;
		}
		string[] array = new string[6] { "Fonts & Materials/NotoSansCJK-Regular SDF", "Fonts & Materials/NotoSansSC-Regular SDF", "Fonts & Materials/SourceHanSans-Regular SDF", "Fonts & Materials/NotoSansCJKsc-Regular SDF", "Fonts & Materials/NotoSansCJKjp-Regular SDF", "Fonts & Materials/NotoSansCJKkr-Regular SDF" };
		foreach (string text in array)
		{
			TMP_FontAsset tMP_FontAsset = Resources.Load<TMP_FontAsset>(text);
			if (tMP_FontAsset != null)
			{
				cachedChineseFont = tMP_FontAsset;
				Debug.Log("Successfully loaded Chinese font: " + text);
				return tMP_FontAsset;
			}
		}
		TMP_FontAsset result = (cachedChineseFont = Resources.Load<TMP_FontAsset>("Fonts & Materials/LiberationSans SDF"));
		Debug.LogWarning("Chinese font not found, using default font");
		return result;
	}

	public static TMP_FontAsset GetChineseFontForInput()
	{
		return GetChineseFont();
	}

	public void CreateUI()
	{
		TMP_FontAsset chineseFont = GetChineseFont();
		if (ChatWindow.LastSenderId != senderId)
		{
			if (LocalManager.players.TryGetValue(senderId, out var value))
			{
				label_playerName = Builder.CreateLabel(ChatWindow.window_messages, 470, 30, 0, 0, value.username);
				TextMeshProUGUI obj = label_playerName.FieldRef<TextMeshProUGUI>("textAdapter");
				obj.font = chineseFont;
				obj.ForceMeshUpdate();
				label_playerName.TextAlignment = TextAlignmentOptions.Left;
				label_playerName.Color = value.iconColor;
			}
			else
			{
				label_playerName = Builder.CreateLabel(ChatWindow.window_messages, 470, 30, 0, 0, "SERVER");
				TextMeshProUGUI obj2 = label_playerName.FieldRef<TextMeshProUGUI>("textAdapter");
				obj2.font = chineseFont;
				obj2.ForceMeshUpdate();
				label_playerName.TextAlignment = TextAlignmentOptions.Left;
				label_playerName.FontStyle = FontStyles.Bold;
			}
		}
		label_message = Builder.CreateLabel(ChatWindow.window_messages, 470, 25, 0, 0, message);
		TextMeshProUGUI textMeshProUGUI = label_message.FieldRef<TextMeshProUGUI>("textAdapter");
		textMeshProUGUI.enableWordWrapping = true;
		textMeshProUGUI.font = chineseFont;
		textMeshProUGUI.color = color;
		textMeshProUGUI.ForceMeshUpdate();
		label_message.AutoFontResize = false;
		label_message.TextAlignment = TextAlignmentOptions.TopLeft;
		label_message.Size = new Vector2(label_message.Size.x, textMeshProUGUI.preferredHeight);
	}

	public void DestroyUI()
	{
		if (label_playerName != null)
		{
			Object.Destroy(label_playerName.gameObject);
			label_playerName = null;
		}
		if (label_message != null)
		{
			Object.Destroy(label_message.gameObject);
			label_message = null;
		}
	}
}
