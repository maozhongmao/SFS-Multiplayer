// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Net;
using System.Reflection;
using System.Threading.Tasks;
using Lidgren.Network;
using MultiplayerSFS.Common;
using SFS;
using SFS.UI;
using SFS.Variables;
using SFS.World;
using SFS.WorldBase;
using UnityEngine;

namespace MultiplayerSFS.Mod
{
    public static class ClientManager
    {
        public static Bool_Local multiplayerEnabled = new Bool_Local() { Value = false };
        public static TcpClientTransport client;
        public static WorldState world;
        public static int playerId;
        private static readonly ControlOwnershipCoordinator controlOwnership = new ControlOwnershipCoordinator();

        // 断线重连：上次的连接信息 + 服务端发的 resume 凭据（服务端支持按 token 恢复身份）
        private static JoinInfo lastJoinInfo;
        private static string resumeToken = string.Empty;
        private static int resumePlayerId = -1;
        private static bool reconnecting;
        // 冷却期被挡下的重连请求。不能丢掉：那时 transport 往往已经 connected=false，
        // 丢掉就等于把客户端永久留在一个死会话上（玩家原话："服务器关闭客户端会卡死，概率性的"）。
        private static string pendingReconnectReason;
        // 防重连风暴：刚重连成功就又掉线时（服务端 supersede / 隧道抖动会连发 Disconnect），先冷静一会儿再动
        private static float lastReconnectSuccessTime = float.NegativeInfinity;

        public static async Task TryConnect(JoinInfo info)
        {
            Disconnect("Re-attempting join request");
            lastJoinInfo = info;
            Menu.loading.Open("Waiting for server response...");
            try
            {
                client = new TcpClientTransport();
                Packet_JoinResponse response = await client.ConnectAsync(info.address, info.port, new Packet_JoinRequest
                {
                    Username = info.username,
                    Password = info.password,
                    SolarSystemName = string.Empty
                });
                Menu.loading.Close();
                resumePlayerId = response.PlayerId;
                resumeToken = response.ResumeToken;
                LoadWorld(response);
            }
            catch (Exception ex)
            {
                // 【必须清掉世界进入的计时器】以前这里只 Menu.loading.Close()，却没清
                // LocalManager 的世界进入计时器 —— 于是这个计时器一直挂着，几十秒后
                // 在玩家已经进了建造场景/主菜单时突然触发世界进入重试，不停加载场景 = 卡顿。
                LocalManager.AbortWorldLoading();
                // 异常必须进日志：以前只 MsgDrawer 弹一下，一闪而过，日志里什么都查不到。
                UnityEngine.Debug.LogError("[SFS-MP] join failed: " + ex);
                MsgDrawer.main?.Log(ex.Message);
                Disconnect("Connection failed");
            }
        }

        // 断线自动重连。玩家没主动退出的情况下一直重试（1s/2s/3s/5s…），
        // 成功后带 resume token 恢复身份、刷新世界基准，继续留在当前场景里飞。
        public static void BeginReconnect(string reason)
        {
            if (reconnecting) return;
            if (UnityEngine.Time.unscaledTime - lastReconnectSuccessTime < 5f)
            {
                // 刚重连成功又被踢：立刻再连只会和服务端"supersede"互相踩（板上日志实证会形成风暴）。
                // 【但绝不能像以前那样空返回】——那时 transport 通常已经 connected=false，
                // 直接 return 会让客户端永久停在死会话上：世界冻住、没有任何提示，玩家只能重启。
                // 改成挂起，冷却一到立刻补一次重连。
                pendingReconnectReason = reason;
                UnityEngine.Debug.Log($"[SFS-MP] reconnect deferred (cooldown) reason={reason}");
                return;
            }
            UnityEngine.Debug.Log($"[SFS-MP] BeginReconnect reason={reason}");
            if (lastJoinInfo == null || lastJoinInfo.address == null)
            {
                // 没有可用的连接信息（例如从没成功连过）：退回旧行为
                SceneLoader.ExitToMainMenu();
                Disconnect("Disconnected by server");
                return;
            }
            reconnecting = true;
            _ = ReconnectLoop(reason);
        }

        // 冷却期挂起的重连：冷却一到就补发。由 UpdateNetwork 每帧调用（放在早退之前）。
        private static void TickPendingReconnect()
        {
            if (pendingReconnectReason == null) return;
            if (reconnecting) return;
            if (UnityEngine.Time.unscaledTime - lastReconnectSuccessTime < 5f) return;
            string reason = pendingReconnectReason;
            pendingReconnectReason = null;
            UnityEngine.Debug.Log($"[SFS-MP] reconnect resumed after cooldown reason={reason}");
            BeginReconnect(reason);
        }

