// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/state.hpp"

namespace sfs::protocol {

// ------------------------------------------------------------
// 碎片/垃圾清理规则（对齐 C# DebrisControlRules.cs）
// ------------------------------------------------------------
class DebrisControlRules {
public:
    // 无人控制且部件数 <= 阈值的火箭是否可清理
    virtual bool can_cleanup_rocket(const RocketState& rocket, std::int32_t max_parts) const = 0;

    // 定期清理的最小间隔（秒）
    virtual std::int32_t cleanup_interval_seconds() const = 0;

    virtual ~DebrisControlRules() = default;
};

class DefaultDebrisControlRules : public DebrisControlRules {
public:
    bool can_cleanup_rocket(const RocketState& rocket, std::int32_t max_parts) const override {
        return static_cast<std::int32_t>(rocket.parts.size()) <= max_parts;
    }
    std::int32_t cleanup_interval_seconds() const override { return 30; }
};

} // namespace sfs::protocol
