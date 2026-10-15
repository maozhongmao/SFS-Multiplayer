// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cmath>

#include "protocol/packets.hpp"   // NetLocation

namespace sfs::protocol {

// ------------------------------------------------------------
// P2P 接近判据（逐条对齐 C# SfsMultiplayer.Protocol/P2PProximityPolicy.cs）
// 服务端撮合 P2P 直连时用它判断两枚火箭是否\"近到值得让各自的玩家直连\"。
//
// C# 原文：
//   if (!double.IsFinite(thresholdMeters) || thresholdMeters < 0) return false;
//   if (!string.Equals(first.Address, second.Address, StringComparison.Ordinal)) return false;
//   dx = second.X - first.X; dy = second.Y - first.Y;
//   if (!double.IsFinite(dx) || !double.IsFinite(dy)) return false;
//   return dx * dx + dy * dy <= thresholdMeters * thresholdMeters;
//
// 三条硬规则（C# 里就有，别\"优化\"掉）：
//   1. 阈值必须是有限值且非负，否则一律不合格；
//   2. 只有 Address 完全相同（序数比较，大小写敏感）才比较距离 —— 不同天体/场景的
//      X/Y 属于各自参考系，直接相减没有物理意义；
//   3. 距离是二维(X/Y)而不是三维，行星表面位置就是这么表达的。
//
// 说明：本文件此前是一份没有任何 TU 引用的\"接口存根\"（虚基类 + 永远返回 true 的
// 默认实现），而且 include 了并不存在的 protocol/state.hpp，一旦被包含就编不过。
// 现在按 C# 语义落地成真正会被服务端调用的策略，调用点见 src/server/p2p.cpp。
// ------------------------------------------------------------
class P2PProximityPolicy {
public:
    static bool is_eligible(const NetLocation& first, const NetLocation& second,
                            double threshold_meters) {
        if (!std::isfinite(threshold_meters) || threshold_meters < 0.0) return false;
        if (first.address != second.address) return false;
        const double dx = second.x - first.x;
        const double dy = second.y - first.y;
        if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
        return dx * dx + dy * dy <= threshold_meters * threshold_meters;
    }
};

}  // namespace sfs::protocol
