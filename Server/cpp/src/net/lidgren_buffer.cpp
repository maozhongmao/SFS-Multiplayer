// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Lidgren NetBuffer 位级兼容读写器实现。格式依据见 lidgren_buffer.hpp 顶部说明。
#include "lidgren_buffer.hpp"

#include <cstring>

namespace sfs::net {

namespace {

constexpr char kHexDigits[] = "0123456789ABCDEF";

} // namespace

// ---------------------------------------------------------------- NetWriter

void NetWriter::clear() {
    std::memset(m_buf.data(), 0, m_buf.size());
    m_bit_len = 0;
}

void NetWriter::ensure_bits(std::size_t extra_bits) {
    const std::size_t need = (m_bit_len + extra_bits + 7) / 8 + kSlack;
    if (m_buf.size() < need) {
        m_buf.resize(need, 0);
    }
}

void NetWriter::write_bit(bool v) {
    if (!v) {
        // 零位无需清除：缓冲区在扩容时已填 0，且每位只被写过一次
        ensure_bits(1);
        ++m_bit_len;
        return;
    }
    ensure_bits(1);
    const std::size_t byte = m_bit_len >> 3;
    const std::size_t shift = m_bit_len & 7;
    m_buf[byte] |= static_cast<uint8_t>(1u << shift);
    ++m_bit_len;
}

void NetWriter::write_bits_le(const void* src, std::size_t nbits) {
    if (nbits == 0) {
        return;
    }
    ensure_bits(nbits);

    const auto* s = static_cast<const uint8_t*>(src);
    std::size_t src_bit = 0;
    std::size_t dst_bit = m_bit_len;
    std::size_t remaining = nbits;

    // 按 32 位小块搬运：每块最多 24 位，保证移位不溢出 32 位
    while (remaining > 0) {
        const std::size_t chunk = remaining < 24 ? remaining : 24;

        uint32_t chunk_value = 0;
        for (std::size_t k = 0; k < chunk; ++k) {
            const std::size_t p = src_bit + k;
            if (((s[p >> 3] >> (p & 7)) & 1u) != 0u) {
                chunk_value |= (1u << k);
            }
        }

        const std::size_t byte = dst_bit >> 3;
        const std::size_t shift = dst_bit & 7;

        uint64_t window = 0;
        for (std::size_t k = 0; k < 6; ++k) {
            window |= static_cast<uint64_t>(m_buf[byte + k]) << (8 * k);
        }
        window |= static_cast<uint64_t>(chunk_value) << shift;
        for (std::size_t k = 0; k < 6; ++k) {
            m_buf[byte + k] = static_cast<uint8_t>(window >> (8 * k));
        }

        dst_bit += chunk;
        src_bit += chunk;
        remaining -= chunk;
    }

    m_bit_len += nbits;
}

void NetWriter::write_f32(float v) {
    uint32_t bits = 0;
    static_assert(sizeof(float) == 4, "float 必须是 32 位");
    std::memcpy(&bits, &v, 4);
    write_u32(bits);
}

void NetWriter::write_f64(double v) {
    uint64_t bits = 0;
    static_assert(sizeof(double) == 8, "double 必须是 64 位");
    std::memcpy(&bits, &v, 8);
    write_u64(bits);
}

void NetWriter::write_var_u32(uint32_t value) {
    while (value >= 0x80u) {
        write_u8(static_cast<uint8_t>((value & 0x7Fu) | 0x80u));
        value >>= 7;
    }
    write_u8(static_cast<uint8_t>(value));
}

void NetWriter::write_string(std::string_view utf8) {
    // Lidgren Write(string) 的前缀是 UTF-8 字节数（已由 '羊城通' -> 09 + 9 字节实测确认）
    write_var_u32(static_cast<uint32_t>(utf8.size()));
    if (!utf8.empty()) {
        write_bits_le(utf8.data(), utf8.size() * 8);
    }
}

void NetWriter::write_bytes(const void* data, std::size_t byte_count) {
    if (byte_count == 0) {
        return;
    }
    write_bits_le(data, byte_count * 8);
}

std::string NetWriter::to_hex() const {
    const std::size_t bytes = byte_length();
    std::string out;
    out.resize(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        const uint8_t b = m_buf[i];
        out[i * 2] = kHexDigits[b >> 4];
        out[i * 2 + 1] = kHexDigits[b & 0x0Fu];
    }
    return out;
}

// ---------------------------------------------------------------- NetReader

void NetReader::require(std::size_t nbits) const {
    if (m_pos + nbits > m_bit_len) {
        throw BufferError("位读取越界：需要 " + std::to_string(nbits) +
                          " 位，位置 " + std::to_string(m_pos) +
                          "，总长 " + std::to_string(m_bit_len) + " 位");
    }
}

bool NetReader::read_bit() {
    require(1);
    const uint8_t b = m_data[m_pos >> 3];
    const bool v = ((b >> (m_pos & 7)) & 1u) != 0u;
    ++m_pos;
    return v;
}

uint64_t NetReader::read_bits_le(std::size_t nbits) {
    if (nbits == 0) {
        return 0;
    }
    if (nbits > 64) {
        throw BufferError("位读取宽度超过 64 位");
    }
    // 一次最多只搬 32 位：非对齐时窗口要跨越 ceil((shift+nbits)/8) 字节，
    // 64 位 + 非零偏移会需要 9 字节（65 位），超出一个 uint64 的容量，
    // 强行一次读会触发 <<64 的未定义行为并把第 9 字节渗进低位。
    if (nbits <= 32) {
        require(nbits);
        const std::size_t byte = m_pos >> 3;
        const std::size_t shift = m_pos & 7;
        const std::size_t need_bytes = (shift + nbits + 7) / 8;  // 最多 5 字节
        uint64_t window = 0;
        for (std::size_t k = 0; k < need_bytes; ++k) {
            window |= static_cast<uint64_t>(m_data[byte + k]) << (8 * k);
        }
        m_pos += nbits;
        return (window >> shift) & ((static_cast<uint64_t>(1) << nbits) - 1u);
    }
    // 33..64 位：拆成低 32 位 + 高 (nbits-32) 位
    const uint64_t low = read_bits_le(32);
    const uint64_t high = read_bits_le(nbits - 32);
    return low | (high << 32);
}

float NetReader::read_f32() {
    const uint32_t bits = read_u32();
    float v = 0.0f;
    std::memcpy(&v, &bits, 4);
    return v;
}

double NetReader::read_f64() {
    const uint64_t bits = read_u64();
    double v = 0.0;
    std::memcpy(&v, &bits, 8);
    return v;
}

uint32_t NetReader::read_var_u32() {
    uint32_t value = 0;
    int shift = 0;
    // 7 位一组，最多 5 组覆盖 32 位
    for (int i = 0; i < 5; ++i) {
        const uint8_t b = read_u8();
        value |= static_cast<uint32_t>(b & 0x7Fu) << shift;
        if ((b & 0x80u) == 0u) {
            return value;
        }
        shift += 7;
    }
    throw BufferError("变长整数超过 5 字节，格式非法");
}

std::string NetReader::read_string() {
    const uint32_t len = read_var_u32();
    std::string out;
    out.resize(len);
    if (len > 0) {
        read_bytes(out.data(), len);
    }
    return out;
}

void NetReader::read_bytes(void* out, std::size_t byte_count) {
    if (byte_count == 0) {
        return;
    }
    require(byte_count * 8);
    auto* dst = static_cast<uint8_t*>(out);
    std::size_t dst_bit = 0;
    std::size_t src_bit = m_pos;
    const std::size_t remaining = byte_count * 8;
    std::size_t left = remaining;
    while (left > 0) {
        const std::size_t chunk = left < 24 ? left : 24;
        uint32_t chunk_value = 0;
        const std::size_t src_byte = src_bit >> 3;
        const std::size_t src_shift = src_bit & 7;
        const std::size_t need = (src_shift + chunk + 7) / 8;
        uint64_t window = 0;
        for (std::size_t k = 0; k < need; ++k) {
            window |= static_cast<uint64_t>(m_data[src_byte + k]) << (8 * k);
        }
        chunk_value = static_cast<uint32_t>((window >> src_shift) &
                                            ((chunk >= 32) ? 0xFFFFFFFFu
                                                           : ((1u << chunk) - 1u)));
        for (std::size_t k = 0; k < chunk; ++k) {
            const std::size_t p = dst_bit + k;
            if (((chunk_value >> k) & 1u) != 0u) {
                dst[p >> 3] |= static_cast<uint8_t>(1u << (p & 7));
            }
        }
        dst_bit += chunk;
        src_bit += chunk;
        left -= chunk;
    }
    m_pos += remaining;
}

} // namespace sfs::net
