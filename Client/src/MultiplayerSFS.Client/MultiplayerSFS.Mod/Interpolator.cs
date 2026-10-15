// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Collections.Generic;
using MultiplayerSFS.Common;
using SFS.Parts;
using SFS.Parts.Modules;
using SFS.UI;
using SFS.World;
using UnityEngine;

namespace MultiplayerSFS.Mod;

public class Interpolator : MonoBehaviour
{
	public enum InterpolationMode
	{
		Hermite,
		Linear,
		Spherical
	}

	public const int MaxBuffer = 10;

	public const int MinBufferForInterpolation = 2;

	public LocalRocket rocket;

	public Packet_UpdateRocketPrimary currentUpdate;

	public bool isNewlyCreated;

	// 数据断流后是否已交回本地物理引擎（见 PredictState）：为 true 时不写 transform，由游戏自己积分
	public bool freeSimulation;

	// 「托管的无主火箭疑似停止推进」探针。交回引擎之后，如果位置不再变化、但速度不为零，
	// 就说明引擎根本没在积分它（现场表现：像 MC 区块被卸载一样冻住）。
	// 这条日志就是用来把结论钉死的：bodyType / PhysicsMode / 位移 / 速度 一起打出来。
	private double hostStallLastX;
	private double hostStallLastY;
	private float hostStallNextCheck;

	// 「包的行星 ≠ 本端活体的行星」诊断，上限 20 条避免刷屏。
	// 位置是"相对某颗行星"的坐标；参照系不一致时，同一组数字会被画到另一颗星球内部，
	// 表现就是"明明在天上、地图却显示在地下"（或反过来）。
	private int planetMismatchLogs;

	// 这枚火箭最近一次收到"权威状态包"时，本端的世界时间是多少。
	// -1 表示本端从未收到过（还没同步上，或它完全归本端驱动）。
	//
	// 为什么需要它：判"这枚火箭该不该交还本地引擎"不能用 controlledRocket（本地玩家当前开哪枚），
	// 那个值在你切换控制权的一瞬间就变了，而服务端是否还在驱动这枚火箭并不会同步变。
	// 切走的那一枚因此被误判成"本端托管" → 当场设成 Dynamic 交还引擎 → 位置从权威链路上脱钩
	// → 再收到包时硬对齐 → 表现为"切走控制权的一瞬间火箭瞬移"。
	// 换成"最近有没有在收这枚火箭的包"就与网络实际状态一致，不受切换动作影响。
	private double lastAuthoritativePacketLocalTime = -1;

	// 多久没收到这枚火箭的包就算"服务端不再驱动它"。取 0.5 秒：
	// 服务端每 2 秒一次 Ping，正常发包间隔在 50~1000ms（见 RocketSyncPolicy），
	// 0.5 秒既能覆盖一次抖动，又快到能在"权威真的移走了"后及时交还引擎。
	private const double AuthorityLossSeconds = 0.5;

	// 远端火箭位置偏差超过这个距离就直接硬对齐（不走平滑），同时立刻向服务端要一次该火箭的
	// 权威快照。10 米仍远大于正常插值误差（<1 米），但能挡住"断流期间本地引擎按轨道推进"造成的
	// 偏差堆积（实测几百米），不会等它涨到几十米才处理。
	//
	// 【2026-09-22 回滚记录】曾把门限改成 max(10, v×τ×3) 想把"平滑必然产生的 v×τ 滞后"排除掉，
	// 结果：闪烁一点没好（说明闪烁不来自这里），反而让真实漂移没人管，
	// 把原本只在 P2P 出现的"双方位置不一致"复现到了中继路径上。别再照那个思路改。
	private const double GrossPositionErrorMeters = 10.0;
	private float nextDriftCorrectionLogTime;
	// 【临时诊断】每枚远端火箭每 0.5 秒打一行同步状态，用来定位"高速时一闪一闪"到底来自哪一层。
	// 查清后删掉。文案必须英文：游戏内控制台没有中文字形，中文会渲染成方框。
	private const double SyncProbeIntervalSeconds = 0.5;

	// ================== EXP 2026-10-09：远端渲染的"相对参照系"平滑 ==================
	//
	// 背景：平滑直接作用在行星相对位置上时，追一个高速目标会**恒定滞后 v×τ**
	//（InterpolationRenderPolicy 文档原话："恒速稳态滞后 ≈ 速度 × 平滑常数"）。
	// 近地轨道 2000 m/s、τ≈6.6ms → 约 13 米 —— 就是玩家说的"有点飘"。
	//
	// 做法：把平滑放到"以本端玩家自己那枚火箭为原点"的相对坐标里做（见 SetState）。
	// 被平滑的量从"绝对位置"变成"相对位移"，其变化率是**相对速度**（同轨道约 2 m/s），
	// 滞后随之降到厘米级。参照点本身是本地模拟的，精确、零延迟。
	//
	// 不改变校正能力：权威包照旧设目标，只是平滑换了个参照系 —— "能被校验拉回来"这条不变。
	// 默认开；F8 面板可切，写 PlayerPrefs 持久化，方便 A/B 对比。
	public const string RelativeFramePrefsKey = "multiplayersfs.exp.relativeframe";
	private static bool relativeFrameLoaded;
	private static bool relativeFrameSmoothing = true;

	public static bool RelativeFrameSmoothing
	{
		get
		{
			if (!relativeFrameLoaded)
			{
				relativeFrameLoaded = true;
				relativeFrameSmoothing = PlayerPrefs.GetInt(RelativeFramePrefsKey, 1) != 0;
			}
			return relativeFrameSmoothing;
		}
		set
		{
			relativeFrameLoaded = true;
			relativeFrameSmoothing = value;
			PlayerPrefs.SetInt(RelativeFramePrefsKey, value ? 1 : 0);
		}
	}
	// ——— 轨迹重建（MC 式：固定延迟 + 定点重建，绝不外推）V1.3.3.5-exp3 ———
	// 依据：Minecraft 走 TCP、延迟比我们高几十倍却不抖。拆开看它的机制是：
	//   服务端按"每个实体各自的变间隔"发包（ServerEntity.updateInterval），
	//   客户端只做定点插值，永不外推（Entity.lerpPositionAndRotationStep：
	//   d = 1.0 / steps; setPos(Mth.lerp(d, getX(), 目标X), ...)），
	//   包停了就渐近停在目标上 —— 从不猜、从不瞬移。
	// 我们反过来：每收到一个包就硬写位置+速度，包间再叠一层 Unity Extrapolate，
	// 于是"每个包的到达"都变成一次可见修正（速度越快越明显，与 RTT 无关）。
	// 本开关把渲染换成"固定延迟 + Hermite 重建"：延迟吸收到达抖动，缓冲不够就停住等包。
	// 关掉即完全回到原来的 re-seed 外推路径，方便当场 A/B。
	public const string TrajectoryPrefsKey = "multiplayersfs.exp.trajrecon";
	private static bool trajectoryLoaded;
	private static bool trajectoryReconstruction = true;

	public static bool TrajectoryReconstruction
	{
		get
		{
			if (!trajectoryLoaded)
			{
				trajectoryLoaded = true;
				trajectoryReconstruction = PlayerPrefs.GetInt(TrajectoryPrefsKey, 1) != 0;
			}
			return trajectoryReconstruction;
		}
		set
		{
			trajectoryLoaded = true;
			trajectoryReconstruction = value;
			PlayerPrefs.SetInt(TrajectoryPrefsKey, value ? 1 : 0);
		}
	}

	// 轨迹重建状态：历史包（按 WorldTime 升序）+ 上一次写进去的值（用来省掉无意义的写）
	private readonly List<Packet_UpdateRocketPrimary> trajectoryHistory = new List<Packet_UpdateRocketPrimary>();
	private const int TrajectoryHistoryMax = 16;
	private const double MinPlayoutDelaySeconds = 0.0;
	private const double MaxPlayoutDelaySeconds = 0.20;
	private const double MaxTrajectoryExtrapolationSeconds = 0.10;

	// 播放延迟档位。偏移 = 速度 × 延迟，所以这是"两边看起来差多远"的唯一旋钮：
	//   align    = 0ms        两边看的是同一时刻（偏移只剩 RTT/2）。曲线仍由 Hermite 重建，
	//                         包与包之间走切线外推（位移小、时间短，误差厘米级），不再硬写。
	//   balanced = 1 × 包间隔 留半个包的插值窗口，抖动稍大的链路更稳。
	//   smooth   = 1.5 × 包间隔 = exp3 的原始值，最抗抖动，但高速时偏移最大。
	public const string PlayoutPrefsKey = "multiplayersfs.exp.playout";
	public const string PlayoutAlign = "align";
	public const string PlayoutBalanced = "balanced";
	public const string PlayoutSmooth = "smooth";
	private static string playoutMode;
	private static bool playoutLoaded;

