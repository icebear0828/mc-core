#pragma once

// Minecraft shield defense & blocking logic for Elden Ring.
// Calculates forward arc (100 degrees cone: half-angle 50 degrees, dot >= cos(50 deg) ~ 0.6427876)
// and tracks shield raise/block states. Pure logic, unit-tested.

#include <algorithm>
#include <cmath>

namespace eldenring::shield {

// Cosine of 50 degrees (cone of 100 degrees total)
inline constexpr float kBlockMinDot = 0.6427876f;
// Minecraft shield raise delay is ~5 ticks = 0.25 seconds
inline constexpr float kShieldRaiseDelaySec = 0.25f;

// Checks whether an attack coming from attacker_pos is blocked by a player at player_pos facing player_yaw.
// In Elden Ring / Dantelion coordinates:
// +Z is forward, +X is right, Y is up.
// player_yaw is the rotation about +Y: forward vector is (sin(yaw), 0, cos(yaw)).
inline bool isAttackBlocked(const float player_pos[3], float player_yaw, const float attacker_pos[3]) {
    const float dx = attacker_pos[0] - player_pos[0];
    const float dz = attacker_pos[2] - player_pos[2];
    const float dist_sq = dx * dx + dz * dz;
    if (dist_sq < 1e-6f) return true; // point blank / on top of player counts as blocked

    const float inv_dist = 1.0f / std::sqrt(dist_sq);
    const float dir_x = dx * inv_dist;
    const float dir_z = dz * inv_dist;

    // Player forward direction in XZ plane
    const float fwd_x = std::sin(player_yaw);
    const float fwd_z = std::cos(player_yaw);

    const float dot = dir_x * fwd_x + dir_z * fwd_z;
    return dot >= kBlockMinDot;
}

class ShieldController {
public:
    // Updates shield blocking state.
    // dt: delta time in seconds
    // right_held: right mouse button is held down
    // has_shield: player holds a shield in main hand or offhand
    void update(float dt, bool right_held, bool has_shield) {
        if (!right_held || !has_shield) {
            held_time_ = 0.f;
            blocking_ = false;
            return;
        }

        held_time_ += dt;
        blocking_ = (held_time_ >= kShieldRaiseDelaySec);
    }

    [[nodiscard]] bool isBlocking() const { return blocking_; }
    [[nodiscard]] float raiseProgress() const {
        return std::clamp(held_time_ / kShieldRaiseDelaySec, 0.f, 1.f);
    }

private:
    float held_time_{0.f};
    bool blocking_{false};
};

} // namespace eldenring::shield