        private static async Task ReconnectLoop(string reason)
        {
            const int giveUpAfterAttempts = 30;   // 约两分钟
            try
            {
                for (int attempt = 1; attempt <= giveUpAfterAttempts; attempt++)
                {
                    MsgDrawer.main?.Log($"Connection lost ({reason}) — reconnecting… #{attempt}");
                    UnityEngine.Debug.Log($"[SFS-MP] reconnect attempt #{attempt} (reason={reason})");
                    double waitSeconds = attempt <= 3 ? attempt : 5;
                    await Task.Delay(TimeSpan.FromSeconds(waitSeconds));
                    if (!reconnecting) return;

                    try
                    {
                        Disconnect("Reconnecting");
                        var transport = new TcpClientTransport();
                        Packet_JoinResponse response = await transport.ConnectAsync(
                            lastJoinInfo.address, lastJoinInfo.port, new Packet_JoinRequest
                            {
                                Username = lastJoinInfo.username,
                                Password = lastJoinInfo.password,
                                ResumePlayerId = resumePlayerId,
                                ResumeToken = resumeToken,
                            });
                        client = transport;
                        lastReconnectSuccessTime = UnityEngine.Time.unscaledTime;
                        pendingReconnectReason = null;
                        UnityEngine.Debug.Log($"[SFS-MP] reconnected ok (attempt {attempt})");
                        playerId = response.PlayerId;
                        resumePlayerId = response.PlayerId;
                        resumeToken = response.ResumeToken;
                        LocalManager.updateRocketsPeriod = response.UpdateRocketsPeriod;
                        // 只刷新世界时间基准，不重进场景：玩家还在飞，重新加载会把人踢出世界。
                        // 服务端握手后会重发初始状态，远端火箭随之重建。
                        world = new WorldState
                        {
                            initWorldTime = response.WorldTime,
                            difficulty = response.Difficulty
                        };
                        MsgDrawer.main?.Log("Reconnected.");
                        return;
                    }
                    catch (Exception ex)
                    {
                        if (attempt == giveUpAfterAttempts) MsgDrawer.main?.Log("Reconnect failed: " + ex.Message);
                    }
                }
            }
            finally
            {
                reconnecting = false;
            }
            // 认输：回到主菜单（旧行为）
            SceneLoader.ExitToMainMenu();
        }

        // 世界进入的重试参数。以前的兜底是「超时就强制关屏」，那会把「卡在加载屏」（看得见）
        // 变成「进去了但世界完全不更新」（看不见、玩家也没办法）。现在留在加载屏上重试。
        private const int WorldEntryMaxAttempts = 5;
        private static int worldEntryAttempts;
        // 重试要重放「把世界场景 load 起来」这一步，需要上次的 JoinResponse（难度等）。
        private static Packet_JoinResponse lastJoinResponse;

        public static void LoadWorld(Packet_JoinResponse response)
        {
            lastJoinResponse = response;
            worldEntryAttempts = 0;
            Menu.loading.Open("Loading multiplayer world...");
            // 记录时间：LocalManager.Update 会盯着这个时间，超时就自动重试世界进入。
            LocalManager.NotifyWorldLoadingStarted();
            controlOwnership.Clear();
            playerId = response.PlayerId;
            LocalManager.updateRocketsPeriod = response.UpdateRocketsPeriod;
            LocalManager.Initialize();
            // [已停用 2026-10-01] 聊天窗口不创建，冷却计时器没有 UI 可服务。
            // 保留调用：它只依赖服务端下发的 ChatMessageCooldown，恢复聊天时这里不用改。
            ChatWindow.CreateCooldownTimer(response.ChatMessageCooldown);
            world = new WorldState
            {
                initWorldTime = response.WorldTime,
                difficulty = response.Difficulty
            };
            EnterWorldScene();
        }

        // 真正把世界场景拉起来的那一步。单独抽出来是为了重试：只重跑这一步，
        // 不重建 ClientManager.world —— 重建会把已经收到的世界状态整份丢掉。
        private static void EnterWorldScene()
        {
            WorldSettings settings = new WorldSettings(
                new SolarSystemReference(""),
                new Difficulty() { difficulty = world.difficulty },
                new WorldMode(WorldMode.Mode.Sandbox) { allowQuicksaves = false },
                new WorldPlaytime(),
                new SandboxSettings.Data());
            typeof(WorldBaseManager).GetMethod("EnterWorld", BindingFlags.NonPublic | BindingFlags.Instance).Invoke(
                Base.worldBase,
                new object[] { null, settings, (Action)Base.sceneLoader.LoadHubScene });
        }

