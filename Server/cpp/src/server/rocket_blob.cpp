// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/rocket_blob.hpp"

#include <cstring>

#include "net/lidgren_buffer.hpp"
#include "third_party/miniz/miniz.h"

namespace sfs::server {
namespace {

// 部件表的序列化：借一个只装 parts 的临时 RocketState 走既有序列化器，
// 不重复实现线格式（避免与协议漂移）。
std::vector<std::uint8_t> serialize_parts(
    const std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& parts) {
    sfs::protocol::RocketState tmp;
    tmp.parts = parts;
    sfs::net::NetWriter writer;
    tmp.serialize(writer);
    return writer.get_bytes();   // 按写入的位长取整字节（NetWriter 没有 size()，是 byte_length()/get_bytes()）
}

}  // namespace

PartsBlob pack_parts(const std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& parts) {
    PartsBlob blob;
    blob.part_count = static_cast<std::uint32_t>(parts.size());
    const std::vector<std::uint8_t> raw = serialize_parts(parts);

    mz_ulong bound = mz_compressBound(static_cast<mz_ulong>(raw.size()));
    std::vector<std::uint8_t> out(bound);
    mz_ulong out_len = bound;
    const int rc = mz_compress2(out.data(), &out_len, raw.data(),
                                static_cast<mz_ulong>(raw.size()), MZ_DEFAULT_LEVEL);
    if (rc == MZ_OK && out_len > 0 && out_len < raw.size()) {
        out.resize(out_len);
        blob.bytes = std::move(out);
        blob.compressed = true;
    } else {
        // 压不动（部件表很小）就原样存：解包用 compressed 标志区分，避免白付一次解压
        blob.bytes = raw;
        blob.compressed = false;
    }
    return blob;
}

bool unpack_parts(const PartsBlob& blob,
                  std::vector<std::pair<std::int32_t, sfs::protocol::PartState>>& out) {
    std::vector<std::uint8_t> raw;
    if (blob.compressed) {
        std::uint64_t guess = static_cast<std::uint64_t>(blob.bytes.size()) * 8u + 1024u;
        for (int attempt = 0; attempt < 4; ++attempt) {
            raw.assign(static_cast<std::size_t>(guess), 0);
            mz_ulong raw_len = static_cast<mz_ulong>(raw.size());
            const int rc = mz_uncompress(raw.data(), &raw_len, blob.bytes.data(),
                                         static_cast<mz_ulong>(blob.bytes.size()));
            if (rc == MZ_OK) {
                raw.resize(raw_len);
                break;
            }
            if (rc != MZ_BUF_ERROR) return false;   // 真坏了，不是"缓冲不够"
            guess *= 2;                             // 缓冲不够就翻倍重试
            if (attempt == 3) return false;
        }
    } else {
        raw = blob.bytes;
    }

    sfs::protocol::RocketState tmp;
    // 注意：NetReader 第二个参数是【位长】不是字节数。传字节数会静默只读 1/8、反序列化失败。
    sfs::net::NetReader reader(raw.data(), raw.size() * 8u);
    tmp.deserialize(reader);
    out = std::move(tmp.parts);
    return out.size() == blob.part_count;
}

}  // namespace sfs::server
