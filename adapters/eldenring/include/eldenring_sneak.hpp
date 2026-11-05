#pragma once

// First-person sneak eye height.
//
// The camera height is interpolated between the standing and sneaking values. This is deliberately independent of
// block collision: it is stepped from the general per-frame camera update, so it works on native terrain (no placed
// blocks) and keeps running even when the block-collision step early-returns (MC mode off, player unreadable).
//
// The constants and the interpolation are the shared first-person rig policy; this header only keeps the Elden Ring
// names the rest of the adapter already uses.

#include "mc/rig/first_person_rig.hpp"

namespace eldenring::sneak {

inline constexpr float kStandingEyeHeight = mc::rig::FirstPersonRig::kStandingEyeHeight;     // metres
inline constexpr float kSneakingEyeHeight = mc::rig::FirstPersonRig::kSneakingEyeHeight;     // metres
inline constexpr float kEyeHeightTimeConstant = mc::rig::FirstPersonRig::kEyeHeightTimeConstant; // seconds

// Frame-rate independent exponential interpolation towards the target eye height.
// Returns `current_eye_height` unchanged when the first-person camera is inactive or `dt` is not positive.
inline float stepSneakEyeHeight(float current_eye_height, bool sneaking, bool first_person, float dt) {
    return mc::rig::FirstPersonRig::stepSneakEyeHeight(current_eye_height, sneaking, first_person, dt);
}

} // namespace eldenring::sneak
