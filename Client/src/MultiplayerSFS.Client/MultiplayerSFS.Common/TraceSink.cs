// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

using System;

namespace MultiplayerSFS.Common;

/// <summary>
/// 协议层追踪的"插座"。
/// Common 层不认识 Mod 层，所以这里只放一个委托；由 Mod 的 DebugTrace 在启动时挂上。
/// 挂空时零开销：只在 TrackPacketType 里多一次 null 判断。
/// </summary>
public static class TraceSink
{
    /// <summary>帧级观察者：(kind, payloadBits, payloadBytes)。</summary>
    public static Action<string, int, int> FrameObserver;
}