	public static string PlayoutMode
	{
		get
		{
			if (!playoutLoaded)
			{
				playoutLoaded = true;
				// 默认 align（0ms，位置完全同步）。实测结论（2026-10-09 实机）：
				//   exp3 smooth = 不抖但慢一个延迟（2000m/s 时约 100m）
				//   exp6 align  = 完全同步但高速抖（一阶切线丢曲率）
				// 现在外推是二次的（用最近两包速度差估加速度），所以 align 才敢当默认。
				// 若某些火箭（推力/大气变化剧烈）仍抖，按 F8 切 smooth 即可回到 exp3 手感。
				playoutMode = PlayerPrefs.GetString(PlayoutPrefsKey, PlayoutAlign);
				if (playoutMode != PlayoutAlign && playoutMode != PlayoutBalanced && playoutMode != PlayoutSmooth)
					playoutMode = PlayoutAlign;
			}
			return playoutMode;
		}
		set
		{
			playoutLoaded = true;
			playoutMode = value;
			PlayerPrefs.SetString(PlayoutPrefsKey, value);
		}
	}

	public static string NextPlayoutMode(string current)
	{
		if (current == PlayoutAlign) return PlayoutBalanced;
		if (current == PlayoutBalanced) return PlayoutSmooth;
		return PlayoutAlign;
	}

	private static double PlayoutDelayFactor
	{
		get
		{
			string mode = PlayoutMode;
			if (mode == PlayoutAlign) return 0.0;
			if (mode == PlayoutBalanced) return 1.0;
			return 1.5;
		}
	}
	private double writtenX = double.NaN;
	private double writtenY = double.NaN;
	private float writtenRot = float.NaN;

	// ——— 预测误差"软吸收"（exp6 基线 + 更好的平滑）———
	// 包落地时：把"我们预测它会落的位置"与"它真实落点"之间的差记成反向修正量，
	// 再用 ~45ms 指数衰减滑着抹平 —— 把"啪一下瞬间补回来"（那个抖）变成"滑过去补回来"。
	// 与旧平滑的本质区别：修正量**稳态归零**，所以不会引入 v × τ 的恒定滞后（"飘"的病根）。
	// 只在接缝后几十毫秒存在；超过 5 米说明不是预测误差而是真跳变（换星球/传送/权威切换），那就不平滑、直接硬对齐。
	private double correctionX;
	private double correctionY;
	private const double CorrectionTauSeconds = 0.045;
	private const double CorrectionSnapMeters = 5.0;
	private double nextSyncProbeTime;

	// ——— 预测式同步（re-seed 形态，V1.2.3.20）———
	// 远端火箭不再"每帧被钉在滞后的插值曲线上"：每收到一个权威包就把 (位置,速度,角度,角速度)
	// 整份写回（走游戏物理 API），包间隔（20~50ms）内由 Unity 外推补足。
	// 相比旧插值路径，渲染延迟从 0.12~0.48s 降到 0.06s → "飘"（600m/s 时 70~290m）大幅减小。
	// 回退开关：改成 false 即完全回到插值路径。
	private const bool PredictiveReconcile = true;
	// 渲染延迟：吸收"包到达抖动"。这是抖动缓冲区（jitter buffer）的经典取值——**半个发包间隔**：
	// 太贴近"当下"（延迟小）→ 网络一抖就露；太靠后（延迟大）→ 又回到"飘"。
	// 所以不再写死：按实测包间隔自适应（见 RenderDelaySeconds），只钳上下限。
	private const double MinRenderDelaySeconds = 0.05;
	private const double MaxRenderDelaySeconds = 0.25;
	// 实测包间隔（指数平滑），初值取常见发包间隔 200ms
	private double smoothedPacketIntervalSeconds = 0.2;
	private double lastPrimaryPacketWorldTime;
	private double RenderDelaySeconds =>
		Math.Max(MinRenderDelaySeconds, Math.Min(MaxRenderDelaySeconds, smoothedPacketIntervalSeconds * 0.5));
	// 超过它就认定本地与权威端已脱节（长断流），记一行日志（写回本身照做）
	private const double HardResyncMeters = 200.0;

	public List<Packet_UpdateRocketPrimary> updateBuffer = new List<Packet_UpdateRocketPrimary>();

	public List<(double, Packet)> packetBuffer = new List<(double, Packet)>();

	public InterpolationMode interpolationMode;

	private double lastSnapshotRequestWorldTime = double.NegativeInfinity;
	private bool authorityStateKnown;
	private bool wasAuthority;

	// 客户端未连接时的兜底档位：NetworkAdaptationPolicy.Evaluate 每次调用都会 new 一个
	// NetworkAdaptiveProfile 对象，而 AdaptiveProfile 在插值路径上是【每帧 × 每枚远端火箭】被读取
	// （SetState / DelayedWorldTime / IsPacketDue），每次都新建 + 三次锁 —— 纯垃圾。
	// 兜底档位是常量，缓存一份即可。
	private static readonly NetworkAdaptiveProfile fallbackAdaptiveProfile = NetworkAdaptationPolicy.Evaluate(0, 0, 0);

	public static NetworkAdaptiveProfile AdaptiveProfile => ClientManager.client?.AdaptiveProfile ?? fallbackAdaptiveProfile;

	public static double TimeDelay => AdaptiveProfile.InterpolationDelaySeconds;

	public static double DelayedWorldTime => (ClientManager.world != null ? ClientManager.world.WorldTime : 0.0) - TimeDelay;

	public static bool IsPacketDue(double packetTime, double delayedWorldTime)
	{
		return packetTime <= delayedWorldTime;
	}

	public static double GetInterpolationFraction(double previousTime, double nextTime, double targetTime)
	{
		double duration = nextTime - previousTime;
		if (duration <= 0.0)
		{
			return targetTime >= nextTime ? 1.0 : 0.0;
		}

		double value = (targetTime - previousTime) / duration;
		return Math.Max(0.0, Math.Min(1.0, value));
	}

	public static void AddPacketToQueue(Packet packet, int rocketId, double worldTime)
	{
		if (!LocalManager.syncedRockets.TryGetValue(rocketId, out var value))
		{
			return;
		}
		Interpolator interpolator = value.interpolator;
		if ((object)interpolator == null)
		{
			return;
		}
		// 【兜底 2026-10-10】原来这里判的是 interpolator.rocket == null —— 但 LocalRocket 的
		// 构造函数早就写了 interpolator.rocket = this，永远非 null ⇒ 这段是死代码 ✗。
		// 真正需要判的是 currentUpdate（Update() 的闸门）。改对之后，任何忘了 prime 的 spawn 路径
		// 都会在收到第一个状态包时自动复活，从根上封住"死插值器"这一类问题。
		if (interpolator.rocket == null)
		{
			interpolator.rocket = value;
		}
		if (interpolator.currentUpdate == null && value.rocket != null)
		{
			interpolator.currentUpdate = value.rocket.ToUpdatePacketPrimary(rocketId);
			if (interpolator.currentUpdate != null)
			{
				interpolator.currentUpdate.WorldTime = 0.0;
			}
			interpolator.isNewlyCreated = true;
			Debug.Log("[SFS-MP] revived dead interpolator on first state packet: id=" + rocketId);
		}
		if (interpolator.freeSimulation)
		{
			// 数据恢复：从"本地物理模拟"交回插值器接管（下一帧起按包插值重新对齐）
			interpolator.freeSimulation = false;
			// 诊断：记录本地模拟与恢复包之间的偏差。若以后再出现"火箭突然飞起来"，
			// 这条日志能区分是本地物理跑飞了（差值巨大），还是包本身就是异常值。
			if (packet is Packet_UpdateRocketPrimary resumed && interpolator.rocket != null
				&& interpolator.rocket.rocket != null)
			{
				Location local = interpolator.rocket.rocket.location.Value;
				double dx = local.position.x - resumed.Location.position.x;
				double dy = local.position.y - resumed.Location.position.y;
				Debug.Log($"[SFS-MP] resume: rocket {rocketId} local sim differs from the packet by {Math.Sqrt(dx * dx + dy * dy):F1}m "
						  + $"(local planet={local.planet.codeName} packet planet={resumed.Location.address})");
			}
		}
		if (packet is Packet_UpdateRocketPrimary item)
		{
			// 【结构对账 2026-10-10】权威方把"活体零件 ID 集合"的哈希带在状态包里（每秒）。
			// 不一致 = 结构分叉 -> 主动要一次权威快照（限流 5 秒/枚），由既有重建路径收敛。
			// 这让"任何一条路径漏了结构事件"都能在 1~2 秒内自愈，且只在真不一致时才重建。
			if (interpolator.rocket != null && interpolator.rocket.parts != null)
			{
				// 只跟【线格式】比：活体零件重算出来的身份两端不收敛（我之前犯的错）
				if (StructureSync.HashForWire(item.RocketId) != item.StructureHash)
				{
					StructureSync.CheckRemote(item.RocketId, item.StructureHash, null);
				}
			}
			// 打时间戳：Update() 靠它判断"这枚火箭还在不在被服务端驱动"。
			// 记本端的世界时间而不是 Time.unscaledTime，因为 Update() 的判据要和
			// ClientManager.world.WorldTime 放在同一个时间轴上比较。
			if (ClientManager.world != null)
			{
				interpolator.lastAuthoritativePacketLocalTime = ClientManager.world.WorldTime;
			}
			if (interpolator.currentUpdate != null && item.WorldTime == interpolator.currentUpdate.WorldTime) return;
			// 手写线性查找替代 updateBuffer.FindIndex(lambda)：FindIndex 每次调用都要分配
			// 一个闭包 + 委托（每个收到的包两次），而且缓冲区只有 ≤10 个元素，手写循环更快。
			int existing = -1;
			var buffer = interpolator.updateBuffer;
			for (int i = 0; i < buffer.Count; i++)
			{
				if (buffer[i].WorldTime == item.WorldTime)
				{
					existing = i;
					break;
				}
			}
			if (existing >= 0) buffer[existing] = item;
			else
			{
				int index = -1;
				for (int i = 0; i < buffer.Count; i++)
				{
					if (buffer[i].WorldTime > item.WorldTime)
					{
						index = i;
						break;
					}
				}
				if (index < 0) buffer.Add(item);
				else buffer.Insert(index, item);
			}
			while (buffer.Count > MaxBuffer) buffer.RemoveAt(0);
		}
		else
		{
			interpolator.packetBuffer.Add((worldTime, packet));
		}
	}

