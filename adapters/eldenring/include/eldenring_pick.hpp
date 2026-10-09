#pragma once

// Choosing the enemy under the crosshair for a melee MC attack. Pure geometry on the enemy list (relative positions,
// metres, Havok axes) and the camera ray, so it is unit-tested without the game.

#include "eldenring_world.hpp"

#include <cmath>
#include <vector>

namespace eldenring::live {

struct PickParams {
    float max_reach{4.0f};      // the enemy must be this close to the player (metres, 3D from the player's feet)
    float hit_radius{0.9f};     // sphere around the enemy's torso that the crosshair ray must pass through
    float center_height{1.0f};  // torso height above the enemy's feet
    float max_ray{12.0f};       // ignore anything farther along the ray than this
};

// `cam_rel`: camera position relative to the player's feet; `dir`: unit view direction. Returns the index of the
// nearest (along the ray) hostile enemy that is in reach and under the crosshair, or -1.
inline int pickTarget(const float cam_rel[3], const float dir[3], const std::vector<EnemyInfo>& enemies, const PickParams& p = {}) {
    int best = -1;
    float best_t = 1e30f;
    for (size_t i = 0; i < enemies.size(); ++i) {
        const EnemyInfo& e = enemies[i];
        if (!e.hostile) continue;
        if (std::sqrt(e.rel_x * e.rel_x + e.rel_y * e.rel_y + e.rel_z * e.rel_z) > p.max_reach) continue;
        const float c[3] = {e.rel_x, e.rel_y + p.center_height, e.rel_z};
        const float v[3] = {c[0] - cam_rel[0], c[1] - cam_rel[1], c[2] - cam_rel[2]};
        const float t = v[0] * dir[0] + v[1] * dir[1] + v[2] * dir[2];
        if (!(t >= 0.f) || t > p.max_ray) continue;
        const float q[3] = {v[0] - dir[0] * t, v[1] - dir[1] * t, v[2] - dir[2] * t};
        if (std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2]) > p.hit_radius) continue;
        if (t < best_t) {
            best_t = t;
            best = static_cast<int>(i);
        }
    }
    return best;
}

} // namespace eldenring::live
