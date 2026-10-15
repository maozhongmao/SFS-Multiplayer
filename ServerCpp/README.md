<!--
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

# ServerCpp first Windows test

这是 C++17 服务端的第一阶段空壳，不是完整联机服务端。它只用于先测基础进程占用和 TCP/UDP socket 生命周期。

## 编译

在 Windows 命令提示符中运行：

```bat
build-windows.bat
```

也可以指定编译器：

```bat
set CXX=clang++
build-windows.bat
```

编译成功后生成：

- `build\sfs-server-cpp.exe`
- `build\protocol_codec_smoke.exe`

## 测量空载内存

```bat
build\sfs-server-cpp.exe --bind 127.0.0.1 --port 29850
```

另开一个命令提示符，用任务管理器查看 `sfs-server-cpp.exe` 的内存。结束时按 `Ctrl+C`。

也可以使用 PowerShell 采样：

```powershell
$p = Get-Process sfs-server-cpp
$p.WorkingSet64 / 1MB
$p.PrivateMemorySize64 / 1MB
```

## 当前范围

- 已实现 TCP/UDP 同端口绑定。
- 已实现非阻塞 socket 和单事件循环。
- 已实现 TCP frame 与 UDP envelope 的编解码烟测。
- 当前不会处理真实 Hello、玩家、火箭或世界状态。
- 当前不能与现有 SFS 客户端联机；这是交给你先测基础内存的实验版本。

## 回滚

删除或移走 `ServerCpp` 实验目录即可；现有 `Server` C# 基线没有修改。