	public static double GetCorrectionTarget(double position, double velocity)
	{
		return position;
	}

	public static float GetCorrectionRotation(float rotation, float angularVelocity)
	{
		return rotation;
	}

	public static bool ShouldPrimeAuthorityState(bool wasAuthority, bool isAuthority)
	{
		return !wasAuthority && isAuthority;
	}

	private void Update()
	{
		if (currentUpdate == null)
		{
			return;
		}
		// 单向冻结根因：若 updateAuthority 误含对方火箭，本端会把它当本地权威（Dynamic 物理体、无输入），导致完全不动。
		// 只有本地玩家真正控制的火箭才走权威路径；否则强制非权威插值，保证对方火箭在本端动起来。
		bool isLocalPlayerControlled = LocalManager.Player != null && LocalManager.Player.controlledRocket.Value == currentUpdate.RocketId;
		bool hosted = LocalManager.updateAuthority.Contains(currentUpdate.RocketId);
		// 本端托管、但服务端已经不再驱动这枚火箭（发射台上留下的 / 分离掉的 / 切走后服务端也放手了的）：
		// 必须由本端游戏引擎推进（on-rails 轨道或物理），模组只负责把它的真实状态发出去。
		// 反例就在下面：一旦被设成 Kinematic + Extrapolate，而它又收不到任何远端包
		// （本端就是权威，没人会发它的状态过来），SetState 会一直 early-return，
		// 于是没有任何代码把它设回 Dynamic —— Unity 便按最后速度对它做匀速外推，
		// 表现为"无人操控的火箭按固定方向一直飞"；Kinematic 还会顶飞真实火箭。
		//
		// 【V1.2.5 修正】判据从"服务端还托管吗(remoteDriven)"改成"本端最近有没有在收这枚火箭的包"。
		// 旧判据在"切换控制权"那一刻必然出错：controlledRocket 立刻变成另一枚，于是这枚被判成
		// "托管但没人开" → 当场设 Dynamic 交还引擎 → 位置从权威链路脱钩 → 收到下个包时硬对齐
		// → 视觉上就是"切走的瞬间火箭瞬移"。同一个错判还会连带两个症状：
		// 免碰撞豁免（判 controlledRocketIdsScratch）与"非权威火箭不爆炸"（判 bodyType != Dynamic）
		// 也都会跟着乱。
		// 新的判据只反映网络实际状态，与切换动作无关。
		double localWorldTime = ClientManager.world != null ? ClientManager.world.WorldTime : -1;
		bool stillReceivingPackets = localWorldTime >= 0
			&& lastAuthoritativePacketLocalTime >= 0
			&& (localWorldTime - lastAuthoritativePacketLocalTime) < AuthorityLossSeconds;
		if (hosted && !isLocalPlayerControlled && !stillReceivingPackets)
		{
			// 【交回引擎前先把最新已知状态写一次】以前这里只翻 bodyType 就 return，
			// 引擎是从"上一帧 transform 停在哪儿"起步的，而不是从我们最后收到的权威状态起步。
			if (!freeSimulation && currentUpdate != null)
			{
				ApplyStateImmediately(currentUpdate.Location.ToVanillaLocation(), currentUpdate.Rotation, currentUpdate.AngularVelocity);
			}
			if (rocket.rocket.rb2d.bodyType != RigidbodyType2D.Dynamic)
			{
				rocket.rocket.rb2d.bodyType = RigidbodyType2D.Dynamic;
			}
			if (rocket.rocket.rb2d.interpolation != RigidbodyInterpolation2D.None)
			{
				rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.None;
			}
			// 从 Kinematic 切到 Dynamic 的刚体可能仍处于休眠态，休眠 = 不被积分 = 看起来"冻住"。
			// 显式唤醒一次；这是"无人操控的火箭像区块卸载一样停止更新"最可能的一环。
			try { rocket.rocket.rb2d.WakeUp(); } catch (System.Exception) { }
			freeSimulation = true;
			HostStallProbe();
			return;
		}
		bool isAuthority = hosted && isLocalPlayerControlled;
		bool gainedAuthority = !authorityStateKnown
			? isAuthority
			: ShouldPrimeAuthorityState(wasAuthority, isAuthority);
		authorityStateKnown = true;
		wasAuthority = isAuthority;
		if (isAuthority)
		{
			if (gainedAuthority) PrimeAuthorityState();
			rocket.rocket.rb2d.bodyType = RigidbodyType2D.Dynamic;
			rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.None;
			RunAllPackets();
			currentUpdate = rocket.rocket.ToUpdatePacketPrimary(currentUpdate.RocketId);
			return;
		}
		if (PredictiveReconcile)
		{
			// 预测式同步：本地引擎推进 + 权威包软纠偏（见字段区注释）
			UpdatePredictivePrimary();
		}
		else
		{
			rocket.rocket.rb2d.bodyType = RigidbodyType2D.Kinematic;
			// 【勿改成 None】2026-09-22 实测：关掉 Extrapolate 后双方位置彻底不同步（"全乱套"）。
			// 说明本类写进去的位置不是每渲染帧直接落到 transform 的（要经游戏物理层），
			// Unity 这层外推是在补渲染帧与物理步之间的空隙，是承重的，不是重复积分。
			rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.Extrapolate;
			if (isNewlyCreated)
			{
				if (updateBuffer.Count > 0)
				{
					isNewlyCreated = false;
					// 非权威端首包硬对齐：直接 snap 到第一个真实 primary 包的物理位置（不走平滑），
					// 消除从"创建包出生位置"慢慢追逐导致的初始错位（表现为需要碰撞一次才同步）。
					PrimeNonAuthorityState();
				}
				return;
			}
			if (updateBuffer.Count < 1)
			{
				PredictState(currentUpdate);
			}
			else
			{
				while (updateBuffer.Count > 0)
				{
					Packet_UpdateRocketPrimary prev = currentUpdate;
					Packet_UpdateRocketPrimary packet_UpdateRocketPrimary = updateBuffer[0];
					if (DelayedWorldTime > packet_UpdateRocketPrimary.WorldTime)
					{
						currentUpdate = updateBuffer[0];
						updateBuffer.RemoveAt(0);
						continue;
					}
					InterpolatePackets(prev, packet_UpdateRocketPrimary);
					break;
				}
			}
		}
		// 手写原地压缩替代 packetBuffer.RemoveAll(委托)：RemoveAll 每次调用都要分配一个闭包 + 委托
		//（本函数是【每帧 × 每枚远端火箭】），内部还要整表复制一遍。语义保持不变：
		// 按时间顺序执行到期包、剔除 WorldTime 未设置的包，未到期的保持相对顺序留在表里。
		int write = 0;
		for (int read = 0; read < packetBuffer.Count; read++)
		{
			(double time, Packet packet) tuple = packetBuffer[read];
			bool consumed;
			if (double.IsNaN(tuple.time))
			{
				Debug.LogError($"Interpolator Error: WorldTime of `{tuple.packet.Type}` packet has not been set!");
				consumed = true;
			}
			else if (IsPacketDue(tuple.time, DelayedWorldTime))
			{
				RunPacket(tuple.packet);
				consumed = true;
			}
			else
			{
				consumed = false;
			}
			if (!consumed)
			{
				packetBuffer[write++] = tuple;
			}
		}
		if (write < packetBuffer.Count)
		{
			packetBuffer.RemoveRange(write, packetBuffer.Count - write);
		}
	}

	private void PrimeAuthorityState()
	{
		Packet_UpdateRocketPrimary authoritative = currentUpdate;
		if (updateBuffer.Count > 0)
		{
			authoritative = updateBuffer[updateBuffer.Count - 1];
		}
		if (authoritative == null || rocket == null || rocket.rocket == null)
		{
			return;
		}
		ApplyStateImmediately(authoritative.Location.ToVanillaLocation(), authoritative.Rotation, authoritative.AngularVelocity);
	}

	// 非权威端首包硬对齐：把 currentUpdate 直接跳到缓冲区里第一个真实包（最早 WorldTime）并立即应用，
	// 使下一帧插值从真实位置起步，而不是从创建包出生位置慢慢平滑追逐（那是"需要碰撞一次才同步"的根因）。
	private void PrimeNonAuthorityState()
	{
		if (updateBuffer.Count == 0 || rocket == null || rocket.rocket == null)
		{
			return;
		}
		var first = updateBuffer[0];
		currentUpdate = first;
		updateBuffer.RemoveAt(0);
		ApplyStateImmediately(first.Location.ToVanillaLocation(), first.Rotation, first.AngularVelocity);
	}

