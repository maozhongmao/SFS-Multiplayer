// SPDX-License-Identifier: MPL-2.0
// TcpFrame 编解码回归测试（直接引用生产源码 TcpFrame.cs）
// 契约：Encode(frame) = [bodyLength(4)][kind(1)][seq(4)][bits(4)][payload]，DecodeBody 只接收 body 部分。
// 重点验证复用缓冲区版本的 DecodeBody(body, length) 与精确长度版本等价。
using System;
using System.IO;
using MultiplayerSFS.Common;

internal static class Program
{
    private static int passed;
    private static int failed;

    private static void Check(string name, bool condition, string detail = "")
    {
        if (condition)
        {
            passed++;
            Console.WriteLine($"PASS  {name}");
        }
        else
        {
            failed++;
            Console.WriteLine($"FAIL  {name} {detail}");
        }
    }

    // 去掉 Encode 输出的 4 字节长度前缀，取 body（长度与内容都要正确）
    private static byte[] Body(byte[] encoded)
    {
        int bodyLength = encoded[0] | (encoded[1] << 8) | (encoded[2] << 16) | (encoded[3] << 24);
        if (bodyLength != encoded.Length - 4)
            throw new Exception($"Encode 前缀长度 {bodyLength} 与总长 {encoded.Length} 不一致");
        byte[] body = new byte[bodyLength];
        Buffer.BlockCopy(encoded, 4, body, 0, bodyLength);
        return body;
    }

