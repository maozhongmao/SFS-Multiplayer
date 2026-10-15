// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "protocol/codec.hpp"
#include "protocol/types.hpp"
#include "protocol/models/packets.hpp"
#include <cstdint>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <optional>
#include <functional>

namespace sfs::net {

// ------------------------------------------------------------
// TCP 发送队列（对齐 C# TcpSendQueue）
// ------------------------------------------------------------
class TcpSendQueue {
public:
    // 关键帧（必须按序发送、不可丢弃）
    void enqueue_critical(protocol::TcpFrame frame) {
        std::lock_guard<std::mutex> lk(mu_);
        critical_.push(std::move(frame));
    }

    // 状态更新帧（可合并：同 key 仅保留最新）
    void enqueue_latest(std::int64_t key, protocol::TcpFrame frame) {
        std::lock_guard<std::mutex> lk(mu_);
        if (latest_.count(key)) ++overwritten_;
        latest_[key] = std::move(frame);
    }

    std::int64_t overwritten_states() const { return overwritten_; }
    std::size_t count() const {
        std::lock_guard<std::mutex> lk(mu_);
        return critical_.size() + latest_.size();
    }

    // 尝试取出下一帧（优先关键帧，再最新状态）
    bool try_dequeue(protocol::TcpFrame& out) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!critical_.empty()) {
            out = std::move(critical_.front());
            critical_.pop();
            return true;
        }
        if (!latest_.empty()) {
            auto it = latest_.begin();
            out = std::move(it->second);
            latest_.erase(it);
            return true;
        }
        return false;
    }

private:
    mutable std::mutex mu_;
    std::queue<protocol::TcpFrame> critical_;
    std::unordered_map<std::int64_t, protocol::TcpFrame> latest_;
    std::int64_t overwritten_ = 0;
};

// ------------------------------------------------------------
// NetPayload 编解码（对齐 C# NetPayloadCodec）
// ------------------------------------------------------------
struct NetPayload {
    std::vector<std::uint8_t> data;
    std::int32_t bit_length = 0;

    NetPayload() = default;
    NetPayload(std::vector<std::uint8_t>&& d, std::int32_t bits) : data(std::move(d)), bit_length(bits) {}
};

// 将任意包按 PacketType 编码为 NetPayload
NetPayload encode_payload(std::uint8_t packet_type, const void* packet_ptr);

// 将 NetPayload 解码为具体包结构（按 packet_type 分发）
bool decode_payload(std::uint8_t packet_type, const NetPayload& payload, void* out);

} // namespace sfs::net
