#pragma once

// Overwatch Doomfist rocket punch displacement. Pure logic: the per-frame
// forward speed curve for the punch's thrust window. The adapter integrates it
// and writes the player position; tests cover the curve and its total distance.

#include <algorithm>
#include <cmath>

namespace eldenring::ow {

// Authored punch clip (109 frames @ 30.278 fps): the windup coils the arm
// through frame 67, the fist releases at frame 68 (2.25 s) and reaches full
// extension at frame 92 (3.05 s). The release sound and the player dash are
// keyed to this window, not to the right-click itself.
inline constexpr float kRocketPunchReleaseTime = 2.25f;
inline constexpr float kRocketPunchEndTime = 3.05f;

struct RocketPunchDashConfig {
    float distance_m{4.0f}; // total forward displacement over the thrust window
    float start_time{kRocketPunchReleaseTime}; // 68 / 30.278
    float end_time{kRocketPunchEndTime};       // 92 / 30.278
};

// The dispatcher must not return to idle before the thrust window closes, or
// the integrated dash distance silently shrinks. A tail shorter than the
// authored window therefore only trims the return-to-idle part after 3.05 s.
inline float rocketPunchStateEnd(float release_time_s, float tail_s, float clip_duration_s) {
    float end = release_time_s + std::max(0.f, tail_s);
    if (end < kRocketPunchEndTime) end = kRocketPunchEndTime;
    if (clip_duration_s > 0.f) end = std::min(end, clip_duration_s);
    return end;
}

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
