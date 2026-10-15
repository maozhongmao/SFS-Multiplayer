// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/state.hpp"

namespace sfs::protocol {

// ------------------------------------------------------------
// 控制所有权策略（对齐 C# ControlOwnershipPolicy.cs）
// 决定玩家能否控制特定火箭/部件
// ------------------------------------------------------------
class ControlOwnershipPolicy {
public:
    virtual bool can_control(std::int32_t player_id, std::int32_t rocket_id) const = 0;
    virtual bool can_control_part(std::int32_t player_id, std::int32_t rocket_id, std::int32_t part_id) const = 0;

    virtual ~ControlOwnershipPolicy() = default;
};

class DefaultControlOwnershipPolicy : public ControlOwnershipPolicy {
public:
    bool can_control(std::int32_t, std::int32_t) const override { return true; }
    bool can_control_part(std::int32_t, std::int32_t, std::int32_t) const override { return true; }
};

} // namespace sfs::protocol
