// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one from https://mozilla.org/MPL/2.0/.

#include "server/dock_math.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <utility>

namespace sfs::dock {

namespace {

constexpr float kPi = 3.14159265358979323846f;

}  // namespace

float port_direction_degrees(const sfs::protocol::PartState& port) {
    return port.orientation_z + (port.orientation_y < 0.0f ? -90.0f : 90.0f);
}

void rotate_offset(float x, float y, float degrees, float& out_x, float& out_y) {
    const float radians = degrees * kPi / 180.0f;
    const float cos_v = std::cos(radians);
    const float sin_v = std::sin(radians);
    out_x = x * cos_v - y * sin_v;
    out_y = x * sin_v + y * cos_v;
}

const sfs::protocol::PartState* part_find(const sfs::protocol::RocketState& rocket,
                                          std::int32_t part_id) {
    for (const auto& [id, part] : rocket.parts) {
        if (id == part_id) return &part;
    }
    return nullptr;
}

std::int32_t part_max_id(const sfs::protocol::RocketState& rocket) {
    std::int32_t maximum = 0;
    for (const auto& [id, part] : rocket.parts) {
        (void)part;
        if (id > maximum) maximum = id;
    }
    return maximum;
}

sfs::protocol::RocketState merge_docked_rockets(sfs::protocol::RocketState keep,
                                                const sfs::protocol::RocketState& remove,
                                                std::int32_t keep_part_id,
                                                std::int32_t remove_part_id) {
    using JointState = sfs::protocol::JointState;
    using PartState = sfs::protocol::PartState;
    using StageState = sfs::protocol::StageState;

    const PartState* keep_port = part_find(keep, keep_part_id);
    const PartState* remove_port = part_find(remove, remove_part_id);
    if (keep_port == nullptr || remove_port == nullptr) return keep;

    const float keep_pivot_x = keep_port->x;
    const float keep_pivot_y = keep_port->y;
    const float remove_pivot_x = remove_port->x;
    const float remove_pivot_y = remove_port->y;

    const float keep_direction = port_direction_degrees(*keep_port);
    const float remove_direction = port_direction_degrees(*remove_port);
    // 对齐到最近的 90 度：对接口只认四个正交方向，这是游戏侧的约定。
    const float align_world =
        std::round((keep_direction - remove_direction + 180.0f) / 90.0f) * 90.0f;
    const float relative_rotation = align_world;

    auto taken = [&keep](std::int32_t id) { return part_find(keep, id) != nullptr; };

    // 编号冲突时必须重编号，否则 remove 的部件会把 keep 已有的同号部件覆盖掉。
    std::map<std::int32_t, std::int32_t> id_map;
    std::int32_t next_id = keep.parts.empty() ? 1 : part_max_id(keep) + 1;
    for (const auto& [original_id, original_part] : remove.parts) {
        while (taken(next_id)) ++next_id;
        const bool collides = taken(original_id);
        const std::int32_t new_id = collides ? next_id++ : original_id;
        id_map[original_id] = new_id;

        PartState moved = original_part;
        const float relative_x = original_part.x - remove_pivot_x;
        const float relative_y = original_part.y - remove_pivot_y;
        float rotated_x = 0.0f;
        float rotated_y = 0.0f;
        rotate_offset(relative_x, relative_y, relative_rotation, rotated_x, rotated_y);
        moved.x = keep_pivot_x + rotated_x;
        moved.y = keep_pivot_y + rotated_y;
        moved.orientation_z += relative_rotation;
        keep.parts.emplace_back(new_id, std::move(moved));
    }

    for (const auto& joint : remove.joints) {
        const auto a = id_map.find(joint.part_a);
        const auto b = id_map.find(joint.part_b);
        if (a == id_map.end() || b == id_map.end()) continue;
        keep.joints.push_back(JointState{a->second, b->second});
    }
    keep.joints.push_back(JointState{keep_part_id, id_map[remove_part_id]});

    for (const auto& stage : remove.stages) {
        StageState moved_stage;
        moved_stage.stage_id = stage.stage_id;
        for (auto id : stage.part_ids) {
            const auto mapped = id_map.find(id);
            if (mapped == id_map.end()) continue;
            moved_stage.part_ids.push_back(mapped->second);
        }
        keep.stages.push_back(std::move(moved_stage));
    }

    // 名字：keep 无名（例如碎片）就用 remove 的，其余保持 keep。
    bool keep_named = false;
    for (char c : keep.rocket_name) {
        if (!std::isspace(static_cast<unsigned char>(c))) { keep_named = true; break; }
    }
    if (!keep_named) keep.rocket_name = remove.rocket_name;
    return keep;
}

std::vector<std::vector<std::int32_t>> connected_part_groups(
    const sfs::protocol::RocketState& rocket) {
    std::vector<std::int32_t> all_ids;
    all_ids.reserve(rocket.parts.size());
    for (const auto& [id, part] : rocket.parts) {
        (void)part;
        all_ids.push_back(id);
    }
    std::sort(all_ids.begin(), all_ids.end());

    std::map<std::int32_t, std::vector<std::int32_t>> neighbours;
    for (const auto& id : all_ids) neighbours[id] = {};
    for (const auto& joint : rocket.joints) {
        neighbours[joint.part_a].push_back(joint.part_b);
        neighbours[joint.part_b].push_back(joint.part_a);
    }

    std::vector<char> visited(all_ids.size(), 0);
    std::vector<std::vector<std::int32_t>> groups;
    for (std::size_t i = 0; i < all_ids.size(); ++i) {
        if (visited[i]) continue;
        std::vector<std::int32_t> group;
        std::vector<std::int32_t> stack{all_ids[i]};
        visited[i] = 1;
        while (!stack.empty()) {
            const std::int32_t current = stack.back();
            stack.pop_back();
            group.push_back(current);
            for (const auto neighbour : neighbours[current]) {
                const auto found = std::lower_bound(all_ids.begin(), all_ids.end(), neighbour);
                if (found == all_ids.end() || *found != neighbour) continue;   // 悬垂 joint
                const std::size_t index = static_cast<std::size_t>(found - all_ids.begin());
                if (visited[index]) continue;
                visited[index] = 1;
                stack.push_back(neighbour);
            }
        }
        std::sort(group.begin(), group.end());
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<std::int32_t> group_containing(const std::vector<std::vector<std::int32_t>>& groups,
                                           std::int32_t part_id) {
    for (const auto& group : groups) {
        if (std::find(group.begin(), group.end(), part_id) != group.end()) return group;
    }
    return {};
}

sfs::protocol::RocketState extract_rocket(const sfs::protocol::RocketState& source,
                                          const std::vector<std::int32_t>& ids) {
    using StageState = sfs::protocol::StageState;
    auto inside = [&ids](std::int32_t id) {
        return std::find(ids.begin(), ids.end(), id) != ids.end();
    };
    sfs::protocol::RocketState result;
    result.rocket_name = source.rocket_name;
    result.location = source.location;
    result.rotation = source.rotation;
    result.angular_velocity = source.angular_velocity;
    result.throttle_on = source.throttle_on;
    result.throttle_percent = source.throttle_percent;
    result.rcs = source.rcs;
    for (const auto& [id, part] : source.parts) {
        if (inside(id)) result.parts.emplace_back(id, part);
    }
    for (const auto& joint : source.joints) {
        if (inside(joint.part_a) && inside(joint.part_b)) result.joints.push_back(joint);
    }
    for (const auto& stage : source.stages) {
        StageState subset;
        subset.stage_id = stage.stage_id;
        for (auto id : stage.part_ids) {
            if (inside(id)) subset.part_ids.push_back(id);
        }
        if (!subset.part_ids.empty()) result.stages.push_back(std::move(subset));
    }
    return result;
}

}  // namespace sfs::dock
