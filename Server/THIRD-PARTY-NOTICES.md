<!--
  This Source Code Form is subject to the terms of the Mozilla Public
  License, v. 2.0. If a copy of the MPL was not distributed with this
  file, You can obtain one at https://mozilla.org/MPL/2.0/.
-->

## Third-party notices

### SpaceWizards.Lidgren.Network 0.3.1

Copyright (c) 2015 lidgren and Space Station 14 contributors.
Licensed under the MIT License.

- Source: https://github.com/space-wizards/SpaceWizards.Lidgren.Network
- NuGet: https://www.nuget.org/packages/SpaceWizards.Lidgren.Network/0.3.1
- Fixed source commit used by the package: `1d85b82e058101b7ebd60cc8883af5359e4c263a`
- Full license: `third_party/spacewizards-lidgren/LICENSE`

This maintained fork is the server runtime dependency. It contains the
`NetReliableSenderChannel` null-dereference fix first released in 0.2.6.

### Original Lidgren.Network 1.0.2 compatibility fixture

Copyright (c) 2015 lidgren. Licensed under the MIT License.
Source: https://github.com/lidgren/lidgren-network-gen3

The original binary is retained under `third_party/lidgren/` only for protocol
interoperability tests with the SFS 1.5 client. It is not the server runtime
dependency and is not emitted beside the single-file executable.

### Multiplayer-SFS protocol compatibility

The wire protocol is implemented for compatibility with the archived
AstroTheRabbit/Multiplayer-SFS project, licensed under GNU GPL version 3.
Source: https://github.com/AstroTheRabbit/Multiplayer-SFS

Spaceflight Simulator and its game assets are not included in this repository.

## quickjs-ng (MIT)

- 位置：`Server/cpp/third_party/quickjs/`
- 上游：https://github.com/quickjs-ng/quickjs
- 版本：commit 9b0617189558（2026-10-11）
- 许可：MIT（Copyright (c) 2017-2026 Fabrice Bellard）
- 用途：服务端插件系统（SFS_PLUGINS=1 时编译；关闭时不参与构建）
- 详情见该目录下的 VENDORED.txt（含剪裁范围与更新方法）
