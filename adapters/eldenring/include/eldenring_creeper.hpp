#pragma once

// Forwarding wrapper for mc::creeper to maintain backward compatibility.
#include "mc/creeper.hpp"

namespace eldenring::creeper {

inline constexpr float kDefaultFuseSeconds = mc::kDefaultCreeperFuseSeconds;
inline constexpr float kIgniteDistance = mc::kCreeperIgniteDistance;
inline constexpr float kDefuseDistance = mc::kCreeperDefuseDistance;
inline constexpr float kExplosionRadius = mc::kCreeperExplosionRadius;
inline constexpr float kExplosionDamage = mc::kCreeperDefaultDamage;
inline constexpr float kMaxSwellScale = mc::kCreeperMaxSwellScale;

using CreeperEventKind = mc::CreeperEventKind;
using CreeperEvent = mc::CreeperEvent;
using CreeperTarget = mc::CreeperTarget;
using CreeperEnemyTarget = mc::CreeperEnemyTarget;
using CreeperManager = mc::CreeperManager;

} // namespace eldenring::creeper
