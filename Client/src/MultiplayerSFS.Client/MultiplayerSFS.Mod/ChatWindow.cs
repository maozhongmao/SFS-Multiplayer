// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Threading.Tasks;
using System.Timers;
using HarmonyLib;
using Lidgren.Network;
using MultiplayerSFS.Common;
using SFS.UI;
using SFS.UI.ModGUI;
using TMPro;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public static class ChatWindow
{
	public static readonly int windowID = Builder.GetRandomID();

	public static readonly int maxMessagesCount = 100;

	public static Color defaultInputColor;

	internal const int WindowWidth = 500;

	internal const int WindowHeight = 700;

	internal const int InnerWidth = 480;

	internal const int MessageWidth = 470;

	public static GameObject holder_window;

	public static Window window;

	public static Container container_colorPicker;

	public static TextInput input_colorPicker;

	public static Label label_colorPicker;

	public static SFS.UI.ModGUI.Button button_colorPicker;

	public static Window window_messages;

	public static readonly List<ChatMessage> messages = new List<ChatMessage>();

	public static Timer cooldownTimer;

	public static bool canSendMessage = true;

	public static TextInput input_sendMessage;

	private static Queue mainThreadActions = new Queue();

	private static object mainThreadActionsLock = new object();

	private static double cooldownSeconds = 0.0;

	public static bool InputSelected { get; private set; }

	public static int LastSenderId
	{
		get
		{
			int result = int.MinValue;
			foreach (ChatMessage message in messages)
			{
				if (message.label_message != null)
				{
					result = message.senderId;
				}
			}
			return result;
		}
	}

	public static async void CreateUI(string sceneName)
	{
		if (holder_window != null || !ClientManager.multiplayerEnabled)
		{
			return;
		}
		while (LocalManager.Player == null)
		{
			if (!(bool)ClientManager.multiplayerEnabled)
			{
				return;
			}
			await Task.Yield();
		}
		holder_window = Builder.CreateHolder(Builder.SceneToAttach.CurrentScene, "Multiplayer SFS - Chat Window Holder");
		window = Builder.CreateWindow(holder_window.transform, windowID, 500, 700, 0, 0, draggable: true, savePosition: true, 1f, "Multiplayer Chat");
		window.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical);
		int num = 620;
		container_colorPicker = Builder.CreateContainer(window);
		container_colorPicker.CreateLayoutGroup(SFS.UI.ModGUI.Type.Horizontal);
		Color.RGBToHSV(LocalManager.Player.iconColor, out var H, out var _, out var _);
		H *= 100f;
		input_colorPicker = Builder.CreateTextInput(container_colorPicker, 160, 50);
		input_colorPicker.Text = H.ToString();
		label_colorPicker = Builder.CreateLabel(container_colorPicker, 80, 50, 0, 0, "▲");
		button_colorPicker = Builder.CreateButton(container_colorPicker, 160, 50, 0, 0, OnColorPickerSubmit, "Change");
		TMP_FontAsset chineseFont = ChatMessage.GetChineseFont();
		TMP_InputField field = input_colorPicker.field;
		field.onSelect.AddListener(delegate
		{
			InputSelected = true;
		});
		field.onDeselect.AddListener(delegate
		{
			InputSelected = false;
		});
		field.textComponent.font = chineseFont;
		field.textComponent.ForceMeshUpdate();
		input_colorPicker.field.onValueChanged.AddListener(OnColorPickerChange);
		OnColorPickerChange(H.ToString());
		num -= 120;
		window_messages = Builder.CreateWindow(window, Builder.GetRandomID(), 480, num, 0, 0, draggable: false, savePosition: false);
		window_messages.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, TextAnchor.LowerLeft, 5f, new RectOffset(5, 5, 5, 5));
		window_messages.EnableScrolling(SFS.UI.ModGUI.Type.Vertical);
		foreach (ChatMessage message in messages)
		{
			message.CreateUI();
		}
		input_sendMessage = Builder.CreateTextInput(window, 480, 50);
		defaultInputColor = input_sendMessage.FieldColor;
		input_sendMessage.field.onSubmit.AddListener(OnMessageSubmit);
		input_sendMessage.field.onSelect.AddListener(delegate
		{
			InputSelected = true;
		});
		input_sendMessage.field.onDeselect.AddListener(delegate
		{
			InputSelected = false;
		});
		input_sendMessage.field.textComponent.alignment = TextAlignmentOptions.Left;
		input_sendMessage.field.textComponent.fontSize = 20f;
		input_sendMessage.field.textComponent.font = chineseFont;
		input_sendMessage.field.textComponent.ForceMeshUpdate();
		ChangeCooldownStatus(canSendMessage);
	}

	public static void DestroyUI()
	{
		if (!(holder_window != null))
		{
			return;
		}
		foreach (ChatMessage message in messages)
		{
			message.DestroyUI();
		}
		UnityEngine.Object.Destroy(holder_window);
	}

	public static void OnColorPickerChange(string hueText)
	{
		if (float.TryParse(hueText, out var result))
		{
			float num = result % 100f;
			if (num < 0f)
			{
				num += 100f;
			}
			if (num != result)
			{
				input_colorPicker.Text = num.ToString();
			}
			else if (label_colorPicker != null)
			{
				label_colorPicker.Color = Color.HSVToRGB(result / 100f, 1f, 1f);
			}
		}
	}

	public static void OnColorPickerSubmit()
	{
		Color color = (LocalManager.Player.iconColor = label_colorPicker.Color);
		ClientManager.SendPacket(new Packet_UpdatePlayerColor
		{
			PlayerId = ClientManager.playerId,
			Color = color
		}, (NetDeliveryMethod)67);
		OnPlayerColorChange(ClientManager.playerId, color);
	}

	public static void OnMessageSubmit(string message)
	{
		if (!string.IsNullOrEmpty(message) && canSendMessage)
		{
			AddMessage(new ChatMessage(message, ClientManager.playerId));
			if (cooldownTimer != null)
			{
				ChangeCooldownStatus(canSend: false);
				cooldownTimer.Stop();
				cooldownTimer.Start();
			}
			ClientManager.SendPacket(new Packet_SendChatMessage
			{
				SenderId = ClientManager.playerId,
				Message = message,
				Color = LocalManager.Player.iconColor
			}, (NetDeliveryMethod)67);
			input_sendMessage.Text = "";
			input_sendMessage.field.ActivateInputField();
		}
	}

	public static void CreateCooldownTimer(double cooldown)
	{
		if (cooldownTimer != null || !(cooldown > 0.0))
		{
			return;
		}
		cooldownSeconds = cooldown;
		cooldownTimer = new Timer
		{
			Interval = 1000.0 * cooldown,
			AutoReset = false
		};
		cooldownTimer.Elapsed += delegate
		{
			lock (mainThreadActionsLock)
			{
				mainThreadActions.Enqueue((Action)delegate
				{
					ChangeCooldownStatus(canSend: true);
				});
			}
		};
	}

	public static void Update()
	{
		while (true)
		{
			Action action = null;
			lock (mainThreadActionsLock)
			{
				if (mainThreadActions.Count <= 0)
				{
					break;
				}
				action = (Action)mainThreadActions.Dequeue();
			}
			action?.Invoke();
		}
	}

	public static void DestroyCooldownTimer()
	{
		if (cooldownTimer != null)
		{
			cooldownTimer.Dispose();
			cooldownTimer = null;
		}
	}

	public static void ChangeCooldownStatus(bool canSend)
	{
		canSendMessage = canSend;
		if (input_sendMessage != null)
		{
			input_sendMessage.FieldColor = (canSend ? defaultInputColor : Color.red);
		}
	}

	public static void AddMessage(ChatMessage message)
	{
		messages.Add(message);
		if (window_messages != null)
		{
			message.CreateUI();
			ScrollToBottom();
		}
		while (messages.Count > maxMessagesCount)
		{
			messages[0].DestroyUI();
			messages.RemoveAt(0);
		}
	}

	public static void ScrollToBottom()
	{
		if (window_messages != null)
		{
			ScrollElement component = window_messages.ChildrenHolder.GetComponent<ScrollElement>();
			if (component != null)
			{
				component.PercentPosition = new Vector2(0.5f, 1f);
			}
		}
	}

	public static void OnPlayerColorChange(int id, Color color)
	{
		foreach (ChatMessage message in messages)
		{
			if (message.senderId == id && message.label_playerName != null)
			{
				message.label_playerName.Color = color;
			}
		}
		if (LocalManager.players.TryGetValue(id, out var value) && LocalManager.syncedRockets.TryGetValue(value.controlledRocket.Value, out var value2) && value2.rocket != null)
		{
			new Traverse(value2.rocket.mapIcon).Method("UpdateAlpha").GetValue();
		}
	}
}