        // 世界进入超时：留在加载屏上重试。由 LocalManager.Update 每帧盯着计时器调用。
        public static void OnWorldEntryTimedOut()
        {
            UnityEngine.Debug.LogWarning($"[SFS-MP] world entry timed out (attempt {worldEntryAttempts + 1}): {DescribeWorldEntryState()}");
            if (lastJoinResponse == null)
            {
                // 没有可重放的加入信息（例如走了别的路径进来）：只能关屏，避免永久遮挡。
                UnityEngine.Debug.LogError("[SFS-MP] world entry timed out and there is no join info to replay; giving up.");
                LocalManager.AbortWorldLoading();
                return;
            }
            // 重试只在「玩家还等着进世界」时做。下面三种场景都说明"进世界这件事已经有结论了"：
            //   Hub_PC   = **连上之后的第一个页面，正常现象**。玩家从这里按正常流程开局时，
            //              才由 DivertLoading（Harmony patch GameManager.LoadPersistentAndLaunch）
            //              真正把联机世界接上。所以 Hub 绝不是失败。
            //   Home_PC  = 玩家回了主菜单（这次加入被放弃了）。
            //   Build_PC = 玩家去建造了。
            // 【1.3.0.4 的错误】当时只放过 Home/Build，把 Hub 当成失败 —— 于是每 10 秒重试一次，
            // 5 次之后把刚连上、还在 Hub 的玩家踢回主菜单并断线。实测日志：
            //   [SFS-MP] 世界进入超时（第 1..5 次）：scene=Hub_PC  ->  退回主菜单。
            // 继续重试还会往他正在用的场景里反复 invoke EnterWorld = 卡顿。这里一并修掉。
            string sceneNow = UnityEngine.SceneManagement.SceneManager.GetActiveScene().name;
            if (sceneNow == "Home_PC" || sceneNow == "Build_PC" || sceneNow == "Hub_PC")
            {
                UnityEngine.Debug.Log($"[SFS-MP] world entry timer finished (scene={sceneNow}, this is normal flow); stopping retries.");
                LocalManager.AbortWorldLoading();
                return;
            }
            worldEntryAttempts++;
            if (worldEntryAttempts >= WorldEntryMaxAttempts)
            {
                UnityEngine.Debug.LogError($"[SFS-MP] world entry failed {WorldEntryMaxAttempts} times in a row; returning to the main menu. {DescribeWorldEntryState()}");
                GiveUpWorldEntry();
                return;
            }
            Menu.loading.Open($"Loading multiplayer world... (retry {worldEntryAttempts}/{WorldEntryMaxAttempts})");
            LocalManager.NotifyWorldLoadingStarted();
            try
            {
                EnterWorldScene();
            }
            catch (Exception ex)
            {
                UnityEngine.Debug.LogError("[SFS-MP] exception while retrying world entry: " + ex);
                GiveUpWorldEntry();
            }
        }

        // 重试也救不回来：**只关掉加载屏并记日志，绝不把玩家踢下线。**
        // 【1.3.0.4 的错误】这里原来是 ExitToMainMenu() + Disconnect("World entry failed")：
        // 把"刚连上、还在 Hub"这个正常状态当成致命错误，50 秒后主动断开玩家。
        // 而"世界没从加入流程加载起来"并不代表这条连接没用 —— 玩家从 Hub 正常开局时，
        // DivertLoading 会把联机世界接上。所以这里只做清理，不做惩罚。
        private static void GiveUpWorldEntry()
        {
            LocalManager.AbortWorldLoading();
            UnityEngine.Debug.LogError("[SFS-MP] the world was not loaded through the join flow (keeping the current scene and connection; not exiting)."
                + DescribeWorldEntryState());
        }

        // 超时那一刻的关键状态。这条日志就是给「进去了但世界完全不更新」这类问题定位用的：
        // WorldTime.main=NULL 说明世界组件根本没建起来（注意：perf 行里的 gameTimeScale=-1
        // 也是这个意思 —— 那是 null 的哨兵值，不是时间倍率变成负数）。
        private static string DescribeWorldEntryState()
        {
            string scene = UnityEngine.SceneManagement.SceneManager.GetActiveScene().name;
            int rockets = world == null || world.rockets == null ? -1 : world.rockets.Count;
            int synced = LocalManager.syncedRockets == null ? -1 : LocalManager.syncedRockets.Count;
            return "scene=" + scene
                + " WorldTime.main=" + (WorldTime.main != null ? "ok" : "NULL")
                + " Player=" + (LocalManager.Player != null ? "ok" : "NULL")
                + " worldRockets=" + rockets
                + " synced=" + synced
                + " pending=" + LocalManager.PendingCreateCount;
        }

        public static void UpdateNetwork()
        {
            // 【必须放在早退之前】断线时 client 可能是 null，CanPumpNetworkPackets() 会直接返回，
            // 而"冷却期挂起的重连"恰恰只在那种状态下需要被补发。
            TickPendingReconnect();
            if (!CanPumpNetworkPackets()) return;
            TcpFrame frame;
            while (client.TryReceive(out frame))
            {
                if (frame.Kind == TcpFrameKind.Packet)
                {
                    HandlePacket(frame.Payload, frame.PayloadBits);
                }
                else if (frame.Kind == TcpFrameKind.Disconnect)
                {
                    // 【不要直接回主菜单】服务端判死（例如 10 秒收不到包）时会发这一帧，
                    // 网络抖一下就退出世界，玩家看到的是"别人的火箭突然全冻在原地"。
                    // 改成死命重连：成功就带 resume token 恢复身份继续玩，一直失败才认输。
                    BeginReconnect("server closed the session");
                    return;
                }
            }
        }

        // The network reader can enqueue initial state before LoadWorld finishes.
        // Leave those frames queued until every packet-handler dependency exists.
        private static bool CanPumpNetworkPackets()
        {
            return CanPumpNetworkPackets(
                client != null,
                world != null,
                LocalManager.players != null,
                LocalManager.syncedRockets != null,
                LocalManager.unsyncedRockets != null,
                LocalManager.updateAuthority != null);
        }

        private static bool CanPumpNetworkPackets(
            bool hasClient,
            bool hasWorld,
            bool hasPlayers,
            bool hasSyncedRockets,
            bool hasUnsyncedRockets,
            bool hasUpdateAuthority)
        {
            return hasClient && hasWorld && hasPlayers && hasSyncedRockets &&
                hasUnsyncedRockets && hasUpdateAuthority;
        }

        public static void RequestTimeScale(double scale)
        {
            if (client != null) SendPacket(new Packet_TimeWarp { Operation = TimeWarpOperation.Request, Multiplier = scale, WorldTime = world?.WorldTime ?? 0.0 });
        }

