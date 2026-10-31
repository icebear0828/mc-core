#pragma once

// Overwatch Doomfist rocket punch displacement. Pure logic: the per-frame
// forward speed curve for the punch's thrust window. The adapter integrates it
// and writes the player position; tests cover the curve and its total distance.

#include <cmath>

namespace eldenring::ow {

struct RocketPunchDashConfig {
    float distance_m{4.0f}; // total forward displacement over the thrust window
    float start_time{0.25f}; // state 3: windup ends, the punch releases
    float end_time{0.60f};   // state 3: thrust ends, recovery begins
};

// Forward speed (m/s) at `anim_time`. A sin(pi * p) profile keeps the total
// displacement equal to `distance_m`: integral over the window is 2/pi, so the
// peak is distance * pi / (2 * span).
inline float rocketPunchSpeed(float anim_time, const RocketPunchDashConfig& cfg = {}) {
    const float span = cfg.end_time - cfg.start_time;
    if (span <= 0.f || anim_time <= cfg.start_time || anim_time >= cfg.end_time) return 0.f;
    const float p = (anim_time - cfg.start_time) / span;
    const float peak = cfg.distance_m * 3.14159265358979f / (2.f * span);
    return peak * std::sin(p * 3.14159265358979f);
}

// Distance travelled from the start of the window to `anim_time`.
inline float rocketPunchDistance(float anim_time, const RocketPunchDashConfig& cfg = {}) {
    const float span = cfg.end_time - cfg.start_time;
    if (span <= 0.f || anim_time <= cfg.start_time) return 0.f;
    if (anim_time >= cfg.end_time) return cfg.distance_m;
    const float p = (anim_time - cfg.start_time) / span;
    return cfg.distance_m * (1.f - std::cos(p * 3.14159265358979f)) * 0.5f;
}

// Horizontal unit direction from a camera forward vector. False when the view
// is (almost) straight up or down and there is no meaningful horizontal heading.
inline bool rocketPunchDirection(const float forward[3], float& out_x, float& out_z) {
    if (forward == nullptr) return false;
    const float len = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (len < 0.1f) return false;
    out_x = forward[0] / len;
    out_z = forward[2] / len;
    return true;
}

} // namespace eldenring::ow
