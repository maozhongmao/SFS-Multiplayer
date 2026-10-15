// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Lidgren NetBuffer 位级兼容读写器（纯 C++17，无外部依赖）。
//
// 唯一权威依据：Server/cpp/golden/ 下由真实 Lidgren.Network 2012.1.7.0 导出的黄金向量。
// 所有位序、字节序、变长整数与字符串格式均来自实测，不做任何推测。
//
// 已实测确认的线格式：
//   - 位序：LSB-first（低位在前），值从当前位偏移起按低位到高位写入
//   - 字节序：小端
//   - 定长标量：bool=1 位；i8/u8=8 位；i16/u16=16 位；i32/u32=32 位；i64/u64=64 位
//   - f32=32 位 IEEE754；f64=64 位 IEEE754（均为小端位模式直拷）
//   - 字符串：7 位一组的变长整数(UTF-8 字节数) + UTF-8 原始字节，无终止符
//   - 字节数组：裸拷贝，无长度前缀
#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sfs::net {

// 缓冲区越界或格式非法。读取端必须以异常中止，不能返回残缺值。
class BufferError final : public std::runtime_error {
public:
    explicit BufferError(const std::string& what) : std::runtime_error(what) {}
};

// 位写入器。缓冲区始终保持 8 字节冗余，便于按 64 位整体读写而不越界。
class NetWriter {
public:
    NetWriter() { m_buf.resize(kSlack, 0); }

    // ---- 定长标量 ----
    void write_bool(bool v) { write_bit(v); }
    void write_bit(bool v);

    void write_u8(uint8_t v) { write_bits_le(&v, 8); }
    void write_i8(int8_t v) { write_u8(static_cast<uint8_t>(v)); }

    void write_u16(uint16_t v) { write_bits_le(&v, 16); }
    void write_i16(int16_t v) { write_u16(static_cast<uint16_t>(v)); }

    void write_u32(uint32_t v) { write_bits_le(&v, 32); }
    void write_i32(int32_t v) { write_u32(static_cast<uint32_t>(v)); }

    void write_u64(uint64_t v) { write_bits_le(&v, 64); }
    void write_i64(int64_t v) { write_u64(static_cast<uint64_t>(v)); }

    // IEEE754 位模式直拷，保证 NaN 载荷与 .NET 一致
    void write_f32(float v);
    void write_f64(double v);

    // ---- 变长 ----
    // 7 位一组，低位在前，高位组带 0x80 继续标志
    void write_var_u32(uint32_t value);

    // 变长整数(UTF-8 字节数) + UTF-8 原始字节，无终止符
    void write_string(std::string_view utf8);

    // 裸拷贝，无长度前缀（与 Lidgren Write(byte[]) 一致）
    void write_bytes(const void* data, std::size_t byte_count);

    // ---- 状态 ----
    std::size_t bit_length() const { return m_bit_len; }
    std::size_t byte_length() const { return (m_bit_len + 7) / 8; }
    std::size_t capacity() const { return m_buf.size(); }
    const uint8_t* data() const { return m_buf.data(); }
    std::vector<uint8_t> get_bytes() const { return std::vector<uint8_t>(m_buf.begin(), m_buf.begin() + byte_length()); }
    std::size_t get_bit_length() const { return m_bit_len; }

    // 大写十六进制，长度 = byte_length()，与 C# Convert.ToHexString 一致
    std::string to_hex() const;

    // 重置为空白，复用已分配容量
    void clear();

private:
    static constexpr std::size_t kSlack = 8;

    void ensure_bits(std::size_t extra_bits);
    // 把 src 的低 nbits 位按 LSB-first 写到当前位偏移
    void write_bits_le(const void* src, std::size_t nbits);

    std::vector<uint8_t> m_buf;
    std::size_t m_bit_len = 0;
};

// 位读取器。越界一律抛 BufferError，绝不静默返回 0。
class NetReader {
public:
    NetReader(const uint8_t* data, std::size_t bit_length)
        : m_data(data), m_bit_len(bit_length) {}

    std::size_t bit_length() const { return m_bit_len; }
    std::size_t position() const { return m_pos; }
    std::size_t remaining_bits() const { return m_bit_len - m_pos; }
    bool at_end() const { return m_pos >= m_bit_len; }

    bool read_bool() { return read_bit(); }
    bool read_bit();

    uint8_t read_u8() { return static_cast<uint8_t>(read_bits_le(8)); }
    int8_t read_i8() { return static_cast<int8_t>(read_u8()); }
    uint16_t read_u16() { return static_cast<uint16_t>(read_bits_le(16)); }
    int16_t read_i16() { return static_cast<int16_t>(read_u16()); }
    uint32_t read_u32() { return static_cast<uint32_t>(read_bits_le(32)); }
    int32_t read_i32() { return static_cast<int32_t>(read_u32()); }
    uint64_t read_u64() { return read_bits_le(64); }
    int64_t read_i64() { return static_cast<int64_t>(read_u64()); }

    float read_f32();
    double read_f64();

    uint32_t read_var_u32();
    std::string read_string();
    // 读出恰好 byte_count 个裸字节
    void read_bytes(void* out, std::size_t byte_count);

private:
    uint64_t read_bits_le(std::size_t nbits);
    void require(std::size_t nbits) const;

    const uint8_t* m_data;
    std::size_t m_bit_len;
    std::size_t m_pos = 0;
};

} // namespace sfs::net
