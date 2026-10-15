// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one from https://mozilla.org/MPL/2.0/.
//
// 对接/拆解纯计算部分的单测（对应迁移项 ②）。
// 旋转对齐、零件重编号、连通块拆分这三处最容易出错，且不依赖网络层，
// 所以单独拉出来验证 —— 没有单测的话这些错误只会在真无人机上暴露成"火箭位置乱飞"。
//
//   zig c++ -std=c++17 -O2 -I src -o build/dock-math-tests.exe \
//       tests/dock_math_tests.cpp src/server/dock_math.cpp src/net/lidgren_buffer.cpp
//   ./build/dock-math-tests.exe

#include "server/dock_math.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& name, const std::string& detail = std::string()) {
    ++checks;
    if (condition) {
        std::cout << "[PASS] " << name << "\n";
    } else {
        ++failures;
        std::cout << "[FAIL] " << name;
        if (!detail.empty()) std::cout << " -> " << detail;
        std::cout << "\n";
    }
}

bool nearly(float a, float b, float tolerance = 1e-3f) {
    return std::fabs(a - b) <= tolerance;
}

// 一枚位于原点、对接口（零件 1）朝上的两零件火箭
sfs::protocol::PartState make_part(const std::string& name, float x, float y, float oz) {
    sfs::protocol::PartState p;
    p.name = name;
    p.x = x;
    p.y = y;
    p.orientation_x = 1.0f;
    p.orientation_y = 1.0f;
    p.orientation_z = oz;
    return p;
}

sfs::protocol::RocketState two_part_rocket(std::int32_t id_a, std::int32_t id_b) {
    sfs::protocol::RocketState rocket;
    rocket.rocket_name = "Pair";
    rocket.parts.emplace_back(id_a, make_part("port", 0.0f, 0.0f, 0.0f));
    rocket.parts.emplace_back(id_b, make_part("tank", 2.0f, 0.0f, 0.0f));
    rocket.joints.push_back(sfs::protocol::JointState{id_a, id_b});
    sfs::protocol::StageState stage;
    stage.stage_id = 0;
    stage.part_ids = {id_a, id_b};
    rocket.stages.push_back(stage);
    return rocket;
}

}  // namespace

