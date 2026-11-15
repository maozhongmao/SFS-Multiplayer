// SPDX-License-Identifier: MPL-2.0
using System;
using System.Net;
using System.Threading.Tasks;
using Lidgren.Network;
using MultiplayerSFS.Common;

internal static class Program
{
    private static async Task<int> Main(string[] args)
    {
        var transport = new TcpClientTransport();
        try
        {
            var response = await transport.ConnectAsync(IPAddress.Loopback, int.Parse(args[0]), new Packet_JoinRequest
            {
                Username = "installed-v323-smoke",
                Password = "",
                SolarSystemName = "",
            });
            var sawTimeWarp24 = false;
            var deadline = DateTime.UtcNow.AddSeconds(6);
            while (DateTime.UtcNow < deadline)
            {
                TcpFrame frame;
                while (transport.TryReceive(out frame))
                {
                    if (frame.Kind != TcpFrameKind.Packet) continue;
                    var message = NetPayloadCodec.ToIncoming(frame.Payload, frame.PayloadBits);
                    if (message.ReadByte() == 24) sawTimeWarp24 = true;
                }
                await Task.Delay(25);
            }
            if (response.PlayerId < 0 || !transport.Connected || transport.ReceivedFrames == 0 || transport.SentFrames == 0 || !sawTimeWarp24)
            {
                Console.Error.WriteLine($"LEGACY_V323_FAIL id={response.PlayerId} connected={transport.Connected} sent={transport.SentFrames} received={transport.ReceivedFrames} saw24={sawTimeWarp24} reason={transport.LastDisconnectReason}");
                return 1;
            }
            Console.WriteLine($"LEGACY_V323_COMPAT_OK PlayerId={response.PlayerId} Connected={transport.Connected} Sent={transport.SentFrames} Received={transport.ReceivedFrames} SawPacket24={sawTimeWarp24}");
            return 0;
        }
        finally
        {
            transport.Disconnect("Legacy smoke complete");
            transport.Dispose();
        }
    }
}
