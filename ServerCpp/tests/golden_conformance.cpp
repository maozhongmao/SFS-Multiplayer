// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 黄金向量一致性测试。
//
// 读取 ServerCpp/golden/ 下由真实 Lidgren.Network 2012.1.7.0 导出的向量，
// 用 C++ 的 NetWriter/NetReader 复现，逐位、逐字节比对。
// 任何不一致都必须让本测试以非零退出码结束；绝不允许"接近正确"。
#include "../src/net/lidgren_buffer.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using sfs::net::NetReader;
using sfs::net::NetWriter;
using sfs::net::BufferError;

namespace {

int g_pass = 0;
int g_fail = 0;

void report_fail(const std::string& name, const std::string& detail) {
    ++g_fail;
    std::cout << "  [失败] " << name << " : " << detail << "\n";
}

// ---------------------------------------------------------------- 工具

std::string to_hex(const uint8_t* data, std::size_t bytes) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.resize(bytes * 2);
    for (std::size_t i = 0; i < bytes; ++i) {
        out[i * 2] = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0F];
    }
    return out;
}

bool from_hex(const std::string& hex, std::vector<uint8_t>& out) {
    if (hex.size() % 2 != 0) {
        return false;
    }
    out.clear();
    out.reserve(hex.size() / 2);
    auto val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = val(hex[i]);
        const int lo = val(hex[i + 1]);
        if (hi < 0 || lo < 0) {
            return false;
        }
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

const char kB64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_decode(const std::string& in) {
    int table[256];
    std::memset(table, -1, sizeof(table));
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kB64Chars[i])] = i;
    }
    std::string out;
    int buffer = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=') {
            break;
        }
        const int v = table[static_cast<unsigned char>(c)];
        if (v < 0) {
            continue;
        }
        buffer = (buffer << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<char>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == sep) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    parts.push_back(cur);
    return parts;
}

std::string trim_cr(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

std::vector<std::string> read_lines(const std::string& path) {
    std::vector<std::string> lines;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("无法打开黄金向量文件: " + path);
    }
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(trim_cr(line));
    }
    return lines;
}

// 比对编码结果与实际向量
void expect_encoding(const std::string& name, const NetWriter& w,
                     std::size_t want_bits, const std::string& want_hex) {
    const std::size_t got_bits = w.bit_length();
    const std::size_t got_bytes = w.byte_length();
    const std::string got_hex = to_hex(w.data(), got_bytes);

    if (got_bits != want_bits) {
        report_fail(name, "位长不符: 期望 " + std::to_string(want_bits) +
                              "，实际 " + std::to_string(got_bits));
        return;
    }
    if (got_hex != want_hex) {
        std::string detail = "字节不符:\n";
        detail += "            期望 " + want_hex + "\n";
        detail += "            实际 " + got_hex;
        report_fail(name, detail);
        return;
    }
    ++g_pass;
}

// ---------------------------------------------------------------- 原语用例

