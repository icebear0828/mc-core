#pragma once

// Forwarding wrapper for mc::XpModel to maintain backward compatibility.
#include "mc/xp.hpp"

namespace eldenring::live {

using XpModel = mc::XpModel;
using mc::xpForKill;

} // namespace eldenring::live
