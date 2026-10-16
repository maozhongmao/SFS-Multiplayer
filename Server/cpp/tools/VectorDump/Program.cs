// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 黄金向量导出器：用真实 Lidgren 生成位级标准答案，供 C++ 侧 conformance 测试逐字节比对。
// 目的：C++ 服务端必须与现有 SFS 客户端字节级兼容，此工具是唯一权威依据，不做任何猜测。
using System.Globalization;
using System.Reflection;
using System.Text;
using Lidgren.Network;
using SfsMultiplayer.Protocol;

namespace VectorDump;

internal static class Program
{
    private static readonly List<string> PrimitiveLines = new();
    private static readonly List<string> SequenceLines = new();
    // 线格式向量：帧/信封/握手/服务器探测，格式 <分组>|<名称>|<位数>|<HEX>
    private static readonly List<string> WireLines = new();

    // 与服务端 NetPayloadCodec 一致：使用非公开无参构造，缓冲区由首次写入惰性分配。
    // 该 Lidgren 版本(2012.1.7.0)的构造函数带 NetPeer 参数，不能直接用。
    private static ConstructorInfo _outCtor;

    private static NetOutgoingMessage NewOut(int capacity = 4096)
    {
        if (_outCtor == null)
        {
            _outCtor = typeof(NetOutgoingMessage).GetConstructor(
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance,
                null, Type.EmptyTypes, null);
            if (_outCtor == null)
            {
                var available = string.Join(", ", typeof(NetOutgoingMessage)
                    .GetConstructors(BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance)
                    .Select(c => $"({string.Join(",", c.GetParameters().Select(p => p.ParameterType.Name))})"));
                throw new InvalidOperationException($"找不到 NetOutgoingMessage 无参构造函数。可用: {available}");
            }
        }
        return (NetOutgoingMessage)_outCtor.Invoke(null);
    }

    private static NetIncomingMessage ToIncoming(byte[] data, int bitLength)
    {
        var ctor = typeof(NetIncomingMessage).GetConstructor(
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null, Type.EmptyTypes, null);
        if (ctor == null) throw new InvalidOperationException("找不到 NetIncomingMessage() 构造函数。");
        var msg = (NetIncomingMessage)ctor.Invoke(null);
        msg.Data = (byte[])data.Clone();
        msg.LengthBits = bitLength;
        msg.Position = 0;
        return msg;
    }

    private static string Hex(NetOutgoingMessage m) =>
        Convert.ToHexString(m.Data.AsSpan(0, m.LengthBytes));

    private static void Prim(string name, Action<NetOutgoingMessage> write)
    {
        var m = NewOut();
        write(m);
        PrimitiveLines.Add($"{name}|{m.LengthBits}|{Hex(m)}");
    }

    private static void Seq(string name, Action<NetOutgoingMessage> write)
    {
        var m = NewOut();
        write(m);
        SequenceLines.Add($"{name}|{m.LengthBits}|{Hex(m)}");
    }

    private static string Str64(string s) => Convert.ToBase64String(Encoding.UTF8.GetBytes(s));

    private static string F32(float v)
    {
        if (float.IsNaN(v)) return "NaN";
        if (float.IsPositiveInfinity(v)) return "Infinity";
        if (float.IsNegativeInfinity(v)) return "-Infinity";
        return v.ToString("R", CultureInfo.InvariantCulture);
    }

    private static string F64(double v)
    {
        if (double.IsNaN(v)) return "NaN";
        if (double.IsPositiveInfinity(v)) return "Infinity";
        if (double.IsNegativeInfinity(v)) return "-Infinity";
        return v.ToString("R", CultureInfo.InvariantCulture);
    }