	// 包里说这颗行星、本端活体却在另一颗上 —— 位置会被当成另一个参照系的坐标。
	private void ReportPlanetMismatch(Location want)
	{
		if (planetMismatchLogs >= 20) return;
		if (want == null || want.planet == null) return;
		if (rocket == null || rocket.rocket == null || rocket.rocket.location == null) return;
		Location cur = rocket.rocket.location.Value;
		if (cur == null || cur.planet == null || cur.planet == want.planet) return;
		planetMismatchLogs++;
		UnityEngine.Debug.LogWarning($"[SFS-MP] planet mismatch: id={currentUpdate?.RocketId} "
			+ $"packet={want.planet.codeName} local={cur.planet.codeName} "
			+ $"packet pos=({want.position.x:F0},{want.position.y:F0}) local pos=({cur.position.x:F0},{cur.position.y:F0})");
	}

	// 托管给引擎的无主火箭，每 2 秒核对一次它到底有没有在动。
	private void HostStallProbe()
	{
		if (rocket == null || rocket.rocket == null || rocket.rocket.location == null) return;
		if (Time.unscaledTime < hostStallNextCheck) return;
		hostStallNextCheck = Time.unscaledTime + 2f;
		Location live = rocket.rocket.location.Value;
		if (live == null || live.planet == null) return;
		double dx = live.position.x - hostStallLastX;
		double dy = live.position.y - hostStallLastY;
		double moved = System.Math.Sqrt(dx * dx + dy * dy);
		double speed = live.velocity.magnitude;
		// 第一次采样（基准还是 0,0）只记基准，不判定。
		if (hostStallLastX != 0.0 || hostStallLastY != 0.0)
		{
			if (moved < 1.0 && speed > 5.0)
			{
				UnityEngine.Debug.LogWarning($"[SFS-MP] managed unowned rocket appears to have stopped thrusting: id={currentUpdate?.RocketId} "
					+ $"2s moved={moved:F2}m speed={speed:F1}m/s body={rocket.rocket.rb2d.bodyType} "
					+ $"phys={rocket.rocket.physics.PhysicsMode} asleep={rocket.rocket.rb2d.IsSleeping()} planet={live.planet.codeName}");
			}
		}
		hostStallLastX = live.position.x;
		hostStallLastY = live.position.y;
	}

	private void ApplyStateImmediately(Location loc, float rot, float angVel)
	{
		loc = ClampBelowTerrain(loc);
		if (loc == null) return;   // 地形未就绪：本帧不写位置/速度/角度，避免把未钳制的状态灌进物理体
		ReportPlanetMismatch(loc);
		rocket.rocket.rb2d.transform.eulerAngles = new Vector3(0f, 0f, rot);
		rocket.rocket.rb2d.angularVelocity = angVel;
		if (rocket.rocket.physics.PhysicsMode)
		{
			((I_Physics)rocket.rocket).LocalPosition = WorldView.ToLocalPosition(loc.position);
			((I_Physics)rocket.rocket).LocalVelocity = WorldView.ToLocalVelocity(loc.velocity);
		}
		else
		{
			rocket.rocket.physics.SetLocationAndState(loc, physicsMode: false);
		}
	}

	private void PredictState(Packet_UpdateRocketPrimary lastPacket)
	{
		// 本端持有这枚火箭的托管权（updateAuthority 含它）但不是本地玩家在操控它 ——
		// 例如发射台上留下的、分离掉的、切走后留下的那枚。这种情况绝不能做线性外推：
		// 外推把速度冻成常量，本端又一直在替它发 Primary 包，结果就是所有客户端都看到
		// 它"按固定方向一直飞"（也就是轨迹没有被真正传输）。正确做法是把轨迹交回本端
		// 游戏引擎（on-rails 轨道推进 / 物理），只把最后已知状态写一次；之后本端继续
		// 替它发 Primary 包，包里就是真实轨迹。
		if (rocket != null && rocket.rocket != null && rocket.rocket.location != null
			&& LocalManager.updateAuthority.Contains(lastPacket.RocketId)
			&& !(LocalManager.Player != null && LocalManager.Player.controlledRocket.Value == lastPacket.RocketId))
		{
			if (!freeSimulation)
			{
				freeSimulation = true;
				ApplyStateImmediately(lastPacket.Location.ToVanillaLocation(), lastPacket.Rotation, lastPacket.AngularVelocity);
				// 一次性诊断：证明轨迹已交回本端引擎（之后本端不再覆盖它，引擎按轨道/物理推进，
				// 发出去的 Primary 包里速度会随时间变化 —— 而不是恒定的直线速度）。
				Debug.Log($"[SFS-MP] handing an uncontrolled managed rocket back to the local engine: id={lastPacket.RocketId} "
						+ $"planet={lastPacket.Location.address} "
						+ $"pos=({lastPacket.Location.position.x:F1},{lastPacket.Location.position.y:F1}) "
						+ $"vel=({lastPacket.Location.velocity.x:F1},{lastPacket.Location.velocity.y:F1})");
			}
			return;
		}
		double num = DelayedWorldTime - lastPacket.WorldTime;
		if (!(num <= 0.0))
		{
			double maximum = AdaptiveProfile.MaximumExtrapolationSeconds;
			if (num > maximum)
			{
				// 数据断流（超过外推上限）：把火箭交回【本地物理引擎】自己算。
				// 客户端的物理引擎才是真实的那一套（权威端本来也是本地物理在跑），
				// 断流时继续硬外推只会卡住/漂移。这里只把最后已知状态写一次（按上限外推到当下），
				// 之后不再每帧写 transform，让游戏自己积分；收到新包会清掉 freeSimulation 重新对齐。
				if (!freeSimulation && rocket != null)
				{
					Location loc = lastPacket.Location.ToVanillaLocation();
					loc.position += lastPacket.Location.velocity * maximum;
					loc.velocity = lastPacket.Location.velocity;
					float rot = lastPacket.Rotation + lastPacket.AngularVelocity * (float)maximum;
					// 必须用 ApplyStateImmediately：它带 ClampBelowTerrain（低于地形的坐标会被钳回来）。
					// 若用普通 SetState 把火箭写在地形里，本地物理一解冻就会被碰撞求解器猛地弹出，
					// 表现为"火箭莫名其妙突然飞起来"。
					ApplyStateImmediately(loc, rot, lastPacket.AngularVelocity);
					freeSimulation = true;
					Debug.Log($"[SFS-MP] no data for {num:F2}s; handing back to local physics: id={lastPacket.RocketId} "
							  + $"planet={lastPacket.Location.address} pos=({loc.position.x:F1},{loc.position.y:F1}) "
							  + $"vel=({loc.velocity.x:F1},{loc.velocity.y:F1}) angVel={lastPacket.AngularVelocity:F3}");
				}
				if (ClientManager.world.WorldTime - lastSnapshotRequestWorldTime >= AdaptiveProfile.ValidationIntervalMilliseconds / 1000.0)
				{
					lastSnapshotRequestWorldTime = ClientManager.world.WorldTime;
					ClientManager.client?.RequestRocketSnapshot(lastPacket.RocketId);
				}
				return;
			}
			num = Math.Min(num, maximum);
			Location location = lastPacket.Location.ToVanillaLocation();
			location.position += lastPacket.Location.velocity * num;
			location.velocity = lastPacket.Location.velocity;
			float rot2 = lastPacket.Rotation + lastPacket.AngularVelocity * (float)num;
			float angularVelocity = lastPacket.AngularVelocity;
			SetState(location, rot2, angularVelocity);
		}
	}

