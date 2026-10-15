#pragma once

// Minecraft-authentic death kinematics and player death lifecycle:
// - Authentic tip-over curve from LivingEntityRenderer.setupRotations:
//     fraction = sqrt((deathTime - 1) / 20 * 1.6)
//   clamped to [0, 1] over 20 ticks (1.0 second).
// - deathFallMatrix: rotates model 90 degrees about the forward axis through the feet onto its side.
// - PlayerDeathTracker:
//   * Manages death timer progression (0 to kDeathSeconds)
//   * Freezes animation / locomotion pose while dying
//   * Disappears the model once deathTime >= kDeathSeconds (just like summons)
//   * Handles respawn reset: atomic reset of death timer, matrices, and limb transforms.

#include "mc/rig.hpp"
#include <algorithm>
#include <cmath>

namespace mc {

inline constexpr float kDeathSeconds = 1.0f; // 20 MC ticks (1.0 second)

// Minecraft's death flip: after `seconds` of dying the body has turned sqrt((ticks - 1) / 20 * 1.6) of the way to lying on
// its side (20 ticks per second), clamped to 0..1 (LivingEntityRenderer.setupRotations).
inline float deathFlipFraction(float seconds) {
    if (!(seconds > 0.f)) return 0.f;
    const float f = (seconds * 20.f - 1.f) / 20.f * 1.6f;
    return f <= 0.f ? 0.f : std::min(1.f, std::sqrt(f));
}

namespace rig {

// Turns the whole figure about its forward axis through the feet (fraction 1 = 90 degrees, lying on its side).
inline Mat4 deathFallMatrix(const Vec3& feet, float yaw, float fraction) {
    const Vec3 forward{std::sin(yaw), 0.f, std::cos(yaw)};
    return translation(feet * -1.0f) * rotationAboutAxis(forward, fraction * 1.5707963f) * translation(feet);
}

} // namespace rig

class PlayerDeathTracker {
public:
    static constexpr float kDeathSeconds = mc::kDeathSeconds;
    static constexpr float kEpsilon = 1e-4f;

    void update(float dt, bool is_dead) {
        just_died_ = false;
        just_expired_ = false;
        just_respawned_ = false;

        if (is_dead) {
            if (!was_dead_) {
                just_died_ = true;
                death_seconds_ = 0.0f;
            }
            const float prev_death = death_seconds_;
            death_seconds_ += dt;
            if (prev_death < kDeathSeconds - kEpsilon && death_seconds_ >= kDeathSeconds - kEpsilon) {
                just_expired_ = true;
            }
            was_dead_ = true;
        } else {
            if (was_dead_) {
                just_respawned_ = true;
                reset();
            }
        }
    }

    [[nodiscard]] bool isDead() const { return was_dead_; }
    [[nodiscard]] bool isDying() const { return was_dead_ && death_seconds_ < kDeathSeconds - kEpsilon; }
    [[nodiscard]] bool isCorpseExpired() const { return was_dead_ && death_seconds_ >= kDeathSeconds - kEpsilon; }
    [[nodiscard]] bool shouldDrawModel() const { return !was_dead_ || death_seconds_ < kDeathSeconds - kEpsilon; }
    [[nodiscard]] bool justDied() const { return just_died_; }
    [[nodiscard]] bool justExpired() const { return just_expired_; }
    [[nodiscard]] bool justRespawned() const { return just_respawned_; }
    [[nodiscard]] float deathSeconds() const { return death_seconds_; }
    [[nodiscard]] float fallFraction() const {
        return was_dead_ ? deathFlipFraction(death_seconds_) : 0.0f;
    }
    [[nodiscard]] float redTintAlpha() const {
        return isDying() ? 1.0f : 0.0f;
    }

    void reset() {
        death_seconds_ = 0.0f;
        was_dead_ = false;
        just_died_ = false;
        just_expired_ = false;
    }

private:
    float death_seconds_{0.0f};
    bool was_dead_{false};
    bool just_died_{false};
    bool just_expired_{false};
    bool just_respawned_{false};
};

} // namespace mc