        // 【2026-10-06 已停用"实验性访问"整套机制】用户决定砍掉：权限分组以后并进密码系统。
        // 消息号 PacketType.ExperimentalAccess = 25 保留占位（消息号永不改），但两端都不再使用。

        // 最近一次服务端命令输出（debug 弹窗显示用）
        public static string LastServerCommandOutput { get; private set; } = string.Empty;

        // 发送一条服务端控制台命令。
        // 注意：本版本服务端**不校验身份**（用户决定先做出来；权限分组后续并进密码系统）。
        public static void RequestServerCommand(string commandLine)
        {
            if (client == null || string.IsNullOrWhiteSpace(commandLine)) return;
            LastServerCommandOutput = "(waiting for the server...)";
            SendPacket(new Packet_ServerCommand { Command = commandLine.Trim() });
        }

        // ---- 日志上传（debug 弹窗按钮）----
        public static string LastLogUploadStatus { get; private set; } = "-";

        private const int LogTailBytes = 4 * 1024 * 1024;   // 只取尾部，控制上传体积

        public static void UploadLog(string comment)
        {
            if (client == null || !client.Connected)
            {
                LastLogUploadStatus = "not connected, cannot upload";
                return;
            }
            try
            {
                string path = System.IO.Path.Combine(Application.persistentDataPath, "Player.log");
                if (!System.IO.File.Exists(path))
                {
                    LastLogUploadStatus = "Player.log not found";
                    return;
                }
                byte[] raw = ReadLogTail(path, LogTailBytes);
                byte[] gz;
                using (var ms = new System.IO.MemoryStream())
                {
                    using (var gzip = new System.IO.Compression.GZipStream(
                        ms, System.IO.Compression.CompressionMode.Compress, true))
                    {
                        gzip.Write(raw, 0, raw.Length);
                    }
                    gz = ms.ToArray();
                }
                SendPacket(new Packet_UploadLog
                {
                    FileName = "Player.log",
                    Comment = comment ?? string.Empty,
                    Data = gz
                });
                LastLogUploadStatus = string.Format("uploaded {0:F1} KB (raw {1:F1} KB)",
                    gz.Length / 1024.0, raw.Length / 1024.0);
                Debug.Log($"[SFS-MP] {LastLogUploadStatus}");
            }
            catch (Exception ex)
            {
                LastLogUploadStatus = "upload failed: " + ex.Message;
                Debug.LogWarning("[SFS-MP] log upload failed: " + ex);
            }
        }

        // 读日志尾部（游戏正在写这个文件，用 FileShare.ReadWrite 共享读）
        private static byte[] ReadLogTail(string path, int maxBytes)
        {
            using (var fs = new System.IO.FileStream(path, System.IO.FileMode.Open,
                       System.IO.FileAccess.Read, System.IO.FileShare.ReadWrite))
            {
                long length = fs.Length;
                long start = length > maxBytes ? length - maxBytes : 0;
                fs.Seek(start, System.IO.SeekOrigin.Begin);
                byte[] buffer = new byte[(int)(length - start)];
                int offset = 0;
                while (offset < buffer.Length)
                {
                    int read = fs.Read(buffer, offset, buffer.Length - offset);
                    if (read <= 0) break;
                    offset += read;
                }
                if (offset == buffer.Length) return buffer;
                byte[] trimmed = new byte[offset];
                Array.Copy(buffer, trimmed, offset);
                return trimmed;
            }
        }

        // 联机下绝不能让游戏被暂停：
        // SFS 的 ScreenManager 会把 Time.timeScale 置 0（打开带 PauseWhileOpen 的界面），
        // 而 DisablePausing 的 transpiler 删掉了原版在 Awake/OpenScreen 里的恢复赋值，
        // 它自己的恢复逻辑又只在「非联机」时执行 —— 于是联机下一次暂停就永久冻结：
        // 网络收发包、时间包全部正常，但画面/时间完全不动（Time.timeScale=0 时 deltaTime 恒为 0）。
        // 这里每帧兜底恢复，取值与游戏原版一致（WorldTime.main.TimeScale）。
        public static void EnforceRunningTimeScale()
        {
            if (!multiplayerEnabled.Value) return;
            // 游戏自己的时间轴也要兜底：WorldTime.TimeScale 直接就是 timewarpSpeed 字段，
            // 一旦为 -1，游戏会把它写进 Time.timeScale 并让 worldTime 朝负数积分
            //（现场表现：unityTimeScale=1.000 但 gameTimeScale=-1.000，世界时间不动）。
            // 联机模型是不做时间加速，所以这里把它拉回 1.0 + realtimePhysics。
            if (WorldTime.main != null && WorldTime.main.TimeScale <= 0f)
            {
                try { WorldTime.main.SetState(1.0, true, false); }
                catch (System.Exception ex) { UnityEngine.Debug.LogWarning("[SFS-MP] failed to restore the game time scale: " + ex.Message); }
            }
            if (Time.timeScale > 0f) return;
            Time.timeScale = (WorldTime.main != null && WorldTime.main.TimeScale > 0f)
                ? WorldTime.main.TimeScale
                : 1f;
        }

