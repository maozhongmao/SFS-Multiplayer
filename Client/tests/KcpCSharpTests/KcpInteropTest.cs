// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// KCP 协议算法参考 skywind3000/kcp（MIT）；本文件为独立的纯 C# 重写，不包含原项目的指针/IL 织入写法。

using System;
using System.Collections.Generic;
using System.Threading;

namespace MultiplayerSFS.Common.Transport.Kcp.Tests
{
    /// <summary>
    /// 内存传输层，模拟网络传输（支持丢包、乱序、延迟）
    /// </summary>
    public class MemoryTransport
    {
        private readonly Queue<Packet> _queue = new Queue<Packet>();
        private readonly Random _rng = new Random();
        private readonly int _latencyMs;
        private readonly double _lossRate;
        private readonly bool _reorder;
        private uint _currentTime;

        public MemoryTransport(int latencyMs = 10, double lossRate = 0.0, bool reorder = false)
        {
            _latencyMs = latencyMs;
            _lossRate = lossRate;
            _reorder = reorder;
            _currentTime = 0;
        }

        public void SetCurrentTime(uint time)
        {
            _currentTime = time;
        }

        public void Send(byte[] data, KcpContext ctx)
        {
            if (_rng.NextDouble() < _lossRate) return; // 丢包

            var packet = new Packet
            {
                Data = (byte[])data.Clone(),
                ArriveTime = _currentTime + (uint)_latencyMs
            };
            _queue.Enqueue(packet);
        }

        public bool TryReceive(out byte[] data)
        {
            data = null;
            if (_queue.Count == 0) return false;

            var packet = _queue.Peek();
            if (_currentTime < packet.ArriveTime) return false;

            _queue.Dequeue();
            data = packet.Data;
            return true;
        }

        public int PendingCount => _queue.Count;

        private class Packet
        {
            public byte[] Data;
            public uint ArriveTime;
        }
    }

    /// <summary>
    /// KCP 互通测试
    /// </summary>
    public class KcpInteropTest
    {
        private const uint Conv = 0x11223344;
        private KcpContext _client;
        private KcpContext _server;
        private MemoryTransport _clientToServer;
        private MemoryTransport _serverToClient;
        private uint _currentTime;

        public void RunAllTests()
        {
            Console.WriteLine("=== KCP C# 实现互通测试 ===\n");

            TestSmallMessageBidirectional();
            TestLargeMessageCrossMtu();
            TestPacketLossRetransmission();

            Console.WriteLine("\n=== 所有测试通过! ===");
        }

        private void Setup(double lossRate = 0.0, bool reorder = false)
        {
            _currentTime = 0;
            _clientToServer = new MemoryTransport(10, lossRate, reorder);
            _serverToClient = new MemoryTransport(10, lossRate, reorder);

            _client = new KcpContext(Conv, (data, ctx) => _clientToServer.Send(data, ctx));
            _server = new KcpContext(Conv, (data, ctx) => _serverToClient.Send(data, ctx));

            // 配置 nodelay 模式加快测试
            _client.SetNoDelay(1, 10, 2, 1);
            _server.SetNoDelay(1, 10, 2, 1);
        }

        private void Tick(int ms = 10)
        {
            _currentTime += (uint)ms;

            // 先处理网络接收，再更新状态（确保收到包后能立即回 ACK）
            _clientToServer.SetCurrentTime(_currentTime);
            _serverToClient.SetCurrentTime(_currentTime);

            while (_clientToServer.TryReceive(out var data))
            {
                Console.WriteLine($"[TEST] Client->Server 收到数据: {data.Length} 字节, 时间={_currentTime}");
                _server.Input(data, 0, data.Length);
            }

            while (_serverToClient.TryReceive(out var data))
            {
                Console.WriteLine($"[TEST] Server->Client 收到数据: {data.Length} 字节");
                _client.Input(data, 0, data.Length);
            }

            _client.Update(_currentTime);
            _server.Update(_currentTime);
            
            // Debug: print server state after update
            Console.WriteLine($"[TICK] time={_currentTime}, clientWait={_client.WaitSnd}, serverWait={_server.WaitSnd}, clientState={_client.State}, serverState={_server.State}, clientSndUna={_client.SndUna}, serverSndUna={_server.SndUna}, clientRcvNxt={_client.RcvNxt}, serverRcvNxt={_server.RcvNxt}");
            
            // Debug: check if server has pending ACKs to send
            if (_server.WaitSnd > 0)
            {
                Console.WriteLine($"[DEBUG] Server has {_server.WaitSnd} packets waiting to send");
            }
            
            // Debug: check server's send buffer
            Console.WriteLine($"[DEBUG] Server SndBuf={_server.SndBuf.Count}, SndQueue={_server.SndQueue.Count}, RcvQueue={_server.RcvQueue.Count}, RcvBuf={_server.RcvBuf.Count}");
        }

