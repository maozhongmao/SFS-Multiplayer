// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// 存档读写的最小验证程序：只做 load -> save -> reload -> 比对，不涉及网络。
// 用于在开发机上快速定位存档解析问题（目标设备上调试手段有限）。

#include "state_store.hpp"

#include <cstdio>
#include <exception>
#include <string>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("用法: %s <server-state.json> [输出路径]\n", argv[0]);
        return 2;
    }
    const std::string in_path = argv[1];

    try {
        std::printf("[1] 读取存档: %s\n", in_path.c_str());
        std::string err;
        auto loaded = sfs::store::load_state(in_path, &err);
        if (!loaded) {
            std::printf("    失败: %s\n", err.c_str());
            return 1;
        }
        std::size_t part_count = 0;
        for (const auto& [rid, rk] : loaded->rockets) part_count += rk.parts.size();
        std::printf("    成功: world_time=%.3f difficulty=%d rockets=%zu parts=%zu\n",
                    loaded->world_time, loaded->difficulty,
                    loaded->rockets.size(), part_count);

        std::size_t joints = 0, stages = 0;
        for (const auto& [rid, rk] : loaded->rockets) {
            joints += rk.joints.size();
            stages += rk.stages.size();
        }
        std::printf("    joints=%zu stages=%zu\n", joints, stages);

        const std::string out_path = (argc >= 3) ? argv[2] : (in_path + ".roundtrip");
        std::printf("[2] 保存到: %s\n", out_path.c_str());
        std::string serr;
        if (!sfs::store::save_state(out_path, *loaded, &serr)) {
            std::printf("    失败: %s\n", serr.c_str());
            return 1;
        }
        std::printf("    成功\n");

        std::printf("[3] 重新读取比对\n");
        std::string err2;
        auto again = sfs::store::load_state(out_path, &err2);
        if (!again) {
            std::printf("    失败: %s\n", err2.c_str());
            return 1;
        }
        if (again->rockets.size() != loaded->rockets.size()) {
            std::printf("    火箭数不一致: %zu vs %zu\n",
                        again->rockets.size(), loaded->rockets.size());
            return 1;
        }
        for (const auto& [rid, rk] : loaded->rockets) {
            auto it = again->rockets.find(rid);
            if (it == again->rockets.end()) {
                std::printf("    火箭 %d 丢失\n", rid);
                return 1;
            }
            if (it->second.parts.size() != rk.parts.size()) {
                std::printf("    火箭 %d 部件数不一致: %zu vs %zu\n",
                            rid, it->second.parts.size(), rk.parts.size());
                return 1;
            }
        }
        std::printf("    成功: 往返一致（%zu 枚火箭）\n", again->rockets.size());
    } catch (const std::exception& e) {
        std::printf("异常: %s\n", e.what());
        return 3;
    } catch (...) {
        std::printf("未知异常\n");
        return 3;
    }

    std::printf("全部通过\n");
    return 0;
}
