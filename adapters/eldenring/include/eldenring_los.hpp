#pragma once

// Line of sight between the player and a target, for MC melee: a hit must not go through a wall. Pure logic on top of
// an injected ray caster (the game's static-geometry ray, filter 0x5D, see docs/ELDENRING_REVERSE.md 1.6), so it is
// unit-tested without the game. Positions are game (Havok) metres, Y up.

#include <cmath>
#include <cstdint>
#include <functional>

namespace eldenring::live {

namespace los {
inline constexpr uintptr_t kHavokManGlobalRva = 0x3D7A0D0;   // CSHavokManImp* (A, REVERSE 1.6)
inline constexpr uint32_t kHavokManVtableRva = 0x2B934E8;
inline constexpr uintptr_t kPhysWorldInHavokMan = 0x98;      // CSPhysWorld*
inline constexpr uint32_t kStaticGeometryFilter = 0x5D;      // terrain and static meshes only; never characters (A)
inline constexpr float kEyeHeight = 1.4f;                    // from the player's feet
inline constexpr float kTargetHeight = 1.0f;                 // torso centre above the victim's feet (same as the picker)
} // namespace los

struct RayResult {
    bool ok{false};        // the cast ran; false = unavailable or failed
    bool hit{false};
    float fraction{1.f};   // of `disp`, valid when hit
};

// from, disp (= to - from) -> result.
using RayCastFn = std::function<RayResult(const float from[3], const float disp[3])>;

struct LosParams {
    float ignore_near_start{0.3f}; // a hit this close to the start is the floor / the player's own surroundings
    float ignore_near_end{0.35f};  // a hit this close to the target is the ground or wall the target stands against
};

// True when nothing static lies between `from` and `to`. Fails open: when the ray cannot be cast the hit is not blocked.
inline bool hasLineOfSight(const RayCastFn& cast, const float from[3], const float to[3], const LosParams& p = {}) {
    if (!cast) return true;
    const float disp[3] = {to[0] - from[0], to[1] - from[1], to[2] - from[2]};
    const float len = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
    if (!(len > 1e-3f)) return true;
    const RayResult r = cast(from, disp);
    if (!r.ok || !r.hit) return true;
    const float d = r.fraction * len;
    if (d <= p.ignore_near_start || d >= len - p.ignore_near_end) return true;
    return false;
}

} // namespace eldenring::live
