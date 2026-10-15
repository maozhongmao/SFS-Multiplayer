// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// KcpTransport 双向互通自测：同一进程内起两个端点，验证会话建立、
// 消息边界（长度前缀）与双向收发。

#include "net/kcp_transport.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using sfs::net::KcpTransport;

namespace {

constexpr std::uint16_t kPortA = 39101;
constexpr std::uint16_t kPortB = 39102;
constexpr std::uint32_t kConv  = 0x1234u;

// 反复驱动两端事件循环，直到收到完整消息或超时。
bool wait_for(KcpTransport& rx, KcpTransport& tx, std::uint32_t conv,
              std::string* out, int max_rounds) {
    for (int i = 0; i < max_rounds; ++i) {
        rx.poll(5);
        tx.poll(5);
        const int n = rx.receive(conv, out);
        if (n > 0) return true;
    }
    return false;
}

}  // namespace

int main() {
    KcpTransport a;
    KcpTransport b;

    if (!a.init(kPortA)) { std::printf("A 绑定 %u 失败\n", kPortA); return 1; }
    if (!b.init(kPortB)) { std::printf("B 绑定 %u 失败\n", kPortB); return 1; }
    std::printf("两端已绑定: A=%u B=%u\n", kPortA, kPortB);

    if (!b.connect(kConv, "127.0.0.1", kPortA)) {
        std::printf("B 连接 A 失败\n");
        return 1;
    }
    std::printf("会话已建立: conv=0x%X\n", kConv);

    std::string got;

    // ---- 阶段 1：B → A ----
    const char* m1 = "hello-from-b";
    if (!b.send(kConv, m1, static_cast<int>(std::strlen(m1)))) {
        std::printf("B 发送失败\n");
        return 1;
    }
    const bool got1 = wait_for(a, b, kConv, &got, 400);
    std::printf("阶段1 B→A : %s\n", got1 ? got.c_str() : "(超时)");

    // ---- 阶段 2：A → B ----
    bool got2 = false;
    const char* m2 = "reply-from-a";
    if (got1) {
        if (!a.send(kConv, m2, static_cast<int>(std::strlen(m2)))) {
            std::printf("A 发送失败\n");
        } else {
            got2 = wait_for(b, a, kConv, &got, 400);
        }
    }
    std::printf("阶段2 A→B : %s\n", got2 ? got.c_str() : "(超时)");

    // ---- 阶段 3：大消息（跨 MTU 分片 + 长度前缀重组）----
    bool got3 = false;
    if (got2) {
        std::string big(64 * 1024, 'X');          // 64 KB，远超 MTU=1400
        big[12345] = 'Y';
        big[65535] = 'Z';
        if (b.send(kConv, big.data(), static_cast<int>(big.size()))) {
            if (wait_for(a, b, kConv, &got, 800)) {
                got3 = (got.size() == big.size()) && (got == big);
                std::printf("阶段3 大消息: 收到 %zu 字节, 内容%s\n", got.size(),
                            got3 ? "一致" : "不一致");
            } else {
                std::printf("阶段3 大消息: (超时)\n");
            }
        }
    }

    a.shutdown();
    b.shutdown();

    const bool pass = got1 && got2 && got3;
    std::printf("\n结果: %s\n", pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
