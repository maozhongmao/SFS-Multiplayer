// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System.Net;
using System.Net.Sockets;
using System.Text;
using SfsMultiplayer.Protocol;

using var tcp = new TcpClient { NoDelay = true };
await tcp.ConnectAsync(IPAddress.Loopback, 19806);
using var stream = tcp.GetStream();
var hello = SessionHandshakeCodec.EncodeHello(new JoinRequestPacket { Username = "final-artifact-smoke" });
await TcpFrameCodec.WriteAsync(stream, new TcpFrame(TcpFrameKind.Hello, SessionHandshakeCodec.Version, hello, hello.Length * 8), CancellationToken.None);
var ackFrame = await TcpFrameCodec.ReadAsync(stream, CancellationToken.None);
if (ackFrame.Kind != TcpFrameKind.HelloAck) throw new Exception($"Expected HelloAck, got {ackFrame.Kind}");
var ack = SessionHandshakeCodec.DecodeAck(ackFrame.Payload);
if (ack.PlayerId < 0 || string.IsNullOrWhiteSpace(ack.UdpSessionToken)) throw new Exception("Invalid session response");

using var udp = new UdpClient(AddressFamily.InterNetwork);
udp.Connect(IPAddress.Loopback, 19806);
var token = Encoding.UTF8.GetBytes(ack.UdpSessionToken);
var bind = new byte[2 + token.Length]; bind[0] = 1; bind[1] = (byte)token.Length; Buffer.BlockCopy(token, 0, bind, 2, token.Length);
await udp.SendAsync(bind, bind.Length);
var receive = udp.ReceiveAsync();
if (await Task.WhenAny(receive, Task.Delay(3000)) != receive) throw new TimeoutException("UDP BindAck timeout");
var reply = receive.Result.Buffer;
if (reply.Length < 2 || reply[0] != 2) throw new Exception("Invalid UDP BindAck");

bool gotPacket = false;
var deadline = DateTime.UtcNow.AddSeconds(3);
while (DateTime.UtcNow < deadline)
{
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
    try
    {
        var frame = await TcpFrameCodec.ReadAsync(stream, timeout.Token);
        if (frame.Kind == TcpFrameKind.Packet) { gotPacket = true; break; }
    }
    catch (OperationCanceledException) { }
}
if (!gotPacket) throw new Exception("No initial world packet received");
Console.WriteLine($"FINAL_SMOKE_OK PlayerId={ack.PlayerId} UdpTokenBytes={token.Length} TCP_INITIAL_PACKET=YES UDP_BIND_ACK=YES");