        // 控制请求的超时重发。Pending 一旦卡住，客户端再也不会为那枚火箭发包（Request 会直接吞掉），
        // 表现就是"某枚火箭永久接不进控制权，只能退出重进"。
        private const float ControlRequestTimeoutSeconds = 2f;
        // 【2026-10-06 提高上限 + 被拒后继续重试】
        // 玩家实测："联机时发射后不能控制火箭（不能切换到，发射新船也是脱离状态）"。
        // 机制：CanApplyNativeSelection 在"服务端确认"之前会**挡住原生切换**，所以确认没到 = 点不进那枚火箭。
        // 而服务端在"火箭不存在"时回的是拒绝（`拒绝控制: 原因=火箭不存在`）——
        // 那是**瞬态**的（CreateRocket 还在路上），可客户端一收到拒绝就 ClearPending 放弃了，
        // 于是玩家再也拿不到那枚新船。改成：被拒后按更长间隔继续重试，直到上限。
        private const int ControlRequestMaxTries = 12;                        // 原为 5
        private const float ControlRequestRejectedRetrySeconds = 3f;          // 被拒后的重试间隔（比超时的 2s 更慢）
        private static int controlRequestTries;
        private static float controlRequestSentTime;
        private static bool controlRequestRejected;

        public static void RequestPlayerControl(int rocketId, ControlRequestOrigin origin = ControlRequestOrigin.NativeSelection)
        {
            if (client == null || !client.Connected) return;   // 未连接时发送是静默丢弃，先挡住
            if (!controlOwnership.Request(rocketId, origin)) return;
            controlRequestTries = 1;
            controlRequestRejected = false;
            controlRequestSentTime = Time.unscaledTime;
            SendPacket(new Packet_UpdatePlayerControl { PlayerId = playerId, RocketId = rocketId });
        }

        // 由 LocalManager.Update 每帧调用：Pending 超过 2 秒没回执就重发（首包容易丢）
        public static void RetryPendingControlRequest()
        {
            if (client == null || !client.Connected) return;
            int pending = controlOwnership.PendingRocketId;
            if (pending < 0) return;
            float wait = controlRequestRejected ? ControlRequestRejectedRetrySeconds : ControlRequestTimeoutSeconds;
            if (Time.unscaledTime - controlRequestSentTime < wait) return;
            if (controlRequestTries >= ControlRequestMaxTries)
            {
                Debug.LogWarning($"[SFS-MP] control request resent {controlRequestTries} times with no reply; giving up (rocketId={pending})");
                controlOwnership.ClearPending();   // 清掉，避免永久卡死导致该火箭再也点不进
                return;
            }
            controlRequestTries++;
            controlRequestSentTime = Time.unscaledTime;
            Debug.Log($"[SFS-MP] no reply to the control request; resending (try {controlRequestTries}, rocketId={pending})");
            SendPacket(new Packet_UpdatePlayerControl { PlayerId = playerId, RocketId = pending });
        }

        public static void ApplyConfirmedLocalPlayerControl(int rocketId)
        {
            controlOwnership.ApplyServerConfirmation(rocketId);
            LocalPlayer localPlayer = LocalManager.Player;
            if (localPlayer != null) localPlayer.controlledRocket.Value = rocketId;

            if (PlayerController.main == null) return;
            if (rocketId < 0)
            {
                PlayerController.main.player.Value = null;
                return;
            }
            if (LocalManager.syncedRockets == null ||
                !LocalManager.syncedRockets.TryGetValue(rocketId, out LocalRocket localRocket) ||
                localRocket.rocket == null ||
                ReferenceEquals(PlayerController.main.player.Value, localRocket.rocket))
                return;

            // The native control request was deliberately deferred until the server
            // confirmed ownership. Re-enter it only after the local ownership record
            // reflects that confirmation, so the selection patch permits the switch.
            PlayerController.main.SmoothChangePlayer(localRocket.rocket);
        }

        public static void Disconnect(string reason)
        {
            // 断线不清空作弊策略：保留服务端最后一次下发的限制（会在日志里说明）。
            CheatPolicy.OnDisconnected();
            controlOwnership.Clear();
            LastServerCommandOutput = string.Empty;
            if (client == null) return;
            client.Disconnect(reason);
            client.Dispose();
            client = null;
        }

        public static void Disconnect()
        {
            Disconnect("Disconnect requested");
        }

