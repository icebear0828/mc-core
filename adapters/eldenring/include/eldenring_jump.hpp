#pragma once

#include "mc/jump.hpp"

namespace eldenring::live {

using mc::kMcJumpSpeed;
using mc::kMcGravity;
using mc::mcJumpPeakHeight;
using mc::JumpTrace;

} // namespace eldenring::live

namespace eldenring::jump {

using mc::kMcJumpInitialVelocity;
using mc::kMcJumpSprintBoost;
using mc::kMcJumpGravity;
using mc::kMcAirDrag;
using mc::kMaxJumpDt;
using mc::kJumpPeakTarget;
using mc::canTakeOff;
using mc::JumpStepResult;
using mc::MinecraftJumpController;
using mc::JumpController;

// Determines Havok physics flags on landing based on whether landing on voxel blocks vs native terrain.
// Standing on a block requires airborne_flag = true (0x1D0 = 1) so Havok integrates +0x120 velocity.
struct LandingResolution {
    bool on_ground_flag{false};   // module + 0x92
    bool airborne_flag{false};    // module + 0x1D0
    bool support_flag{false};     // module + 0x1D1
};

inline LandingResolution computeLandingFlags(bool landed_on_block) {
    LandingResolution res;
    if (landed_on_block) {
        // Landing on placed voxel blocks:
        // Must maintain airborne_flag = true (0x1D0 = 1) so Havok continues consuming +0x120 horizontal velocity!
        res.on_ground_flag = true;
        res.airborne_flag = true;
        res.support_flag = true;
    } else {
        // Landing on native terrain: restore native grounded state
        res.on_ground_flag = true;
        res.airborne_flag = false;
        res.support_flag = true;
    }
    return res;
}

} // namespace eldenring::jump
