// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;
using System.Net.Sockets;

namespace MultiplayerSFS.Common
{
    /// <summary>
    /// Windows 上 UDP 的经典坑：只要本机往一个"没有监听"的地址发过数据报，
    /// 对端回 ICMP 端口不可达后，OS 会给这个 socket 的所有后续 Receive 抛
    /// WSAECONNRESET(10054) —— 中文即"远程主机强迫关闭了一个现有的连接"。
    ///
    /// 后果不是"少收几个包"：接收循环一起步就报错，等于整条接收路径废掉，
    /// 表现为 recv 恒为 0、世界加载超时、双方位置完全不同步。
    ///
    /// 关掉 SIO_UDP_CONNRESET 是 Windows 上 UDP 程序的标准做法（默认是"把 ICMP
    /// 错误上报给应用"）。非 Windows 平台该调用无意义，失败也不影响功能。
    /// </summary>
    public static class UdpSocketGuard
    {
        // SIO_UDP_CONNRESET = 0x9800000C（Windows SDK 里的负值写法）
        private const int SioUdpConnReset = -1744830452;

        public static void DisableConnReset(UdpClient client)
        {
            if (client == null || client.Client == null) return;
            try
            {
                client.Client.IOControl((IOControlCode)SioUdpConnReset, new byte[] { 0, 0, 0, 0 }, null);
            }
            catch (Exception)
            {
                // 非 Windows / 平台不支持：忽略即可（WinSock 之外没有这个行为）
            }
        }
    }
}