    private static int Main(string[] args)
    {
        var outDir = args.Length > 0 ? args[0] : "golden";
        Directory.CreateDirectory(outDir);

        var asm = typeof(NetOutgoingMessage).Assembly;
        Console.WriteLine($"Lidgren 程序集: {asm.FullName}");
        Console.WriteLine($"位置: {asm.Location}");

        // ---------- 标量原语 ----------
        Prim("bool.false", m => m.Write(false));
        Prim("bool.true", m => m.Write(true));

        foreach (var v in new[] { 0, 1, -1, 12345, -12345, int.MinValue, int.MaxValue, 255, 256, -256 })
            Prim($"i32.{v}", m => m.Write(v));

        foreach (var v in new uint[] { 0u, 1u, 4294967295u, 2147483648u, 65535u })
            Prim($"u32.{v}", m => m.Write(v));

        foreach (var v in new byte[] { 0, 1, 127, 255 })
            Prim($"u8.{v}", m => m.Write(v));

        foreach (var v in new short[] { 0, 1, -1, short.MinValue, short.MaxValue })
            Prim($"i16.{v}", m => m.Write(v));

        foreach (var v in new long[] { 0L, 1L, -1L, long.MinValue, long.MaxValue, 1234567890123L })
            Prim($"i64.{v}", m => m.Write(v));

        foreach (var v in new[] { 0f, 1f, -1f, 1.5f, -1.5f, 0.1f, 3.1415927f,
                                  float.MinValue, float.MaxValue, float.Epsilon,
                                  float.NaN, float.PositiveInfinity, float.NegativeInfinity })
            Prim($"f32.{F32(v)}", m => m.Write(v));

        foreach (var v in new[] { 0d, 1d, -1d, 1.5d, -1.5d, 0.1d, Math.PI,
                                  double.MinValue, double.MaxValue, double.Epsilon,
                                  double.NaN, double.PositiveInfinity, double.NegativeInfinity })
            Prim($"f64.{F64(v)}", m => m.Write(v));

        // ---------- 字符串（Lidgren Write(string) 的真实格式） ----------
        foreach (var s in new[]
                 {
                     "", "a", "hello world", "羊城通", "maozhongmao",
                     new string('x', 127), new string('y', 128), new string('z', 300),
                     "Earth", "换行\n制表\t引号\"反斜杠\\", "emoji 🚀 混合 ASCII"
                 })
            Prim($"str.{Str64(s)}", m => m.Write(s));

        // ---------- 字节数组 ----------
        Prim("bytes.empty", m => m.Write(Array.Empty<byte>()));
        Prim("bytes.5", m => m.Write(new byte[] { 0, 1, 2, 253, 254 }));
        Prim("bytes.300", m =>
        {
            var buf = new byte[300];
            for (var i = 0; i < buf.Length; i++) buf[i] = (byte)(i & 0xFF);
            m.Write(buf);
        });

        // ---------- 位偏移序列（证明位级打包行为） ----------
        Seq("seq.bool1_then_i32", m => { m.Write(true); m.Write(0x12345678); });
        Seq("seq.bool3_then_f32", m => { m.Write(true); m.Write(false); m.Write(true); m.Write(1.5f); });
        Seq("seq.bool_then_str", m => { m.Write(true); m.Write("hello"); });
        Seq("seq.bool_then_bytes", m => { m.Write(true); m.Write(new byte[] { 0xAA, 0xBB }); });
        Seq("seq.i32_f32_f64", m => { m.Write(7); m.Write(2.5f); m.Write(3.25d); });
        Seq("seq.many_bools", m =>
        {
            for (var i = 0; i < 17; i++) m.Write(i % 3 == 0);
        });
        Seq("seq.u8_bool_u8", m => { m.Write((byte)200); m.Write(true); m.Write((byte)201); });

        // ---------- 复合：真实协议里的形状 ----------
        // NetLocation = double X, double Y, double Vx, double Vy, string Address
        Seq("seq.netlocation", m =>
        {
            m.Write(1.5d); m.Write(-2.25d); m.Write(0.125d); m.Write(1000.5d); m.Write("Earth");
        });

        // Color3 = float R, G, B
        Seq("seq.color3", m => { m.Write(0.2f); m.Write(0.8f); m.Write(1f); });

        // Server 版 WriteCollection：int32 计数 + 项目
        Seq("seq.collection_i32x3", m =>
        {
            m.Write(3);
            m.Write(10); m.Write(20); m.Write(30);
        });

        // 64 位非对齐回归（read_bits_le nbits=64 + shift>0 的 UBN 修复后回归）
        Seq("seq.bool_then_f64", m => { m.Write(true); m.Write(12345.678d); });
        Seq("seq.i32_then_f64", m => { m.Write(42); m.Write(3.14159d); });
        Seq("seq.bool_then_i64", m => { m.Write(true); m.Write(123456789012345L); });

        // ---------- 解析回读自检：编码后再解码，确认值一致 ----------
        var roundTripFailures = new List<string>();
        void Check<T>(string name, T value, Action<NetOutgoingMessage, T> write, Func<NetIncomingMessage, T> read)
        {
            var m = NewOut();
            write(m, value);
            var back = read(ToIncoming(m.Data, m.LengthBits));
            if (!EqualityComparer<T>.Default.Equals(back, value))
                roundTripFailures.Add($"{name}: {value} -> {back}");
        }

        Check("rt.i32", -1234567, (m, v) => m.Write(v), m => m.ReadInt32());
        Check("rt.u32", 4000000000u, (m, v) => m.Write(v), m => m.ReadUInt32());
        Check("rt.f32", 1.5f, (m, v) => m.Write(v), m => m.ReadFloat());
        Check("rt.f64", -2.75d, (m, v) => m.Write(v), m => m.ReadDouble());
        Check("rt.bool", true, (m, v) => m.Write(v), m => m.ReadBoolean());
        Check("rt.u8", (byte)201, (m, v) => m.Write(v), m => m.ReadByte());
        Check("rt.str", "羊城通 rocket", (m, v) => m.Write(v), m => m.ReadString());

        PrimitiveLines.Add($"#roundtrip_failures={roundTripFailures.Count}");
        foreach (var f in roundTripFailures) PrimitiveLines.Add($"#FAIL {f}");

        // ---------- 线格式向量：TCP 帧 / UDP 信封 / 握手 / 服务器探测 ----------
        // 全部调用真实的协议实现（TcpFrameCodec / SessionHandshakeCodec / ServerInfoWire），
        // 唯一例外是 UDP 信封——服务端把它内联在 UdpStateTransport.Send 私有方法里，
        // 这里按源码原样转写，标注为转写而非调用。
        void Wire(string group, string name, byte[] bytes, int bits) =>
            WireLines.Add($"{group}|{name}|{bits}|{Convert.ToHexString(bytes)}");

        (byte[] data, int bits) PayloadBytes(Action<NetOutgoingMessage> write)
        {
            var m = NewOut();
            write(m);
            return (m.Data.AsSpan(0, m.LengthBytes).ToArray(), m.LengthBits);
        }

        void WireFrame(string name, TcpFrameKind kind, int seq, byte[] payload, int bits) =>
            Wire("tcpframe", name, TcpFrameCodec.Encode(new TcpFrame(kind, seq, payload, bits)), bits);

        var netloc = PayloadBytes(m => { m.Write(1.5d); m.Write(-2.25d); m.Write(0.125d); m.Write(1000.5d); m.Write("Earth"); });
        var bitOdd = PayloadBytes(m => { m.Write(true); m.Write(0x12345678); }); // 33 位，非字节对齐
        var bigPayload = PayloadBytes(m =>
        {
            var buf = new byte[300];
            for (var i = 0; i < buf.Length; i++) buf[i] = (byte)(i & 0xFF);
            m.Write(buf);
        });

        // 每个 TcpFrameKind 都必须在向量里出现，否则 C++ 侧漏实现不会被发现。
        WireFrame("hello.empty", TcpFrameKind.Hello, 0, Array.Empty<byte>(), 0);
        WireFrame("hello.netlocation", TcpFrameKind.Hello, 0, netloc.data, netloc.bits);
        WireFrame("helloack.empty", TcpFrameKind.HelloAck, 0, Array.Empty<byte>(), 0);
        WireFrame("packet.seq0", TcpFrameKind.Packet, 0, netloc.data, netloc.bits);
        WireFrame("packet.seq12345", TcpFrameKind.Packet, 12345, netloc.data, netloc.bits);
        WireFrame("packet.seq_min", TcpFrameKind.Packet, int.MinValue, netloc.data, netloc.bits);
        WireFrame("packet.bitodd33", TcpFrameKind.Packet, 3, bitOdd.data, bitOdd.bits);
        WireFrame("packet.payload300", TcpFrameKind.Packet, 300, bigPayload.data, bigPayload.bits);
        WireFrame("ping.seq_max", TcpFrameKind.Ping, int.MaxValue, Array.Empty<byte>(), 0);
        WireFrame("pong.seq_min", TcpFrameKind.Pong, int.MinValue, Array.Empty<byte>(), 0);
        WireFrame("disconnect.seq_neg1", TcpFrameKind.Disconnect, -1, Array.Empty<byte>(), 0);
        WireFrame("request_world_snapshot.empty", TcpFrameKind.RequestWorldSnapshot, 7, Array.Empty<byte>(), 0);
        WireFrame("request_rocket_snapshot.empty", TcpFrameKind.RequestRocketSnapshot, 8, Array.Empty<byte>(), 0);
        WireFrame("server_info_request.empty", TcpFrameKind.ServerInfoRequest, 0, Array.Empty<byte>(), 0);
        var serverInfo = ServerInfoWire.EncodeResponse(3, 16);
        WireFrame("server_info_response.3of16", TcpFrameKind.ServerInfoResponse, 0, serverInfo, serverInfo.Length * 8);
        var serverInfoEmpty = ServerInfoWire.EncodeResponse(0, 1);
        WireFrame("server_info_response.0of1", TcpFrameKind.ServerInfoResponse, 0, serverInfoEmpty, serverInfoEmpty.Length * 8);

        // UDP 信封（转写自 UdpStateTransport.Send）：[kind:1][tokenLen:1][token UTF-8][payload]
        byte[] UdpEnvelope(byte kind, string token, byte[] payload)
        {
            var tokenBytes = Encoding.UTF8.GetBytes(token);
            var data = new byte[2 + tokenBytes.Length + payload.Length];
            data[0] = kind;
            data[1] = (byte)tokenBytes.Length;
            Buffer.BlockCopy(tokenBytes, 0, data, 2, tokenBytes.Length);
            Buffer.BlockCopy(payload, 0, data, 2 + tokenBytes.Length, payload.Length);
            return data;
        }

        Wire("udp", "bind.tok3_empty", UdpEnvelope(1, "tok", Array.Empty<byte>()), 0);
        Wire("udp", "bind.emptytoken", UdpEnvelope(1, "", Array.Empty<byte>()), 0);
        Wire("udp", "bindack.token32", UdpEnvelope(2, new string('a', 32), Array.Empty<byte>()), 0);
        Wire("udp", "data.emptytoken_payload", UdpEnvelope(3, "", netloc.data), netloc.bits);
        Wire("udp", "data.token255_payload", UdpEnvelope(3, new string('b', 255), netloc.data), netloc.bits);
        Wire("udp", "data.utf8token_cn_payload", UdpEnvelope(3, "羊城通", netloc.data), netloc.bits);

        // 握手 Hello（BinaryWriter 小端，字符串 = int32 字节长度 + UTF-8）
        var helloBasic = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket
        {
            Username = "羊城通",
            Password = "",
            SolarSystemName = "Earth",
            ResumePlayerId = -1,
            ResumeToken = "",
        });
        Wire("handshake", "hello.basic", helloBasic, helloBasic.Length * 8);

