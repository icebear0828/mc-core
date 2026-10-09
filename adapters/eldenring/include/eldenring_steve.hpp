#pragma once

// Elden Ring's view of the shared Steve rig (mc/rig.hpp). The game uses the same Dantelion coordinate system as
// Sekiro: metres, Y up, left-handed, row vectors, +Z forward and +X to the right. Pure math, unit-tested.

#include "mc/rig.hpp"

#include "mc/animator.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eldenring::render {

// Canonical (X forward, Y left, Z up, cm) -> game (Z forward, -X left i.e. X right, Y up, metres).
inline constexpr mc::rig::HostBasis kBasis{{0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.01f};

// Heading of a character from its orientation quaternion (x, y, z, w): the right-hand rotation about +Y that takes
// +Z to the direction the character faces (the unit forward vector rotated by q, projected on the ground).
inline float yawFromQuat(float x, float y, float z, float w) {
    const float fx = 2.f * (x * z + w * y);
    const float fz = 1.f - 2.f * (x * x + y * y);
    return std::atan2(fx, fz);
}

using PartMatrices = std::array<mc::rig::Mat4, static_cast<size_t>(mc::StevePart::Count)>;

// Every part of the rig in its rest pose, standing with the feet at `feet` and facing `yaw` (host rotation about +Y).
inline PartMatrices restPoseMatrices(const mc::Vec3& feet, float yaw) {
    PartMatrices out;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = mc::rig::partMatrix(static_cast<mc::StevePart>(i), mc::Quat{0.f, 0.f, 0.f, 1.f}, feet, yaw, kBasis);
    }
    return out;
}

// The same rotation as the canonical quaternion, expressed in the game's axes (the basis change is a reflection, so the
// rotation axis flips: the same mapping the Sekiro adapter uses).
inline mc::Quat hostQuat(const mc::Quat& q) { return {q.y, -q.z, -q.x, q.w}; }

// Every part posed by the animator (canonical rotations), standing at `feet`, facing `yaw`.
inline PartMatrices posedMatrices(const mc::SteveAnimator::PartTransforms& t, const mc::Vec3& feet, float yaw) {
    PartMatrices out;
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = mc::rig::partMatrix(static_cast<mc::StevePart>(i), hostQuat(t[i].rot), feet, yaw, kBasis);
    }
    return out;
}

// Minecraft's death flip: after `seconds` of dying the body has turned sqrt((ticks - 1) / 20 * 1.6) of the way to lying on
// its side (20 ticks per second), clamped to 0..1 (LivingEntityRenderer.setupRotations).
inline float deathFlipFraction(float seconds) {
    if (!(seconds > 0.f)) return 0.f;
    const float f = (seconds * 20.f - 1.f) / 20.f * 1.6f;
    return f <= 0.f ? 0.f : std::min(1.f, std::sqrt(f));
}

// Turns the whole figure about its forward axis through the feet (fraction 1 = 90 degrees, lying on its side).
inline mc::rig::Mat4 deathFallMatrix(const mc::Vec3& feet, float yaw, float fraction) {
    const mc::Vec3 forward{std::sin(yaw), 0.f, std::cos(yaw)};
    return mc::rig::translation(feet * -1.0f) * mc::rig::rotationAboutAxis(forward, fraction * 1.5707963f) * mc::rig::translation(feet);
}

// Walking speed of the player from successive standing points, relative to the heading. A jump of more than 1.5 m in
// one update (floating origin re-base, teleport) is not movement: it resets the state.
class SteveMotion {
public:
    mc::SteveAnimInput update(float dt, const float feet[3], float yaw) {
        mc::SteveAnimInput in;
        dt = std::clamp(dt, 1.f / 240.f, 0.1f);
        if (have_prev_) {
            const float dx = feet[0] - prev_[0], dz = feet[2] - prev_[2];
            if (std::sqrt(dx * dx + dz * dz) <= 1.5f) {
                const float vx = dx / dt, vz = dz / dt;
                const float fwd_x = std::sin(yaw), fwd_z = std::cos(yaw);
                const float forward = vx * fwd_x + vz * fwd_z;
                const float strafe = vx * fwd_z - vz * fwd_x; // along the heading's right-hand side (+X at yaw 0)
                smooth_forward_ += (forward - smooth_forward_) * 0.35f;
                smooth_strafe_ += (strafe - smooth_strafe_) * 0.35f;
            } else {
                smooth_forward_ = smooth_strafe_ = 0.f;
            }
        }
        prev_[0] = feet[0];
        prev_[1] = feet[1];
        prev_[2] = feet[2];
        have_prev_ = true;
        in.forward_speed = smooth_forward_;
        in.strafe_speed = smooth_strafe_;
        return in;
    }
    void reset() { have_prev_ = false; smooth_forward_ = smooth_strafe_ = 0.f; }

private:
    bool have_prev_{false};
    float prev_[3]{};
    float smooth_forward_{0.f}, smooth_strafe_{0.f};
};

// Third-person head tracking: the head follows where the camera looks, like Minecraft's: up to 50 degrees off the
// body's heading either way, pitch over the full range. `cam_*` is the camera's forward vector in game axes, `body_yaw`
// the heading the body is drawn with (host rotation about +Y). Fills look_yaw (canonical: counter-clockwise positive,
// so the opposite sign of the host's) and look_pitch (positive = looking down). When the camera is nearly opposite the
// body it keeps the side the head was last on, so the head does not snap across as the angle wraps.
class HeadTracker {
public:
    static constexpr float kMaxYaw = 0.8726646f; // 50 degrees

    void update(float cam_x, float cam_y, float cam_z, float body_yaw, mc::SteveAnimInput& in) {
        const float len = std::sqrt(cam_x * cam_x + cam_y * cam_y + cam_z * cam_z);
        if (!std::isfinite(len) || !std::isfinite(body_yaw) || len < 1e-4f) {
            in.look_yaw = in.look_pitch = 0.f;
            return;
        }
        constexpr float kPi = 3.14159265f;
        float offset = std::atan2(cam_x, cam_z) - body_yaw; // host: positive turns toward +X
        offset = std::remainder(offset, 2.f * kPi);
        const bool behind = std::fabs(offset) > kPi - 0.4f;
        if (!behind) side_ = offset < 0.f ? -1.f : 1.f;
        const float host_yaw = behind ? side_ * kMaxYaw : std::clamp(offset, -kMaxYaw, kMaxYaw);
        in.look_yaw = -host_yaw;
        in.look_pitch = -std::asin(std::clamp(cam_y / len, -1.f, 1.f));
    }

private:
    float side_{1.f};
};

} // namespace eldenring::render
