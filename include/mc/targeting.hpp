#pragma once

#include "mc/types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mc {

struct TargetableEntity {
    uint64_t id{0};
    int32_t type_id{0};
    int hp{0};
    int max_hp{0};
    Vec3 rel_pos{}; // relative to player feet: +X right, +Y up, +Z forward (ER/Havok coordinates)
    bool is_hostile{false};
    bool is_alive{true};
};

struct PickParams {
    float max_reach{4.5f};      // metres, 3D from player's feet. Default 4.5m for pursuit
    float hit_radius{1.3f};     // sphere around torso that crosshair ray must intersect
    float center_height{1.0f};  // torso height above feet
    float max_ray{12.0f};       // ignore anything farther along the ray than this
    bool allow_neutral{true};   // neutral and passive mobs can be targeted in Minecraft
};

// Returns the index of the best target under the crosshair, or -1.
// cam_rel: camera position relative to player feet.
// dir: unit view direction vector.
// entities: list of targetable entities.
inline int pickTarget(const float cam_rel[3], const float dir[3], const std::vector<TargetableEntity>& entities, const PickParams& p = {}) {
    int best_hostile = -1;
    float best_hostile_t = 1e30f;
    int best_neutral = -1;
    float best_neutral_t = 1e30f;

    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        if (!e.is_alive) continue;
        if (!e.is_hostile && !p.allow_neutral) continue;
        const float d2 = e.rel_pos.x * e.rel_pos.x + e.rel_pos.y * e.rel_pos.y + e.rel_pos.z * e.rel_pos.z;
        if (d2 > p.max_reach * p.max_reach) continue;

        const float c[3] = {e.rel_pos.x, e.rel_pos.y + p.center_height, e.rel_pos.z};
        const float v[3] = {c[0] - cam_rel[0], c[1] - cam_rel[1], c[2] - cam_rel[2]};
        const float t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2];
        if (!(t >= 0.f) || t > p.max_ray) continue;

        const float q[3] = {v[0] - dir[0] * t, v[1] - dir[1] * t, v[2] - dir[2] * t};
        const float q2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2];
        if (q2 > p.hit_radius * p.hit_radius) continue;

        if (e.is_hostile) {
            if (t < best_hostile_t) {
                best_hostile_t = t;
                best_hostile = static_cast<int>(i);
            }
        } else {
            if (t < best_neutral_t) {
                best_neutral_t = t;
                best_neutral = static_cast<int>(i);
            }
        }
    }

    if (best_hostile >= 0) return best_hostile;
    return best_neutral;
}

// Finds the nearest hostile entity within max_distance, or -1 if none found.
inline int findNearestHostile(const std::vector<TargetableEntity>& entities, float max_distance = 25.0f) {
    int best_idx = -1;
    float best_d2 = max_distance * max_distance;

    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& e = entities[i];
        if (!e.is_alive || !e.is_hostile) continue;
        const float d2 = e.rel_pos.x * e.rel_pos.x + e.rel_pos.y * e.rel_pos.y + e.rel_pos.z * e.rel_pos.z;
        if (d2 <= best_d2) {
            best_d2 = d2;
            best_idx = static_cast<int>(i);
        }
    }

    return best_idx;
}

// Helper structure for air-swing throttling: prevents high-frequency redundant full-world scans
struct ScanThrottle {
    uint64_t last_scan_ms{0};
    Vec3 last_player_pos{};
    bool last_scan_empty{true};

    [[nodiscard]] bool shouldThrottle(uint64_t now_ms, const Vec3& current_player_pos,
                                      uint64_t throttle_window_ms = 200, float max_move_dist = 0.5f) const noexcept {
        if (!last_scan_empty) return false; // If last scan found enemies, don't throttle
        if (now_ms < last_scan_ms || (now_ms - last_scan_ms) > throttle_window_ms) return false;
        const float dx = current_player_pos.x - last_player_pos.x;
        const float dy = current_player_pos.y - last_player_pos.y;
        const float dz = current_player_pos.z - last_player_pos.z;
        return (dx * dx + dy * dy + dz * dz) <= (max_move_dist * max_move_dist);
    }

    void record(uint64_t now_ms, const Vec3& current_player_pos, bool empty) noexcept {
        last_scan_ms = now_ms;
        last_player_pos = current_player_pos;
        last_scan_empty = empty;
    }

    void reset() noexcept {
        last_scan_ms = 0;
        last_player_pos = {};
        last_scan_empty = true;
    }
};

} // namespace mc