        var helloResume = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket
        {
            Username = "maozhongmao",
            Password = "pw-123",
            SolarSystemName = "Jupiter",
            ResumePlayerId = 42,
            ResumeToken = "resume-token-abc",
        });
        Wire("handshake", "hello.resume", helloResume, helloResume.Length * 8);

        // 握手 Ack（比 Lidgren 版 JoinResponsePacket 多一个 ResumeToken 字段）
        var ack = SessionHandshakeCodec.EncodeAck(new JoinResponsePacket
        {
            PlayerId = 7,
            UpdateRocketsPeriod = 20d,
            ChatMessageCooldown = 3d,
            WorldTime = 12345.678d,
            SendTime = 98765.4321d,
            Difficulty = (DifficultyType)2,
            SolarSystemName = "Earth",
            UdpSessionToken = "token-0123456789abcdef",
            ResumeToken = "resume-xyz",
        });
        Wire("handshake", "ack.basic", ack, ack.Length * 8);

        // ---------- 游戏包向量 ----------
        // 每个包都用真实实现编码：首字节 = 包类型，其后是包体。
        var packetLines = new List<string>();
        void Packet<T>(string name, PacketType type, T packet) where T : INetData
        {
            var m = NewOut();
            m.Write((byte)type);
            packet.Serialize(m);
            packetLines.Add($"packet|{name}|{m.LengthBits}|{Convert.ToHexString(m.Data.AsSpan(0, m.LengthBytes))}");
        }

        // 一个字段都填满的火箭，覆盖集合、可空 Burns、非 ASCII 字符串
        var richRocket = new RocketState
        {
            RocketName = "羊城通号",
            Location = new NetLocation(123.5d, -456.25d, 1.5d, -2.5d, "Earth"),
            Rotation = 1.25f,
            AngularVelocity = -0.75f,
            ThrottleOn = true,
            ThrottlePercent = 0.875f,
            Rcs = false,
        };
        richRocket.Parts.Add(10, new PartState
        {
            Name = "Engine",
            X = 1.5f, Y = -2.5f,
            OrientationX = 1f, OrientationY = 0f, OrientationZ = 0.5f,
            Temperature = 320.25f,
        });
        richRocket.Parts[10].NumberVariables["fuel"] = 0.75d;
        richRocket.Parts[10].ToggleVariables["active"] = true;
        richRocket.Parts[10].TextVariables["label"] = "主引擎";
        richRocket.Parts[10].Burns = new BurnMarkState(0.5f, 0.25f, 1.5f, "top", "bottom");
        richRocket.Parts.Add(11, new PartState { Name = "Empty" });
        richRocket.Joints.Add(new JointState(10, 11));
        richRocket.Stages.Add(new StageState(0, new[] { 10, 11 }));

        Packet("joinrequest.utf8", PacketType.JoinRequest, new JoinRequestPacket
        { Username = "羊城通", Password = "", SolarSystemName = "Earth", ResumePlayerId = -1, ResumeToken = "" });
        Packet("joinrequest.ascii", PacketType.JoinRequest, new JoinRequestPacket
        { Username = "maozhongmao", Password = "pw-123", SolarSystemName = "Jupiter", ResumePlayerId = 42, ResumeToken = "resume-token-abc" });

        Packet("joinresponse.basic", PacketType.JoinResponse, new JoinResponsePacket
        {
            PlayerId = 7, UpdateRocketsPeriod = 20d, ChatMessageCooldown = 3d,
            WorldTime = 12345.678d, SendTime = 98765.4321d, Difficulty = DifficultyType.Hard,
            SolarSystemName = "Earth", UdpSessionToken = "token-0123456789abcdef", ResumeToken = "resume-xyz",
        });
        Packet("joinresponse.defaults", PacketType.JoinResponse, new JoinResponsePacket());

        Packet("playerconnected.basic", PacketType.PlayerConnected, new PlayerConnectedPacket
        { PlayerId = 3, Username = "羊城通", IconColor = new Color3(0.1f, 0.2f, 0.3f), PrintMessage = true });
        Packet("playerconnected.defaults", PacketType.PlayerConnected, new PlayerConnectedPacket());

        Packet("playerdisconnected.basic", PacketType.PlayerDisconnected, new PlayerDisconnectedPacket { PlayerId = 9 });
        Packet("playerdisconnected.neg1", PacketType.PlayerDisconnected, new PlayerDisconnectedPacket());

        Packet("updateplayercontrol.basic", PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket { PlayerId = 5, RocketId = 42 });
        Packet("updateplayercontrol.neg", PacketType.UpdatePlayerControl, new UpdatePlayerControlPacket());

        Packet("updateplayerauthority.empty", PacketType.UpdatePlayerAuthority, new UpdatePlayerAuthorityPacket());
        Packet("updateplayerauthority.three", PacketType.UpdatePlayerAuthority, new UpdatePlayerAuthorityPacket { RocketIds = new HashSet<int> { 1, 2, 3 } });

        Packet("updateworldtime.basic", PacketType.UpdateWorldTime, new UpdateWorldTimePacket { WorldTime = 12345.678d });
        Packet("updateworldtime.negative", PacketType.UpdateWorldTime, new UpdateWorldTimePacket { WorldTime = -999.5d });

        Packet("updateplayercolor.basic", PacketType.UpdatePlayerColor, new UpdatePlayerColorPacket { PlayerId = 4, Color = new Color3(0.9f, 0.5f, 0.25f) });

        Packet("sendchatmessage.utf8", PacketType.SendChatMessage, new SendChatMessagePacket { SenderId = 2, Message = "hello 世界" });
        Packet("sendchatmessage.empty", PacketType.SendChatMessage, new SendChatMessagePacket { SenderId = 1, Message = "" });

        Packet("createrocket.rich", PacketType.CreateRocket, new CreateRocketPacket
        { WorldTime = 100.25d, LocalId = 3, GlobalId = 77, ForLaunch = true, Rocket = richRocket });
        Packet("createrocket.emptyrocket", PacketType.CreateRocket, new CreateRocketPacket
        { WorldTime = 0d, LocalId = -1, GlobalId = -1, ForLaunch = false, Rocket = new RocketState() });

        Packet("destroyrocket.basic", PacketType.DestroyRocket, new DestroyRocketPacket { WorldTime = 55.5d, RocketId = 7, Reason = 4 });
        Packet("destroyrocket.reason0", PacketType.DestroyRocket, new DestroyRocketPacket { WorldTime = 0d, RocketId = -1, Reason = 0 });

        Packet("updaterocketprimary.basic", PacketType.UpdateRocketPrimary, new UpdateRocketPrimaryPacket
        { WorldTime = 12.5d, RocketId = 7, Location = new NetLocation(1.5d, 2.5d, -3.5d, 4.5d, "Moon"), Rotation = 0.5f, AngularVelocity = -0.125f });

        Packet("updaterocketsecondary.basic", PacketType.UpdateRocketSecondary, new UpdateRocketSecondaryPacket
        {
            WorldTime = 12.5d, RocketId = 7, InputTurn = 0.5f,
            RawX = 1f, RawY = 2f, HorizontalX = 3f, HorizontalY = 4f,
            VerticalX = 5f, VerticalY = 6f, ThrottlePercent = 0.875f,
            ThrottleOn = true, Rcs = false,
        });

        Packet("destroypart.basic", PacketType.DestroyPart, new DestroyPartPacket
        { WorldTime = 30.5d, RocketId = 5, PartId = 10, CreateExplosion = true, Reason = 4 });
        Packet("destroypart.noexplosion", PacketType.DestroyPart, new DestroyPartPacket
        { WorldTime = 0d, RocketId = -1, PartId = -1, CreateExplosion = false, Reason = 0 });

        Packet("updatestaging.empty", PacketType.UpdateStaging, new UpdateStagingPacket { WorldTime = 1d, RocketId = 2 });
        Packet("updatestaging.two", PacketType.UpdateStaging, new UpdateStagingPacket
        {
            WorldTime = 2d, RocketId = 3,
            Stages = new List<StageState> { new(0, new[] { 10, 11 }), new(1, new[] { 12 }) },
        });

        Packet("updatepartengine.basic", PacketType.UpdatePart_EngineModule, new UpdatePartEnginePacket
        { WorldTime = 1.5d, RocketId = 2, PartId = 3, EngineOn = true });
        Packet("updatepartwheel.basic", PacketType.UpdatePart_WheelModule, new UpdatePartWheelPacket
        { WorldTime = 1.5d, RocketId = 2, PartId = 4, WheelOn = false });
        Packet("updatepartbooster.basic", PacketType.UpdatePart_BoosterModule, new UpdatePartBoosterPacket
        { WorldTime = 1.5d, RocketId = 2, PartId = 5, Primed = true, Throttle = 0.5f, FuelPercent = 0.625f });
        Packet("updatepartparachute.basic", PacketType.UpdatePart_ParachuteModule, new UpdatePartParachutePacket
        { WorldTime = 1.5d, RocketId = 2, PartId = 6, State = 0.25f, TargetState = 0.75f });
        Packet("updatepartmove.basic", PacketType.UpdatePart_MoveModule, new UpdatePartMovePacket
        { WorldTime = 1.5d, RocketId = 2, PartId = 7, Time = 1.25f, TargetTime = 2.5f });

        Packet("updatepartresource.basic", PacketType.UpdatePart_ResourceModule, new UpdatePartResourcePacket
        { WorldTime = 1.5d, RocketId = 2, ResourcePercent = 0.875d, PartIds = new HashSet<int> { 8, 9 } });
        Packet("updatepartresource.empty", PacketType.UpdatePart_ResourceModule, new UpdatePartResourcePacket
        { WorldTime = 0d, RocketId = -1, ResourcePercent = 0d });

        Packet("showtoast.utf8", PacketType.ShowToastMessage, new PlayerEventToastPacket { Message = "已进入轨道 \U0001F680" });
        Packet("showtoast.empty", PacketType.ShowToastMessage, new PlayerEventToastPacket());

        Packet("docktransaction.uncommitted", PacketType.DockTransaction, new DockTransactionPacket
        { TransactionId = 3, Operation = DockTransactionOperation.Undock, Committed = false, KeepRocketId = 1, RemoveRocketId = 2, KeepPartId = 10, RemovePartId = 20, WorldTime = 5.5d });
        Packet("docktransaction.committed_nosecond", PacketType.DockTransaction, new DockTransactionPacket
        { TransactionId = 4, Operation = DockTransactionOperation.Dock, Committed = true, KeepRocketId = 1, RemoveRocketId = 2, KeepPartId = 10, RemovePartId = 20, WorldTime = 6.5d, MergedRocket = richRocket, SecondRocketId = -1 });
        Packet("docktransaction.committed_withsecond", PacketType.DockTransaction, new DockTransactionPacket
        { TransactionId = 5, Operation = DockTransactionOperation.Dock, Committed = true, KeepRocketId = 1, RemoveRocketId = 2, KeepPartId = 10, RemovePartId = 20, WorldTime = 7.5d, MergedRocket = richRocket, SecondRocketId = 3, SecondRocket = richRocket });

        Packet("timewarp.notice", PacketType.TimeWarp, new TimeWarpPacket
        {
            Operation = TimeWarpOperation.Notice, VoteId = 0, RequesterId = -1, RequesterName = "",
            Multiplier = 1d, Approved = false, WorldTime = 0d, TimeoutSeconds = 0, Message = "",
        });
        Packet("timewarp.request", PacketType.TimeWarp, new TimeWarpPacket
        {
            Operation = TimeWarpOperation.Request, VoteId = 11, RequesterId = 2, RequesterName = "羊城通",
            Multiplier = 10d, Approved = true, WorldTime = 4321.5d, TimeoutSeconds = 30, Message = "加速到 10x",
        });

        Packet("experimentalaccess.request", PacketType.ExperimentalAccess, new ExperimentalAccessPacket
        { Request = true, Passphrase = "", Granted = false, Message = "" });
        Packet("experimentalaccess.granted", PacketType.ExperimentalAccess, new ExperimentalAccessPacket
        { Request = false, Passphrase = "口令", Granted = true, Message = "已开启" });

        Packet("p2ppeeroffer.inactive", PacketType.P2PPeerOffer, new P2PPeerOfferPacket());
        Packet("p2ppeeroffer.active", PacketType.P2PPeerOffer, new P2PPeerOfferPacket
        {
            Active = true, PeerPlayerId = 2, PeerAddress = "192.168.1.10", PeerPort = 21899,
            PairToken = "pair-abc", TransitionBufferSeconds = 10,
            LocalRocketIds = new List<int> { 1, 2, 3 },
            PeerRocketIds = new List<int> { 4, 5 },
        });

        // ---------- 落盘 ----------
        var primPath = Path.Combine(outDir, "primitives.txt");
        var seqPath = Path.Combine(outDir, "sequences.txt");
        var wirePath = Path.Combine(outDir, "wire.txt");
        File.WriteAllLines(primPath, PrimitiveLines, new UTF8Encoding(false));
        File.WriteAllLines(seqPath, SequenceLines, new UTF8Encoding(false));
        File.WriteAllLines(wirePath, WireLines, new UTF8Encoding(false));

        Console.WriteLine($"primitives.txt: {PrimitiveLines.Count} 行 -> {primPath}");
        Console.WriteLine($"sequences.txt : {SequenceLines.Count} 行 -> {seqPath}");
        Console.WriteLine($"wire.txt      : {WireLines.Count} 行 -> {wirePath}");
        var packetPath = Path.Combine(outDir, "packets.txt");
        File.WriteAllLines(packetPath, packetLines, new UTF8Encoding(false));

        Console.WriteLine($"packets.txt   : {packetLines.Count} 行 -> {packetPath}");
        Console.WriteLine(roundTripFailures.Count == 0
            ? "自检: 回读全部一致"
            : $"自检: {roundTripFailures.Count} 项不一致");

        return roundTripFailures.Count == 0 ? 0 : 1;
    }
}
