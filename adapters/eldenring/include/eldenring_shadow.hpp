#pragma once

#include "mc/shadow.hpp"
#include "eldenring_los.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <optional>

namespace eldenring::shadow {

using mc::kBaseRadius;
using mc::kMaxStrength;
using mc::kFadeHeight;
using mc::kLift;
using mc::kRayStartAbove;

using mc::Shadow;
using mc::shadowAt;
using mc::shadowMatrix;
using mc::falloff;
using mc::buildShadowDisc;

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

} // namespace eldenring::shadow