        public static void HandlePacket(byte[] data, int bitLength)
        {
            var msg = NetPayloadCodec.ToIncoming(data, bitLength);
            PacketType packetType = (PacketType) msg.ReadByte();
            DebugTrace.Inbound(packetType.ToString(), data.Length);
            if (Packet.ShouldDebug(packetType))
                Debug.Log($"Recieved packet of type {packetType}.");
            switch (packetType)
            {
                // * Player/server Info Packets
                case PacketType.PlayerConnected:
                    OnPacket_PlayerConnected(msg);
                    break;
                case PacketType.PlayerDisconnected:
                    OnPacket_PlayerDisconnected(msg);
                    break;
                case PacketType.UpdatePlayerControl:
                    OnPacket_UpdatePlayerControl(msg);
                    break;
                case PacketType.UpdatePlayerAuthority:
                    OnPacket_UpdatePlayerAuthority(msg);
                    break;
                case PacketType.UpdateWorldTime:
                    OnPacket_UpdateWorldTime(msg);
                    break;
                case PacketType.UpdatePlayerColor:
                    OnPacket_UpdatePlayerColor(msg);
                    break;
                case PacketType.SendChatMessage:
                    OnPacket_SendChatMessage(msg);
                    break;
                case PacketType.TimeWarp:
                    TimeWarpVoteUI.Receive(msg.Read<Packet_TimeWarp>());
                    break;
                // 25（ExperimentalAccess）已停用：服务端不再发送，收到也当未知包忽略。
                // 删掉了它的处理器而不是留着，是为了让"已停用"在代码里就是事实，而不是靠一句注释。
                case PacketType.P2PPeerOffer:
                    P2PConnectionManager.ApplyOffer(msg.Read<Packet_P2PPeerOffer>());
                    break;
                // 22（UpdateCheatStatus）从"非法空位"改成服务端 -> 客户端的作弊策略下发。
                // 客户端只接收并应用；未收到前 CheatPolicy.HasPolicy == false，行为与以前一致。
                case PacketType.UpdateCheatStatus:
                    CheatPolicy.Receive(msg.Read<Packet_UpdateCheatStatus>());
                    break;
                case PacketType.ServerCommand:
                    {
                        var commandResult = msg.Read<Packet_ServerCommand>();
                        LastServerCommandOutput = string.IsNullOrEmpty(commandResult.Output)
                            ? "(server returned empty output)"
                            : commandResult.Output;
                        // 同时打进日志 —— 游戏原生的 F1 控制台会把 Unity 日志显示出来，
                        // 这就是命令回包在控制台里的呈现路径。
                        ServerConsoleBridge.WriteReply(LastServerCommandOutput);
                        break;
                    }

                // * Rocket Packets
                case PacketType.CreateRocket:
                    OnPacket_CreateRocket(msg);
                    break;
                case PacketType.DestroyRocket:
                    OnPacket_DestroyRocket(msg);
                    break;
                case PacketType.DockTransaction:
                    // 【2026-10-10 修复】以前这里没有这个 case：服务端权威算好的对接/分离结果被永久丢弃，
                    // 两边各自保留对接前的零件表 -> 拆完/撞完之后永远不同步。
                    OnPacket_DockTransaction(msg);
                    break;
                case PacketType.UpdateRocketPrimary:
                    OnPacket_UpdateRocketPrimary(msg);
                    break;
                case PacketType.UpdateRocketSecondary:
                    OnPacket_UpdateRocketSecondary(msg);
                    break;

                // * Part & Staging Packets
                case PacketType.DestroyPart:
                    OnPacket_DestroyPart(msg);
                    break;
                case PacketType.UpdateStaging:
                    OnPacket_UpdateStaging(msg);
                    break;
                case PacketType.UpdatePart_EngineModule:
                    OnPacket_UpdatePart_EngineModule(msg);
                    break;
                case PacketType.UpdatePart_WheelModule:
                    OnPacket_UpdatePart_WheelModule(msg);
                    break;
                case PacketType.UpdatePart_BoosterModule:
                    OnPacket_UpdatePart_BoosterModule(msg);
                    break;
                case PacketType.UpdatePart_ParachuteModule:
                    OnPacket_UpdatePart_ParachuteModule(msg);
                    break;
                case PacketType.UpdatePart_MoveModule:
                    OnPacket_UpdatePart_MoveModule(msg);
                    break;
                case PacketType.UpdatePart_ResourceModule:
                    OnPacket_UpdatePart_ResourceModule(msg);
                    break;

                // * Invalid Packets
                case PacketType.JoinResponse:
                    Debug.LogWarning($"Recieved server info packet outside of connection attempt.");
                    break;
                case PacketType.JoinRequest:
                    Debug.LogWarning($"Recieved packet (of type {packetType}) intended for the server.");
                    break;
                default:
                    Debug.LogWarning($"Unhandled packet type ({packetType})!");
                    break;
            }
        }

        public static void SendPacket(Packet packet)
        {
            // 守卫：Disconnect 会把 client 置空，而被踢/掉线时场景卸载钩子仍会发包
            // （曾因此每次退出世界必抛 NullReferenceException）。
            if (packet == null || client == null || !client.Connected) return;
            DebugTrace.Outbound(packet.Type.ToString(), 0);
            if (Packet.ShouldDebug(packet.Type))
                Debug.Log($"Sending packet of type {packet.Type}.");

            client.Send(packet);
        }

        public static void SendPacket(Packet packet, NetDeliveryMethod _)
        {
            SendPacket(packet);
        }
        static void OnPacket_PlayerConnected(NetIncomingMessage msg)
        {
            Packet_PlayerConnected packet = msg.Read<Packet_PlayerConnected>();
            if (LocalManager.players != null)
            {
                // 用索引器而不是 Add：断线重连 / 重复广播时同一 player 会再来一次，
                // Add 会抛 ArgumentException（曾触发崩溃报告上传）
                LocalManager.players[packet.PlayerId] = new LocalPlayer(packet.Username, packet.IconColor);
            }
            if (packet.PrintMessage)
            {
                string message = $"{packet.Username} connected";
                MsgDrawer.main.Log(message);
                ChatWindow.AddMessage(new ChatMessage(message));
            }
        }

