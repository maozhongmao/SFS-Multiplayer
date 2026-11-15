// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net;
using System.Net.Sockets;
using System.Text;
using Lidgren.Network;

namespace SfsMultiplayer.Server;

internal sealed class UdpStateTransport : IDisposable
{
    internal const byte Bind = 1;
    internal const byte BindAck = 2;
    internal const byte Data = 3;
    private readonly UdpClient socket;
    private readonly Func<byte, string, IPEndPoint, byte[], bool> receive;
    private CancellationTokenSource? cancellation;

    public UdpStateTransport(IPAddress bindAddress, int port, Func<byte, string, IPEndPoint, byte[], bool> receive)
    {
        socket = new UdpClient(new IPEndPoint(bindAddress, port));
        this.receive = receive;
    }

    public void Start()
    {
        var token = new CancellationTokenSource();
        cancellation = token;
        Task.Run(() => ReceiveLoop(token.Token));
    }

    private async Task ReceiveLoop(CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            UdpReceiveResult result;
            try
            {
                result = await socket.ReceiveAsync().ConfigureAwait(false);
            }
            catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
            {
                return;
            }
            catch (SocketException ex) when (!cancellationToken.IsCancellationRequested)
            {
                Console.WriteLine($"[UDP接收] 套接字异常，继续监听: {ex.Message}");
                try { await Task.Delay(100, cancellationToken).ConfigureAwait(false); }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { return; }
                continue;
            }
            catch (Exception ex) when (!cancellationToken.IsCancellationRequested)
            {
                Console.WriteLine($"[UDP接收] 接收异常，继续监听: {ex.Message}");
                try { await Task.Delay(100, cancellationToken).ConfigureAwait(false); }
                catch (OperationCanceledException) when (cancellationToken.IsCancellationRequested) { return; }
                continue;
            }

            try
            {
                var data = result.Buffer;
                if (!TryDecode(data, out var kind, out var token, out var payload)) continue;
                if (!receive(kind, token, result.RemoteEndPoint, payload)) continue;
                if (kind == Bind) Send(result.RemoteEndPoint, token, BindAck, Array.Empty<byte>());
            }
            catch (Exception ex) when (!cancellationToken.IsCancellationRequested)
            {
                // 单个坏包不能杀死全局 UDP 状态接收循环。
                Console.WriteLine($"[UDP拒绝] {result.RemoteEndPoint}: {ex.Message}");
            }
        }
    }

    private static bool TryDecode(byte[] data, out byte kind, out string token, out byte[] payload)
    {
        kind = 0;
        token = string.Empty;
        payload = Array.Empty<byte>();
        if (data.Length < 2) return false;
        kind = data[0];
        if (kind != Bind && kind != Data) return false;
        var tokenLength = data[1];
        var payloadOffset = 2 + tokenLength;
        if (data.Length < payloadOffset) return false;
        token = Encoding.UTF8.GetString(data, 2, tokenLength);
        payload = new byte[data.Length - payloadOffset];
        Buffer.BlockCopy(data, payloadOffset, payload, 0, payload.Length);
        return true;
    }

    public void SendState(IPEndPoint endpoint, string token, byte[] payload)
    {
        Send(endpoint, token, Data, payload);
    }


    private void Send(IPEndPoint? endpoint, string token, byte kind, byte[] payload)
    {
        if (endpoint is null || string.IsNullOrEmpty(token)) return;
        var tokenBytes = Encoding.UTF8.GetBytes(token);
        if (tokenBytes.Length > byte.MaxValue) return;
        var data = new byte[2 + tokenBytes.Length + payload.Length];
        data[0] = kind;
        data[1] = (byte)tokenBytes.Length;
        Buffer.BlockCopy(tokenBytes, 0, data, 2, tokenBytes.Length);
        Buffer.BlockCopy(payload, 0, data, 2 + tokenBytes.Length, payload.Length);
        try { socket.Send(data, data.Length, endpoint); } catch { }
    }

    public void Dispose()
    {
        try { cancellation?.Cancel(); } catch { }
        try { socket.Close(); } catch { }
    }
}
