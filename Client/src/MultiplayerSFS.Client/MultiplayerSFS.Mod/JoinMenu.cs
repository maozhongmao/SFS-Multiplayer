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
	// 鍐呭瀹為珮 = 杈撳叆鍖?20 + 澹版槑鍖?320+8+24=352) + 鎸夐挳90 + 涓ゅ闂磋窛40 + 鍐呰竟璺?0 = 622锛?
	// 鍔犳爣棰樻爮绾?65 鈫?742 鏃跺簳閮ㄧ暀鐧戒笌涓婁竴鐗?760/鍐呭640)涓€鑷淬€傝澧炲噺搴曢儴鐣欑櫧鐩存帴鏀硅繖涓暟銆?
	// 銆?026-10-06 搴曢儴 +12 鍍忕礌銆戠敤鎴烽€愬儚绱犳暟鍑烘潵"搴曢儴宸?12 鍍忕礌"锛岃姹傝ˉ涓娿€?
	// 澶栧眰甯冨眬鏄?childAlignment: MiddleCenter 鈥斺€?浣欓噺**涓婁笅瀵瑰崐鍒?*锛堣繖鐐圭敤鎴锋棭鍏堢籂姝ｈ繃鎴戯細
	// "涓嶈兘鐩存帴鍑忕畻"锛夈€傛墍浠ヨ璁╁簳閮ㄥ 12px锛屽緱鎶婄獥鍙ｉ珮搴﹀姞 **24**锛?
	// 搴曢儴 +12銆侀《閮ㄤ篃 +12锛堟暣绐楅珮 24px锛夈€?
	// 鑻ュ彧鎯宠"搴曢儴澶?12銆侀《閮ㄤ笉鍔?锛屾敼鐨勫氨涓嶆槸杩欎釜鏁帮紝鑰屾槸 CreateLayoutGroup 鐨?
	// padding 搴曡竟锛?24 浼氭妸鏁村潡鍐呭涓婄Щ 12銆佸簳閮ㄥ 12锛夈€傝繖閲屽厛鎸?鏁寸獥闀块珮"澶勭悊銆?
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
	// 鐘舵€佽鍙湁涓€琛屾枃瀛楋紙瀛楀彿 18锛岃楂樼害 22锛夛細缁欏埌 24 灏辨槸璐磋竟锛屼笉鍐嶄骇鐢熶笂涓嬬┖鐧藉甫
	private const int StatusHeight = 24;
	// 鍦板潃鏍忓彧鍐?IP銆佹病鍐欑鍙ｆ椂鐨勯粯璁ょ鍙?
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
4. Copyright: Copyright 漏 2026 STCH Studio (Developer: maozhongmao / yangchengtong). All rights reserved.
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
	// 瀵嗙爜琛屽父椹绘樉绀恒€備笉鍋氥€屾湇鍔＄瑕佸瘑鐮佹墠鑷姩鍑虹幇銆嶉偅濂楋細澶氫竴鏉￠殣鐜伴€昏緫灏卞涓€澶?
	// 涓嶇‘瀹氭€э紙鏍囧織浣嶆病浼犲埌灏变笉鏄剧ず锛岀帺瀹舵牴鏈病娉曡緭瀵嗙爜锛夛紝鑰屾湇鍔＄涓嶈瀵嗙爜鏃剁暀绌哄嵆鍙€?
	// 瀵嗙爜鍙暀鍦ㄥ唴瀛橀噷锛屼笉鍐?PlayerPrefs 鈥斺€?鍙傝€冩枃妗ｆ妸鍙ｄ护鍒楀叆绂佹鎸佷箙鍖栫殑鍐呭銆?
	private Label label_password;
	private TextInput input_password;
	// 涓婃淇濆瓨鐨勫湴鍧€鍘熸枃锛堢敤鎴峰彲鑳界渷鐣ヤ簡绔彛锛夛紝鍙敤浜庡洖濉緭鍏ユ
	private string endpointInputText;
	// 鎺㈡祴寰幆涓婃瑙ｆ瀽杩囩殑杈撳叆妗嗗唴瀹癸紝鐢ㄦ潵閬垮厤姣忕閲嶅瑙ｆ瀽鍚屼竴涓湴鍧€
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

	// 澹版槑绐楁爣棰橀殢鎺ュ彈鐘舵€佸彉鍖栥€傚叏鑻辨枃锛歋FS 涓嶆敮鎸佷腑鏂囷紝涓枃浼氭覆鏌撴垚鏂规銆?
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
			// 绔彛鍙渷锛氭病鍐欐寜榛樿绔彛绠楋紱鍐欎簡灏卞繀椤绘槸鍚堟硶绔彛锛屽惁鍒欐暣鏉′笉璁?
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
				// 鍥炲～涓€寰嬬敤鍘熸枃銆備互鍓嶅彧鍦?IPAddress.TryParse 鎴愬姛鏃舵墠鍥炲～锛岀粨鏋滃瓨浜嗗煙鍚嶇殑鐜╁
				// 閲嶅紑鑿滃崟浼氳闈欓粯鎹㈡垚榛樿鐨?127.0.0.1:9806 鈥斺€?鍩熷悕鍦ㄨВ鏋愬櫒鐪奸噷"涓嶇畻鍦板潃"銆?
				endpointInputText = endpoint;
				// 鍙湁 IP 瀛楅潰閲忚兘鍦ㄨ繖閲屽悓姝ュ～杩?joinInfo锛涘煙鍚嶈绛夊紓姝ヨВ鏋愶紙鎺㈡祴寰幆姣忚疆浼氳ˉ锛?
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
			// 绾㈣壊鍙〃绀衡€滆繖涓瓧绗︿覆涓嶆槸鍚堟硶鍦板潃鍐欐硶鈥濓紱DNS 鏆傛椂澶辫触涓嶈兘鎶婂悎娉曞煙鍚嶆爣绾€?
			bool syntaxOk = IsEndpointSyntaxValid(input);
			input_endpoint.FieldColor = syntaxOk ? defaultTextInputColor : Color.red;
			if (syntaxOk) await TryParseEndpoint(input, joinInfo);
		});
		defaultTextInputColor = input_endpoint.FieldColor;

		Builder.CreateLabel(labelColumn, LabelWidth, RowHeight, 0, 0, "Username").TextAlignment = TextAlignmentOptions.MidlineLeft;
		input_username = Builder.CreateTextInput(inputColumn, InputWidth, RowHeight, 0, 0, joinInfo.username, delegate(string input)
		{
			input_username.Text = joinInfo.username = input.Trim();
			// PC 绔彧鏀寔 ASCII锛氫腑鏂?闈炶嫳鏂囦細娓叉煋鎴愭柟妗嗐€傛闄愬埗浠?PC 绔湁锛孉ndroid 涓嶈蛋杩欏杈撳叆妗嗐€?
			input_username.FieldColor = IsAsciiOnly(input) ? defaultTextInputColor : Color.red;
		});

		// 瀵嗙爜琛屽父椹汇€傜獥鍙ｅ昂瀵镐笉鍔細搴曢儴鏈潵灏辩暀鏈変綑閲忥紝澶氳繖涓€琛屾濂借惤鍦ㄩ偅鍧楃暀鐧介噷锛?
		// 涓嶈鍐嶅幓鍘嬬缉澹版槑鍖?鈥斺€?閭ｆ牱鐣欑櫧杩樺湪銆佸０鏄庡尯鍙嶈€屽彉鐭紝甯冨眬灏遍毦鐪嬩簡銆?
		// 瀵嗙爜涓嶅仛 Trim锛氬彛浠ら噷鐨勭┖鏍煎彲鑳芥槸鏈夋晥瀛楃銆?
		label_password = Builder.CreateLabel(labelColumn, LabelWidth, RowHeight, 0, 0, "Password");
		label_password.TextAlignment = TextAlignmentOptions.MidlineLeft;
		input_password = Builder.CreateTextInput(inputColumn, InputWidth, RowHeight, 0, 0, joinInfo.password, delegate(string input)
		{
			input_password.Text = joinInfo.password = input;
			// 鍚屼笂锛歅C 绔彧鏀寔 ASCII锛岄潪鑻辨枃浼氭樉绀烘柟妗嗭紱Android 鏃犳杈撳叆鏈哄埗銆?
			input_password.FieldColor = IsAsciiOnly(input) ? defaultTextInputColor : Color.red;
		});
		statementArea = Builder.CreateContainer(window);
		statementArea.CreateLayoutGroup(SFS.UI.ModGUI.Type.Vertical, childAlignment: TextAnchor.MiddleCenter, spacing: 8f, padding: new RectOffset(0, 0, 0, 0), disableChildSizeControl: true);
		// 鎺ュ彈鐘舵€佸苟杩涘０鏄庣獥鏍囬锛堝師鏉ユ槸绐椾笂鏂瑰崟鐙竴琛岋紝鐧藉崰涓€鎴珮搴︼級銆?
		// 鏂囨淇濇寔鑻辨枃锛歋FS 涓嶆敮鎸佷腑鏂囷紝涓枃浼氭覆鏌撴垚鏂规銆?
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

		// 鐘舵€佽濉炶繘澹版槑鍖洪噷锛堝師鏉ュ畠鏄?window 鐨勭嫭绔嬪瓙椤癸紝涓婁笅鍚勫悆涓€娆?RowGap=20锛?
		// 绛変簬鍑┖澶氬嚭涓ゆ埅绌虹櫧锛夈€傜幇鍦ㄥ０鏄庘啋8鈫掔姸鎬佽鈫?0鈫掓寜閽紝鍙暀鎸夐挳涓庨』鐭ヤ箣闂撮偅涓€浠界暀鐧姐€?
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
		// 鐗堟湰甯у熬閮ㄧ殑鍙€?flags锛歜it0 = 鏈嶅姟绔姹傚瘑鐮併€傛棫鏈嶅姟绔笉鍙戯紝璇诲埌灏辨槸 0銆?
		public int Flags;
		public bool PasswordRequired => (Flags & ServerVersionCodec.ServerPolicyFlagPasswordRequired) != 0;
	}

	private async Task PollServerStatusAsync(int generation)
	{
		while (generation == serverInfoGeneration && windowHolder != null)
		{
			// 姣忚疆鎸夎緭鍏ユ褰撳墠鍐呭閲嶆柊瑙ｆ瀽銆俆extInput 鐨勫€煎洖璋冧笉涓€瀹氭瘡娆℃寜閿兘瑙﹀彂锛?
			// 鍙潬鍥炶皟浼氳鎺㈡祴涓€鐩存墦鍦ㄤ笂涓€娆＄殑鏃у湴鍧€涓婏紙闈㈡澘姘歌繙 unreachable锛屼絾鐐?Join 鑳借繛涓婏級銆?
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
			// 鐗堟湰瀛楃涓插彧鐢ㄤ簬灞曠ず锛涘悇缁勪欢杩樻病缁熶竴鎵撳寘锛屼笉鑳藉洜 V2.1/V1.4 宸紓闃绘鍔犲叆銆?
			// 鐪熸鐨勭‖闂ㄦ鏄彙鎵嬬増鏈€?
			ServerVersionCodec.IsCompatible(status.HandshakeVersion, status.ServerVersion,
				SessionHandshakeCodec.Version, Main.ModVersionInfo.Version, out reason);
			compatible = status.HandshakeVersion == SessionHandshakeCodec.Version;
		}

		joinAllowedByStatus = compatible;
		// 鐘舵€佽骞虫椂鍙湁涓€琛岋紙楂?24锛夛紱甯︽嫆缁濆師鍥犳椂鏄袱琛岋紝涓嶆斁瀹戒細鎶婂師鍥犳埅鎺夈€?
		//
		// 銆愬簳閮ㄤ负浠€涔堜細宸?12 鍍忕礌銆戝灞?window 鐨勫竷灞€鏄?childAlignment: MiddleCenter 鈥斺€?
		// 澶氬嚭鏉ョ殑浣欓噺**涓婁笅瀵瑰崐鍒?*銆傜姸鎬佽浠?24 鍙樻垚 48 浼氳澹版槑鍖洪珮 24锛?
		// 浜庢槸搴曢儴鐣欑櫧姝ｅソ灏?12銆傝繖灏辨槸"鐧诲綍椤靛簳閮ㄥ樊 12 鍍忕礌"鐨勬潵婧愶紙鐢ㄦ埛閫愬儚绱犳暟鍑烘潵鐨勶級銆?
		//
		// 鏍规不鍔炴硶锛氳銆屽０鏄庣獥 + 鐘舵€佽銆嶈繖涓€娈?*鎬婚珮鎭掑畾** 鈥斺€?鐘舵€佽闀块珮澶氬皯锛屽０鏄庣獥灏辩煯澶氬皯
		// 锛堝０鏄庣獥鏈潵灏卞彲婊氬姩锛屽皯 24px 涓嶅奖鍝嶅彲璇绘€э級銆傝繖鏍峰簳閮ㄧ暀鐧芥案杩滄槸 25px锛屼笉鍐嶈烦銆?
		// 娉ㄦ剰涓嶈兘鍙嶈繃鏉ユ敼 windowSize锛歁iddleCenter 涓嬫敼瀹?1px 鍙尓 0.5px锛岀畻涓嶅噯銆?
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
		// 涓夌粍鏁版嵁妯悜鎺掍竴琛岋細绔栫潃鎽嗕笁琛屼細鎶婄獥鍙ｉ珮搴﹀悆婊★紝鎸夐挳琚《鍑哄彲瑙嗗尯銆?
		// 鐗堟湰涓嶅尮閰嶇殑鍘熷洜锛坮eason锛変粛鍙﹁捣涓€琛岋紝鍙湪鐪熷嚭闂鏃跺崰绗簩琛屻€?
		// 鏈嶅姟绔湪鐗堟湰甯х殑 flags 閲岃鏄庤涓嶈瀵嗙爜鏃讹紝鍙湪鐘舵€佽鍔犱竴鍙ユ彁绀?鈥斺€?涓嶆敼甯冨眬銆?
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

	// 鎺㈤拡浼氳瘽鏍囪瘑锛氭瘡娆￠€掑锛岀粷涓嶅鐢紙鍘熷洜瑙?QueryServerStatusAsync 閲岀殑娉ㄩ噴锛夈€?
	private static int probeConvCounter = 0x53465349;
	private static uint NextProbeConv() =>
		(uint)System.Threading.Interlocked.Increment(ref probeConvCounter);

	// UdpClient(AddressFamily) 寤哄嚭鏉ョ殑鏄湭缁戝畾 socket锛屽瀹冭皟 ReceiveAsync 浼氭姏
	// "鍦ㄦ墽琛屾鎿嶄綔鍓嶅繀椤诲厛璋冪敤 Bind 鏂规硶"銆備紶杈撳眰銆丳2P 閫氶亾閮借俯杩囪繖涓潙锛岃繖閲岀粺涓€鏄惧紡缁戝畾銆?
	private static UdpClient CreateBoundUdp(AddressFamily family)
	{
		var client = new UdpClient(family);
		client.Client.Bind(new IPEndPoint(
			family == AddressFamily.InterNetworkV6 ? IPAddress.IPv6Any : IPAddress.Any, 0));
		MultiplayerSFS.Common.UdpSocketGuard.DisableConnReset(client);
		// 鎺㈡祴涔熻蛋鍚屼竴濂楃紦鍐茶缃細鐧诲綍椤垫瘡绉掍竴娆℃帰娴嬶紝缂撳啿澶皬鍚屾牱浼氳鍐呮牳涓㈠寘銆?
		client.Client.ReceiveBufferSize = 1 << 20;
		client.Client.SendBufferSize = 1 << 20;
		return client;
	}

	private static byte[] WrapKcpMessage(byte[] frameData)
	{
		byte[] message = new byte[4 + frameData.Length];
		message[0] = (byte)frameData.Length;
		message[1] = (byte)(frameData.Length >> 8);
		message[2] = (byte)(frameData.Length >> 16);
		message[3] = (byte)(frameData.Length >> 24);
		Buffer.BlockCopy(frameData, 0, message, 4, frameData.Length);
		return message;
	}

	private static async Task<ServerStatus> QueryServerStatusAsync(IPAddress address, int port)
	{
		var status = new ServerStatus();
		if (address == null || port < 1 || port > 65535) return status;

		var stopwatch = System.Diagnostics.Stopwatch.StartNew();
		try
		{
			// 銆愬繀椤绘瘡娆℃崲 conv銆戜互鍓嶅浐瀹?0x53465349锛氫笂涓€娆℃帰閽堢殑杩熷埌閲嶄紶浼氳惤杩涗笅涓€娆＄殑鏂颁細璇?
			// 锛堟湇鍔＄鎸?conv 璁や細璇濓紝鍚屼竴 conv 鎹㈡簮绔彛灏遍噸寤猴級锛屾搴忓彿绌洪棿涓€娣凤紝鏈嶅姟绔氨
			// "TCP frame kind is invalid" 骞惰涪鎺変細璇?鈥斺€?绾夸笂鏃ュ織閲岃繖鏉″埛浜?3832 娆°€?
			uint conv = NextProbeConv();
			using (var udp = CreateBoundUdp(address.AddressFamily))
			using (var cancellation = new System.Threading.CancellationTokenSource(3000))
			using (var kcp = new KcpContext(conv, (data, _) => udp.Send(data, data.Length, new IPEndPoint(address, port))))
			{
				// 棰勮闆嗕腑鍦?Common/KcpTransportPresets锛堣繛鎺ュ墠鎺㈤拡涔熺敤鍚屼竴濂楋紝閬垮厤鍚勫鍙傛暟婕傜Щ锛夈€?
				MultiplayerSFS.Common.KcpTransportPresets.Apply(kcp, MultiplayerSFS.Common.KcpTransportPresets.Relay);
				byte[] infoRequest = WrapKcpMessage(TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.ServerInfoRequest, 0, ServerInfoCodec.EncodeRequest(), 0)));
				byte[] versionRequest = WrapKcpMessage(TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.ServerVersionRequest, 0, ServerVersionCodec.EncodeRequest(), 0)));
				kcp.Send(infoRequest, 0, infoRequest.Length);
				kcp.Send(versionRequest, 0, versionRequest.Length);
				kcp.Flush();
				UnityEngine.Debug.Log("[SFS-MP][probe] sent info=" + infoRequest.Length + " version=" + versionRequest.Length);

				var deadline = DateTime.UtcNow + TimeSpan.FromSeconds(3);
				DateTime countsArrivedAt = DateTime.MinValue;
				while (DateTime.UtcNow < deadline)
				{
					// 鏃ф湇鍔＄涓嶈 ServerVersionRequest锛氫汉鏁板埌鎵嬪悗鍐嶇瓑 700ms 灏辨敹宸ワ紝鍒瘡娆￠兘鎷栨弧 3 绉掋€?
					if (status.Reachable && !status.VersionKnown && DateTime.UtcNow > countsArrivedAt + TimeSpan.FromMilliseconds(700)) break;

					kcp.Update((uint)Environment.TickCount);
					Task<UdpReceiveResult> receive = udp.ReceiveAsync();
					Task completed = await Task.WhenAny(receive, Task.Delay(20, cancellation.Token)).ConfigureAwait(false);
					if (completed == receive)
					{
						UdpReceiveResult datagram = await receive.ConfigureAwait(false);
						UnityEngine.Debug.Log("[SFS-MP][probe] udp rx=" + datagram.Buffer.Length);
						kcp.Input(datagram.Buffer, 0, datagram.Buffer.Length);
						byte[] buffer = new byte[4096];
						int length;
						while ((length = kcp.Recv(buffer, 0, buffer.Length)) > 0)
						{
							UnityEngine.Debug.Log("[SFS-MP][probe] kcp rx=" + length);
							if (length < 8) throw new InvalidDataException("probe response too short");
							int outer = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16) | (buffer[3] << 24);
							int bodyLength = buffer[4] | (buffer[5] << 8) | (buffer[6] << 16) | (buffer[7] << 24);
							if (outer + 4 != length || bodyLength < 9 || bodyLength + 4 != outer)
								throw new InvalidDataException("invalid KCP probe framing");
							byte[] frameBody = new byte[bodyLength];
							Buffer.BlockCopy(buffer, 8, frameBody, 0, bodyLength);
							TcpFrame frame = TcpFrameCodec.DecodeBody(frameBody);
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
		catch (System.Exception ex)
		{
			UnityEngine.Debug.LogWarning("[SFS-MP][probe] " + ex.GetType().Name + ": " + ex.Message);
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
			// 瀵嗙爜鍚屾牱鐩存帴鍙栬緭鍏ユ褰撳墠鍊硷紝涓嶄緷璧?onChange 鍥炶皟锛堝洖璋冨彲鑳借惤鍚庝竴娆¤緭鍏ワ級
			if (input_password != null && input_password.gameObject != null)
			{
				input_password.FieldColor = defaultTextInputColor;
				joinInfo.password = input_password.Text ?? string.Empty;
			}

			// 鐐?Join 灏卞厛钀界洏锛屼笉绛夋牎楠屽拰杩炴帴缁撴灉锛氬湴鍧€鑳借В鏋愬氨瀛樺湴鍧€锛岀敤鎴峰悕闈炵┖灏卞瓨鐢ㄦ埛鍚嶃€?
			// 浠ュ墠鏄?鍦板潃+鐢ㄦ埛鍚嶉兘杩囧叧"鎵嶅瓨 鈥斺€?鐢ㄦ埛鍚嶇┖鐫€鏃舵柊濉殑鍦板潃浼氫涪锛屼笅娆¤繘鑿滃崟鐪嬪埌鐨勬槸鏃у湴鍧€銆?
			// 鐢ㄦ埛鍚嶅悓鏍风洿鎺ュ彇杈撳叆妗嗗綋鍓嶅€硷紝涓嶄緷璧栧洖璋?鈥斺€?鍚﹀垯鍙兘鎷夸笂涓€娆＄殑鏃у悕瀛楀幓杩?
			string usernameText = (input_username.Text ?? string.Empty).Trim();
			if (usernameText.Length > 0) joinInfo.username = usernameText;

			bool endpointOk = await TryParseEndpoint(input_endpoint.Text, joinInfo);
			if (endpointOk)
			{
				// 瀛樺師鏂囷細鐢ㄦ埛鐪佺暐绔彛鏃惰繛鎺ユ寜 9806 绠楋紝浣嗕笅娆℃墦寮€妗嗛噷杩樻槸浠栨墦鐨勯偅鏍?
				PlayerPrefs.SetString(LastServerKey, input_endpoint.Text.Trim());
				if (usernameText.Length > 0) PlayerPrefs.SetString(LastUsernameKey, usernameText);
				PlayerPrefs.Save();
			}

			if (string.IsNullOrWhiteSpace(input_endpoint.Text))
			{
				// 绌虹潃灏辩┖鐫€锛屼笉鏍囩孩锛堝彲鑳芥槸杩樻病濉級锛屽彧鐣欎竴鍙ユ彁绀?
				MsgDrawer.main?.Log("Enter the server IP address.");
			}
			else if (!endpointOk)
			{
				input_endpoint.FieldColor = Color.red;
				MsgDrawer.main?.Log("Server must use IP:Port");
			}
			else if (string.IsNullOrWhiteSpace(input_username.Text))
			{
				input_username.FieldColor = Color.red;
				MsgDrawer.main?.Log("Username cannot be empty");
			}
			else
			{
				MsgDrawer.main?.Log("Attempting to connect...");
				await ClientManager.TryConnect(joinInfo);
			}
		}
		catch (Exception ex)
		{
			string message = string.IsNullOrWhiteSpace(ex.Message) ? "Connection failed." : ex.Message;
			MsgDrawer.main?.Log(message);
			Debug.LogError(ex);
		}
	}

	// PC 绔緭鍏ュ彧鏀寔 ASCII锛氶潪鑻辨枃浼氬湪娓告垙瀛椾綋閲屾樉绀烘垚鏂规銆?
	// 浠?PC 绔湁杩欎釜闄愬埗锛汚ndroid 娌℃湁杩欏杈撳叆妗嗭紝涔熶笉鎷︽埅鐜╁鍚?瀵嗙爜銆?
	private static bool IsAsciiOnly(string input)
	{
		if (string.IsNullOrEmpty(input)) return true;
		foreach (char c in input)
			if (c < 0x20 || c > 0x7E) return false;
		return true;
	}

	private static bool IsEndpointSyntaxValid(string input)
	{
		string text = input == null ? string.Empty : input.Trim();
		if (text.Length == 0) return false;
		foreach (char c in text) if (char.IsWhiteSpace(c)) return false;
		if (text[0] == '[')
		{
			int close = text.IndexOf(']');
			if (close < 2) return false;
			string ipv6 = text.Substring(1, close - 1);
			if (!IPAddress.TryParse(ipv6, out _)) return false;
			string rest = text.Substring(close + 1);
			if (rest.Length == 0) return true;
			return rest[0] == ':' && IsValidPort(rest.Substring(1));
		}
		if (IPAddress.TryParse(text, out _)) return true;
		string host = text;
		int separator = text.LastIndexOf(':');
		if (separator >= 0)
		{
			if (separator == 0 || separator == text.Length - 1) return false;
			if (!IsValidPort(text.Substring(separator + 1))) return false;
			host = text.Substring(0, separator);
		}
		if (host.Length == 0) return false;
		foreach (char c in host)
			if (!(char.IsLetterOrDigit(c) || c == '.' || c == '-' || c == '_')) return false;
		return true;
	}

	private static bool IsValidPort(string text)
	{
		return int.TryParse(text, out int port) && port >= 1 && port <= 65535;
	}

	private static async Task<bool> TryParseEndpoint(string input, JoinInfo info)
	{
		string text = input == null ? string.Empty : input.Trim();
		if (text.Length == 0) return false;

		// 杩炴帴涓茬殑涓夌鍐欐硶閮借璁わ紙IPv6 蹇呴』鍗曠嫭澶勭悊锛岃涓嬶級锛?
		//   1.2.3.4:9806 / example.com:9806   鍩熷悕鎴?IPv4 + 绔彛
		//   1.2.3.4 / example.com             鐪佺暐绔彛 -> 榛樿绔彛 + SRV 鏌ヨ
		//   [240e:3bd::6173]:9806             甯︽柟鎷彿鐨?IPv6 + 绔彛锛堝敮涓€鏃犳涔夌殑鍐欐硶锛?
		//   240e:3bd::6173                    瑁?IPv6 瀛楅潰閲忥紙榛樿绔彛锛?
		//
		// 銆愪负浠€涔堝繀椤荤壒鍒ゃ€慖Pv6 瀛楅潰閲忛噷鍏ㄦ槸鍐掑彿銆傛寜"鏈€鍚庝竴涓啋鍙?鍒囩鍙ｆ椂锛?
		// `240e:3bd:231:...:6173` 浼氳鍒囨垚涓绘満 `240e:3bd:231:...:5d0c` + 绔彛 `6173`
		// 鈥斺€?绔彛鎭板ソ鍚堟硶锛屼簬鏄笉鎶ラ敊锛屽彧鏄湴鍧€琚垏鍧忥紝鏈€鍚庤〃鐜颁负"鍦板潃鏃犳晥"锛屽緢闅炬煡銆?
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
			// 鏁翠覆灏辨槸涓€涓湴鍧€锛圛Pv4 鎴栬８ IPv6锛夛細涓嶅仛绔彛鍒囧垎銆?
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
		// 娌″啓绔彛鎵嶅幓闂?SRV锛氬煙鍚嶈儗鍚庣殑鍏綉绔彛鏈繀绛変簬鏈嶅姟绔洃鍚殑 9806
		// 锛堝唴缃戠┛閫忓彧缁?鈮?0000 鐨勭鍙ｏ級銆傛樉寮忕鍙ｆ案杩滀紭鍏堬紝涓嶈蛋杩欐潯璺€?
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

			// 鍩熷悕鍙兘鍚屾椂鏈?A 鍜?AAAA锛岃€?GetHostAddresses 鐨勯『搴忔病鏈変繚璇併€?
			// 鏈閮ㄧ讲鏄?鍩熷悕鍙寕 AAAA 鎸囧悜鐩爣璁惧"锛屾墍浠ユ湁鍙敤鐨?IPv6 鏃朵紭鍏堣蛋 IPv6锛?
			// 浣嗘湰鏈烘病鏈?IPv6 鍑哄彛鏃朵紭鍏堝畠鍙細鐧界瓑涓€涓秴鏃讹紝鎵€浠ラ偅鏃堕€€鍥?IPv4銆?
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

	// 鏈満鏄惁鏈?鐪熻兘鐢?鐨?IPv6锛氬瓨鍦ㄤ竴寮犲凡鍚敤銆侀潪鍥炵幆/闈為毀閬撶綉鍗′笂鐨?
	// 闈?link-local銆侀潪 site-local 鐨?IPv6 鍗曟挱鍦板潃銆?
	// 鐢ㄥ畠鏄负浜嗛伩鍏?鍩熷悕鏈?AAAA 浣嗘湰鏈烘牴鏈病 IPv6"鏃剁櫧绛変竴鏁翠釜杩炴帴瓒呮椂銆?
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
			// 鏌ョ綉鍗″け璐ヤ笉璇ラ樆鏂繛鎺ワ細褰撲綔娌℃湁 IPv6锛岄€€鍥?IPv4銆?
		}
		return false;
	}
}

