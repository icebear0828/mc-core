#pragma once

// The elytra wing models that must be registered at startup, plus the vanilla wing pose.
//
// Vanilla ElytraModel has one geometry and three poses. Only the crouching pose moves the wings (3 px down), so
// it needs its own file; the pose rotations are applied at runtime through boneMatrices' `extra` (see
// bedrockEulerQuat), which is why the standing file also covers the gliding pose.

#include <algorithm>

namespace mc::model {

inline constexpr const char* kElytraWingsStanding = "elytra_wings";
inline constexpr const char* kElytraWingsCrouch = "elytra_wings_crouch";

inline constexpr const char* kElytraWingPoses[] = {kElytraWingsStanding, kElytraWingsCrouch};

// The left wing's Euler angles (degrees about the Bedrock axes) for the current state. From 1.21.8
// PlayerElytraAnimationState.tick(): standing 15/0/-15, crouching 40/5/-45, and while fall flying
// xRot = lerp(f, 15, 20) with zRot = -lerp(f, 15, 90), where f = 1 at level or climbing and falls to 0 in a
// steep dive (the wings fold back). The right wing mirrors yRot and zRot.
struct WingPose {
    float x_rot;
    float y_rot;
    float z_rot;
};

inline WingPose elytraWingPose(bool gliding, float glide_blend, bool sneaking) {
    if (gliding) {
        const float f = std::clamp(glide_blend, 0.f, 1.f);
        return {15.f + 5.f * f, 0.f, -(15.f + 75.f * f)};
    }
    if (sneaking) return {40.f, 5.f, -45.f};
    return {15.f, 0.f, -15.f};
}

} // namespace mc::model
