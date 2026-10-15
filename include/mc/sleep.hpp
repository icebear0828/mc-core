#pragma once

// Minecraft-authentic sleeping kinematics and player sleep lifecycle:
// - Authentic lying-down posture from Minecraft Pose.SLEEPING & LivingEntityRenderer.setupRotations:
//   * Model rotated 90 degrees backwards around lateral right axis, resting on back (face-up toward sky).
//   * Ground offset (+0.1125m) so torso and head rest exactly at surface without ground clipping.
//   * Gentle respiration cycle (3.0s sinusoidal wave) simulating deep restful breathing.
// - Smoothstep transition curve (0.8s / 16 ticks) preventing visual popping on enter/exit.
// - PlayerSleepTracker managing sleep timer, smooth entering/waking transitions, and breathing state.

#include "mc/rig.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace mc {

inline constexpr float kSleepTransitionSeconds = 0.8f;
inline constexpr float kSleepGroundOffset = 0.1125f; // 2 model pixels (half-depth of 4-pixel torso)
inline constexpr float kSleepRespirationPeriod = 3.0f; // 3 seconds per breath cycle
inline constexpr float kSleepRespirationAmplitude = 0.025f; // ~1.4 degrees subtle chest & arm rise

// Smoothstep transition curve (0 to 1 over duration)
inline float sleepTransitionFraction(float elapsed, float duration = kSleepTransitionSeconds) {
    if (elapsed <= 0.0f) return 0.0f;
    if (duration <= 0.0f || elapsed >= duration) return 1.0f;
    const float t = elapsed / duration;
    return t * t * (3.0f - 2.0f * t);
}

namespace rig {

// Rotates the model 90 degrees backwards around its lateral right axis onto its back (face up),
// with vertical offset compensation so Steve's back touches the ground surface.
inline Mat4 sleepPoseMatrix(const Vec3& feet, float yaw, float fraction, float vertical_offset = kSleepGroundOffset) {
    if (fraction <= 0.0f) {
        return Mat4{}; // Identity
    }
    // Lateral right vector perpendicular to forward heading: (cos(yaw), 0, -sin(yaw))
    const Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const float angle = -1.57079632679f * fraction; // -90 degrees * fraction

    const Vec3 offset_translation = feet + Vec3{0.0f, vertical_offset * fraction, 0.0f};
    return translation(feet * -1.0f) * rotationAboutAxis(right, angle) * translation(offset_translation);
}

} // namespace rig

class PlayerSleepTracker {
public:
    static constexpr float kDuration = kSleepTransitionSeconds;
    static constexpr float kEpsilon = 1e-4f;

    void update(float dt, bool is_sleeping) {
        if (is_sleeping) {
            timer_ = std::min(kDuration, timer_ + dt);
            age_ += dt;
        } else {
            timer_ = std::max(0.0f, timer_ - dt);
            if (timer_ <= kEpsilon) {
                age_ = 0.0f;
            } else {
                age_ += dt;
            }
        }
    }

    [[nodiscard]] bool isSleeping() const {
        return timer_ >= kDuration - kEpsilon;
    }

    [[nodiscard]] bool isTransitioning() const {
        return timer_ > kEpsilon && timer_ < kDuration - kEpsilon;
    }

    [[nodiscard]] bool isAwake() const {
        return timer_ <= kEpsilon;
    }

    [[nodiscard]] float sleepFraction() const {
        return sleepTransitionFraction(timer_, kDuration);
    }

    [[nodiscard]] float breathingAngle() const {
        if (timer_ <= kEpsilon) return 0.0f;
        constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;
        return sleepFraction() * kSleepRespirationAmplitude * std::sin(age_ * (two_pi / kSleepRespirationPeriod));
    }

    void reset() {
        timer_ = 0.0f;
        age_ = 0.0f;
    }

private:
    float timer_{0.0f};
    float age_{0.0f};
};

} // namespace mc
