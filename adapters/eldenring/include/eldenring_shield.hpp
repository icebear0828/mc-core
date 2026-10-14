#pragma once

// Forwarding wrapper for mc::ShieldController and shield blocking logic to maintain backward compatibility.
#include "mc/shield.hpp"

namespace eldenring::shield {

inline constexpr float kBlockMinDot = mc::kBlockMinDot;
inline constexpr float kShieldRaiseDelaySec = mc::kShieldRaiseDelaySec;

using mc::frontDot;
using mc::yawFromForward;
using mc::isAttackBlocked;
using mc::shieldBlocksHit;
using ShieldController = mc::ShieldController;

} // namespace eldenring::shield
