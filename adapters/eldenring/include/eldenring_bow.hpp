#pragma once

// Forwarding wrapper for mc::bow to maintain backward compatibility.
#include "mc/bow.hpp"

namespace eldenring::bow {

inline constexpr float kTicksPerSecond = mc::kTicksPerSecond;
inline constexpr float kMinPower = mc::kMinPower;

using mc::powerForTicks;
using mc::damageForPower;
using Update = mc::BowUpdate;
using BowController = mc::BowController;
using mc::pullingStage;
using mc::shownBow;
using mc::mainHandClaimsRightClick;
using mc::bowHandUsable;
using mc::bowInOffhand;
using mc::quatForward;
using AimError = mc::AimError;
using mc::aimError;
using mc::determinant;
using mc::buildBasis;

} // namespace eldenring::bow