int main() {
    using sfs::protocol::RocketState;

    // ---------- 端口方向角 ----------
    {
        const auto port = make_part("port", 0.0f, 0.0f, 0.0f);
        // OrientationY > 0 -> +90
        check(nearly(sfs::dock::port_direction_degrees(port), 90.0f), "1a 端口方向 +90（Y 为正）");

        auto flipped = port;
        flipped.orientation_y = -1.0f;
        check(nearly(sfs::dock::port_direction_degrees(flipped), -90.0f),
              "1b 端口方向 -90（Y 为负）");
    }

    // ---------- 旋转：90 度必须是精确的轴交换，不能有余弦误差 ----------
    {
        float x = 1.0f;
        float y = 0.0f;
        sfs::dock::rotate_offset(1.0f, 0.0f, 90.0f, x, y);
        check(nearly(x, 0.0f) && nearly(y, 1.0f), "2a 90 度旋转 (1,0)->(0,1)",
              std::to_string(x) + "," + std::to_string(y));
        sfs::dock::rotate_offset(2.0f, 0.0f, 180.0f, x, y);
        check(nearly(x, -2.0f) && nearly(y, 0.0f), "2b 180 度旋转 (2,0)->(-2,0)",
              std::to_string(x) + "," + std::to_string(y));
        sfs::dock::rotate_offset(1.0f, 2.0f, 0.0f, x, y);
        check(nearly(x, 1.0f) && nearly(y, 2.0f), "2c 0 度旋转保持不变");
    }

    // ---------- 合并：部件数拼接 + 桥接 joint ----------
    {
        RocketState keep = two_part_rocket(1, 2);
        RocketState remove = two_part_rocket(1, 2);          // 编号与 keep 完全冲突
        remove.location.address = "Moon";
        const RocketState merged = sfs::dock::merge_docked_rockets(keep, remove, 1, 1);
        check(merged.parts.size() == 4, "3a 合并后部件数相加", std::to_string(merged.parts.size()));

        // 逐个编号必须唯一：冲突时必须重编号，绝不允许同号部件互相覆盖
        bool unique = true;
        for (std::size_t i = 0; i < merged.parts.size(); ++i) {
            for (std::size_t j = i + 1; j < merged.parts.size(); ++j) {
                if (merged.parts[i].first == merged.parts[j].first) unique = false;
            }
        }
        check(unique, "3b 合并后部件编号不重复");

        // 必须有一条把两个对接口连起来的桥
        bool bridged = false;
        for (const auto& joint : merged.joints) {
            if (joint.part_a == 1 || joint.part_b == 1) bridged = true;
        }
        check(bridged, "3c 生成了对接桥接 joint");
        check(merged.rocket_name == "Pair", "3d keep 有名时保留 keep 的名字", merged.rocket_name);
    }

    // ---------- 合并：几何必须对得上（旋转后新部件应落在 keep 对接口处） ----------
    {
        RocketState keep = two_part_rocket(1, 2);
        keep.parts[0].second = make_part("port", 0.0f, 0.0f, 0.0f);
        RocketState remove = two_part_rocket(7, 8);
        // remove 的对接口在 (0,0)，另一零件在 (3,0)
        remove.parts[0].second = make_part("port", 0.0f, 0.0f, 0.0f);
        remove.parts[1].second = make_part("tank", 3.0f, 0.0f, 0.0f);

        const RocketState merged = sfs::dock::merge_docked_rockets(keep, remove, 1, 7);
        // remove 的 port 自身相对于 pivot 偏移 (0,0) -> 合并后应正好落在 keep 的对接口 (0,0)
        const auto* moved_port = sfs::dock::part_find(merged, 7);
        check(moved_port != nullptr, "4a 迁移后的 port 仍在（编号 7）");
        if (moved_port != nullptr) {
            check(nearly(moved_port->x, 0.0f) && nearly(moved_port->y, 0.0f),
                  "4b 迁移后的 port 落在 keep 的对接口原点",
                  std::to_string(moved_port->x) + "," + std::to_string(moved_port->y));
        }
        const auto* moved_tank = sfs::dock::part_find(merged, 8);
        check(moved_tank != nullptr, "4c 迁移后的 tank 仍在（编号 8）");
        if (moved_tank != nullptr) {
            const float distance = std::sqrt(moved_tank->x * moved_tank->x +
                                             moved_tank->y * moved_tank->y);
            // 平移+旋转是刚体变换，(3,0) 到 pivot 的距离必须还是 3
            check(nearly(distance, 3.0f), "4d 刚体变换保持部件间距不变",
                  std::to_string(distance));
        }
    }

    // ---------- 合并：keep 无名时用 remove 的名字 ----------
    {
        RocketState keep = two_part_rocket(1, 2);
        keep.rocket_name = "";
        RocketState remove = two_part_rocket(7, 8);
        remove.rocket_name = "Survivor";
        const RocketState merged = sfs::dock::merge_docked_rockets(keep, remove, 1, 7);
        check(merged.rocket_name == "Survivor", "5a keep 无名时继承 remove 的名字",
              merged.rocket_name);
    }

    // ---------- 连通块拆分 ----------
    {
        RocketState rocket;
        for (std::int32_t id = 1; id <= 4; ++id) {
            rocket.parts.emplace_back(id, make_part("p" + std::to_string(id), 0.0f, 0.0f, 0.0f));
        }
        // 1-2 一堆，3-4 一堆
        rocket.joints.push_back(sfs::protocol::JointState{1, 2});
        rocket.joints.push_back(sfs::protocol::JointState{3, 4});
        const auto groups = sfs::dock::connected_part_groups(rocket);
        check(groups.size() == 2, "6a 拆成两堆", std::to_string(groups.size()));
        if (groups.size() == 2) {
            check(groups[0].size() == 2 && groups[1].size() == 2, "6b 每堆两个零件");
        }
        const auto first = sfs::dock::group_containing(groups, 1);
        check(first.size() == 2 && sfs::dock::group_containing(groups, 3) != first,
              "6c group_containing 能区分两堆");
        check(sfs::dock::group_containing(groups, 99).empty(), "6d 找不到的零件返回空");
    }

    // ---------- 连通块：单个孤立零件各自成一堆 ----------
    {
        RocketState rocket;
        rocket.parts.emplace_back(1, make_part("a", 0.0f, 0.0f, 0.0f));
        rocket.parts.emplace_back(2, make_part("b", 0.0f, 0.0f, 0.0f));
        const auto groups = sfs::dock::connected_part_groups(rocket);
        check(groups.size() == 2, "7a 无 joint 时每个零件各成一堆", std::to_string(groups.size()));
    }

    // ---------- 连通块：悬垂 joint（指向不存在零件）不能让 拆分崩掉 ----------
    {
        RocketState rocket;
        rocket.parts.emplace_back(1, make_part("a", 0.0f, 0.0f, 0.0f));
        rocket.parts.emplace_back(2, make_part("b", 0.0f, 0.0f, 0.0f));
        rocket.joints.push_back(sfs::protocol::JointState{1, 9999});   // 悬垂
        const auto groups = sfs::dock::connected_part_groups(rocket);
        check(groups.size() == 2, "8a 悬垂 joint 被忽略，不崩溃", std::to_string(groups.size()));
    }

    // ---------- extract_rocket：只带走指定零件，并筛选 joints / stages ----------
    {
        RocketState rocket;
        rocket.rocket_name = "Source";
        rocket.rotation = 1.5f;
        rocket.angular_velocity = 0.25f;
        rocket.throttle_percent = 0.5f;
        for (std::int32_t id = 1; id <= 4; ++id) {
            rocket.parts.emplace_back(id, make_part("p" + std::to_string(id), 0.0f, 0.0f, 0.0f));
        }
        rocket.joints.push_back(sfs::protocol::JointState{1, 2});
        rocket.joints.push_back(sfs::protocol::JointState{2, 3});   // 跨堆，必须被剔除
        sfs::protocol::StageState stage;
        stage.stage_id = 0;
        stage.part_ids = {1, 2, 3};
        rocket.stages.push_back(stage);

        const RocketState taken = sfs::dock::extract_rocket(rocket, {1, 2});
        check(taken.parts.size() == 2, "9a 只带走指定零件", std::to_string(taken.parts.size()));
        check(taken.joints.size() == 1, "9b 跨堆 joint 被剔除", std::to_string(taken.joints.size()));
        check(taken.stages.size() == 1 && taken.stages[0].part_ids.size() == 2,
              "9c stage 里的零件被筛选到仍在这一堆的部分");
        check(nearly(taken.rotation, 1.5f) && nearly(taken.angular_velocity, 0.25f),
              "9d 姿态与油门等标量被继承下来");
        check(taken.rocket_name == "Source", "9e 名字被继承下来");
    }

    std::cout << "\n通过 " << (checks - failures) << " / 共 " << checks << " 项";
    if (failures > 0) {
        std::cout << " —— 失败 " << failures << " 项\n";
        return 1;
    }
    std::cout << " —— 全部通过\n";
    return 0;
}
