// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using MultiplayerSFS.Common;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public sealed class NetworkDebugOverlay : MonoBehaviour
{
	private static NetworkDebugOverlay instance;
	private bool visible;
	private Rect windowRect = new Rect(20, 20, 480, 560);
	private Vector2 scrollPosition;
	private GUIStyle labelStyle;
	private GUIStyle titleStyle;
	// 实验性功能口令：**不再预填默认值**。
	// 以前两端都默认 "abcde"，等于任何玩家都能解锁远程命令通道（含 stop 关服）。
	// 【2026-10-06】原来的口令通道已随"实验性访问"机制一并砍掉（见 docs 执行记录）。
	// （原 experimentalPassphrase 已随"实验性访问"机制一并删除，见 docs 执行记录）
	// 服务端命令输入框内容
	// （原 serverCommandInput 已随命令入口一并撤出 F8：命令以后走游戏原生的 F1 控制台。）

	public static void Create()
	{
		if (instance != null) return;
		verbosePerfLog = PlayerPrefs.GetInt(VerbosePerfLogKey, 0) != 0;
		GameObject holder = new GameObject("SFS Multiplayer TCP Network Pump");
		DontDestroyOnLoad(holder);
		instance = holder.AddComponent<NetworkDebugOverlay>();
	}

	// ---- 性能快照：打开后每秒往日志写一行，用来判断卡顿来自我们的代码、GC 还是网络 ----
	// 默认关闭：发行包常驻每秒写一行会持续吃 Player.log（V1.2.4 已知问题之一）。
	// 真正的故障日志（Debug.LogError / LogWarning）不受这个开关影响。
	private const string VerbosePerfLogKey = "multiplayersfs.verbose.perflog";
	private static bool verbosePerfLog;
	private readonly System.Diagnostics.Stopwatch perfWatch = new System.Diagnostics.Stopwatch();
	private float perfWindowStart;
	private float perfMaxFrameMs;
	private float perfOurMsTotal;
	private int perfFrames;
	private int perfGc0Last;
	private int perfGc1Last;
	private int perfGc2Last;
	private long perfRecvLast;
	private long perfSentLast;

	// 默认安静，但真卡了必须留证据：单帧超过这个毫秒数就每秒打一行（最多每秒一行）。
	// 为什么要有这条：以前 perf 日志要么每秒刷屏、要么被完全关掉，结果"突然卡顿"发生时
	// 日志里什么都没有，只能猜。现在默认只记卡顿。
	private const float HitchLogThresholdMs = 100f;

	private void Update()
	{
		if (Input.GetKeyDown(KeyCode.F8)) visible = !visible;

		// 帧耗时统计几乎不要钱（一次比较 + 一次自增），永远开着。
		float frameMs = Time.unscaledDeltaTime * 1000f;
		if (frameMs > perfMaxFrameMs) perfMaxFrameMs = frameMs;
		perfFrames++;
		if (verbosePerfLog) perfWatch.Restart();

		ClientManager.EnforceRunningTimeScale();   // 防联机下被暂停冻结（Time.timeScale=0）
		ClientManager.UpdateNetwork();
		P2PConnectionManager.Update();
		LocalManager.Update();

		if (verbosePerfLog) perfOurMsTotal += (float)perfWatch.Elapsed.TotalMilliseconds;
		UpdatePerformanceWindow();
	}

	// 每秒结算一次：verbosePerfLog 打开 -> 每秒一行 [perf]；关着 -> 只在真卡了时打一行 [hitch]。
	private void UpdatePerformanceWindow()
	{
		if (perfWindowStart <= 0f)
		{
			perfWindowStart = Time.realtimeSinceStartup;
			perfGc0Last = GC.CollectionCount(0);
			perfGc1Last = GC.CollectionCount(1);
			perfGc2Last = GC.CollectionCount(2);
			perfRecvLast = ClientManager.client?.ReceivedFrames ?? 0;
			perfSentLast = ClientManager.client?.SentFrames ?? 0;
			return;
		}
		if (Time.realtimeSinceStartup - perfWindowStart < 1f) return;

		// 前缀区分"按开关逐秒记录"和"抓到一次卡顿"。
		bool report = verbosePerfLog || perfMaxFrameMs >= HitchLogThresholdMs;
		if (report) UnityEngine.Debug.Log((verbosePerfLog ? "[SFS-MP][perf] " : "[SFS-MP][hitch] ") + string.Format(
			// ourMsAvg 只在"每秒性能日志"打开时才真正测量（见 Update 里的 perfWatch 门控）。
			// 以前这里无论开没开都打 0.00，看起来像"模组零开销"—— 那是假证据。没测就打 n/a。
			"fps={0} maxFrameMs={1:F1} ourMsAvg={2} gc0=+{3} gc1=+{4} gc2=+{5} recv=+{6} sent=+{7} auth={12} rockets={8} pending={9} unityTimeScale={10:F3} WorldTime.main={11}",
			perfFrames,
			perfMaxFrameMs,
			// gc0=+N 才是弱机的关键指标：它跟机器多强无关，稳定为 0 就说明模组不制造 GC 尖峰。
			verbosePerfLog ? (perfFrames > 0 ? (perfOurMsTotal / perfFrames).ToString("F2") : "0.00") : "n/a",
			GC.CollectionCount(0) - perfGc0Last,
			GC.CollectionCount(1) - perfGc1Last,
			GC.CollectionCount(2) - perfGc2Last,
			(ClientManager.client?.ReceivedFrames ?? 0) - perfRecvLast,
			(ClientManager.client?.SentFrames ?? 0) - perfSentLast,
			LocalManager.syncedRockets == null ? 0 : LocalManager.syncedRockets.Count,
			LocalManager.PendingCreateCount,
			Time.timeScale,
			// 曾经这里打的是 gameTimeScale=-1，被误读成"时间倍率变成 -1"；那个 -1 其实是
			// WorldTime.main 取不到时的哨兵。现在直接说是 NULL 还是具体值，不再有歧义。
			WorldTime.main == null ? "NULL" : WorldTime.main.TimeScale.ToString("F3"),
			// auth= 客户端当前认为"自己有权更新"的火箭数：为 0 就说明服务端下发的权威列表
			// 没被客户端接受，发送循环会一枚都不发（日志表现 sent=+0，两边位置对不上）。
			LocalManager.updateAuthority == null ? -1 : LocalManager.updateAuthority.Count));

		perfFrames = 0;
		perfMaxFrameMs = 0f;
		perfOurMsTotal = 0f;
		perfGc0Last = GC.CollectionCount(0);
		perfGc1Last = GC.CollectionCount(1);
		perfGc2Last = GC.CollectionCount(2);
		perfRecvLast = ClientManager.client?.ReceivedFrames ?? 0;
		perfSentLast = ClientManager.client?.SentFrames ?? 0;
		perfWindowStart = Time.realtimeSinceStartup;
	}

	private void OnGUI()
	{
		// 【只保留这一个调试弹窗】TAB 玩家列表与常驻 HUD 已移除（见 docs 执行记录）：
		// 常驻 UI 在弱机上是每帧分配 + IMGUI 绘制的固定代价，而它只在调试时才有用。
		if (!visible) return;
		windowRect = GUI.Window(864217, windowRect, DrawWindow, "SFS Multiplayer V" + Main.ModVersionInfo.Version + " - Network Debug");
	}

	private void DrawWindow(int id)
	{
		if (labelStyle == null)
		{
			labelStyle = new GUIStyle(GUI.skin.label) { fontSize = 14, wordWrap = true };
			titleStyle = new GUIStyle(labelStyle) { fontSize = 16, fontStyle = FontStyle.Bold };
		}

		GUILayout.Label("F8 显示/隐藏", titleStyle);
		GUILayout.BeginHorizontal();
		bool newVerbosePerfLog = GUILayout.Toggle(verbosePerfLog, " 每秒性能日志（默认关）", GUILayout.Height(28));
		if (newVerbosePerfLog != verbosePerfLog)
		{
			verbosePerfLog = newVerbosePerfLog;
			PlayerPrefs.SetInt(VerbosePerfLogKey, verbosePerfLog ? 1 : 0);
			PlayerPrefs.Save();
		}
		GUILayout.EndHorizontal();

		// 播放延迟档位：偏移 = 速度 × 延迟，这是"两边看起来差多远"的唯一旋钮。
		GUILayout.BeginHorizontal();
		// 超级调试：协议层追踪（每秒一行聚合；过滤器非空时只输出命中的逐包明细）
		GUILayout.BeginHorizontal();
		bool newTrace = GUILayout.Toggle(DebugTrace.Enabled, " 超级调试（协议层追踪）", GUILayout.Height(28));
		if (newTrace != DebugTrace.Enabled) { DebugTrace.Enabled = newTrace; PlayerPrefs.Save(); }
		GUILayout.EndHorizontal();
		GUILayout.BeginHorizontal();
		GUILayout.Label("过滤器：", labelStyle, GUILayout.Width(60));
		string newFilter = GUILayout.TextField(DebugTrace.Filter, GUILayout.Height(24));
		if (newFilter != DebugTrace.Filter) { DebugTrace.Filter = newFilter; PlayerPrefs.Save(); }
		GUILayout.EndHorizontal();
		GUILayout.Label("  " + DebugTrace.LastSummary, labelStyle);
		if (GUILayout.Button("播放延迟：" + Interpolator.PlayoutMode + "（点击切换 对齐0ms / 平衡1× / 平滑1.5×）", GUILayout.Height(28)))
		{
			Interpolator.PlayoutMode = Interpolator.NextPlayoutMode(Interpolator.PlayoutMode);
			PlayerPrefs.Save();
		}
		GUILayout.EndHorizontal();

		scrollPosition = GUILayout.BeginScrollView(scrollPosition, GUILayout.ExpandHeight(true));
		if (ClientManager.client == null)
		{
			GUILayout.Label("状态：尚未创建 KCP 连接", labelStyle);
				GUILayout.EndScrollView();
			GUI.DragWindow();
			return;
		}

		TcpClientTransport transport = ClientManager.client;
		string server = string.IsNullOrEmpty(transport.RemoteAddress) ? "-" : transport.RemoteAddress;
		string rtt = transport.RoundTripMs <= 0 ? "-" : transport.RoundTripMs.ToString("F2") + "ms";
		string jitter = transport.JitterMs <= 0 ? "-" : transport.JitterMs.ToString("F2") + "ms";
		string lastReceive = FormatAge(transport.SecondsSinceReceive);
		string disconnect = string.IsNullOrEmpty(transport.LastDisconnectReason) ? "-" : transport.LastDisconnectReason;

		GUILayout.Label("KCP 单会话 / V" + Main.ModVersionInfo.Version, labelStyle);
		GUILayout.Label("状态：" + (transport.Connected ? "已连接" : "已断开"), labelStyle);
		GUILayout.Label("服务器：" + server, labelStyle);
		GUILayout.Label("RTT：" + rtt + "    抖动：" + jitter + "（最近 5 次采样均值）", labelStyle);
		GUILayout.Label("最后收包：" + lastReceive + "    发送队列：" + transport.QueueCount, labelStyle);
		GUILayout.Label("上行：" + transport.SentBytes + " bytes/" + transport.SentFrames + " frames", labelStyle);
		GUILayout.Label("下行：" + transport.ReceivedBytes + " bytes/" + transport.ReceivedFrames + " frames", labelStyle);
		GUILayout.Label("最后业务包：" + transport.LastPacketType, labelStyle);
		GUILayout.Label("最后断线原因：" + disconnect, labelStyle);
		GUILayout.Label("P2P：" + P2PConnectionManager.Status + "    直连玩家：" + P2PConnectionManager.ActivePeerCount, labelStyle);
		// 【诊断分细】每个 peer 走到哪一步、为什么退化成中继、包分类计数（以前只有上面那一句）。
		GUILayout.Label(P2PConnectionManager.DescribePeers(), labelStyle);
		GUILayout.Label(string.Format("火箭：{0}    权威：{1}", ClientManager.world == null ? 0 : ClientManager.world.rockets.Count, LocalManager.updateAuthority == null ? 0 : LocalManager.updateAuthority.Count), labelStyle);

		GUILayout.BeginHorizontal();
		if (GUILayout.Button("清空统计", GUILayout.Height(34))) transport.ClearStatistics();
		if (GUILayout.Button("重同步世界（强制整枚重建）", GUILayout.Height(34)))
		{
			// 【2026-10-10】以前这里只发快照请求：快照到了只刷位姿、零件数据被丢掉，结构拉不回来。
			// 现在同时打开"强制重建窗口"，让随后到达的每枚远端火箭都按权威状态整枚重建一次。
			LocalManager.ForceFullRebuild();
			transport.RequestWorldSnapshot();
		}
		GUILayout.Label("    强制重建：" + LocalManager.ForcedRebuildStatus, labelStyle);
		GUILayout.EndHorizontal();
		GUILayout.BeginHorizontal();
		if (GUILayout.Button("复制诊断", GUILayout.Height(34)))
		{
			GUIUtility.systemCopyBuffer = BuildDiagnostics();
		}
		if (GUILayout.Button("关闭", GUILayout.Height(34))) visible = false;
		GUILayout.EndHorizontal();

		// 日志上传：把 Player.log 尾部压缩后发给服务端（服务端存到自己的 logs/ 目录）
		GUILayout.BeginHorizontal();
		if (GUILayout.Button("上传日志", GUILayout.Height(34)))
		{
			ClientManager.UploadLog("debug overlay");
		}
		GUILayout.Label("状态：" + ClientManager.LastLogUploadStatus, labelStyle);
		GUILayout.EndHorizontal();


		GUILayout.EndScrollView();
		GUI.DragWindow();
	}

	private static string BuildDiagnostics()
	{
		TcpClientTransport transport = ClientManager.client;
		if (transport == null) return "SFS Multiplayer V1.2.2.21\nConnected=False\nServer=-";
		string server = string.IsNullOrEmpty(transport.RemoteAddress) ? "-" : transport.RemoteAddress;
		string rtt = transport.RoundTripMs <= 0 ? "-" : transport.RoundTripMs.ToString("F2") + "ms";
		string jitter = transport.JitterMs <= 0 ? "-" : transport.JitterMs.ToString("F2") + "ms";
		string disconnect = string.IsNullOrEmpty(transport.LastDisconnectReason) ? "-" : transport.LastDisconnectReason;
		return string.Join("\n", new[]
		{
			"SFS Multiplayer V" + Main.ModVersionInfo.Version,
			"Connected=" + transport.Connected,
			"Server=" + server,
			"RTT=" + rtt,
			"Jitter=" + jitter,
			"LastReceive=" + FormatAge(transport.SecondsSinceReceive),
			"Queue=" + transport.QueueCount,
			"Sent=" + transport.SentBytes + " bytes/" + transport.SentFrames + " frames",
			"Received=" + transport.ReceivedBytes + " bytes/" + transport.ReceivedFrames + " frames",
			"LastPacket=" + transport.LastPacketType,
			"Disconnect=" + disconnect,
			"KcpPresetRelay=" + MultiplayerSFS.Common.KcpTransportPresets.Relay,
			"KcpPresetP2P=" + MultiplayerSFS.Common.KcpTransportPresets.P2P,
			"P2P=" + P2PConnectionManager.DescribePeers()
		});
	}

	private static string FormatAge(double seconds)
	{
		return double.IsInfinity(seconds) ? "-" : seconds.ToString("F1") + "s";
	}
}
