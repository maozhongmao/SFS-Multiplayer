// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using System;

namespace SfsMultiplayer.Server.ServerPatch;

// 注入给补丁的受限服务器上下文。参考 MC 插件拿 Server 引用的范式，
// 但只暴露补丁安全可用的入口，避免补丁直接依赖服务器内部实现细节。
public interface IServerContext
{
    // 服务器实例引用（可读取在线玩家、世界时间，向客户端广播等）。
    TcpMultiplayerServer Server { get; }

    // 当前生效的服务器配置（只读快照，补丁不应原地修改）。
    ServerSettings Settings { get; }

    // 补丁专用日志（统一前缀，便于排查）。
    void Log(string message);

    // 补丁专用日志（带格式串）。
    void Log(string format, params object[] args);
}
