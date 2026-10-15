// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/state.hpp"

namespace sfs::protocol {

// ------------------------------------------------------------
// 时间加速控制规则（对齐 C# TimeWarpControlRules.cs）
// ------------------------------------------------------------
class TimeWarpControlRules {
public:
    virtual bool can_request_timewarp(std::int32_t player_id) const = 0;
    virtual bool can_vote_timewarp(std::int32_t player_id) const = 0;
    virtual std::int32_t max_multiplier() const = 0;

    virtual ~TimeWarpControlRules() = default;
};

class DefaultTimeWarpControlRules : public TimeWarpControlRules {
public:
    bool can_request_timewarp(std::int32_t) const override { return true; }
    bool can_vote_timewarp(std::int32_t) const override { return true; }
    std::int32_t max_multiplier() const override { return 10; }
};

} // namespace sfs::protocol
