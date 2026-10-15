// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/state.hpp"
#include <unordered_set>

namespace sfs::protocol {

// ------------------------------------------------------------
// 权威分配策略（对齐 C# AuthorityAllocationPolicy.cs）
// 服务端决定哪个玩家拥有哪个火箭的物理权威
// ------------------------------------------------------------
class AuthorityAllocationPolicy {
public:
    // 给定玩家和火箭，返回该玩家是否应拥有权威
    virtual bool should_have_authority(std::int32_t player_id, std::int32_t rocket_id) const = 0;

    // 当前权威分配发生变化时的回调
    virtual void on_authority_changed(std::int32_t rocket_id, std::int32_t old_owner, std::int32_t new_owner) = 0;

    virtual ~AuthorityAllocationPolicy() = default;
};

// 默认实现：创建者拥有权威，断线则转移给最近的玩家
class DefaultAuthorityAllocationPolicy : public AuthorityAllocationPolicy {
public:
    bool should_have_authority(std::int32_t player_id, std::int32_t rocket_id) const override {
        // 存根：总是返回 true，实际逻辑后续填充
        return true;
    }
    void on_authority_changed(std::int32_t, std::int32_t, std::int32_t) override {}
};

} // namespace sfs::protocol
