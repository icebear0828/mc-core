#pragma once

#include "mc/flight.hpp"

#include <cmath>
#include <cstdint>

namespace eldenring::flight {

using mc::kDoubleTapMs;
using mc::kHorizontalSpeed;
using mc::kVerticalSpeed;
using mc::kMaxStep;

using mc::DoubleTap;
using mc::Keys;
using mc::Heading;
using mc::headingFromCamera;
using mc::step;
using mc::positionOverwritten;

// The game recentres its physics coordinates ("floating origin") by whole multiples of 8 m when the player is far from the origin, keeping
// the fraction (observed 2026-10-10: -24, -32, -16, -8, +32 on single axes). Flight keeps its own position (the game's gravity would pull
// a position read back every frame), so after each write the difference to what is read back is only taken over where it is such a
// re-base: whole grid steps on an axis whose difference is at least `threshold`. Gravity's tenths of a metre are left alone.
inline void followRebase(float own[3], const float wrote[3], const float read[3], float grid = 8.f, float threshold = 4.f) {
    for (int i = 0; i < 3; ++i) {
        const float d = read[i] - wrote[i];
        if (std::fabs(d) >= threshold) own[i] += std::round(d / grid) * grid;
    }
}

// The heading (rotation about +Y, like yawFromQuat) the body is drawn with while flying: the game no longer turns the character because it
// never sees the movement keys, so the figure faces where the camera looks.
inline float bodyYaw(const Heading& h) { return std::atan2(h.fwd[0], h.fwd[1]); }

// DirectInput scan codes the game must not see while flying: movement, jump/roll, crouch/sprint.
inline constexpr int kHiddenKeys[] = {0x11 /*W*/, 0x1E /*A*/, 0x1F /*S*/, 0x20 /*D*/, 0x39 /*Space*/, 0x2A /*LShift*/, 0x36 /*RShift*/, 0x1D /*LCtrl*/};

} // namespace eldenring::flight
