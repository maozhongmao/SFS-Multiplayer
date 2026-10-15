// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one from https://mozilla.org/MPL/2.0/.
//
// 存档健壮性回归测试（对应迁移项 ⑤⑥）：
//   截断 / 写坏 / 超大档 / 难度重启 / Schema 校验 / 内容自愈 / .bak 回退
//
// 编译运行（本机唯一可用编译器是 zig 0.16.0）：
//   zig c++ -std=c++17 -O2 -I src -o build/state-store-robustness.exe \
//       tests/state_store_robustness.cpp src/state_store.cpp
//   ./build/state-store-robustness.exe

#include "state_store.hpp"

#include <cstdio>
#include <fstream>
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

void write_text(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

bool exists(const std::string& path) {
    std::ifstream probe(path, std::ios::binary);
    return probe.good();
}

// 一份最小的合法存档：两枚火箭、难度 Hard(=2)、世界时间非 0
const char* kGoodState = R"JSON({
  "SchemaVersion": 1,
  "SavedAtUtc": "2026-09-18T00:00:00Z",
  "World": {
    "WorldTime": 12345.5,
    "Difficulty": 2,
    "Rockets": {
      "100": {
        "RocketName": "Alpha",
        "Location": {"X": 1.0, "Y": 2.0, "Vx": 3.0, "Vy": 4.0, "Address": "Earth"},
        "Rotation": 0.5,
        "AngularVelocity": 0.25,
        "ThrottleOn": true,
        "ThrottlePercent": 0.75,
        "Rcs": true,
        "Parts": {
          "1": {"Name": "pod", "X": 0.0, "Y": 0.0, "OrientationX": 1.0, "OrientationY": 1.0,
                "OrientationZ": 0.0, "Temperature": "-Infinity",
                "NumberVariables": {}, "ToggleVariables": {}, "TextVariables": {}, "Burns": null},
          "2": {"Name": "engine", "X": 2.0, "Y": 0.0, "OrientationX": 1.0, "OrientationY": 1.0,
                "OrientationZ": 0.0, "Temperature": 300.0,
                "NumberVariables": {}, "ToggleVariables": {}, "TextVariables": {}, "Burns": null}
        },
        "Joints": [{"PartA": 1, "PartB": 2}],
        "Stages": [{"StageId": 0, "PartIds": [1, 2]}]
      },
      "200": {
        "RocketName": "Beta",
        "Location": {"X": 10.0, "Y": 20.0, "Vx": 0.0, "Vy": 0.0, "Address": "Moon"},
        "Rotation": 0.0,
        "AngularVelocity": 0.0,
        "ThrottleOn": false,
        "ThrottlePercent": 0.0,
        "Rcs": false,
        "Parts": {},
        "Joints": [],
        "Stages": []
      }
    }
  }
}
)JSON";

const char* kPrimaryPath = "state-robustness-primary.json";

void cleanup() {
    std::remove(kPrimaryPath);
    std::remove((std::string(kPrimaryPath) + ".bak").c_str());
}

}  // namespace

