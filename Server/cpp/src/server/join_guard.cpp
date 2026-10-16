// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "server/join_guard.hpp"

#include <cstddef>
#include <vector>

namespace sfs::server {
namespace {

// ---------------------------------------------------------------- UTF-8 解码
// 非法字节序列按 U+FFFD 处理并只前进 1 字节（C# Encoding.UTF8 也用 U+FFFD 替换坏字节），
// 这样坏字节不会让后面的合法字符被整段跳过 —— 校验必须"看得见"每一个字节。
struct Decoded {
    std::uint32_t code_point;
    std::size_t bytes;
};

Decoded decode_utf8(const std::string& value, std::size_t pos) {
    const auto b0 = static_cast<std::uint8_t>(value[pos]);
    if (b0 < 0x80) return {b0, 1};

    const auto at = [&](std::size_t i) -> std::uint32_t {
        return static_cast<std::uint8_t>(value[pos + i]);
    };
    const auto is_cont = [&](std::size_t i) -> bool {
        return pos + i < value.size() && (at(i) & 0xC0u) == 0x80u;
    };

    if ((b0 & 0xE0u) == 0xC0u && is_cont(1)) {
        const std::uint32_t cp = ((b0 & 0x1Fu) << 6) | (at(1) & 0x3Fu);
        if (cp >= 0x80u) return {cp, 2};   // 拒绝过长编码
    } else if ((b0 & 0xF0u) == 0xE0u && is_cont(1) && is_cont(2)) {
        const std::uint32_t cp = ((b0 & 0x0Fu) << 12) | ((at(1) & 0x3Fu) << 6) | (at(2) & 0x3Fu);
        if (cp >= 0x800u && !(cp >= 0xD800u && cp <= 0xDFFFu)) return {cp, 3};
    } else if ((b0 & 0xF8u) == 0xF0u && is_cont(1) && is_cont(2) && is_cont(3)) {
        const std::uint32_t cp = ((b0 & 0x07u) << 18) | ((at(1) & 0x3Fu) << 12)
            | ((at(2) & 0x3Fu) << 6) | (at(3) & 0x3Fu);
        if (cp >= 0x10000u && cp <= 0x10FFFFu) return {cp, 4};
    }
    return {0xFFFDu, 1};
}

// C# char.IsWhiteSpace 的等价集合（Trim 用的就是它）。
bool is_whitespace(std::uint32_t cp) {
    switch (cp) {
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x20:
        case 0x85: case 0xA0: case 0x1680:
        case 0x2028: case 0x2029: case 0x202F: case 0x205F: case 0x3000:
            return true;
        default:
            return cp >= 0x2000u && cp <= 0x200Au;
    }
}

unsigned char ascii_lower(unsigned char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<unsigned char>(c - 'A' + 'a') : c;
}

// ---------------------------------------------------------------- SHA-256
// 只为对齐 C# 的 PasswordMatches（两边各自 SHA-256 再比较）而实现，不引入外部依赖
//（目标设备上是交叉编译的静态产物，多一个加密库就多一份部署风险）。
constexpr std::uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

std::uint32_t rotr(std::uint32_t value, int bits) {
    return (value >> bits) | (value << (32 - bits));
}

}  // namespace

std::array<std::uint8_t, 32> sha256_bytes(const std::string& data) {
    std::uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};

    // 填充：0x80 + 若干 0x00 到 56 (mod 64)，再补 64 位大端位长度
    std::vector<std::uint8_t> msg(data.begin(), data.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * 8u;
    msg.push_back(0x80u);
    while (msg.size() % 64u != 56u) msg.push_back(0x00u);
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>(bit_length >> (8 * i)));
    }

    for (std::size_t offset = 0; offset < msg.size(); offset += 64) {
        std::uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            const std::size_t p = offset + static_cast<std::size_t>(i) * 4u;
            w[i] = (static_cast<std::uint32_t>(msg[p]) << 24)
                | (static_cast<std::uint32_t>(msg[p + 1]) << 16)
                | (static_cast<std::uint32_t>(msg[p + 2]) << 8)
                | static_cast<std::uint32_t>(msg[p + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 = hh + s1 + ch + kSha256K[i] + w[i];
            const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + maj;
            hh = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d;
        h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[static_cast<std::size_t>(i) * 4u] = static_cast<std::uint8_t>(h[i] >> 24);
        digest[static_cast<std::size_t>(i) * 4u + 1] = static_cast<std::uint8_t>(h[i] >> 16);
        digest[static_cast<std::size_t>(i) * 4u + 2] = static_cast<std::uint8_t>(h[i] >> 8);
        digest[static_cast<std::size_t>(i) * 4u + 3] = static_cast<std::uint8_t>(h[i]);
    }
    return digest;
}

// ---------------------------------------------------------------- 规则

std::string trim_username(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();

    while (begin < end) {
        const Decoded d = decode_utf8(value, begin);
        if (!is_whitespace(d.code_point)) break;
        begin += d.bytes;
    }
    while (end > begin) {
        // 反向找到一个码点的起点（UTF-8 续字节是 0b10xxxxxx）
        std::size_t start = end - 1;
        while (start > begin && (static_cast<std::uint8_t>(value[start]) & 0xC0u) == 0x80u) --start;
        const Decoded d = decode_utf8(value, start);
        if (!is_whitespace(d.code_point)) break;
        end = start;
    }
    return value.substr(begin, end - begin);
}

int username_utf16_length(const std::string& value) {
    int units = 0;
    for (std::size_t pos = 0; pos < value.size();) {
        const Decoded d = decode_utf8(value, pos);
        pos += d.bytes;
        units += (d.code_point > 0xFFFFu) ? 2 : 1;
    }
    return units;
}

bool has_control_char(const std::string& value) {
    for (std::size_t pos = 0; pos < value.size();) {
        const Decoded d = decode_utf8(value, pos);
        pos += d.bytes;
        if (d.code_point < 0x20u || (d.code_point >= 0x7Fu && d.code_point <= 0x9Fu)) return true;
    }
    return false;
}

bool username_equals_ci(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto ca = static_cast<unsigned char>(a[i]);
        const auto cb = static_cast<unsigned char>(b[i]);
        if (ca == cb) continue;
        if (ca < 0x80u && cb < 0x80u && ascii_lower(ca) == ascii_lower(cb)) continue;
        return false;
    }
    return true;
}

bool password_matches(const std::string& supplied, const std::string& expected) {
    if (expected.empty()) return true;   // C#：expected.Length == 0 -> true（不校验）
    const auto left = sha256_bytes(supplied);
    const auto right = sha256_bytes(expected);
    // C# CryptographicOperations.FixedTimeEquals：逐字节累积，不提前返回
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        diff = static_cast<std::uint8_t>(diff | static_cast<std::uint8_t>(left[i] ^ right[i]));
    }
    return diff == 0;
}

}  // namespace sfs::server
