// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <limits>
#include <stdexcept>

// ------------------------------------------------------------
// 前向声明（避免循环依赖） —— 必须在 namespace 之前包含
// ------------------------------------------------------------
#include "net/lidgren_buffer.hpp"
#include "packets.hpp"

namespace sfs::protocol {

// ------------------------------------------------------------
// 基础类型与常量（对齐 C# ProtocolPrimitives.cs）
// ------------------------------------------------------------

// 协议常量（在 codec.hpp 已定义，此处不重复）
// constexpr std::int32_t kProtocolVersion = 32;
// 复用 packets.hpp 中的定义，避免重复

// ------------------------------------------------------------
// 网络坐标、颜色、难度、包类型 —— 统一由 packets.hpp 定义
// types.hpp 仅提供序列化辅助函数声明
// ------------------------------------------------------------

// ------------------------------------------------------------
// 字符串/集合/NetLocation/Color3 的读写声明（实现放在 types.cpp）
// ------------------------------------------------------------
std::string read_string_bounded(net::NetReader& reader);
void write_string_bounded(net::NetWriter& writer, std::string_view value);

template <typename T, typename WriteFn, typename ReadFn>
inline void write_collection(net::NetWriter& writer, const std::vector<T>& values, WriteFn&& write_item) {
    if (values.size() > static_cast<std::size_t>(kMaxCollectionCount)) {
        throw std::invalid_argument("Collection is too large.");
    }
    writer.write_i32(static_cast<std::int32_t>(values.size()));
    for (const auto& item : values) {
        write_item(item);
    }
}

template <typename T, typename ReadFn>
inline std::vector<T> read_collection(net::NetReader& reader, ReadFn&& read_item) {
    const std::int32_t count = reader.read_i32();
    if (count < 0 || count > kMaxCollectionCount) {
        throw std::invalid_argument("Invalid collection count: " + std::to_string(count));
    }
    std::vector<T> result;
    result.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        result.push_back(read_item());
    }
    return result;
}

void write_netlocation(net::NetWriter& w, const NetLocation& loc);
NetLocation read_netlocation(net::NetReader& r);
void write_color3(net::NetWriter& w, const Color3& c);
Color3 read_color3(net::NetReader& r);

} // namespace sfs::protocol
