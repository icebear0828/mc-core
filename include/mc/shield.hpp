#pragma once

// Minecraft shield defense & blocking mechanics.
// Calculates forward arc (100 degrees cone: half-angle 50 degrees, dot >= cos(50 deg) ~ 0.6427876)
// and tracks shield raise/block timing states. Pure logic, unit-tested.

#include <algorithm>
#include <cmath>

namespace mc {

// Cosine of 50 degrees (cone of 100 degrees total)
inline constexpr float kBlockMinDot = 0.6427876f;
// Minecraft shield raise delay is ~5 ticks = 0.25 seconds
inline constexpr float kShieldRaiseDelaySec = 0.25f;
// Vanilla Minecraft movement speed multiplier while blocking with shield (~30% of normal speed, equivalent to sneak speed)
inline constexpr float kShieldBlockSpeedMultiplier = 0.3f;

// Evaluates effective movement speed considering sneak, sprint, and shield blocking.
// Shield blocking forces speed to sneak speed (~20%~30% of normal walk) and cancels sprint.
inline float effectiveMovementSpeed(float walk_speed, float sprint_speed, float sneak_speed,
                                    bool sneaking, bool sprinting, bool blocking) {
    if (blocking || sneaking) {
        return sneak_speed;
    }
    if (sprinting) {
        return sprint_speed;
    }
    return walk_speed;
}

// Cosine between the direction the player faces and the direction to the attacker, on the ground plane (1 = dead ahead, -1 = behind).
// Forward vector is (sin(yaw), 0, cos(yaw)). An attacker directly on top of the player counts as dead ahead.
inline float frontDot(const float player_pos[3], float player_yaw, const float attacker_pos[3]) {
    const float dx = attacker_pos[0] - player_pos[0];
    const float dz = attacker_pos[2] - player_pos[2];
    const float dist_sq = dx * dx + dz * dz;
    if (dist_sq < 1e-6f) return 1.0f;
    const float inv_dist = 1.0f / std::sqrt(dist_sq);
    return dx * inv_dist * std::sin(player_yaw) + dz * inv_dist * std::cos(player_yaw);
}

// The yaw (rotation about vertical axis) whose forward vector (sin(yaw), 0, cos(yaw)) points along (fx, fz).
inline float yawFromForward(float fx, float fz) { return std::atan2(fx, fz); }

// Checks whether an attack coming from attacker_pos is blocked by a player at player_pos facing player_yaw.
inline bool isAttackBlocked(const float player_pos[3], float player_yaw, const float attacker_pos[3]) {
    return frontDot(player_pos, player_yaw, attacker_pos) >= kBlockMinDot;
}

// The decision made for a hit the player's raised shield may stop.
inline bool shieldBlocksHit(bool has_attacker, bool has_positions, const float player_pos[3], float player_yaw, const float attacker_pos[3]) {
    return has_attacker && has_positions && isAttackBlocked(player_pos, player_yaw, attacker_pos);
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

} // namespace mc
