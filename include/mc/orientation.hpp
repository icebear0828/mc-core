#pragma once

#include <algorithm>
#include <cmath>

namespace mc {

// Quaternion representing rotation around Y-axis.
// A pure yaw rotation around +Y is q = (0, sin(yaw/2), 0, cos(yaw/2)).
struct OrientationQuat {
    float x{0.f};
    float y{0.f};
    float z{0.f};
    float w{1.f};

    static OrientationQuat fromYaw(float yaw_rad) {
        OrientationQuat q;
        const float half = yaw_rad * 0.5f;
        q.x = 0.f;
        q.y = std::sin(half);
        q.z = 0.f;
        q.w = std::cos(half);
        return q;
    }

    [[nodiscard]] float toYaw() const {
        // Rotated unit forward vector (+Z):
        // fx = 2 * (x * z + w * y)
        // fz = 1 - 2 * (x * x + y * y)
        const float fx = 2.f * (x * z + w * y);
        const float fz = 1.f - 2.f * (x * x + y * y);
        return std::atan2(fx, fz);
    }
};

// Computes the desired world-space movement vector from camera forward/right and WASD input keys.
// Returns true if any movement key is pressed, false if neutral.
inline bool computeInputDirection(float fwd_key, float right_key,
                                  const float cam_forward[3], const float cam_right[3],
                                  float out_dir[2]) {
    if (fwd_key == 0.f && right_key == 0.f) {
        out_dir[0] = 0.f;
        out_dir[1] = 0.f;
        return false;
    }
    float fx = cam_forward[0], fz = cam_forward[2];
    float rx = cam_right[0], rz = cam_right[2];
    const float fl = std::sqrt(fx * fx + fz * fz);
    const float rl = std::sqrt(rx * rx + rz * rz);
    if (fl < 1e-4f || rl < 1e-4f) {
        out_dir[0] = 0.f;
        out_dir[1] = 0.f;
        return false;
    }
    fx /= fl; fz /= fl;
    rx /= rl; rz /= rl;

    const float wx = fwd_key * fx + right_key * rx;
    const float wz = fwd_key * fz + right_key * rz;
    const float wl = std::sqrt(wx * wx + wz * wz);
    if (wl < 1e-4f) {
        out_dir[0] = 0.f;
        out_dir[1] = 0.f;
        return false;
    }
    out_dir[0] = wx / wl;
    out_dir[1] = wz / wl;
    return true;
}

// Normalizes an angle into [-pi, pi]
inline float normalizeAngle(float angle_rad) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.f * kPi;
    while (angle_rad > kPi) angle_rad -= kTwoPi;
    while (angle_rad < -kPi) angle_rad += kTwoPi;
    return angle_rad;
}

// Computes the new yaw given current yaw, target yaw, dt, and turn speed limit.
// If instant_turn is true, returns target_yaw directly.
inline float stepYaw(float current_yaw, float target_yaw, float dt, float turn_speed_rad_per_sec, bool instant_turn) {
    if (instant_turn || turn_speed_rad_per_sec <= 0.f || dt <= 0.f) {
        return normalizeAngle(target_yaw);
    }
    const float diff = normalizeAngle(target_yaw - current_yaw);
    const float max_step = turn_speed_rad_per_sec * dt;
    if (std::abs(diff) <= max_step) {
        return normalizeAngle(target_yaw);
    }
    return normalizeAngle(current_yaw + (diff > 0.f ? max_step : -max_step));
}

// Standard offset between native Havok orientation quaternion and Steve/character model front (180 deg / pi).
inline constexpr float kSteveYawOffset = 3.14159265358979323846f;

// Encodes a desired character heading (where character model faces) into the native quaternion.
inline OrientationQuat characterQuatFromHeading(float desired_heading_rad, float offset_rad = kSteveYawOffset) {
    return OrientationQuat::fromYaw(normalizeAngle(desired_heading_rad - offset_rad));
}

// Decodes character heading from raw quaternion.
inline float headingFromCharacterQuat(const OrientationQuat& q, float offset_rad = kSteveYawOffset) {
    return normalizeAngle(q.toYaw() + offset_rad);
}

} // namespace mc