        static void OnPacket_PlayerDisconnected(NetIncomingMessage msg)
        {
            Packet_PlayerDisconnected packet = msg.Read<Packet_PlayerDisconnected>();
            if (LocalManager.players.TryGetValue(packet.PlayerId, out LocalPlayer player))
            {
                string message = $"{player.username} disconnected";
                MsgDrawer.main.Log(message);
                ChatWindow.AddMessage(new ChatMessage(message));
                LocalManager.players.Remove(packet.PlayerId);
            }
        }

        static void OnPacket_UpdatePlayerControl(NetIncomingMessage msg)
        {
            Packet_UpdatePlayerControl packet = msg.Read<Packet_UpdatePlayerControl>();
            if (LocalManager.players.TryGetValue(packet.PlayerId, out LocalPlayer player))
            {
                player.controlledRocket.Value = packet.RocketId;
            }
            else
            {
                Debug.LogError("Missing player while trying to update controlled rocket!");
            }
            if (packet.PlayerId != playerId) return;

            // 服务端用同一种包回"批准"与"拒绝"（拒绝时回的是你当前实际控制的目标）。
            // 只有与本次请求目标一致的才算批准；否则无条件确认会把自己从正在开的火箭里踢出去，
            // 而且 Confirmed 被写成服务端的值后，重发/重试会被守卫吞掉（该火箭再也点不进去）。
            int pending = controlOwnership.PendingRocketId;
            if (pending >= 0 && packet.RocketId != pending)
            {
                // 【不要立刻放弃】服务端这里的拒绝有两种原因：火箭不存在（瞬态，CreateRocket 还在路上）
                // 或已被别人控制。前者只要再等一会儿就能拿到，而"放弃"会让玩家永久点不进那枚新船
                //（原生切换被 CanApplyNativeSelection 卡在"必须已确认"）。
                // 改成标记为"被拒"，由 RetryPendingControlRequest 按更长间隔继续重试到上限。
                Debug.Log($"[SFS-MP] control request rejected: requested={pending}, server still has={packet.RocketId} (will retry every 3s)");
                controlRequestRejected = true;
                controlRequestSentTime = Time.unscaledTime;
                return;
            }

            ApplyConfirmedLocalPlayerControl(packet.RocketId);
        }

        static void OnPacket_UpdatePlayerAuthority(NetIncomingMessage msg)
        {
            Packet_UpdatePlayerAuthority packet = msg.Read<Packet_UpdatePlayerAuthority>();
            LocalManager.updateAuthority = packet.RocketIds ?? new System.Collections.Generic.HashSet<int>();

            foreach (int id in LocalManager.updateAuthority)
            {
                if (LocalManager.syncedRockets.TryGetValue(id, out LocalRocket rocket))
                {

                }
            }
        }

        static void OnPacket_UpdateWorldTime(NetIncomingMessage msg)
        {
            Packet_UpdateWorldTime packet = msg.Read<Packet_UpdateWorldTime>();
            if (WorldTime.main == null) return;
            // 【个人加速期间绝不能覆盖本地时间】服务端时钟是 1x 的，它盖的章当然也是 1x 的时间；
            // 每 2 秒硬覆盖一次，就等于把玩家自己快进获得的时间反复抹掉（"飞往火星白飞"）。
            // 只有在本地倍率回到 1x 时才继续用心跳校准。
            // 【读真实值，别用缓存】以前读 TimeWarpVoteUI 里缓存的 LocalTimeScale —— 那只覆盖"经过我们
            // 这条路"的加速；而地图上的 Timewarp Here 是游戏自己写 timewarpSpeed，缓存会失真。
            // 一旦失真，心跳就会每 2 秒把玩家快进出来的时间拉回去。直接读 WorldTime.main.timewarpSpeed 才是真相。
            if (WorldTime.main.timewarpSpeed != 1.0) return;
            WorldTime.main.worldTime = packet.WorldTime;
        }

        static void OnPacket_UpdatePlayerColor(NetIncomingMessage msg)
        {
            Packet_UpdatePlayerColor packet = msg.Read<Packet_UpdatePlayerColor>();
            if (LocalManager.players.TryGetValue(packet.PlayerId, out LocalPlayer player))
            {
                player.iconColor = packet.Color;
                ChatWindow.OnPlayerColorChange(packet.PlayerId, packet.Color);
            }
        }

        static void OnPacket_SendChatMessage(NetIncomingMessage msg)
        {
            Packet_SendChatMessage packet = msg.Read<Packet_SendChatMessage>();
            ChatWindow.AddMessage(new ChatMessage(packet.Message, packet.SenderId));
        }

        static void OnPacket_CreateRocket(NetIncomingMessage msg)
        {
            Packet_CreateRocket packet = msg.Read<Packet_CreateRocket>();
            // 回执（LocalId >= 0）在本地已有该火箭时不要覆盖：那是客户端重发带回来的"创建那一刻"旧状态，
            // 覆盖会让火箭瞬移/贴回发射台。权威覆盖包（LocalId < 0，服务端快照/状态覆盖）照常覆盖。
            if (packet.LocalId < 0 || !world.rockets.ContainsKey(packet.GlobalId))
                world.rockets[packet.GlobalId] = packet.Rocket;
            LocalManager.OnPacket_CreateRocket(packet);
        }

        static void OnPacket_DestroyRocket(NetIncomingMessage msg)
        {
            Packet_DestroyRocket packet = msg.Read<Packet_DestroyRocket>();
            world.rockets.Remove(packet.RocketId);
            LocalManager.TrueDestructionReason = packet.Reason;
            LocalManager.DestroyLocalRocket(packet.RocketId);
        }