	// ——— 预测式同步实现 ———
	// 远端火箭由本地引擎推进，我们每帧只做两件事：
	//   1) 有新权威包 → 换算成"此刻应该在哪儿"，与本地状态求差，存成待纠偏量
	//   2) 有待纠偏量 → 按窗口吃掉一小口（指数衰减），全程不瞬移
	// 没有偏差时完全不碰物理体，引擎自己积分（滑行段与权威端同解）。
	private void UpdatePredictivePrimary()
	{
		if (rocket == null || rocket.rocket == null || rocket.rocket.rb2d == null) return;

		if (isNewlyCreated)
		{
			if (updateBuffer.Count == 0) return;
			isNewlyCreated = false;
			PrimeNonAuthorityState();   // 首包硬对齐一次：从出生位置直接落到真实位置
		}

		// 【顺序很重要】先消费新包，再判断断流。
		// V1.2.3.20 把顺序写反了：断流判断在消费之前 return，于是首次误判后就永远吃不到包、
		// currentUpdate 永远停在旧值（日志实证：packet stall 1071698.8s = 世界时间 - 0），
		// 表现为远端火箭"彻底不动"。
		if (updateBuffer.Count > 0)
		{
			if (TrajectoryReconstruction)
			{
				// 【顺序要紧】先算"按老轨迹预测它会落在哪"、把差记成软吸收修正量，再并入历史。
				for (int i = 0; i < updateBuffer.Count; i++) AbsorbPredictionError(updateBuffer[i]);
				// MC 式：这一批包按 WorldTime 全部压进轨迹历史（不再"只留最后一个，其余丢掉"）
				for (int i = 0; i < updateBuffer.Count; i++) AppendTrajectory(updateBuffer[i]);
			}
			Packet_UpdateRocketPrimary newest = updateBuffer[updateBuffer.Count - 1];
			updateBuffer.Clear();
			// 用相邻包的 WorldTime 差估发包间隔 → 自适应的抖动缓冲区长度（见 RenderDelaySeconds）
			if (lastPrimaryPacketWorldTime > 0.0 && newest.WorldTime > lastPrimaryPacketWorldTime)
			{
				double interval = newest.WorldTime - lastPrimaryPacketWorldTime;
				if (interval < 2.0)
				{
					smoothedPacketIntervalSeconds = smoothedPacketIntervalSeconds * 0.7 + interval * 0.3;
				}
			}
			if (newest.WorldTime > 0.0) lastPrimaryPacketWorldTime = newest.WorldTime;
			currentUpdate = newest;
			freeSimulation = false;
			// 从断流恢复时刚体可能还停在 Dynamic，这里改回 Kinematic + Extrapolate
			if (rocket.rocket.rb2d.bodyType != RigidbodyType2D.Kinematic)
				rocket.rocket.rb2d.bodyType = RigidbodyType2D.Kinematic;
			if (rocket.rocket.rb2d.interpolation != RigidbodyInterpolation2D.Extrapolate)
				rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.Extrapolate;
			if (TrajectoryReconstruction)
			{
				// 不硬写：交给每帧的轨迹重建（包少来靠固定延迟吸收，绝不猜）
				SampleTrajectory();
				return;
			}
			ReSeedFromAuthoritative(newest);
			return;
		}

		// 没有新包：只有"确实很久没收到包"才交回本地引擎。
		// WorldTime<=0 的包（服务端部分初始包就是这样）不参与判断，否则会立刻误判成断流。
		if (currentUpdate != null && ClientManager.world != null && currentUpdate.WorldTime > 0.0)
		{
			double packetAge = ClientManager.world.WorldTime - currentUpdate.WorldTime;
			if (packetAge > AdaptiveProfile.MaximumExtrapolationSeconds)
			{
				if (!freeSimulation)
				{
					freeSimulation = true;
					rocket.rocket.rb2d.bodyType = RigidbodyType2D.Dynamic;
					rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.None;
					Debug.Log($"[SFS-MP] packet stall {packetAge:F1}s, hand to local physics: id={currentUpdate.RocketId}");
				}
				return;
			}
		}

		// Kinematic + Extrapolate：火箭由我们的写回驱动，包间由 Unity 外推补足
		if (rocket.rocket.rb2d.bodyType != RigidbodyType2D.Kinematic)
			rocket.rocket.rb2d.bodyType = RigidbodyType2D.Kinematic;
		if (rocket.rocket.rb2d.interpolation != RigidbodyInterpolation2D.Extrapolate)
			rocket.rocket.rb2d.interpolation = RigidbodyInterpolation2D.Extrapolate;
		if (TrajectoryReconstruction) SampleTrajectory();
	}

	// ——— 轨迹重建实现（MC 式）———
	// 历史里按 WorldTime 升序保存最近的权威包；负责去重插入与裁剪。
	private void AppendTrajectory(Packet_UpdateRocketPrimary packet)
	{
		if (packet == null) return;
		int n = trajectoryHistory.Count;
		if (n == 0 || trajectoryHistory[n - 1].WorldTime < packet.WorldTime)
		{
			trajectoryHistory.Add(packet);
		}
		else
		{
			int i = n - 1;
			while (i >= 0 && trajectoryHistory[i].WorldTime > packet.WorldTime) i--;
			if (i >= 0 && trajectoryHistory[i].WorldTime == packet.WorldTime) trajectoryHistory[i] = packet;
			else trajectoryHistory.Insert(i + 1, packet);
		}
		while (trajectoryHistory.Count > TrajectoryHistoryMax) trajectoryHistory.RemoveAt(0);
	}

	// 采样时刻 = 本端世界时间 - 固定播放延迟；只在两个已知包之间重建，绝不长距离外推。
	// 采样点比最老的包还早（刚加入 / 换星球 / 长断流恢复）→ 硬对齐最新包一次。
	// 采样点比最新的包还晚（包没到）→ 最多切向外推 100ms，超过就停在原地等包（与 MC 一致）。
	private void SampleTrajectory()
	{
		int n = trajectoryHistory.Count;
		if (n == 0 || ClientManager.world == null) return;
		double now = ClientManager.world.WorldTime;
		if (now <= 0.0) return;

		double delay = smoothedPacketIntervalSeconds * PlayoutDelayFactor;
		if (delay < MinPlayoutDelaySeconds) delay = MinPlayoutDelaySeconds;
		else if (delay > MaxPlayoutDelaySeconds) delay = MaxPlayoutDelaySeconds;
		double sampleTime = now - delay;
		Packet_UpdateRocketPrimary newest = trajectoryHistory[n - 1];

		if (sampleTime <= newest.WorldTime)
		{
			int i = n - 2;
			while (i >= 0 && trajectoryHistory[i].WorldTime > sampleTime) i--;
			if (i >= 0)
			{
				Packet_UpdateRocketPrimary a = trajectoryHistory[i];
				Packet_UpdateRocketPrimary b = trajectoryHistory[i + 1];
				double span = b.WorldTime - a.WorldTime;
				if (span > 1e-6)
				{
					double f = (sampleTime - a.WorldTime) / span;
					if (f < 0.0) f = 0.0; else if (f > 1.0) f = 1.0;
					HermiteInterpolation(a, b, f, out Location loc, out float rot, out float angVel);
					WriteTrajectory(loc, rot, angVel, false);
					ProbeTrajectory(newest, 0.0, true);
					return;
				}
			}
			WriteTrajectory(newest.Location.ToVanillaLocation(), newest.Rotation, newest.AngularVelocity, true);
			return;
		}

		double over = sampleTime - newest.WorldTime;
		double overLimit = MaxTrajectoryExtrapolationSeconds;
		double intervalLimit = smoothedPacketIntervalSeconds * 2.0;
		if (intervalLimit > overLimit) overLimit = intervalLimit;   // 包间隔大时放宽，否则会频繁冻结
		if (over > overLimit) return;
		// 【exp6 基线】一阶切线继续：位置 + 速度×时间。
		// 刻意不再加二次项：加速度是从两个包的速度差估的，低速时速度差几乎全是浮点噪声，
		// 二次预测自己就会抖。预测误差交给下面的软吸收去抹平，而不是靠把预测做得更复杂。
		Location extrapolated = newest.Location.ToVanillaLocation();
		extrapolated.position += newest.Location.velocity * over;
		extrapolated.velocity = newest.Location.velocity;
		WriteTrajectory(extrapolated, newest.Rotation + newest.AngularVelocity * (float)over, newest.AngularVelocity, false);
		ProbeTrajectory(newest, 0.0, false);
	}

	// 用当前历史（一阶切线）预测 time 时刻的位置 —— 即"我们以为下一个包会说什么"。
	private bool PredictFromNewest(double time, out Location loc, out float rot)
	{
		int n = trajectoryHistory.Count;
		if (n == 0)
		{
			loc = null;
			rot = 0f;
			return false;
		}
		Packet_UpdateRocketPrimary newest = trajectoryHistory[n - 1];
		double over = time - newest.WorldTime;
		loc = newest.Location.ToVanillaLocation();
		loc.position += newest.Location.velocity * over;
		loc.velocity = newest.Location.velocity;
		rot = newest.Rotation + newest.AngularVelocity * (float)over;
		return true;
	}

	// 包到达：把"预测落点"与"真实落点"之差吸收成反向修正量（随后由 WriteTrajectory 逐帧衰减）。
	private void AbsorbPredictionError(Packet_UpdateRocketPrimary packet)
	{
		if (packet == null || packet.Location == null) return;
		Location predicted;
		float predictedRot;
		if (!PredictFromNewest(packet.WorldTime, out predicted, out predictedRot)) return;
		Location actual = packet.Location.ToVanillaLocation();
		double dx = actual.position.x - predicted.position.x;
		double dy = actual.position.y - predicted.position.y;
		if (dx * dx + dy * dy > CorrectionSnapMeters * CorrectionSnapMeters)
		{
			// 差得太多 = 不是预测误差（换星球/传送/权威切换）：不平滑，交给硬对齐
			correctionX = 0.0;
			correctionY = 0.0;
			return;
		}
		correctionX -= dx;
		correctionY -= dy;
	}

	// 【诊断回归 2026-10-09】exp3 把渲染路径换成轨迹重建后，原来的 syncprobe 在 SetState 里，
	// 再也走不到 —— 日志里 syncprobe 计数为 0。挪到真正生效的地方：报出锚包有多旧、预测项有多大。
	private void ProbeTrajectory(Packet_UpdateRocketPrimary newest, double curvatureMeters, bool interpolated)
	{
		if (Time.unscaledTime < nextSyncProbeTime) return;
		nextSyncProbeTime = Time.unscaledTime + SyncProbeIntervalSeconds;
		double lagMs = ClientManager.world != null ? (ClientManager.world.WorldTime - newest.WorldTime) * 1000.0 : -1.0;
		Vector2 v = WorldView.ToLocalVelocity(newest.Location.velocity);
		Debug.Log($"[SFS-MP][syncprobe] id={newest.RocketId} mode={PlayoutMode} src={(interpolated ? "interp" : "predict")} "
			+ $"v={v.magnitude:F0}m/s delay={Math.Max(0.0, lagMs):F0}ms interval={smoothedPacketIntervalSeconds * 1000:F0}ms "
			+ $"hist={trajectoryHistory.Count} corr={Math.Sqrt(correctionX * correctionX + correctionY * correctionY):F2}m");
	}

