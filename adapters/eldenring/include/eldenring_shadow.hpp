#pragma once

// The blob shadow under a figure (Minecraft draws one under every entity), so the figure does not float over the scene. Pure logic:
// where the ground is (the game's static-geometry ray plus the placed blocks), how dark and how wide the shadow is for a given height
// above it, the matrix that puts a unit disc there, and the disc mesh itself. Unit-tested without the game. Game metres, Y up.

#include "eldenring_los.hpp"
#include "mc/rig.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

namespace eldenring::shadow {

inline constexpr float kBaseRadius = 0.5f;   // Minecraft: a player's / zombie's shadow radius
inline constexpr float kMaxStrength = 0.6f;  // alpha of the black disc at its centre when standing
inline constexpr float kFadeHeight = 16.f;   // Minecraft fades the shadow out 16 blocks above the ground
inline constexpr float kLift = 0.03f;        // the disc floats this far above the ground so it is not buried in it
inline constexpr float kRayStartAbove = 0.3f; // the downward ray starts a little above the feet: a floor that rose slightly is still found

// Highest placed-block top under (x, z) that is no higher than `feet_y` + a little and no lower than `feet_y - max_drop`.
using BlockTopFn = std::function<std::optional<float>(float x, float feet_y, float z, float max_drop)>;

// The height of the ground under the feet: the higher of the terrain (ray straight down) and the placed blocks. Empty when neither is found.
inline std::optional<float> groundBelow(const live::RayCastFn& cast, const BlockTopFn& block_top, const float feet[3]) {
    std::optional<float> best;
    const float search = kFadeHeight + kRayStartAbove;
    if (cast) {
        const float from[3] = {feet[0], feet[1] + kRayStartAbove, feet[2]};
        const float disp[3] = {0.f, -search, 0.f};
        const live::RayResult r = cast(from, disp);
        if (r.ok && r.hit && std::isfinite(r.fraction)) best = from[1] + disp[1] * r.fraction;
    }
    if (block_top) {
        const std::optional<float> t = block_top(feet[0], feet[1], feet[2], kFadeHeight);
        if (t && (!best || *t > *best)) best = t;
    }
    return best;
}

struct Shadow {
    bool visible{false};
    float x{0.f}, y{0.f}, z{0.f}; // disc centre (y already lifted)
    float radius{0.f};
    float strength{0.f}; // alpha of the black at the centre
};

// `radius_scale`: 1 for a player-sized figure.
inline Shadow shadowAt(float x, float feet_y, float z, float ground_y, float radius_scale = 1.f) {
    Shadow s;
    const float h = std::max(0.f, feet_y - ground_y); // feet sunk a little into a slope still count as standing
    if (!(h < kFadeHeight)) return s;
    const float t = h / kFadeHeight;
    s.visible = true;
    s.x = x;
    s.z = z;
    s.y = ground_y + kLift;
    s.radius = kBaseRadius * radius_scale * (1.f - 0.5f * t);
    s.strength = kMaxStrength * (1.f - t);
    return s;
}

// World matrix for the unit disc (radius 1, centred on the origin, flat in XZ).
inline mc::rig::Mat4 shadowMatrix(const Shadow& s) {
    mc::rig::Mat4 m;
    m.m[0] = s.radius;
    m.m[10] = s.radius;
    m.m[12] = s.x;
    m.m[13] = s.y;
    m.m[14] = s.z;
    return m;
}

// Alpha multiplier from the distance r (0 centre .. 1 rim) from the centre: soft edge.
inline float falloff(float r) {
    const float t = 1.f - r * r;
    return t > 0.f ? t : 0.f;
}

// Triangle fan: vertex 0 is the centre, 1..segments the rim. The uv holds (x, z) of the vertex so the shader gets the distance from the centre.
inline mc::rig::RigMesh buildShadowDisc(int segments) {
    mc::rig::RigMesh mesh;
    mesh.vertices.push_back({0.f, 0.f, 0.f, 0.f, 0.f});
    const float two_pi = 6.28318530718f;
    for (int i = 0; i < segments; ++i) {
        const float a = two_pi * static_cast<float>(i) / static_cast<float>(segments);
        const float c = std::cos(a), s = std::sin(a);
        mesh.vertices.push_back({c, 0.f, s, c, s});
    }
    for (int i = 0; i < segments; ++i) {
        mesh.indices.push_back(0);
        mesh.indices.push_back(static_cast<uint16_t>(1 + i));
        mesh.indices.push_back(static_cast<uint16_t>(1 + (i + 1) % segments));
    }
    return mesh;
}

} // namespace eldenring::shadow
