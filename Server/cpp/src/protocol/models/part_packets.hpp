// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 转发头：本目录原先另有一份同名类型定义（PartState/RocketState 用 unordered_map，
// 与 protocol/packets.hpp 的 vector<pair> 版本内存布局不同）。两套定义在同一命名空间
// 下混用属 ODR 违规 —— 按 A 布局构造对象、却调用按 B 布局编译的方法，会把内存
// 解释错乱并直接导致进程崩溃。现统一到 protocol/packets.hpp（其实现已逐字节对拍通过）。

#pragma once

#include "protocol/packets.hpp"
