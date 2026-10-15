#pragma once

#include "mc/orientation.hpp"

namespace eldenring::motion {

using mc::OrientationQuat;
using mc::computeInputDirection;
using mc::normalizeAngle;
using mc::stepYaw;
using mc::kSteveYawOffset;
using mc::characterQuatFromHeading;
using mc::headingFromCharacterQuat;

} // namespace eldenring::motion
