#pragma once

// CS2 AWP scope state. RMB cycles off -> zoom 1 -> zoom 2 -> off, matching
// CS2's two-stage AWP scope. Only weapons whose ballistics declare has_scope
// can scope; everything else stays off.

#include <cstdint>

namespace eldenring::cs {

inline constexpr uint8_t kScopeOff = 0;
inline constexpr uint8_t kScopeLevel1 = 1;
inline constexpr uint8_t kScopeLevel2 = 2;
inline constexpr uint8_t kScopeLevelCount = 3; // off + two zoom stages

inline uint8_t nextScopeLevel(uint8_t level, bool has_scope) {
    if (!has_scope) return kScopeOff;
    return static_cast<uint8_t>((level + 1u) % kScopeLevelCount);
}

inline bool scopeActive(uint8_t level) {
    return level == kScopeLevel1 || level == kScopeLevel2;
}

// Vertical FOV (degrees) to apply for a zoom stage. 0 means "no override".
inline float scopeFovDegrees(uint8_t level, float fov1_deg, float fov2_deg) {
    if (level == kScopeLevel2) return fov2_deg;
    if (level == kScopeLevel1) return fov1_deg;
    return 0.f;
}

// CS2's scoped sensitivity: base * (scope_fov / native_fov) * ratio.
// Returns 1.0 when not scoped or when the native FOV is unknown.
inline float scopeSensitivityScale(uint8_t level, float fov1_deg, float fov2_deg,
                                   float native_fov_deg, float ratio) {
    if (!scopeActive(level) || !(native_fov_deg > 0.f)) return 1.f;
    const float fov = scopeFovDegrees(level, fov1_deg, fov2_deg);
    if (!(fov > 0.f)) return 1.f;
    return (fov / native_fov_deg) * ratio;
}

} // namespace eldenring::cs
