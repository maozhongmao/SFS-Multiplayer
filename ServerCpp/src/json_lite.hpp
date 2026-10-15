// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// 精简 JSON 解析器（递归下降，无外部依赖）。
//
// 只覆盖 SFS 服务端存档实际用到的 JSON 子集：
//   对象、数组、字符串（含 \" \\ \/ \b \f \n \r \t \uXXXX 转义）、
//   数字（含 -0.5、1e-12、2.5E+3 等科学计数法）、true/false/null。
//
// 不做的事情：不保留键顺序、不做流式解析、不支持注释或尾随逗号。
// 存档是我们自己写的，格式受控，够用即可。

#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sfs::jsonlite {

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& what) : std::runtime_error(what) {}
};

class Value;

using Object = std::map<std::string, Value>;
using Array = std::vector<Value>;

// JSON 值：用一个枚举标注类型，各类型载荷用成员变量承载（简单直接，够用）
class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::shared_ptr<Array> array;
    std::shared_ptr<Object> object;

    bool is_null() const { return type == Type::Null; }
    bool is_bool() const { return type == Type::Bool; }
    bool is_number() const { return type == Type::Number; }
    bool is_string() const { return type == Type::String; }
    bool is_array() const { return type == Type::Array; }
    bool is_object() const { return type == Type::Object; }

    // ---- 取值（带默认值与类型容错）----
    double as_number(double fallback = 0.0) const {
        if (type == Type::Number) return number;
        // 存档里 Temperature 可能是字符串形式的特殊值
        if (type == Type::String) {
            const auto& s = string;
            if (s == "Infinity") return std::numeric_limits<double>::infinity();
            if (s == "-Infinity") return -std::numeric_limits<double>::infinity();
            if (s == "NaN") return std::numeric_limits<double>::quiet_NaN();
            try { return std::stod(s); } catch (...) { return fallback; }
        }
        if (type == Type::Bool) return boolean ? 1.0 : 0.0;
        return fallback;
    }

    std::int32_t as_int32(std::int32_t fallback = 0) const {
        if (type == Type::Number) return static_cast<std::int32_t>(number);
        if (type == Type::String) {
            try { return static_cast<std::int32_t>(std::stoll(string)); } catch (...) { return fallback; }
        }
        return fallback;
    }

    float as_float(float fallback = 0.0f) const {
        return static_cast<float>(as_number(static_cast<double>(fallback)));
    }

    bool as_bool(bool fallback = false) const {
        if (type == Type::Bool) return boolean;
        if (type == Type::Number) return number != 0.0;
        return fallback;
    }

    std::string as_string(const std::string& fallback = std::string()) const {
        if (type == Type::String) return string;
        return fallback;
    }

    // 取对象成员；不存在或类型不符返回 nullptr
    const Value* find(const std::string& key) const {
        if (type != Type::Object || !object) return nullptr;
        auto it = object->find(key);
        return it == object->end() ? nullptr : &it->second;
    }

    // 取对象成员，带默认值（避免到处判空）
    double get_number(const std::string& key, double fallback = 0.0) const {
        const Value* v = find(key);
        return v ? v->as_number(fallback) : fallback;
    }
    std::int32_t get_int32(const std::string& key, std::int32_t fallback = 0) const {
        const Value* v = find(key);
        return v ? v->as_int32(fallback) : fallback;
    }
    bool get_bool(const std::string& key, bool fallback = false) const {
        const Value* v = find(key);
        return v ? v->as_bool(fallback) : fallback;
    }
    std::string get_string(const std::string& key, const std::string& fallback = std::string()) const {
        const Value* v = find(key);
        return v ? v->as_string(fallback) : fallback;
    }
};

namespace detail {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Value parse() {
        skip_ws();
        Value v = parse_value();
        skip_ws();
        if (pos_ != s_.size()) {
            throw ParseError("JSON 尾部有多余内容（位置 " + std::to_string(pos_) + "）");
        }
        return v;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& why) const {
        throw ParseError("JSON 解析失败（位置 " + std::to_string(pos_) + "）：" + why);
    }

    void skip_ws() {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++pos_; continue; }
            break;
        }
    }

    bool consume(char c) {
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    void expect(char c) {
        if (!consume(c)) fail(std::string("期望 '") + c + "'");
    }

    bool literal(const char* text) {
        const std::size_t n = std::strlen(text);
        if (s_.compare(pos_, n, text) == 0) { pos_ += n; return true; }
        return false;
    }

    Value parse_value() {
        if (pos_ >= s_.size()) fail("内容意外结束");
        const char c = s_[pos_];
        switch (c) {
            case '{': return parse_object();
            case '[': return parse_array();
            case '"': {
                Value v;
                v.type = Value::Type::String;
                v.string = parse_string();
                return v;
            }
            case 't': case 'f': case 'n': return parse_literal();
            default: return parse_number();
        }
    }

    Value parse_literal() {
        Value v;
        if (literal("true"))  { v.type = Value::Type::Bool; v.boolean = true; return v; }
        if (literal("false")) { v.type = Value::Type::Bool; v.boolean = false; return v; }
        if (literal("null"))  { v.type = Value::Type::Null; return v; }
        fail("非法字面量（只支持 true/false/null）");
    }

    Value parse_object() {
        expect('{');
        Value v;
        v.type = Value::Type::Object;
        v.object = std::make_shared<Object>();
        skip_ws();
        if (consume('}')) return v;
        while (true) {
            skip_ws();
            if (pos_ >= s_.size() || s_[pos_] != '"') fail("对象键必须是字符串");
            const std::string key = parse_string();
            skip_ws();
            expect(':');
            skip_ws();
            (*v.object)[key] = parse_value();
            skip_ws();
            if (consume(',')) continue;
            expect('}');
            return v;
        }
    }

    Value parse_array() {
        expect('[');
        Value v;
        v.type = Value::Type::Array;
        v.array = std::make_shared<Array>();
        skip_ws();
        if (consume(']')) return v;
        while (true) {
            skip_ws();
            v.array->push_back(parse_value());
            skip_ws();
            if (consume(',')) continue;
            expect(']');
            return v;
        }
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("字符串未闭合");
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= s_.size()) fail("转义序列未完成");
            const char e = s_[pos_++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > s_.size()) fail("\\u 转义不足 4 位");
                    unsigned cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = s_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned>(h - 'A' + 10);
                        else fail("\\u 转义含非法十六进制字符");
                    }
                    // 编码为 UTF-8（含代理对的基本处理）
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: fail("不支持的转义字符");
            }
        }
    }

    Value parse_number() {
        const std::size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            const bool ok = (c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E'
                            || c == '+' || c == '-';
            if (!ok) break;
            ++pos_;
        }
        if (pos_ == start) fail("不是合法的数字");
        const std::string text = s_.substr(start, pos_ - start);
        Value v;
        v.type = Value::Type::Number;
        try {
            v.number = std::stod(text);
        } catch (...) {
            fail("数字无法解析：" + text);
        }
        return v;
    }
};

} // namespace detail

inline Value parse(const std::string& text) {
    return detail::Parser(text).parse();
}

} // namespace sfs::jsonlite