    private static int Main()
    {
        // 1) Ping：往返
        var ping = new TcpFrame(TcpFrameKind.Ping, 7, BitConverter.GetBytes(1234567L), 64);
        TcpFrame pingBack = TcpFrameCodec.DecodeBody(Body(TcpFrameCodec.Encode(ping)));
        Check("Ping 往返 kind", pingBack.Kind == TcpFrameKind.Ping);
        Check("Ping 往返 sequence", pingBack.Sequence == 7, $"got {pingBack.Sequence}");
        Check("Ping 往返 payload", BitConverter.ToInt64(pingBack.Payload, 0) == 1234567L);
        Check("Ping 往返 bits", pingBack.PayloadBits == 64, $"got {pingBack.PayloadBits}");

        // 2) 空 payload（Disconnect）
        var disc = new TcpFrame(TcpFrameKind.Disconnect, 0, Array.Empty<byte>(), 0);
        TcpFrame discBack = TcpFrameCodec.DecodeBody(Body(TcpFrameCodec.Encode(disc)));
        Check("Disconnect 空 payload 往返", discBack.Kind == TcpFrameKind.Disconnect && discBack.Payload.Length == 0);

        // 3) 大 payload（模拟 153 字节握手 ACK）
        byte[] big = new byte[153];
        for (int i = 0; i < big.Length; i++) big[i] = (byte)(i * 7 + 3);
        var ack = new TcpFrame(TcpFrameKind.HelloAck, 2, big, big.Length * 8);
        byte[] ackBody = Body(TcpFrameCodec.Encode(ack));
        TcpFrame ackBack = TcpFrameCodec.DecodeBody(ackBody);
        bool same = ackBack.Payload.Length == big.Length;
        for (int i = 0; same && i < big.Length; i++) same = ackBack.Payload[i] == big[i];
        Check("153 字节 payload 往返", same, $"len={ackBack.Payload.Length}");

        // 4) 核心改动：复用大缓冲区 + 显式长度，结果必须与精确长度一致
        byte[] scratch = new byte[65536];
        for (int i = 0; i < scratch.Length; i++) scratch[i] = 0xAB;   // 填垃圾，验证只读前 bodyLen 字节
        Buffer.BlockCopy(ackBody, 0, scratch, 0, ackBody.Length);
        TcpFrame reuseBack = TcpFrameCodec.DecodeBody(scratch, ackBody.Length);
        bool reuseSame = reuseBack.Payload.Length == big.Length
                         && reuseBack.Kind == TcpFrameKind.HelloAck
                         && reuseBack.Sequence == 2;
        for (int i = 0; reuseSame && i < big.Length; i++) reuseSame = reuseBack.Payload[i] == big[i];
        Check("复用缓冲 + 显式长度解析一致", reuseSame, $"len={reuseBack.Payload.Length}");

        // 5) 同一份数据，新旧签名结果一致
        TcpFrame exactBack = TcpFrameCodec.DecodeBody(ackBody);
        Check("新旧签名结果一致",
            exactBack.Payload.Length == reuseBack.Payload.Length &&
            exactBack.Sequence == reuseBack.Sequence &&
            exactBack.PayloadBits == reuseBack.PayloadBits);

        // 6) 边界：长度小于帧头 -> 抛异常
        bool threw = false;
        try { TcpFrameCodec.DecodeBody(scratch, 5); } catch (InvalidDataException) { threw = true; }
        Check("长度小于帧头抛 InvalidDataException", threw);

        // 7) 边界：length 超出数组 -> 抛异常
        threw = false;
        try { TcpFrameCodec.DecodeBody(new byte[10], 11); } catch (InvalidDataException) { threw = true; }
        Check("length 超出数组抛 InvalidDataException", threw);

        // 8) 边界：payloadBits 超过实际 payload -> 抛异常
        //    构造函数本身会校验，所以这里手工拼一个非法 body
        threw = false;
        try
        {
            byte[] badBody = new byte[9 + 4];
            badBody[0] = (byte)TcpFrameKind.Ping;
            badBody[5] = 0xE7; badBody[6] = 0x03;   // payloadBits = 999
            TcpFrameCodec.DecodeBody(badBody);
        }
        catch (InvalidDataException) { threw = true; }
        Check("payloadBits 越界抛 InvalidDataException", threw);

        // 9) 边界：非法 kind
        threw = false;
        try
        {
            byte[] bogus = new byte[9];
            bogus[0] = 200;   // 不存在的 kind
            TcpFrameCodec.DecodeBody(bogus);
        }
        catch (InvalidDataException) { threw = true; }
        Check("非法 kind 抛 InvalidDataException", threw);

        // 10) 收包循环模拟：把第二帧挪到缓冲开头再解析（生产代码消费完第一帧后就是从新偏移继续解析）
        byte[] stream = new byte[65536];
        byte[] first = Body(TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.Ping, 11, new byte[8], 64)));
        byte[] second = Body(TcpFrameCodec.Encode(new TcpFrame(TcpFrameKind.Packet, 12, Big(9), 72)));
        Buffer.BlockCopy(first, 0, stream, 0, first.Length);
        Buffer.BlockCopy(second, 0, stream, first.Length, second.Length);
        TcpFrame f1 = TcpFrameCodec.DecodeBody(stream, first.Length);
        Check("缓冲第 1 帧解析正确（不受尾部后续帧影响）",
            f1.Kind == TcpFrameKind.Ping && f1.Sequence == 11 && f1.Payload.Length == 8);
        Buffer.BlockCopy(stream, first.Length, stream, 0, second.Length);
        TcpFrame f2 = TcpFrameCodec.DecodeBody(stream, second.Length);
        Check("偏移搬移后第 2 帧解析正确",
            f2.Kind == TcpFrameKind.Packet && f2.Sequence == 12 && f2.Payload.Length == 9
            && f2.Payload[8] == 9, $"kind={f2.Kind} seq={f2.Sequence} len={f2.Payload.Length}");

        Console.WriteLine();
        Console.WriteLine($"结果: {passed} 通过 / {failed} 失败");
        return failed == 0 ? 0 : 1;

        static byte[] Big(int n)
        {
            byte[] result = new byte[n];
            for (int i = 0; i < n; i++) result[i] = (byte)(i + 1);
            return result;
        }
    }
}