        static void OnPacket_UpdateRocketPrimary(NetIncomingMessage msg)
        {
            Packet_UpdateRocketPrimary packet = msg.Read<Packet_UpdateRocketPrimary>();
            // 【单源仲裁 2026-10-10】这枚火箭正被 P2P 喂 -> 丢弃中继副本。
            // 两条路不能同时喂：它们的 WorldTime 分属不同时钟轴（中继那份是服务端重盖的），
            // 交替喂给插值器就是当年"位置来回飘移"的来源。
            if (P2PConnectionManager.HasFreshState(packet.RocketId)) return;
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState state))
            {
                state.UpdateRocketPrimary(packet);
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
            else
            {
                Debug.Log("Missing rocket from world state!!!");
            }
        }

        static void OnPacket_DockTransaction(NetIncomingMessage msg)
        {
            Packet_DockTransaction packet = msg.Read<Packet_DockTransaction>();
            // 【2026-10-10 子 agent 定位】客户端以前【根本没有这个分支】—— 服务端权威算出的
            // 对接/分离结果（MergedRocket / SecondRocket）被永久丢弃，这就是"拆完/撞完永远不同步"。
            // 这里刻意【不做本地推演】（推演正是分叉来源），只按权威结果补快照，
            // 交给"编号集合不一致 -> 整枚重建"那条路径收敛。
            Debug.Log("[SFS-MP] dock transaction committed=" + packet.Committed
                + " keep=" + packet.KeepRocketId + " remove=" + packet.RemoveRocketId + " second=" + packet.SecondRocketId);
            if (!packet.Committed) return;
            ClientManager.RequestRocketSnapshot(packet.KeepRocketId);
            if (packet.RemoveRocketId >= 0) ClientManager.RequestRocketSnapshot(packet.RemoveRocketId);
            if (packet.SecondRocketId >= 0) ClientManager.RequestRocketSnapshot(packet.SecondRocketId);
        }

        static void OnPacket_UpdateRocketSecondary(NetIncomingMessage msg)
        {
            Packet_UpdateRocketSecondary packet = msg.Read<Packet_UpdateRocketSecondary>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState state))
            {
                state.UpdateRocketSecondary(packet);
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_DestroyPart(NetIncomingMessage msg)
        {
            Packet_DestroyPart packet = msg.Read<Packet_DestroyPart>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState state))
            {
                state.RemovePart(packet.PartId);
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdateStaging(NetIncomingMessage msg)
        {
            Packet_UpdateStaging packet = msg.Read<Packet_UpdateStaging>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState state))
            {
                state.stages = packet.Stages;
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_EngineModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_EngineModule packet = msg.Read<Packet_UpdatePart_EngineModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                if (rocketState.parts.TryGetValue(packet.PartId, out PartState partState))
                {
                    partState.part.TOGGLE_VARIABLES["engine_on"] = packet.EngineOn;
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_WheelModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_WheelModule packet = msg.Read<Packet_UpdatePart_WheelModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                if (rocketState.parts.TryGetValue(packet.PartId, out PartState partState))
                {
                    partState.part.TOGGLE_VARIABLES["wheel_on"] = packet.WheelOn;
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_BoosterModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_BoosterModule packet = msg.Read<Packet_UpdatePart_BoosterModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                if (rocketState.parts.TryGetValue(packet.PartId, out PartState partState))
                {
                    partState.part.NUMBER_VARIABLES["fuel_percent"] = packet.FuelPercent;
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_ParachuteModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_ParachuteModule packet = msg.Read<Packet_UpdatePart_ParachuteModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                if (rocketState.parts.TryGetValue(packet.PartId, out PartState partState))
                {
                    partState.part.NUMBER_VARIABLES["animation_state"] = packet.State;
                    partState.part.NUMBER_VARIABLES["deploy_state"] = packet.TargetState;
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_MoveModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_MoveModule packet = msg.Read<Packet_UpdatePart_MoveModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                if (rocketState.parts.TryGetValue(packet.PartId, out PartState partState))
                {
                    partState.part.NUMBER_VARIABLES["state"] = packet.Time;
                    partState.part.NUMBER_VARIABLES["state_target"] = packet.TargetTime;
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        static void OnPacket_UpdatePart_ResourceModule(NetIncomingMessage msg)
        {
            Packet_UpdatePart_ResourceModule packet = msg.Read<Packet_UpdatePart_ResourceModule>();
            if (world.rockets.TryGetValue(packet.RocketId, out RocketState rocketState))
            {
                foreach (int partId in packet.PartIds)
                {
                    if (rocketState.parts.TryGetValue(partId, out PartState partState))
                    {
                        // TODO! A lot of these save variable names will most likely be different for non-vanilla parts, but currently idk what the best way to properly get them is.
                        // TODO! I might need some form of register that associates a part's name and module variable names to their save variable names.
                        partState.part.NUMBER_VARIABLES["fuel_percent"] = packet.ResourcePercent;
                    }
                }
                Interpolator.AddPacketToQueue(packet, packet.RocketId, packet.WorldTime);
            }
        }

        public static void RequestRocketSnapshot(int rocketId)
        {
            if (rocketId >= 0) client?.RequestRocketSnapshot(rocketId);
        }
    }
}