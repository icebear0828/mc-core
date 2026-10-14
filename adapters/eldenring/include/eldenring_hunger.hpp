#pragma once

// Forwarding wrapper for mc::HungerModel to maintain backward compatibility.
#include "mc/hunger.hpp"

namespace eldenring::live {

using FoodValue = mc::FoodValue;
using HungerModel = mc::HungerModel;
using mc::foodValue;

} // namespace eldenring::live