int main() {
    using sfs::store::load_state;
    using sfs::store::load_state_with_fallback;

    // ---------- 1. 好档往返 + 难度持久化（迁移项"难度重启"）----------
    cleanup();
    write_text(kPrimaryPath, kGoodState);
    {
        std::string err;
        const auto world = load_state(kPrimaryPath, &err);
        check(world.has_value(), "1a 好档可以加载", err);
        if (world.has_value()) {
            check(world->difficulty == 2, "1b 难度=Hard 被读出", "difficulty=" + std::to_string(world->difficulty));
            check(world->rockets.size() == 2, "1c 两枚火箭都在", "rockets=" + std::to_string(world->rockets.size()));
            check(std::llround(world->world_time) == 12346LL, "1d 世界时间接上了存档",
                  std::to_string(world->world_time));
        }
    }

    // 难度持久化：save -> load 后仍然是 2（以前 save 写死 0，Hard 档重启会变 Normal）
    {
        sfs::store::SavedWorld out;
        out.world_time = 999.0;
        out.difficulty = 2;
        std::string err;
        check(sfs::store::save_state(kPrimaryPath, out, &err), "1e 保存成功", err);
        std::string load_err;
        const auto world = load_state(kPrimaryPath, &load_err);
        check(world.has_value() && world->difficulty == 2, "1f 难度跨重启保住",
              load_err + " difficulty=" + (world ? std::to_string(world->difficulty) : "n/a"));
    }

    // ---------- 2. 场景：主档被截断 -> 回退 .bak ----------
    cleanup();
    {
        write_text(std::string(kPrimaryPath) + ".bak", kGoodState);
        write_text(kPrimaryPath, std::string(kGoodState).substr(0, 400));   // 截断：JSON 不闭合
        const auto result = load_state_with_fallback(kPrimaryPath);
        check(result.ok, "2a 截断后能读回世界");
        check(result.source == sfs::store::LoadSource::kBackup, "2b 来源标记为 .bak");
        check(!result.isolated_path.empty(), "2c 坏档已被改名隔离", result.isolated_path);
        if (result.ok) {
            check(result.world.difficulty == 2, "2d 回退后难度仍在");
        }
    }

    // ---------- 3. 场景：主档写坏（不是 JSON）-> 回退 .bak ----------
    cleanup();
    {
        write_text(std::string(kPrimaryPath) + ".bak", kGoodState);
        write_text(kPrimaryPath, "\x00\x01\x02 这不是 JSON，多半是掉电写坏的一页 \xff\xfe");
        const auto result = load_state_with_fallback(kPrimaryPath);
        check(result.ok, "3a 写坏后能读回世界");
        check(result.source == sfs::store::LoadSource::kBackup, "3b 来源标记为 .bak");
    }

    // ---------- 4. 场景：超大档 -> 拒收（不能在读进内存之后才发现）----------
    cleanup();
    {
        std::ofstream big(kPrimaryPath, std::ios::binary | std::ios::trunc);
        const std::string chunk(1024 * 1024, 'x');
        for (std::int64_t written = 0; written < sfs::store::kMaxStateFileBytes + 1;
             written += static_cast<std::int64_t>(chunk.size())) {
            big.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
        big.close();
        std::string err;
        const auto world = load_state(kPrimaryPath, &err);
        check(!world.has_value(), "4a 超大档被拒收");
        check(err.find("体积超限") != std::string::npos, "4b 报错说明是体积超限", err);
    }

    // ---------- 5. Schema 校验：版本不匹配必须拒收 ----------
    cleanup();
    {
        std::string text = kGoodState;
        const auto pos = text.find("\"SchemaVersion\": 1");
        check(pos != std::string::npos, "5a 基准档里有 SchemaVersion");
        text.replace(pos, std::string("\"SchemaVersion\": 1").size(), "\"SchemaVersion\": 99");
        write_text(kPrimaryPath, text);
        std::string err;
        const auto world = load_state(kPrimaryPath, &err);
        check(!world.has_value(), "5b 更高 SchemaVersion 被拒收");
        check(err.find("SchemaVersion") != std::string::npos, "5c 报错提到 SchemaVersion", err);

        // 两份都坏 -> 明确失败，但不允许删任何文件
        write_text(std::string(kPrimaryPath) + ".bak", "{ 坏档 ");
        const auto result = load_state_with_fallback(kPrimaryPath);
        check(!result.ok, "5d 主档与备份都坏时返回失败");
        check(exists(std::string(kPrimaryPath) + ".bak"), "5e 失败时不删除备份");
    }

    // ---------- 6. 内容自愈：非法 joint / stage 引用被剔除，火箭本身保留 ----------
    cleanup();
    {
        std::string text = kGoodState;
        text.replace(text.find("\"PartB\": 2"), std::string("\"PartB\": 2").size(), "\"PartB\": 9999");
        write_text(kPrimaryPath, text);
        std::string err;
        const auto world = load_state(kPrimaryPath, &err);
        check(world.has_value(), "6a 指向不存在部件的 joint 不会毁掉整份存档", err);
        if (world.has_value()) {
            const auto it = world->rockets.find(100);
            check(it != world->rockets.end(), "6b 目标火箭仍在");
            if (it != world->rockets.end()) {
                check(it->second.joints.empty(), "6c 非法 joint 已被剔除");
            }
        }
    }

    // ---------- 7. WorldTime 非有限值必须拒收（否则服务端时钟整个失效）----------
    cleanup();
    {
        std::string text = kGoodState;
        text.replace(text.find("\"WorldTime\": 12345.5"), std::string("\"WorldTime\": 12345.5").size(),
                     "\"WorldTime\": \"Infinity\"");
        write_text(kPrimaryPath, text);
        std::string err;
        const auto world = load_state(kPrimaryPath, &err);
        check(!world.has_value(), "7a WorldTime=Infinity 被拒收");
        check(err.find("WorldTime") != std::string::npos, "7b 报错提到 WorldTime", err);
    }

    cleanup();
    std::cout << "\n通过 " << (checks - failures) << " / 共 " << checks << " 项";
    if (failures > 0) {
        std::cout << " —— 失败 " << failures << " 项\n";
        return 1;
    }
    std::cout << " —— 全部通过\n";
    return 0;
}
