// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

using SfsMultiplayer.Server.ServerPatch;

// 验证插件：仅用于确认 DLL 补丁加载机制可用。
// 启动加载时在终端打印一句话，不加任何定时逻辑，纯粹验证生命周期。
public sealed class SamplePatch : IServerPatch
{
    public string Name => "SamplePatch";
    public string Version => "1.0.0";

    public void OnLoad(IServerContext context)
    {
        context.Log("补丁加载验证成功：这句话说明 IServerPatch.OnLoad 已被服务器正确调用。");
    }

    public void OnUnload()
    {
        // 验证插件无资源需要释放。
    }
}
