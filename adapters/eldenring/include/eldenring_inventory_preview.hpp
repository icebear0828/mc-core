#pragma once

#include "eldenring_steve.hpp"
#include "mc/animator.hpp"
#include "mc/rig.hpp"

#include <algorithm>
#include <cmath>

namespace eldenring::preview {

inline constexpr float kPi = 3.14159265f;
inline constexpr float kDeg = kPi / 180.f;

// Default preview body orientation: slight angle to reveal 3D side profile facing camera (-15 degrees)
inline constexpr float kDefaultPreviewBodyYaw = -15.f * kDeg;

// Angular limits matching vanilla Minecraft inventory screen
inline constexpr float kMaxPreviewHeadYaw = 50.f * kDeg;   // ~0.8727 rad
inline constexpr float kMaxPreviewHeadPitch = 30.f * kDeg; // ~0.5236 rad

// Computes Minecraft vanilla head-tracking and body yaw relative to mouse position
inline void computePreviewTracking(float mouse_x, float mouse_y, float center_x, float center_y, float scale,
                                   float& out_head_yaw, float& out_head_pitch, float& out_body_yaw) {
    const float s = std::max(1.f, scale);
    const float dx = mouse_x - center_x;
    const float dy = mouse_y - center_y;

    // Relative mouse angle offsets
    const float f = std::atan(dx / (40.f * s));
    const float g = std::atan(dy / (40.f * s));

    // When mouse is on screen right (mx > cx -> dx > 0 -> f > 0), head turns to screen right.
    // Canonical coordinate: look_yaw > 0 rotates head towards host -X (screen right).
    out_head_yaw = std::clamp(f * 0.7f, -kMaxPreviewHeadYaw, kMaxPreviewHeadYaw);

    // In Minecraft animator convention, look_pitch > 0 is looking DOWN.
    // When mouse is below center (my > cy -> dy > 0 -> g > 0), head tilts down (look_pitch > 0).
    // When mouse is above center (my < cy -> dy < 0 -> g < 0), head tilts up (look_pitch < 0).
    out_head_pitch = std::clamp(g * 0.5f, -kMaxPreviewHeadPitch, kMaxPreviewHeadPitch);

    // Body slightly tracks the head around the default forward-facing pose
    out_body_yaw = kDefaultPreviewBodyYaw + f * 0.25f;
}

// Builds the view-projection matrix for the 3D inventory preview
inline mc::rig::Mat4 computePreviewViewProjection(float aspect) {
    // Camera placed 3.2m in front of Steve facing -Z, aiming at Steve chest (y = 0.95m)
    mc::rig::Camera cam;
    cam.position = {0.f, 0.95f, 3.2f};
    cam.forward = {0.f, 0.f, -1.f};
    cam.up = {0.f, 1.f, 0.f};
    cam.right = {-1.f, 0.f, 0.f};

    // 35 degrees vertical FOV perfectly frames Steve from feet to head with vanilla padding
    const float fov_y = 35.f * kDeg;
    return mc::rig::viewProjection(cam, fov_y, aspect, 0.1f, 10.f);
}

// Generates 12 posed part matrices for the Steve figure in the inventory preview
inline eldenring::render::PartMatrices computePreviewPartMatrices(float head_yaw, float head_pitch, float body_yaw) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput in{};
    in.look_yaw = head_yaw;
    in.look_pitch = head_pitch;
    in.forward_speed = 0.f;
    in.strafe_speed = 0.f;
    in.swing_progress = 0.f;
    in.is_crouching = false;
    animator.update(0.f, in);

    return eldenring::render::posedMatrices(animator.getTransforms(), {0.f, 0.f, 0.f}, body_yaw);
}

} // namespace eldenring::preview
