// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/state.hpp"

namespace sfs::protocol {

// ------------------------------------------------------------
// 火箭延迟补偿（对齐 C# RocketLatencyCompensation.cs）
// 用于客户端预测与服务端权威同步的时间插值
// ------------------------------------------------------------
class RocketLatencyCompensation {
public:
    virtual ~RocketLatencyCompensation() = default;

    // 给定服务端状态与客户端已知延迟，计算插值后的显示状态
    virtual RocketState interpolate(const RocketState& server_state, double rtt_ms) const = 0;
};

class DefaultRocketLatencyCompensation : public RocketLatencyCompensation {
public:
    RocketState interpolate(const RocketState& server_state, double) const override {
        // 存根：直接返回服务端状态，不做插值
        return server_state;
    }
};

} // namespace sfs::protocol