	private void WriteTrajectory(Location loc, float rot, float angVel, bool force)
	{
		if (force)
		{
			correctionX = 0.0;   // 硬对齐 = 瞬移：修正量必须清零，否则会把上次的残差带过去
			correctionY = 0.0;
		}
		else
		{
			// 逐帧指数衰减；修正量稳态归零，因此不产生恒定滞后，只把接缝"滑"过去
			double dt = Time.unscaledDeltaTime;
			if (dt > 0.0 && dt < 0.5)
			{
				double decay = Math.Exp(-dt / CorrectionTauSeconds);
				correctionX *= decay;
				correctionY *= decay;
			}
			loc.position.x += correctionX;
			loc.position.y += correctionY;
		}
		if (!force && !float.IsNaN(writtenRot)
			&& Math.Abs(loc.position.x - writtenX) < 1e-3
			&& Math.Abs(loc.position.y - writtenY) < 1e-3
			&& Math.Abs(rot - writtenRot) < 1e-4f)
		{
			return;   // 这一帧没动到肉眼可辨的程度：不碰物理体
		}
		writtenX = loc.position.x;
		writtenY = loc.position.y;
		writtenRot = rot;
		ApplyStateImmediately(loc, rot, angVel);
	}

	// 每收到一个权威包，把权威状态整份写回（走游戏自己的物理 API）。
	//
	// 为什么是"整份写回 + 包间交给游戏推进"，而不是逐帧写、也不是弹簧速度：
	//  - 逐帧写位置（V1.2.3.18）= 每帧扰动刚体 → 高频中幅抖动；
	//  - 弹簧速度写 rb2d.velocity（V1.2.3.19）= 被游戏物理模块覆盖，纠偏不生效 → 位置不同步（实测）；
	//  - 整份写回走的是 ApplyStateImmediately → LocalPosition/LocalVelocity 或 SetLocationAndState，
	//    与旧插值路径同一个原语（实机证明有效）。包间隔 20~50ms 内由 Unity 外推补足，误差 ~½at²。
	private void ReSeedFromAuthoritative(Packet_UpdateRocketPrimary packet)
	{
		Location here = rocket.rocket.location.Value;
		if (here == null) return;

		Location want = packet.Location.ToVanillaLocation();
		double renderTime = ClientManager.world != null ? ClientManager.world.WorldTime : packet.WorldTime;
		double renderDelay = RenderDelaySeconds;
		double age = renderTime - renderDelay - packet.WorldTime;
		if (age > 0.0)
		{
			// 把包推到"渲染时刻"（比旧插值延迟小一个量级，所以不再有几十上百米的滞后）
			double max = AdaptiveProfile.MaximumExtrapolationSeconds;
			want.position += packet.Location.velocity * Math.Min(age, max);
		}

		if (here.planet != want.planet)
		{
			// 换星球/传送：必须硬对齐
			ApplyStateImmediately(want, packet.Rotation, packet.AngularVelocity);
			return;
		}

		Vector2 posErr = WorldView.ToLocalPosition(want.position) - WorldView.ToLocalPosition(here.position);
		Vector2 velErr = WorldView.ToLocalVelocity(want.velocity) - WorldView.ToLocalVelocity(here.velocity);
		float posErrLen = posErr.magnitude;

		// 【临时诊断】预测模式下的实时误差
		if (Time.unscaledTime >= nextSyncProbeTime)
		{
			nextSyncProbeTime = Time.unscaledTime + SyncProbeIntervalSeconds;
			Debug.Log($"[SFS-MP][syncprobe] id={packet.RocketId} q={AdaptiveProfile.Quality} "
				+ $"v={WorldView.ToLocalVelocity(want.velocity).magnitude:F0}m/s posErr={posErrLen:F1}m "
				+ $"velErr={velErr.magnitude:F1}m/s age={age:F3}s buf={updateBuffer.Count} "
				+ $"delay={renderDelay * 1000:F0}ms interval={smoothedPacketIntervalSeconds * 1000:F0}ms "
				+ $"body={rocket.rocket.rb2d.bodyType} phys={rocket.rocket.physics.PhysicsMode}");
		}

		if (posErrLen > HardResyncMeters)
		{
			// 几十上百米：说明本地与权威端已经脱节（长断流/长时间无包），对齐一次并留日志
			Debug.Log($"[SFS-MP] remote rocket resync: id={packet.RocketId} err={posErrLen:F0}m");
		}

		// 整份写回：位置、速度、角度、角速度都取权威值（含地形钳制与物理模式处理）
		ApplyStateImmediately(want, packet.Rotation, packet.AngularVelocity);
	}

	private void InterpolatePackets(Packet_UpdateRocketPrimary prev, Packet_UpdateRocketPrimary next)
	{
		if (prev.Location.address != next.Location.address)
		{
			SetState(next.Location.ToVanillaLocation(), next.Rotation, next.AngularVelocity);
			if (LocalManager.players != null)
			{
				foreach (var player in LocalManager.players.Values)
				{
					if (player.controlledRocket.Value == next.RocketId)
					{
						MsgDrawer.main?.Log($"{player.username} arrived at {next.Location.address}");
						break;
					}
				}
			}
			return;
		}
		double value = GetInterpolationFraction(prev.WorldTime, next.WorldTime, DelayedWorldTime);
		Location loc = prev.Location.ToVanillaLocation();
		float rot;
		float angVel;
		switch (interpolationMode)
		{
		case InterpolationMode.Hermite:
			HermiteInterpolation(prev, next, value, out loc, out rot, out angVel);
			break;
		case InterpolationMode.Linear:
			LinearInterpolation(prev, next, value, out loc, out rot, out angVel);
			break;
		case InterpolationMode.Spherical:
			SphericalInterpolation(prev, next, value, out loc, out rot, out angVel);
			break;
		default:
			HermiteInterpolation(prev, next, value, out loc, out rot, out angVel);
			break;
		}
		SetState(loc, rot, angVel);
	}

	private void HermiteInterpolation(Packet_UpdateRocketPrimary prev, Packet_UpdateRocketPrimary next, double t, out Location loc, out float rot, out float angVel)
	{
		double num = t * t;
		double num2 = num * t;
		Double2 position = prev.Location.position;
		Double2 velocity = prev.Location.velocity;
		Double2 position2 = next.Location.position;
		Double2 velocity2 = next.Location.velocity;
		double duration = Math.Max(0.0001, next.WorldTime - prev.WorldTime);
		double num3 = 2.0 * num2 + -3.0 * num + 1.0;
		double num4 = num2 + -2.0 * num + t;
		double num5 = -2.0 * num2 + 3.0 * num;
		double num6 = num2 - num;
		loc = prev.Location.ToVanillaLocation();
		loc.position = num3 * position + num4 * duration * velocity + num5 * position2 + num6 * duration * velocity2;
		loc.velocity = Double2.Lerp(velocity, velocity2, t);
		rot = Mathf.LerpAngle(prev.Rotation, next.Rotation, (float)t);
		angVel = Mathf.Lerp(prev.AngularVelocity, next.AngularVelocity, (float)t);
	}

	private void LinearInterpolation(Packet_UpdateRocketPrimary prev, Packet_UpdateRocketPrimary next, double t, out Location loc, out float rot, out float angVel)
	{
		loc = prev.Location.ToVanillaLocation();
		loc.position = Double2.Lerp(prev.Location.position, next.Location.position, t);
		loc.velocity = Double2.Lerp(prev.Location.velocity, next.Location.velocity, t);
		rot = Mathf.LerpAngle(prev.Rotation, next.Rotation, (float)t);
		angVel = Mathf.Lerp(prev.AngularVelocity, next.AngularVelocity, (float)t);
	}

	private void SphericalInterpolation(Packet_UpdateRocketPrimary prev, Packet_UpdateRocketPrimary next, double t, out Location loc, out float rot, out float angVel)
	{
		loc = prev.Location.ToVanillaLocation();
		double num = t * t * (3.0 - 2.0 * t);
		loc.position = Double2.Lerp(prev.Location.position, next.Location.position, num);
		loc.velocity = Double2.Lerp(prev.Location.velocity, next.Location.velocity, num);
		rot = Mathf.LerpAngle(prev.Rotation, next.Rotation, (float)num);
		angVel = Mathf.Lerp(prev.AngularVelocity, next.AngularVelocity, (float)num);
	}

	private static bool terrainLookupFailed;

