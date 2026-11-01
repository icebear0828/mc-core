#pragma once

// OW (Doomfist) feel tuning: fire cadence, recoil, ability cooldowns and the
// authored-clip tail trim. Pure functions only, so the dispatcher and the
// renderer share the same curves and the unit tests can pin them down.

#include <algorithm>
#include <cmath>

namespace eldenring::ow {

struct OwFeelConfig {
    float fire_interval{0.25f};      // seconds between hand cannon shots
    float punch_cooldown{0.8f};      // seconds after the rocket punch fires
    float uppercut_cooldown{1.2f};   // seconds after the rising uppercut
    float slam_cooldown{2.0f};       // seconds after the seismic slam
    float punch_tail{0.35f};         // clip seconds kept after the release frame
    float anim_speed{1.0f};          // attack clip playback multiplier
    float hitstop{0.05f};            // seconds the attack clip freezes on a hit
    float slam_shake{0.06f};         // metres of camera shake on the slam
    float recoil_kick{0.05f};        // metres the gauntlet kicks towards the camera
    float recoil_recover{0.12f};     // time constant of the recoil decay
    float fov_pulse{1.5f};           // degrees added to the viewmodel FOV on fire
};

// 1 at the shot, decaying to 0 with the given time constant.
inline float owDecay(float since_s, float time_constant_s) {
    if (!(since_s >= 0.f) || !(time_constant_s > 0.f)) return 0.f;
    return std::exp(-since_s / time_constant_s);
}

// Viewmodel-space Z offset: negative pulls the gauntlet towards the camera.
inline float owRecoilOffset(float since_shot_s, const OwFeelConfig& cfg) {
    return -cfg.recoil_kick * owDecay(since_shot_s, cfg.recoil_recover);
}

inline float owFovPulse(float since_shot_s, const OwFeelConfig& cfg) {
    return cfg.fov_pulse * owDecay(since_shot_s, cfg.recoil_recover);
}

// The authored rocket punch is ~3.57 s long and the last ~1.3 s is the arm
// returning to idle. Keeping it all makes the next input feel dead, so the
// dispatcher ends the state `tail` seconds after the release frame.
inline float owTrimmedEndTime(float release_time_s, float tail_s, float clip_duration_s) {
    const float end = release_time_s + std::max(0.f, tail_s);
    if (!(clip_duration_s > 0.f)) return end;
    return std::min(clip_duration_s, end);
}

inline bool owReady(float cooldown_left_s) { return !(cooldown_left_s > 0.f); }

} // namespace eldenring::ow
