// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

namespace SfsMultiplayer.Server.ServerPatch;

// 补丁（插件）实现此接口。服务器在启动或 reload 时实例化并调用生命周期方法。
// 范式参考 Minecraft 服务端插件：插件拿到 Server 上下文引用，但只能通过接口约定
// 的入口扩展，不直接依赖未公开的内部类型。
public interface IServerPatch
{
    // 补丁显示名（用于日志与 reload 列表）。
    string Name { get; }

    // 补丁版本（语义版本字符串即可）。
    string Version { get; }

    // 加载时调用，传入受限服务器上下文。可在此注册事件、改写世界、启动定时任务等。
    void OnLoad(IServerContext context);

    // 卸载/热重载替换前调用。应释放自己持有的资源、注销回调，避免内存泄漏。
    void OnUnload();
}