	// 供 LocalManager 在"建火箭/快照刷新"这类一次性写入处复用。
	// 语义：返回钳制后的 Location；返回 null = 地形数据未就绪、本帧无法判断（调用方必须跳过写入）。
	public static Location ClampBelowTerrain(Location loc)
	{
		if (loc == null || loc.planet == null || loc.position.sqrMagnitude <= 1E-10)
		{
			return loc;
		}
		// 【性能关键，勿删】远高于地表时不可能埋进地形：直接跳过地形采样 ——
		// 与游戏本体 Planet.IsInsideTerrain 的首行守卫完全同款
		//（本体的判定是 position.Mag_MoreThan(Radius + maxTerrainHeight) 就直接 return false）。
		// 为什么必须加：GetTerrainHeight → Planet.GetTerrainHeightAtAngle 每次调用都会
		//   ① new double[1]（angle 数组）② 里面再 new double[] 两次 ③ 构造 TerrainSample
		//   （含一个 Dictionary<string,double[]>）④ 跑一整轮地形采样解释器
		//   （AddHeightMap/曲线，每步都分配数组）⑤ 有海水的行星还要逐角度做
		//   Texture2D.GetPixelBilinear 读切图；
		// 而本函数在插值路径上是【每帧 × 每枚远端火箭】都会被调用一次
		//（SetState / ApplyStateImmediately），12~18 枚火箭 = 每秒上千次这种调用，
		// 且火箭在轨/在太空时这次采样纯属浪费 ——
		// 这就是"只有联机时才内存暴涨到数 GB（每秒一次 GC、MarkObjects 40ms）+ CPU 拉满"的主因。
		// 该守卫与旧行为严格等价：|pos| > Radius + maxTerrainHeight 时
		// terrainHeightAtAngle(angle) ≤ maxTerrainHeight，GetTerrainHeight() 必然 ≥ 0，
		// 原代码本来就会原样 return loc。
		var planetData = loc.planet.data;
		if (planetData == null || planetData.basics == null)
		{
			return loc;
		}
		double terrainLimit = planetData.basics.radius + loc.planet.maxTerrainHeight;
		if (loc.position.sqrMagnitude > terrainLimit * terrainLimit)
		{
			return loc;
		}
		// 地形数据未就绪时 GetTerrainHeightAtAngles 会抛 NullReferenceException（行星地形尚未生成，
		// 实测一局 22 次）。Unity 每个异常都要抓栈并触发一次崩溃报告上传，而且异常会中断调用方的
		// 状态写入（位置/速度写一半）—— 那是"火箭消失/位置异常"的温床。这里吞掉并跳过钳制，
		// 位置照常写入；地形可用后自动恢复钳制。
		double belowTerrain;
		try
		{
#if SFS15
			// SFS 1.5 没有海洋：Location.GetTerrainHeight(clampToWater) 不存在，
			// 等价物是 TerrainHeight 属性（纯地形高度）。按"海洋当陆地"的口径直接用陆地高度。
			belowTerrain = loc.TerrainHeight;
#else
			belowTerrain = loc.GetTerrainHeight(clampToWater: true);
#endif
		}
		catch (Exception)
		{
			if (!terrainLookupFailed)
			{
				terrainLookupFailed = true;
				UnityEngine.Debug.LogWarning("[SFS-MP] planet terrain data is not ready; skipping position clamping for now (recovers automatically).");
			}
			// 返回 null 表示"本帧无法判断位置是否在地面以下"：调用方必须跳过本帧位置写入。
			// 旧版在这里原样返回未钳制的 Location，调用方照样写进物理体 —— 位置落到地形内部后，
			// 原版 SFS.World.Physics.Update → Planet.IsInsideTerrain → GetTerrainHeightAtAngles
			// 会在地形未就绪时抛 NullReferenceException（现场 NRE ×10+、崩溃报告 ×3 的唯一栈）。
			return null;
		}
		terrainLookupFailed = false;
		if (belowTerrain >= 0.0)
		{
			return loc;
		}
		loc.position += loc.position.normalized * -belowTerrain;
		return loc;
	}

	private void SetState(Location loc, float rot, float angVel)
	{
		loc = ClampBelowTerrain(loc);
		if (loc == null) return;   // 地形未就绪：本帧不写位置/速度/角度，避免把未钳制的状态灌进物理体
		// 防飘修复（勿回退）：旧版在此对插值结果叠加重指数平滑（CorrectionSeconds 0.45~1.4s），
		// 恒速滞后 = 速度 × 常数，真机表现为远端火箭持续“飘”。全裸切（零平滑）又会在包边界微跳。
		// 折衷：平滑常数 = 包间隔 × 0.2（钳制 0.02~0.12s），滞后比旧版低一个量级且不闪。
		// 跳变恢复仍由首包硬对齐（PrimeNonAuthorityState/PrimeAuthorityState）负责。
		NetworkAdaptiveProfile profile = AdaptiveProfile;
		double smoothing = InterpolationRenderPolicy.SmoothingSeconds(profile.ControlledIntervalMilliseconds / 1000.0);
		double delta = Math.Max(0.001, Time.unscaledDeltaTime);
		Location current = rocket.rocket.location.Value;
		if (current != null && current.planet == loc.planet)
		{
			// 粗差硬对齐（仅在真断流时启用，见下方 if 的 freeSimulation 门）：
			// 平滑是"每帧朝目标靠一点"，偏差几百米时永远靠不完，所以需要一次性对齐。
			// 【2026-09-22 修正】以前是无条件启用，而平滑跟踪运动目标的稳态滞后就是 v×τ
			//（600m/s × 0.02s = 12m），远大于这里 10m 的门限 → 每几帧被判成"粗差"瞬移一次，
			// 实机就是"速度越快越一闪一闪、延迟越大越明显"。
			// 反编译对照 1.1.4.5（用户认为不闪的那版）：它的 SetState 只有纯平滑，根本没有这段硬对齐。
			double errorX = loc.position.x - current.position.x;
			double errorY = loc.position.y - current.position.y;

			// 【临时诊断】分清两种错位：
			//   targetErr = 我们要的位置 与 逻辑当前位置 之差（插值/包/纠偏层面的）
			//   renderErr = 我们要的位置 与 屏幕上实际位置 之差（含 Unity 外推的那一段）
			// 闪烁若来自外推层，renderErr 会随帧相位大幅摆动而 targetErr 很小；反之则是插值/包的问题。
			if (Time.unscaledTime >= nextSyncProbeTime)
			{
				nextSyncProbeTime = Time.unscaledTime + SyncProbeIntervalSeconds;
				Vector2 wantPos = WorldView.ToLocalPosition(loc.position);
				Vector2 renderPos = rocket.rocket.rb2d.transform.position;
				Debug.Log($"[SFS-MP][syncprobe] id={currentUpdate.RocketId} q={profile.Quality} "
					+ $"v={loc.velocity.magnitude:F0}m/s targetErr={Math.Sqrt(errorX * errorX + errorY * errorY):F1}m "
					+ $"renderErr={Vector2.Distance(renderPos, wantPos):F1}m tau={smoothing * 1000:F0}ms "
					+ $"age={DelayedWorldTime - currentUpdate.WorldTime:F3}s buf={updateBuffer.Count} "
					+ $"freeSim={freeSimulation} stepMs={Time.fixedDeltaTime * 1000:F1} frameMs={delta * 1000:F1}");
			}

			// 只有"本地引擎在擅自推进"（断流后的 freeSimulation）才可能出现几百米级真偏差；
			// 常规飞行交给平滑跟踪，不做瞬移。
			if (freeSimulation
				&& errorX * errorX + errorY * errorY > GrossPositionErrorMeters * GrossPositionErrorMeters)
			{
				if (Time.unscaledTime >= nextDriftCorrectionLogTime)
				{
					nextDriftCorrectionLogTime = Time.unscaledTime + 1f;
					Debug.Log($"[SFS-MP] stall resync: id={currentUpdate.RocketId} "
							+ $"err={Math.Sqrt(errorX * errorX + errorY * errorY):F0}m");
				}
				// 偏差是"本地引擎擅自推进"造成的，硬对齐之后必须再从服务端取一次权威状态，
				// 否则下一帧又会按本地结果漂出去。
				if (ClientManager.world.WorldTime - lastSnapshotRequestWorldTime >= 0.5)
				{
					lastSnapshotRequestWorldTime = ClientManager.world.WorldTime;
					ClientManager.client?.RequestRocketSnapshot(currentUpdate.RocketId);
				}
			}
			else
			{
				// 【EXP 2026-10-09】平滑在"以本端玩家火箭为原点"的相对坐标里做。
				// 取不到参照点（没在开船 / 星球不同 / 就是它自己）时，ox..ovy 全为 0，
				// 于是退化成与原来完全一致的绝对坐标平滑 —— 不会更差。
				double ox = 0.0, oy = 0.0, ovx = 0.0, ovy = 0.0;
				if (RelativeFrameSmoothing && currentUpdate != null && loc.planet != null
					&& LocalManager.TryGetLocalReferencePose(currentUpdate.RocketId, loc.planet.codeName,
						out ox, out oy, out ovx, out ovy))
				{
					(loc.position.x, loc.position.y) = InterpolationRenderPolicy.ResolvePosition(
						smoothing, delta,
						current.position.x - ox, current.position.y - oy,
						loc.position.x - ox, loc.position.y - oy);
					(loc.velocity.x, loc.velocity.y) = InterpolationRenderPolicy.ResolveVelocity(
						smoothing, delta,
						current.velocity.x - ovx, current.velocity.y - ovy,
						loc.velocity.x - ovx, loc.velocity.y - ovy);
					loc.position.x += ox; loc.position.y += oy;
					loc.velocity.x += ovx; loc.velocity.y += ovy;
				}
				else
				{
					(loc.position.x, loc.position.y) = InterpolationRenderPolicy.ResolvePosition(
						smoothing, delta, current.position.x, current.position.y, loc.position.x, loc.position.y);
					(loc.velocity.x, loc.velocity.y) = InterpolationRenderPolicy.ResolveVelocity(
						smoothing, delta, current.velocity.x, current.velocity.y, loc.velocity.x, loc.velocity.y);
				}
			}
		}
		rot = InterpolationRenderPolicy.ResolveRotation(smoothing, delta, rocket.rocket.rb2d.transform.eulerAngles.z, rot);
		angVel = InterpolationRenderPolicy.ResolveAngularVelocity(smoothing, delta, rocket.rocket.rb2d.angularVelocity, angVel);
		rocket.rocket.rb2d.transform.eulerAngles = new Vector3(0f, 0f, rot);
		rocket.rocket.rb2d.angularVelocity = angVel;
		if (rocket.rocket.physics.PhysicsMode)
		{
			((I_Physics)rocket.rocket).LocalPosition = WorldView.ToLocalPosition(loc.position);
			((I_Physics)rocket.rocket).LocalVelocity = WorldView.ToLocalVelocity(loc.velocity);
		}
		else
		{
			rocket.rocket.physics.SetLocationAndState(loc, physicsMode: false);
		}
	}