// 把名字解析成值并重新编码，与向量比对；再解码向量验证回读一致
void run_primitive(const std::string& line) {
    const auto parts = split(line, '|');
    if (parts.size() != 3) {
        report_fail("(解析)", "原语行字段数不是 3: " + line);
        return;
    }
    const std::string& name = parts[0];
    const std::size_t want_bits = static_cast<std::size_t>(std::strtoull(parts[1].c_str(), nullptr, 10));
    const std::string& want_hex = parts[2];

    std::vector<uint8_t> want_bytes;
    if (!from_hex(want_hex, want_bytes)) {
        report_fail(name, "向量十六进制非法");
        return;
    }

    NetWriter w;

    if (name == "bool.false") {
        w.write_bool(false);
    } else if (name == "bool.true") {
        w.write_bool(true);
    } else if (name.rfind("i32.", 0) == 0) {
        w.write_i32(static_cast<int32_t>(std::strtoll(name.c_str() + 4, nullptr, 10)));
    } else if (name.rfind("u32.", 0) == 0) {
        w.write_u32(static_cast<uint32_t>(std::strtoull(name.c_str() + 4, nullptr, 10)));
    } else if (name.rfind("u8.", 0) == 0) {
        w.write_u8(static_cast<uint8_t>(std::strtoul(name.c_str() + 3, nullptr, 10)));
    } else if (name.rfind("i16.", 0) == 0) {
        w.write_i16(static_cast<int16_t>(std::strtol(name.c_str() + 4, nullptr, 10)));
    } else if (name.rfind("i64.", 0) == 0) {
        w.write_i64(static_cast<int64_t>(std::strtoll(name.c_str() + 4, nullptr, 10)));
    } else if (name.rfind("f32.", 0) == 0) {
        const std::string v = name.substr(4);
        if (v == "NaN") {
            // .NET float.NaN 的精确位模式；std::nanf 载荷不保证一致
            const uint32_t bits = 0xFFC00000u;
            float f = 0.0f;
            std::memcpy(&f, &bits, 4);
            w.write_f32(f);
        } else if (v == "Infinity") {
            w.write_f32(std::numeric_limits<float>::infinity());
        } else if (v == "-Infinity") {
            w.write_f32(-std::numeric_limits<float>::infinity());
        } else {
            w.write_f32(std::strtof(v.c_str(), nullptr));
        }
    } else if (name.rfind("f64.", 0) == 0) {
        const std::string v = name.substr(4);
        if (v == "NaN") {
            // .NET double.NaN 的精确位模式
            const uint64_t bits = 0xFFF8000000000000ull;
            double d = 0.0;
            std::memcpy(&d, &bits, 8);
            w.write_f64(d);
        } else if (v == "Infinity") {
            w.write_f64(std::numeric_limits<double>::infinity());
        } else if (v == "-Infinity") {
            w.write_f64(-std::numeric_limits<double>::infinity());
        } else {
            w.write_f64(std::strtod(v.c_str(), nullptr));
        }
    } else if (name.rfind("str.", 0) == 0) {
        w.write_string(base64_decode(name.substr(4)));
    } else if (name == "bytes.empty") {
        w.write_bytes(nullptr, 0);
    } else if (name == "bytes.5") {
        const uint8_t raw[] = {0, 1, 2, 253, 254};
        w.write_bytes(raw, sizeof(raw));
    } else if (name == "bytes.300") {
        std::vector<uint8_t> raw(300);
        for (std::size_t i = 0; i < raw.size(); ++i) {
            raw[i] = static_cast<uint8_t>(i & 0xFF);
        }
        w.write_bytes(raw.data(), raw.size());
    } else {
        report_fail(name, "未知原语用例");
        return;
    }

    expect_encoding(name, w, want_bits, want_hex);

    // 反向：用向量字节构造读取器，确认不越界且可完整消费
    try {
        NetReader r(want_bytes.data(), want_bits);
        if (name.rfind("str.", 0) == 0) {
            const std::string back = r.read_string();
            const std::string expect = base64_decode(name.substr(4));
            if (back != expect) {
                report_fail(name, "回读字符串不一致");
                return;
            }
        } else if (name == "bytes.5") {
            uint8_t out[5] = {0};
            r.read_bytes(out, 5);
            const uint8_t expect[5] = {0, 1, 2, 253, 254};
            if (std::memcmp(out, expect, 5) != 0) {
                report_fail(name, "回读字节数组不一致");
                return;
            }
        } else if (name == "bytes.300") {
            std::vector<uint8_t> out(300);
            r.read_bytes(out.data(), 300);
            for (std::size_t i = 0; i < 300; ++i) {
                if (out[i] != static_cast<uint8_t>(i & 0xFF)) {
                    report_fail(name, "回读 300 字节数组第 " + std::to_string(i) + " 项不一致");
                    return;
                }
            }
        } else if (name.rfind("f32.", 0) == 0) {
            const uint32_t got = [&] {
                uint32_t b = 0;
                const float f = r.read_f32();
                std::memcpy(&b, &f, 4);
                return b;
            }();
            uint32_t expect = 0;
            std::memcpy(&expect, w.data(), 4);
            if (got != expect) {
                report_fail(name, "回读 f32 位模式不一致");
                return;
            }
        } else if (name.rfind("f64.", 0) == 0) {
            uint64_t got = 0;
            const double d = r.read_f64();
            std::memcpy(&got, &d, 8);
            uint64_t expect = 0;
            std::memcpy(&expect, w.data(), 8);
            if (got != expect) {
                report_fail(name, "回读 f64 位模式不一致");
                return;
            }
        }
    } catch (const BufferError& e) {
        report_fail(name, std::string("回读越界: ") + e.what());
    }
}

// ---------------------------------------------------------------- 序列用例

void run_sequence(const std::string& line) {
    const auto parts = split(line, '|');
    if (parts.size() != 3) {
        report_fail("(解析)", "序列行字段数不是 3: " + line);
        return;
    }
    const std::string& name = parts[0];
    const std::size_t want_bits = static_cast<std::size_t>(std::strtoull(parts[1].c_str(), nullptr, 10));
    const std::string& want_hex = parts[2];

    NetWriter w;

    if (name == "seq.bool1_then_i32") {
        w.write_bool(true);
        w.write_i32(0x12345678);
    } else if (name == "seq.bool3_then_f32") {
        w.write_bool(true);
        w.write_bool(false);
        w.write_bool(true);
        w.write_f32(1.5f);
    } else if (name == "seq.bool_then_str") {
        w.write_bool(true);
        w.write_string("hello");
    } else if (name == "seq.bool_then_bytes") {
        w.write_bool(true);
        const uint8_t raw[] = {0xAA, 0xBB};
        w.write_bytes(raw, sizeof(raw));
    } else if (name == "seq.i32_f32_f64") {
        w.write_i32(7);
        w.write_f32(2.5f);
        w.write_f64(3.25);
    } else if (name == "seq.many_bools") {
        for (int i = 0; i < 17; ++i) {
            w.write_bool(i % 3 == 0);
        }
    } else if (name == "seq.u8_bool_u8") {
        w.write_u8(200);
        w.write_bool(true);
        w.write_u8(201);
    } else if (name == "seq.netlocation") {
        w.write_f64(1.5);
        w.write_f64(-2.25);
        w.write_f64(0.125);
        w.write_f64(1000.5);
        w.write_string("Earth");
    } else if (name == "seq.color3") {
        w.write_f32(0.2f);
        w.write_f32(0.8f);
        w.write_f32(1.0f);
    } else if (name == "seq.collection_i32x3") {
        w.write_i32(3);
        w.write_i32(10);
        w.write_i32(20);
        w.write_i32(30);
    } else if (name == "seq.bool_then_f64") {
        w.write_bool(true);
        w.write_f64(12345.678);
    } else if (name == "seq.i32_then_f64") {
        w.write_i32(42);
        w.write_f64(3.14159);
    } else if (name == "seq.bool_then_i64") {
        w.write_bool(true);
        w.write_i64(123456789012345LL);
    } else {
        report_fail(name, "未知序列用例");
        return;
    }

    expect_encoding(name, w, want_bits, want_hex);
}

