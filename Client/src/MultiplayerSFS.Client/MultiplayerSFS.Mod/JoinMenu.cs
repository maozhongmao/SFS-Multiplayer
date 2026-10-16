// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.IO;
using System.Linq;
using System.Net;
using System.Net.NetworkInformation;
using System.Net.Sockets;
using System.Threading.Tasks;
using MultiplayerSFS.Common;
using MultiplayerSFS.Common.Transport.Kcp;
using SFS.Input;
using SFS.UI;
using SFS.UI.ModGUI;
using TMPro;
using UITools;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public class JoinMenu : BasicMenu
{
	public static JoinMenu main;
	public static Window window;
	public static GameObject windowHolder;
	private static readonly int windowID = Builder.GetRandomID();
	// 内容实高 = 输入区120 + 声明区(320+8+24=352) + 按钮90 + 两处间距40 + 内边距20 = 622，
	// 加标题栏约 65 → 742 时底部留白与上一版(760/内容640)一致。要增减底部留白直接改这个数。
	// 【2026-10-06 底部 +12 像素】用户逐像素数出来"底部差 12 像素"，要求补上。
	// 外层布局是 childAlignment: MiddleCenter —— 余量**上下对半分**（这点用户早先纠正过我：
	// "不能直接减算"）。所以要让底部多 12px，得把窗口高度加 **24**：
	// 底部 +12、顶部也 +12（整窗高 24px）。
	// 若只想要"底部多 12、顶部不动"，改的就不是这个数，而是 CreateLayoutGroup 的
	// padding 底边（+24 会把整块内容上移 12、底部多 12）。这里先按"整窗长高"处理。
	private static readonly Vector2Int windowSize = new Vector2Int(1000, 766);
	private const int ContentWidth = 940;
	private const int RowGap = 20;
	private const int LabelWidth = 300;
	private const int InputWidth = ContentWidth - LabelWidth - RowGap;
	private const int RowHeight = 50;
	private const int ButtonWidth = (ContentWidth - RowGap) / 2;
	private const int ButtonHeight = 90;
	private const int StatementInnerPadding = 15;
	private const int StatementExpandedHeight = 320;
	// 状态行只有一行文字（字号 18，行高约 22）：给到 24 就是贴边，不再产生上下空白带
	private const int StatusHeight = 24;
	// 地址栏只写 IP、没写端口时的默认端口
	private const int DefaultPort = 9806;
	private const string Statement = @"STCH Studio Multiplayer Mod and Relay Server Statement

Updated: August 16, 2026

STCH Studio, independently established and operated by the developer known as maozhongmao / yangchengtong (""STCH Studio"" or ""the Developer""), hereby states:

This multiplayer mod and relay server (the ""Service"") are independently developed by the Developer as a non-official public-interest tool intended to support multiplayer play for the Spaceflight Simulator (SFS) community. Please read and comply with this Statement before use. By using the Service, you are deemed to have accepted all terms of this Statement.

1. Non-Official Status

The Service is independently developed by STCH Studio. It has no affiliation, authorization, partnership, endorsement, or other relationship with SFS, its official team, or its rights holders.

2. Legitimate Game Ownership

Before using the Service, you must have lawfully purchased and own a legitimate copy of SFS. The Service does not provide, distribute, or bundle any game files, and the Developer accepts no responsibility arising from the legality of a user's game copy.

3. Free and Non-Commercial Use

The Service is permanently free for all players. No person or organization may charge fees, create donation or sponsorship portals, sell access, provide paid top-ups, or conduct any commercial or profit-making activity through the Service.

4. Technical Scope

The Service only relays real-time player spacecraft synchronization data. It does not reverse engineer, modify, inject into, or emulate a crack of the core game program. If SFS rights holders formally request removal, STCH Studio will promptly stop the Service and cooperate as required.

5. User Responsibility

Each user is solely responsible for all content, statements, actions, and interactions created during multiplayer sessions. The Developer may record, warn, restrict, or terminate access for violations and may report unlawful information to the relevant authorities.

6. Service Availability Disclaimer

The Service is provided ""as is."" The server runs on the Developer's local equipment and is subject to personal hardware and network limitations. Availability, uninterrupted operation, low latency, and absolute security are not guaranteed. STCH Studio and the Developer are not liable for interruptions or data loss caused by device failures, network instability, third-party attacks, force majeure, natural disasters, policy changes, or official game restrictions.

7. Data Collection and Privacy

1. To operate the Service, only player game IDs, IP addresses, and spacecraft synchronization data are recorded. No other sensitive information is collected.
2. Such data is stored on the Developer's local equipment solely for multiplayer operation and service security. It will not be sold or disclosed to third parties without user consent.
3. The Service will handle such information in accordance with applicable laws and regulations.

8. Prohibited Conduct

Users must not use the Service to:

1. Distribute malicious code or viruses, or carry out network attacks, including attacks against this server.
2. Exploit vulnerabilities to disrupt other players or damage server stability.
3. Impersonate others, publish unlawful content, or commit fraud.
4. Use the Service for any unlawful purpose.

Violators may be banned and held accountable.

9. Changes and Termination

The Developer may update, suspend, or permanently terminate the Service at any time and will provide notice where reasonably possible. Stored user data will not be retained after termination; users must maintain their own backups.

10. Governing Law and Disputes

This Statement is governed by applicable laws and regulations. Disputes should first be resolved through friendly negotiation. If negotiation fails, either party may bring the dispute before a court with jurisdiction over the Developer's actual place of residence.

11. Support and Feedback

For questions, suggestions, or reports of violations:

Official QQ Group 2: 679991439
Email: maozhongmao@qq.com / yangchengtong@stch.de5.net

12. Open-Source Notice

This multiplayer mod is released under the MIT License:

1. License: MIT License
2. License URL: https://opensource.org/licenses/MIT
3. Source repository: https://github.com/maozhongmao/Spaceflight-simulator-Multiplayer
4. Copyright: Copyright © 2026 STCH Studio (Developer: maozhongmao / yangchengtong). All rights reserved.
5. This mod is an independently rewritten implementation, not a copy or fork of any existing SFS multiplayer project. During reference and learning work, the Developer found prior reference projects unable to meet the required functionality and completely reworked the network transport, synchronization strategy, event handling, and architecture through multiple independent iterations: Net V1, TCP Net V2, and TCP V3. Any use, modification, or distribution must comply with the MIT License. STCH Studio retains independent copyright in its added code.

STCH Studio (Developer: maozhongmao / yangchengtong)
Private email: maozhongmao@qq.com / yangchengtong@stch.de5.net
Studio email: stch-stuido@stch.de5.net
Published: August 16, 2026";
	private const string StatementAcceptedKey = "multiplayersfs.statement.accepted";
	private const string StatementVersion = "2026-08-16";
	private const string LastServerKey = "multiplayersfs.last.server";
	private const string LastUsernameKey = "multiplayersfs.last.username";

	public JoinInfo joinInfo = new JoinInfo();
	private Color defaultTextInputColor;
	private TextInput input_endpoint;
	private TextInput input_username;
	// 密码行常驻显示。不做「服务端要密码才自动出现」那套：多一条隐现逻辑就多一处
	// 不确定性（标志位没传到就不显示，玩家根本没法输密码），而服务端不要密码时留空即可。
	// 密码只留在内存里，不写 PlayerPrefs —— 参考文档把口令列入禁止持久化的内容。
	private Label label_password;
	private TextInput input_password;
	// 上次保存的地址原文（用户可能省略了端口），只用于回填输入框
	private string endpointInputText;
	// 探测循环上次解析过的输入框内容，用来避免每秒重复解析同一个地址
	private string probedEndpointText;
	private SFS.UI.ModGUI.Button agreeButton;
	private ScrollElement statementScroll;
	private Container statementArea;
	private UITools.ClosableWindow statementWindow;
	private Label statusLabel;
	private bool statementRead;
	private int statementLayoutFrames;
	private int serverInfoGeneration;
	private bool joinAllowedByStatus = true;

	// 声明窗标题随接受状态变化。全英文：SFS 不支持中文，中文会渲染成方框。
	private string statementWindowTitle => statementRead
		? "User Statement (accepted)"
		: "User Statement (read to the end)";

	protected override CloseMode OnEscape => CloseMode.Current;

	public static void OpenMenu()
	{
		windowHolder = Builder.CreateHolder(Builder.SceneToAttach.CurrentScene, "MultiplayerSFS - Join Menu Holder");
		main = windowHolder.AddComponent<JoinMenu>();
		main.OnOpen();
	}

	public override void OnOpen()
	{
		LoadLastConnection();
		if (ScreenManager.main.CurrentScreen != this)
		{
			ScreenManager.main.OpenScreen(() => this);
			windowHolder.SetActive(value: true);
			ClientManager.multiplayerEnabled.Value = true;
			window = Builder.CreateWindow(windowHolder.transform, windowID, windowSize.x, windowSize.y, 0, windowSize.y / 2, draggable: false, savePosition: false, 1f, "Multiplayer SFS - Join Menu");
			CreateUI();
		}
	}

	private void LoadLastConnection()
	{
		string endpoint = PlayerPrefs.GetString(LastServerKey, string.Empty).Trim();
		string username = PlayerPrefs.GetString(LastUsernameKey, string.Empty);
		if (endpoint.Length > 0)
		{
			// 端口可省：没写按默认端口算；写了就必须是合法端口，否则整条不认
			int separator = endpoint.LastIndexOf(':');
			string host = endpoint;
			int port = DefaultPort;
			bool parsed = true;
			if (separator > 0)
			{
				host = endpoint.Substring(0, separator);
				if (!int.TryParse(endpoint.Substring(separator + 1), out port) || port < 1 || port > 65535)
					parsed = false;
			}
			if (parsed)
			{
				// 回填一律用原文。以前只在 IPAddress.TryParse 成功时才回填，结果存了域名的玩家
				// 重开菜单会被静默换成默认的 127.0.0.1:9806 —— 域名在解析器眼里"不算地址"。
				endpointInputText = endpoint;
				// 只有 IP 字面量能在这里同步填进 joinInfo；域名要等异步解析（探测循环每轮会补）
				if (IPAddress.TryParse(host, out IPAddress address))
				{
					joinInfo.address = address;
					joinInfo.port = port;
				}
			}
		}
		if (!string.IsNullOrWhiteSpace(username)) joinInfo.username = username;
	}

	public override void Close()
	{
		serverInfoGeneration++;
		if (ScreenManager.main.CurrentScreen == this && windowHolder != null)
		{
			ClientManager.multiplayerEnabled.Value = false;
			ScreenManager.main.CloseCurrent();
			windowHolder.SetActive(value: false);
		}
	}

	private void CreateUI()
	{
		statementRead = HasAcceptedStatement();
		statementLayoutFrames = 0;
		window.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.MiddleCenter, spacing: RowGap, padding: new RectOffset(10, 10, 10, 10), disableChildSizeControl: true);
		Container row = Builder.CreateContainer(window);
		row.CreateLayoutGroup(SFS.UI.ModGUI.Type.Horizontal, childAlignment: TextAnchor.MiddleCenter, spacing: RowGap, disableChildSizeControl: true);
		Container labelColumn = Builder.CreateContainer(row);
		Container inputColumn = Builder.CreateContainer(row);
		labelColumn.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.MiddleLeft, spacing: RowGap, disableChildSizeControl: true);
		inputColumn.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.MiddleLeft, spacing: RowGap, disableChildSizeControl: true);

		Builder.CreateLabel(labelColumn, LabelWidth, RowHeight, 0, 0, "Server").TextAlignment = TextAlignmentOptions.MidlineLeft;
		input_endpoint = Builder.CreateTextInput(inputColumn, InputWidth, RowHeight, 0, 0,
			endpointInputText ?? (joinInfo.address + ":" + joinInfo.port), async delegate(string input)
		{
			// 空框是"还没填"的正常状态，不标红；只有非空却解析不出地址时才标红
			input_endpoint.FieldColor = string.IsNullOrWhiteSpace(input) || await TryParseEndpoint(input, joinInfo)
				? defaultTextInputColor
				: Color.red;
		});
		defaultTextInputColor = input_endpoint.FieldColor;

		Builder.CreateLabel(labelColumn, LabelWidth, RowHeight, 0, 0, "Username").TextAlignment = TextAlignmentOptions.MidlineLeft;
		input_username = Builder.CreateTextInput(inputColumn, InputWidth, RowHeight, 0, 0, joinInfo.username, delegate(string input)
		{
			input_username.Text = joinInfo.username = input.Trim();
			input_username.FieldColor = defaultTextInputColor;
		});

		// 密码行常驻。窗口尺寸不动：底部本来就留有余量，多这一行正好落在那块留白里，
		// 不要再去压缩声明区 —— 那样留白还在、声明区反而变矮，布局就难看了。
		// 密码不做 Trim：口令里的空格可能是有效字符。
		label_password = Builder.CreateLabel(labelColumn, LabelWidth, RowHeight, 0, 0, "Password");
		label_password.TextAlignment = TextAlignmentOptions.MidlineLeft;
		input_password = Builder.CreateTextInput(inputColumn, InputWidth, RowHeight, 0, 0, joinInfo.password, delegate(string input)
		{
			input_password.Text = joinInfo.password = input;
			input_password.FieldColor = defaultTextInputColor;
		});
		statementArea = Builder.CreateContainer(window);
		statementArea.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.MiddleCenter, spacing: 8f, padding: new RectOffset(0, 0, 0, 0), disableChildSizeControl: true);
		// 接受状态并进声明窗标题（原来是窗上方单独一行，白占一截高度）。
		// 文案保持英文：SFS 不支持中文，中文会渲染成方框。
		statementWindow = UIToolsBuilder.CreateClosableWindow(statementArea, Builder.GetRandomID(), ContentWidth, StatementExpandedHeight, 0, 0, draggable: false, savePosition: false, opacity: 0.45f, titleText: statementWindowTitle, minimized: false);
		statementWindow.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.UpperLeft, spacing: 8f, padding: new RectOffset(StatementInnerPadding, StatementInnerPadding, StatementInnerPadding, StatementInnerPadding), disableChildSizeControl: true);
		statementWindow.EnableScrolling(SFS.UI.ModGUI.Type.Vertical);
		statementScroll = statementWindow.ChildrenHolder.GetComponent<ScrollElement>();
		statementScroll.border = 0;
		statementScroll.startPivot = new Vector2(0.5f, 1f);
		statementScroll.backupPivot = statementScroll.startPivot;
		statementScroll.ResetPosition();
		Label statementLabel = Builder.CreateLabel(statementWindow, ContentWidth - StatementInnerPadding * 2, 0, 0, 0, Statement);
		statementLabel.AutoFontResize = false;
		statementLabel.FontSize = 18f;
		statementLabel.TextAlignment = TextAlignmentOptions.TopLeft;
		statementLabel.gameObject.AddComponent<UnityEngine.UI.ContentSizeFitter>().verticalFit = UnityEngine.UI.ContentSizeFitter.FitMode.PreferredSize;
		Builder.CreateSpace(statementWindow, ContentWidth - StatementInnerPadding * 2, 48);

		// 状态行塞进声明区里（原来它是 window 的独立子项，上下各吃一次 RowGap=20，
		// 等于凭空多出两截空白）。现在声明→8→状态行→20→按钮，只留按钮与须知之间那一份留白。
		statusLabel = Builder.CreateLabel(statementArea, ContentWidth, StatusHeight, 0, 0, "Server status unavailable.");
		statusLabel.AutoFontResize = false;
		statusLabel.FontSize = 18f;
		statusLabel.TextAlignment = TextAlignmentOptions.MidlineLeft;

		Container buttons = Builder.CreateContainer(window);
		buttons.CreateLayoutGroup(SFS.UI.ModGUI.Type.Horizontal, childAlignment: TextAnchor.MiddleLeft, spacing: RowGap, disableChildSizeControl: true);
		Builder.CreateButton(buttons, ButtonWidth, ButtonHeight, 0, 0, Close, "Back");
		agreeButton = Builder.CreateButton(buttons, ButtonWidth, ButtonHeight, 0, 0, CheckAndJoin, statementRead ? "Join Server" : "Read to the end to continue");
		agreeButton.gameObject.GetComponent<ButtonPC>().SetEnabled(statementRead);
		serverInfoGeneration++;
		_ = PollServerStatusAsync(serverInfoGeneration);
	}

	private sealed class ServerStatus
	{
		public bool Reachable;
		public double RoundTripMilliseconds;
		public int CurrentPlayers;
		public int MaxPlayers;
		public bool VersionKnown;
		public int HandshakeVersion;
		public int ProtocolVersion;
		public string ServerVersion = string.Empty;
		// 版本帧尾部的可选 flags：bit0 = 服务端要求密码。旧服务端不发，读到就是 0。
		public int Flags;
		public bool PasswordRequired => (Flags & ServerVersionCodec.ServerPolicyFlagPasswordRequired) != 0;
	}

	private async Task PollServerStatusAsync(int generation)
	{
		while (generation == serverInfoGeneration && windowHolder != null)
		{
			// 每轮按输入框当前内容重新解析。TextInput 的值回调不一定每次按键都触发，
			// 只靠回调会让探测一直打在上一次的旧地址上（面板永远 unreachable，但点 Join 能连上）。
			if (input_endpoint != null && input_endpoint.gameObject != null)
			{
				string text = input_endpoint.Text;
				if (text != probedEndpointText)
				{
					probedEndpointText = text;
					await TryParseEndpoint(text, joinInfo);
				}
			}

			ServerStatus status = await QueryServerStatusAsync(joinInfo.address, joinInfo.port);
			if (generation != serverInfoGeneration || statusLabel == null || statusLabel.gameObject == null) return;
			ApplyServerStatus(status);
			await Task.Delay(1000);
		}
	}

	private void ApplyServerStatus(ServerStatus status)
	{
		string serverLine;
		if (!status.Reachable) serverLine = "Server: unreachable";
		else if (!status.VersionKnown) serverLine = "Server: version not reported";
		else serverLine = "Server V" + status.ServerVersion + " (handshake " + status.HandshakeVersion + ")";
		string clientLine = "Client V" + Main.ModVersionInfo.Version + " (handshake " + SessionHandshakeCodec.Version + ")";
		string metricsLine = status.Reachable
			? string.Format("Ping: {0:F0} ms   Players: {1}/{2}", status.RoundTripMilliseconds, status.CurrentPlayers, status.MaxPlayers)
			: "Ping: --   Players: --";

		bool compatible = true;
		string reason = null;
		if (status.Reachable && status.VersionKnown)
		{
			compatible = ServerVersionCodec.IsCompatible(status.HandshakeVersion, status.ServerVersion,
				SessionHandshakeCodec.Version, Main.ModVersionInfo.Version, out reason);
		}

		joinAllowedByStatus = compatible;
		// 状态行平时只有一行（高 24）；带拒绝原因时是两行，不放宽会把原因截掉。
		//
		// 【底部为什么会差 12 像素】外层 window 的布局是 childAlignment: MiddleCenter ——
		// 多出来的余量**上下对半分**。状态行从 24 变成 48 会让声明区高 24，
		// 于是底部留白正好少 12。这就是"登录页底部差 12 像素"的来源（用户逐像素数出来的）。
		//
		// 根治办法：让「声明窗 + 状态行」这一段**总高恒定** —— 状态行长高多少，声明窗就矮多少
		// （声明窗本来就可滚动，少 24px 不影响可读性）。这样底部留白永远是 25px，不再跳。
		// 注意不能反过来改 windowSize：MiddleCenter 下改它 1px 只挪 0.5px，算不准。
		if (statusLabel != null && statusLabel.gameObject != null)
		{
			int statusHeight = reason == null ? StatusHeight : StatusHeight * 2;
			statusLabel.Size = new Vector2(ContentWidth, statusHeight);
			if (statementWindow != null && statementWindow.gameObject != null)
			{
				statementWindow.Size = new Vector2(
					ContentWidth, StatementExpandedHeight - (statusHeight - StatusHeight));
			}
		}
		// 三组数据横向排一行：竖着摆三行会把窗口高度吃满，按钮被顶出可视区。
		// 版本不匹配的原因（reason）仍另起一行，只在真出问题时占第二行。
		// 服务端在版本帧的 flags 里说明要不要密码时，只在状态行加一句提示 —— 不改布局。
		statusLabel.Text = serverLine
			+ (status.PasswordRequired ? "   Password: required" : string.Empty)
			+ "   " + clientLine + "   " + metricsLine
			+ (reason == null ? string.Empty : "\n" + reason);
		RefreshJoinButton();
	}


	private void RefreshJoinButton()
	{
		if (agreeButton == null || agreeButton.gameObject == null) return;
		agreeButton.gameObject.GetComponent<ButtonPC>().SetEnabled(statementRead && joinAllowedByStatus);
		agreeButton.Text = !joinAllowedByStatus
			? "Version mismatch"
			: (statementRead ? "Join Server" : "Read to the end to continue");
	}

	// 探针会话标识：每次递增，绝不复用（原因见 QueryServerStatusAsync 里的注释）。
	private static int probeConvCounter = 0x53465349;
	private static uint NextProbeConv() =>
		(uint)System.Threading.Interlocked.Increment(ref probeConvCounter);

	// UdpClient(AddressFamily) 建出来的是未绑定 socket，对它调 ReceiveAsync 会抛
	// "在执行此操作前必须先调用 Bind 方法"。传输层、P2P 通道都踩过这个坑，这里统一显式绑定。
	private static UdpClient CreateBoundUdp(AddressFamily family)
	{
		var client = new UdpClient(family);
		client.Client.Bind(new IPEndPoint(
			family == AddressFamily.InterNetworkV6 ? IPAddress.IPv6Any : IPAddress.Any, 0));
		MultiplayerSFS.Common.UdpSocketGuard.DisableConnReset(client);
		// 探测也走同一套缓冲设置：登录页每秒一次探测，缓冲太小同样会被内核丢包。
		client.Client.ReceiveBufferSize = 1 << 20;
		client.Client.SendBufferSize = 1 << 20;
		return client;
	}

	private static async Task<ServerStatus> QueryServerStatusAsync(IPAddress address, int port)
	{
		var status = new ServerStatus();
		if (address == null || port < 1 || port > 65535) return status;

		var stopwatch = System.Diagnostics.Stopwatch.StartNew();
		try
		{
			// 【必须每次换 conv】以前固定 0x53465349：上一次探针的迟到重传会落进下一次的新会话
			// （服务端按 conv 认会话，同一 conv 换源端口就重建），段序号空间一混，服务端就
			// "TCP frame kind is invalid" 并踢掉会话 —— 线上日志里这条刷了 3832 次。
			uint conv = NextProbeConv();
			using (var udp = CreateBoundUdp(address.AddressFamily))
			using (var cancellation = new System.Threading.CancellationTokenSource(3000))
			using (var kcp = new KcpContext(conv, (data, _) => udp.Send(data, data.Length, new IPEndPoint(address, port))))
			{
				// 预设集中在 Common/KcpTransportPresets（连接前探针也用同一套，避免各处参数漂移）。
				MultiplayerSFS.Common.KcpTransportPresets.Apply(kcp, MultiplayerSFS.Common.KcpTransportPresets.Relay);
				byte[] infoRequest = TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.ServerInfoRequest, 0, ServerInfoCodec.EncodeRequest(), 0));
				byte[] versionRequest = TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.ServerVersionRequest, 0, ServerVersionCodec.EncodeRequest(), 0));
				kcp.Send(infoRequest, 0, infoRequest.Length);
				kcp.Send(versionRequest, 0, versionRequest.Length);
				kcp.Flush();

				var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
				DateTime countsArrivedAt = DateTime.MinValue;
				while (DateTime.UtcNow < deadline)
				{
					// 旧服务端不认 ServerVersionRequest：人数到手后再等 700ms 就收工，别每次都拖满 3 秒。
					if (status.Reachable && !status.VersionKnown && DateTime.UtcNow > countsArrivedAt + TimeSpan.FromMilliseconds(700)) break;

					kcp.Update((uint)Environment.TickCount);
					Task<UdpReceiveResult> receive = udp.ReceiveAsync();
					Task completed = await Task.WhenAny(receive, Task.Delay(20, cancellation.Token)).ConfigureAwait(false);
					if (completed == receive)
					{
						UdpReceiveResult datagram = await receive.ConfigureAwait(false);
						kcp.Input(datagram.Buffer, 0, datagram.Buffer.Length);
						byte[] buffer = new byte[4096];
						int length;
						while ((length = kcp.Recv(buffer, 0, buffer.Length)) > 0)
						{
							byte[] body = new byte[length];
							Buffer.BlockCopy(buffer, 0, body, 0, length);
							TcpFrame frame = TcpFrameCodec.DecodeBody(body);
							if (frame.Kind == TcpFrameKind.ServerInfoResponse)
							{
								var counts = ServerInfoCodec.DecodeResponse(frame.Payload);
								status.Reachable = true;
								status.RoundTripMilliseconds = stopwatch.Elapsed.TotalMilliseconds;
								status.CurrentPlayers = counts.CurrentPlayers;
								status.MaxPlayers = counts.MaxPlayers;
								countsArrivedAt = DateTime.UtcNow;
							}
							else if (frame.Kind == TcpFrameKind.ServerVersionResponse)
							{
								var version = ServerVersionCodec.DecodeResponse(frame.Payload);
								status.VersionKnown = true;
								status.HandshakeVersion = version.HandshakeVersion;
								status.ProtocolVersion = version.ProtocolVersion;
								status.ServerVersion = version.ServerVersion;
								status.Flags = version.Flags;
							}
						}
					}
					kcp.Update((uint)Environment.TickCount);
					kcp.Flush();
				}
			}
		}
		catch
		{
			status.Reachable = false;
		}
		return status;
	}

	private static bool HasAcceptedStatement()
	{
		return PlayerPrefs.GetString(StatementAcceptedKey, string.Empty) == StatementVersion;
	}

	private static void SaveStatementAcceptance()
	{
		PlayerPrefs.SetString(StatementAcceptedKey, StatementVersion);
		PlayerPrefs.Save();
	}

	private void OnDestroy()
	{
		serverInfoGeneration++;
	}

	private void Update()
	{
		if (statementRead || statementScroll == null) return;
		if (++statementLayoutFrames < 4) return;
		if (statementScroll.FreeMoveSpace.y <= 0f) return;
		if (statementScroll.PercentPosition.y <= 0.0001f)
		{
			statementRead = true;
			SaveStatementAcceptance();
			if (statementWindow != null && statementWindow.gameObject != null)
				statementWindow.Title = statementWindowTitle;
			RefreshJoinButton();
		}
	}

	private async void CheckAndJoin()
	{
		if (!statementRead || !joinAllowedByStatus) return;
		try
		{
			input_endpoint.FieldColor = defaultTextInputColor;
			input_username.FieldColor = defaultTextInputColor;
			// 密码同样直接取输入框当前值，不依赖 onChange 回调（回调可能落后一次输入）
			if (input_password != null && input_password.gameObject != null)
			{
				input_password.FieldColor = defaultTextInputColor;
				joinInfo.password = input_password.Text ?? string.Empty;
			}

			// 点 Join 就先落盘，不等校验和连接结果：地址能解析就存地址，用户名非空就存用户名。
			// 以前是"地址+用户名都过关"才存 —— 用户名空着时新填的地址会丢，下次进菜单看到的是旧地址。
			// 用户名同样直接取输入框当前值，不依赖回调 —— 否则可能拿上一次的旧名字去连
			string usernameText = (input_username.Text ?? string.Empty).Trim();
			if (usernameText.Length > 0) joinInfo.username = usernameText;

			bool endpointOk = await TryParseEndpoint(input_endpoint.Text, joinInfo);
			if (endpointOk)
			{
				// 存原文：用户省略端口时连接按 9806 算，但下次打开框里还是他打的那样
				PlayerPrefs.SetString(LastServerKey, input_endpoint.Text.Trim());
				if (usernameText.Length > 0) PlayerPrefs.SetString(LastUsernameKey, usernameText);
				PlayerPrefs.Save();
			}

			if (string.IsNullOrWhiteSpace(input_endpoint.Text))
			{
				// 空着就空着，不标红（可能是还没填），只留一句提示
				MsgDrawer.main.Log("Enter the server IP address.");
			}
			else if (!endpointOk)
			{
				input_endpoint.FieldColor = Color.red;
				MsgDrawer.main.Log("Server must use IP:Port");
			}
			else if (string.IsNullOrWhiteSpace(input_username.Text))
			{
				input_username.FieldColor = Color.red;
				MsgDrawer.main.Log("Username cannot be empty");
			}
			else
			{
				MsgDrawer.main.Log("Attempting to connect...");
				await ClientManager.TryConnect(joinInfo);
			}
		}
		catch (Exception ex)
		{
			string message = string.IsNullOrWhiteSpace(ex.Message) ? "Connection failed." : ex.Message;
			MsgDrawer.main.Log(message);
			Debug.LogError(ex);
		}
	}

	private static async Task<bool> TryParseEndpoint(string input, JoinInfo info)
	{
		string text = input == null ? string.Empty : input.Trim();
		if (text.Length == 0) return false;

		// 连接串的三种写法都要认（IPv6 必须单独处理，见下）：
		//   1.2.3.4:9806 / example.com:9806   域名或 IPv4 + 端口
		//   1.2.3.4 / example.com             省略端口 -> 默认端口 + SRV 查询
		//   [240e:3bd::6173]:9806             带方括号的 IPv6 + 端口（唯一无歧义的写法）
		//   240e:3bd::6173                    裸 IPv6 字面量（默认端口）
		//
		// 【为什么必须特判】IPv6 字面量里全是冒号。按"最后一个冒号"切端口时，
		// `240e:3bd:231:...:6173` 会被切成主机 `240e:3bd:231:...:5d0c` + 端口 `6173`
		// —— 端口恰好合法，于是不报错，只是地址被切坏，最后表现为"地址无效"，很难查。
		int port = DefaultPort;
		bool portSpecified = false;
		string host = text;

		if (text[0] == '[')
		{
			int close = text.IndexOf(']');
			if (close < 0) return false;
			host = text.Substring(1, close - 1).Trim();
			string rest = text.Substring(close + 1).Trim();
			if (rest.Length > 0)
			{
				if (rest[0] != ':') return false;
				if (!int.TryParse(rest.Substring(1), out port) || port < 1 || port > 65535) return false;
				portSpecified = true;
			}
		}
		else if (IPAddress.TryParse(text, out _))
		{
			// 整串就是一个地址（IPv4 或裸 IPv6）：不做端口切分。
			host = text;
		}
		else
		{
			int separator = text.LastIndexOf(':');
			portSpecified = separator >= 0;
			if (portSpecified)
			{
				if (separator == text.Length - 1) return false;
				if (!int.TryParse(text.Substring(separator + 1), out port) || port < 1 || port > 65535) return false;
				host = text.Substring(0, separator).Trim();
			}
		}
		IPAddress address = await TryParseAddress(host);
		if (address == null) return false;
		// 没写端口才去问 SRV：域名背后的公网端口未必等于服务端监听的 9806
		// （内网穿透只给 ≥10000 的端口）。显式端口永远优先，不走这条路。
		if (!portSpecified)
		{
			int? srvPort = await SrvPortLookup.QueryAsync(host);
			if (srvPort.HasValue) port = srvPort.Value;
		}
		info.address = address;
		info.port = port;
		return true;
	}

	private static async Task<IPAddress> TryParseAddress(string input)
	{
		if (IPAddress.TryParse(input, out IPAddress address)) return address;
		try
		{
			IPAddress[] all = await Dns.GetHostAddressesAsync(input);
			if (all == null || all.Length == 0) return null;

			// 域名可能同时有 A 和 AAAA，而 GetHostAddresses 的顺序没有保证。
			// 本套部署是"域名只挂 AAAA 指向目标设备"，所以有可用的 IPv6 时优先走 IPv6；
			// 但本机没有 IPv6 出口时优先它只会白等一个超时，所以那时退回 IPv4。
			if (HasUsableIPv6())
			{
				foreach (IPAddress candidate in all)
				{
					if (candidate.AddressFamily != AddressFamily.InterNetworkV6) continue;
					if (candidate.IsIPv6LinkLocal || candidate.IsIPv6SiteLocal) continue;
					return candidate;
				}
			}
			foreach (IPAddress candidate in all)
			{
				if (candidate.AddressFamily == AddressFamily.InterNetwork) return candidate;
			}
			return all[0];
		}
		catch
		{
			return null;
		}
	}

	// 本机是否有"真能用"的 IPv6：存在一张已启用、非回环/非隧道网卡上的
	// 非 link-local、非 site-local 的 IPv6 单播地址。
	// 用它是为了避免"域名有 AAAA 但本机根本没 IPv6"时白等一整个连接超时。
	private static bool HasUsableIPv6()
	{
		try
		{
			foreach (NetworkInterface ni in NetworkInterface.GetAllNetworkInterfaces())
			{
				if (ni.OperationalStatus != OperationalStatus.Up) continue;
				if (ni.NetworkInterfaceType == NetworkInterfaceType.Loopback) continue;
				if (ni.NetworkInterfaceType == NetworkInterfaceType.Tunnel) continue;
				foreach (UnicastIPAddressInformation ua in ni.GetIPProperties().UnicastAddresses)
				{
					IPAddress a = ua.Address;
					if (a.AddressFamily != AddressFamily.InterNetworkV6) continue;
					if (a.IsIPv6LinkLocal || a.IsIPv6SiteLocal || IPAddress.IsLoopback(a)) continue;
					return true;
				}
			}
		}
		catch
		{
			// 查网卡失败不该阻断连接：当作没有 IPv6，退回 IPv4。
		}
		return false;
	}
}
