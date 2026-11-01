#pragma once

// First-person sneak eye height.
//
// The camera height is interpolated between the standing and sneaking values. This is deliberately independent of
// block collision: it is stepped from the general per-frame camera update, so it works on native terrain (no placed
// blocks) and keeps running even when the block-collision step early-returns (MC mode off, player unreadable).
//
// Pure logic only: no globals, no side effects, easy to unit test.

#include <cmath>

namespace eldenring::sneak {

inline constexpr float kStandingEyeHeight = 1.65f;     // metres
inline constexpr float kSneakingEyeHeight = 1.27f;     // metres
inline constexpr float kEyeHeightTimeConstant = 0.05f; // seconds (~95% of the way in 150 ms)

// Frame-rate independent exponential interpolation towards the target eye height.
// Returns `current_eye_height` unchanged when the first-person camera is inactive or `dt` is not positive.
inline float stepSneakEyeHeight(float current_eye_height, bool sneaking, bool first_person, float dt) {
    if (!first_person || !(dt > 0.f)) return current_eye_height;
    const float target = sneaking ? kSneakingEyeHeight : kStandingEyeHeight;
    const float blend = 1.f - std::exp(-dt / kEyeHeightTimeConstant);
    return current_eye_height + (target - current_eye_height) * blend;
}

} // namespace eldenring::sneak