        private void WaitForDelivery(int maxTicks = 5000)
        {
            int ticks = 0;
            while ((_client.WaitSnd > 0 || _server.WaitSnd > 0 ||
                    _clientToServer.PendingCount > 0 || _serverToClient.PendingCount > 0) &&
                   ticks < maxTicks)
            {
                Tick(10);
                ticks += 10;
                
                // Debug: print status every 100 ticks
                if (ticks % 100 == 0)
                {
                    Console.WriteLine($"[WAIT] ticks={ticks}, clientWait={_client.WaitSnd}, serverWait={_server.WaitSnd}, c2s={_clientToServer.PendingCount}, s2c={_serverToClient.PendingCount}");
                }
            }
            if (ticks >= maxTicks)
                throw new TimeoutException($"数据传输超时 (ticks={ticks}, clientWait={_client.WaitSnd}, serverWait={_server.WaitSnd})");
        }

        private void TestSmallMessageBidirectional()
        {
            Console.WriteLine("测试 1: 小消息双向收发");
            Setup();

            // 客户端 -> 服务端
            var msg1 = System.Text.Encoding.UTF8.GetBytes("Hello Server!");
            int sent = _client.Send(msg1);
            Console.WriteLine($"  客户端发送: {sent} 字节");
            WaitForDelivery();

            var buf1 = new byte[1024];
            int recv1 = _server.Recv(buf1, 0, buf1.Length);
            var str1 = System.Text.Encoding.UTF8.GetString(buf1, 0, recv1);
            Console.WriteLine($"  服务端接收: {recv1} 字节 = \"{str1}\"");
            if (str1 != "Hello Server!") throw new Exception("消息内容不匹配");

            // 服务端 -> 客户端
            var msg2 = System.Text.Encoding.UTF8.GetBytes("Hello Client!");
            sent = _server.Send(msg2);
            Console.WriteLine($"  服务端发送: {sent} 字节");
            WaitForDelivery();

            var buf2 = new byte[1024];
            int recv2 = _client.Recv(buf2, 0, buf2.Length);
            var str2 = System.Text.Encoding.UTF8.GetString(buf2, 0, recv2);
            Console.WriteLine($"  客户端接收: {recv2} 字节 = \"{str2}\"");
            if (str2 != "Hello Client!") throw new Exception("消息内容不匹配");

            Console.WriteLine("  ✓ 通过\n");
        }

        private void TestLargeMessageCrossMtu()
        {
            Console.WriteLine("测试 2: 跨 MTU 大消息 (64 KB)");
            Setup();

            // 生成 64KB 测试数据
            var largeData = new byte[64 * 1024];
            var rng = new Random(42);
            rng.NextBytes(largeData);

            int sent = _client.Send(largeData);
            Console.WriteLine($"  客户端发送: {sent} 字节 (分片数: {_client.WaitSnd})");
            WaitForDelivery(20000);

            var recvBuf = new byte[64 * 1024];
            int totalRecv = 0;
            while (totalRecv < largeData.Length)
            {
                int n = _server.Recv(recvBuf, totalRecv, recvBuf.Length - totalRecv);
                if (n <= 0) break;
                totalRecv += n;
            }

            Console.WriteLine($"  服务端接收: {totalRecv} 字节");

            // 验证数据完整性
            for (int i = 0; i < largeData.Length; i++)
            {
                if (largeData[i] != recvBuf[i])
                    throw new Exception($"数据校验失败 at offset {i}: 期望 {largeData[i]}, 实际 {recvBuf[i]}");
            }
            Console.WriteLine("  ✓ 数据完整性校验通过\n");
        }

        private void TestPacketLossRetransmission()
        {
            Console.WriteLine("测试 3: 丢包重传 (10% 丢包率)");
            Setup(lossRate: 0.1);

            var msg = System.Text.Encoding.UTF8.GetBytes("Packet loss test message - should survive retransmission!");
            int sent = _client.Send(msg);
            Console.WriteLine($"  客户端发送: {sent} 字节");
            WaitForDelivery(30000);

            var buf = new byte[1024];
            int recv = _server.Recv(buf, 0, buf.Length);
            var str = System.Text.Encoding.UTF8.GetString(buf, 0, recv);
            Console.WriteLine($"  服务端接收: {recv} 字节 = \"{str}\"");
            if (str != System.Text.Encoding.UTF8.GetString(msg))
                throw new Exception("丢包重传后消息内容不匹配");

            Console.WriteLine("  ✓ 丢包重传测试通过\n");
        }

        public void Dispose()
        {
            _client?.Dispose();
            _server?.Dispose();
        }
    }

    class Program
    {
        static void Main(string[] args)
        {
            var test = new KcpInteropTest();
            try
            {
                test.RunAllTests();
            }
            catch (Exception ex)
            {
                Console.WriteLine($"\n✗ 测试失败: {ex.Message}");
                Console.WriteLine(ex.StackTrace);
                Environment.Exit(1);
            }
            finally
            {
                test.Dispose();
            }
        }
    }
}