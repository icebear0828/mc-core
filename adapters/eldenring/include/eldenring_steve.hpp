#pragma once

// Elden Ring's view of the shared Steve rig (mc/rig.hpp). The game uses the same Dantelion coordinate system as
// Sekiro: metres, Y up, left-handed, row vectors, +Z forward and +X to the right. Pure math, unit-tested.

#include "mc/rig.hpp"

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

} // namespace eldenring::render