// ---------------------------------------------------------------- 附加不变量

void run_invariants() {
    // 1) 长位流跨字节边界：写 1000 个随机位，回读必须完全一致
    {
        std::vector<bool> bits(1000);
        uint32_t seed = 0x12345678u;
        for (auto&& b : bits) {
            seed = seed * 1664525u + 1013904223u;
            b = (seed >> 16) & 1u;
        }
        NetWriter w;
        for (bool b : bits) {
            w.write_bool(b);
        }
        NetReader r(w.data(), w.bit_length());
        bool ok = true;
        for (std::size_t i = 0; i < bits.size(); ++i) {
            if (r.read_bool() != bits[i]) {
                ok = false;
                break;
            }
        }
        if (ok) {
            ++g_pass;
        } else {
            report_fail("invariant.bitstream1000", "位流回读不一致");
        }
    }

    // 2) 变长整数边界
    {
        const uint32_t cases[] = {0u, 1u, 127u, 128u, 129u, 16383u, 16384u,
                                  2097151u, 2097152u, 0xFFFFFFFFu};
        bool ok = true;
        for (uint32_t v : cases) {
            NetWriter w;
            w.write_var_u32(v);
            NetReader r(w.data(), w.bit_length());
            if (r.read_var_u32() != v) {
                ok = false;
                std::cout << "      变长整数失败值: " << v << "\n";
                break;
            }
        }
        if (ok) {
            ++g_pass;
        } else {
            report_fail("invariant.varint", "变长整数边界不一致");
        }
    }

    // 3) 越界读取必须抛异常，不得静默返回 0
    {
        const uint8_t raw[] = {0xFF};
        NetReader r(raw, 8);
        bool threw = false;
        try {
            r.read_i32();
        } catch (const BufferError&) {
            threw = true;
        }
        if (threw) {
            ++g_pass;
        } else {
            report_fail("invariant.bounds", "越界读取未抛异常");
        }
    }

    // 4) 未对齐写入后仍能按字节精确比对（标量错位）
    {
        NetWriter w;
        w.write_bool(true);
        w.write_u8(0x5A);
        w.write_u16(0xBEEF);
        w.write_u32(0xDEADBEEF);
        w.write_u64(0x0123456789ABCDEFull);
        NetReader r(w.data(), w.bit_length());
        const bool b0 = r.read_bool();
        const uint8_t v8 = r.read_u8();
        const uint16_t v16 = r.read_u16();
        const uint32_t v32 = r.read_u32();
        const uint64_t v64 = r.read_u64();
        if (b0 && v8 == 0x5A && v16 == 0xBEEF && v32 == 0xDEADBEEF &&
            v64 == 0x0123456789ABCDEFull && r.at_end()) {
            ++g_pass;
        } else {
            report_fail("invariant.misaligned", "错位标量回读不一致");
        }
    }

    // 5) 空写入的长度必须为 0
    {
        NetWriter w;
        if (w.bit_length() == 0 && w.byte_length() == 0) {
            ++g_pass;
        } else {
            report_fail("invariant.empty", "空写入长度不为 0");
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    std::setlocale(LC_NUMERIC, "C");

    const std::string dir = (argc > 1) ? argv[1] : "golden";

    std::cout << "=== 黄金向量一致性测试 ===\n";
    std::cout << "向量目录: " << dir << "\n\n";

    try {
        const auto prim = read_lines(dir + "/primitives.txt");
        std::cout << "[原语] " << prim.size() << " 行\n";
        for (const auto& line : prim) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            run_primitive(line);
        }

        const auto seq = read_lines(dir + "/sequences.txt");
        std::cout << "[序列] " << seq.size() << " 行\n";
        for (const auto& line : seq) {
            if (line.empty() || line[0] == '#') {
                continue;
            }
            run_sequence(line);
        }
    } catch (const std::exception& e) {
        std::cout << "致命错误: " << e.what() << "\n";
        return 2;
    }

    std::cout << "\n[不变量]\n";
    run_invariants();

    std::cout << "\n=== 结果: 通过 " << g_pass << " / 失败 " << g_fail << " ===\n";
    return g_fail == 0 ? 0 : 1;
}