	private void RunAllPackets()
	{
		foreach (var item2 in packetBuffer)
		{
			Packet item = item2.Item2;
			RunPacket(item);
		}
		packetBuffer.Clear();
		Packet_UpdateRocketPrimary packet_UpdateRocketPrimary = null;
		while (updateBuffer.Count > 0)
		{
			packet_UpdateRocketPrimary = updateBuffer[0];
			updateBuffer.RemoveAt(0);
		}
		if (packet_UpdateRocketPrimary != null)
		{
			SetState(packet_UpdateRocketPrimary.Location.ToVanillaLocation(), packet_UpdateRocketPrimary.Rotation, packet_UpdateRocketPrimary.AngularVelocity);
		}
	}

	private void RunPacket(Packet packet)
	{
		switch (packet.Type)
		{
		case PacketType.UpdateRocketSecondary:
			OnPacket_UpdateRocketSecondary(packet as Packet_UpdateRocketSecondary);
			break;
		case PacketType.DestroyPart:
			OnPacket_DestroyPart(packet as Packet_DestroyPart);
			break;
		case PacketType.UpdateStaging:
			OnPacket_UpdateStaging(packet as Packet_UpdateStaging);
			break;
		case PacketType.UpdatePart_EngineModule:
			OnPacket_UpdatePart_EngineModule(packet as Packet_UpdatePart_EngineModule);
			break;
		case PacketType.UpdatePart_WheelModule:
			OnPacket_UpdatePart_WheelModule(packet as Packet_UpdatePart_WheelModule);
			break;
		case PacketType.UpdatePart_BoosterModule:
			OnPacket_UpdatePart_BoosterModule(packet as Packet_UpdatePart_BoosterModule);
			break;
		case PacketType.UpdatePart_ParachuteModule:
			OnPacket_UpdatePart_ParachuteModule(packet as Packet_UpdatePart_ParachuteModule);
			break;
		case PacketType.UpdatePart_MoveModule:
			OnPacket_UpdatePart_MoveModule(packet as Packet_UpdatePart_MoveModule);
			break;
		case PacketType.UpdatePart_ResourceModule:
			OnPacket_UpdatePart_ResourceModule(packet as Packet_UpdatePart_ResourceModule);
			break;
		default:
			Debug.LogError($"Invalid packet type used in interpolator: {packet.Type}");
			break;
		}
	}

	private void OnPacket_UpdateRocketSecondary(Packet_UpdateRocketSecondary packet)
	{
		Arrowkeys arrowkeys = rocket.rocket.arrowkeys;
		arrowkeys.turnAxis.Value = packet.Input_Turn;
		arrowkeys.rawArrowkeysAxis.Value = packet.Input_Raw;
		arrowkeys.horizontalAxis.Value = packet.Input_Horizontal;
		arrowkeys.verticalAxis.Value = packet.Input_Vertical;
		arrowkeys.rcs.Value = packet.RCS;
		rocket.rocket.throttle.throttlePercent.Value = packet.ThrottlePercent;
		rocket.rocket.throttle.throttleOn.Value = packet.ThrottleOn;
	}

	private void OnPacket_DestroyPart(Packet_DestroyPart packet)
	{
		// 【静默丢失修复 2026-10-10】以前 TryGetValue 失败就一声不吭丢掉整次销毁：
		// 权威端少了零件、本端留着 -> live = incoming + 1（实测 15 次全是这个签名）
		// -> 只能靠整枚重建兜底（重建既修状态又重置插值器 = 抖）。现在：喊出来 + 立刻要一次权威快照自愈。
		if (!rocket.parts.TryGetValue(packet.PartId, out var value) || value == null)
		{
			Debug.LogWarning("[SFS-MP] destroy part " + packet.PartId + " not found on rocket " + packet.RocketId
				+ " (parts=" + rocket.parts.Count + "); requesting authoritative resync");
			ClientManager.RequestRocketSnapshot(packet.RocketId);
			return;
		}
		if (value != null)
		{
			LocalManager.TrueDestructionReason = packet.Reason;
			// 爆炸特效走每帧预算：整枚火箭被炸时不让一帧内生成几十个特效（那是卡顿来源）。
			// 预算用尽时部件照常摧毁，只是不放特效。
			bool explosion = packet.CreateExplosion && LocalManager.AllowRemoteExplosion();
			value.DestroyPart(explosion, updateJoints: true, (DestructionReason)4);
		}
	}

	private void OnPacket_UpdateStaging(Packet_UpdateStaging packet)
	{
		rocket.rocket.staging.ClearStages(record: false);
		foreach (StageState stage in packet.Stages)
		{
			List<Part> list = new List<Part>();
			foreach (int partID in stage.partIDs)
			{
				if (rocket.parts.TryGetValue(partID, out var part))
					list.Add(part);
			}
			rocket.rocket.staging.InsertStage(new Stage(stage.stageID, list), record: false);
		}
	}

	private void OnPacket_UpdatePart_EngineModule(Packet_UpdatePart_EngineModule packet)
	{
		if (rocket.parts.TryGetValue(packet.PartId, out var value))
		{
			EngineModule[] modules = value.GetModules<EngineModule>();
			if (modules.Length > 1)
			{
				Debug.LogWarning("OnPacket_UpdatePart_EngineModule: Found multiple engine modules on part \"" + value.Name + "\".");
			}
			modules[0].engineOn.Value = packet.EngineOn;
		}
	}

	private void OnPacket_UpdatePart_WheelModule(Packet_UpdatePart_WheelModule packet)
	{
		if (rocket.parts.TryGetValue(packet.PartId, out var value))
		{
			WheelModule[] modules = value.GetModules<WheelModule>();
			if (modules.Length > 1)
			{
				Debug.LogWarning("OnPacket_UpdatePart_WheelModule: Found multiple wheel modules on part \"" + value.Name + "\".");
			}
			modules[0].on.Value = packet.WheelOn;
		}
	}

	private void OnPacket_UpdatePart_BoosterModule(Packet_UpdatePart_BoosterModule packet)
	{
		if (rocket.parts.TryGetValue(packet.PartId, out var value))
		{
			BoosterModule[] modules = value.GetModules<BoosterModule>();
			if (modules.Length > 1)
			{
				Debug.LogWarning("OnPacket_UpdatePart_BoosterModule: Found multiple booster modules on part \"" + value.Name + "\".");
			}
			modules[0].boosterPrimed.Value = packet.Primed;
			modules[0].throttle_Out.Value = packet.Throttle;
			modules[0].fuelPercent.Value = packet.FuelPercent;
		}
	}

	private void OnPacket_UpdatePart_ParachuteModule(Packet_UpdatePart_ParachuteModule packet)
	{
		if (rocket.parts.TryGetValue(packet.PartId, out var value))
		{
			ParachuteModule[] modules = value.GetModules<ParachuteModule>();
			if (modules.Length > 1)
			{
				Debug.LogWarning("OnPacket_UpdatePart_ParachuteModule: Found multiple parachute modules on part \"" + value.Name + "\".");
			}
			modules[0].state.Value = packet.State;
			modules[0].targetState.Value = packet.TargetState;
		}
	}

	private void OnPacket_UpdatePart_MoveModule(Packet_UpdatePart_MoveModule packet)
	{
		if (rocket.parts.TryGetValue(packet.PartId, out var value))
		{
			MoveModule[] modules = value.GetModules<MoveModule>();
			if (modules.Length > 1)
			{
				Debug.LogWarning("OnPacket_UpdatePart_MoveModule: Found multiple move modules on part \"" + value.Name + "\".");
			}
			modules[0].time.Value = packet.Time;
			modules[0].targetTime.Value = packet.TargetTime;
		}
	}

	private void OnPacket_UpdatePart_ResourceModule(Packet_UpdatePart_ResourceModule packet)
	{
		foreach (int partId in packet.PartIds)
		{
			if (rocket.parts.TryGetValue(partId, out var value))
			{
				ResourceModule[] modules = value.GetModules<ResourceModule>();
				if (modules.Length == 0)
				{
					// 无资源模块的部件（结构件等）收到资源更新包时会走到这里，
					// 直接跳过。原先跳过了长度 0 的判断，会用 modules[0] 抛 IndexOutOfRangeException。
					continue;
				}
				if (modules.Length > 1)
				{
					Debug.LogWarning("OnPacket_UpdatePart_ResourceModule: Found multiple resource modules on part \"" + value.Name + "\".");
				}
				modules[0].resourcePercent.Value = packet.ResourcePercent;
			}
		}
	}
}
