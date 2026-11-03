#pragma once

// Controlled teleport test (debug only): the key thread asks for a vertical
// offset, the game thread performs one write and logs the result. The write
// path mirrors the verified creative-flight path: +0x70, +0x80 (last position),
// then +0x91 (proxy sync request). This is NOT read-only; it is enabled only by
// mc_er_physwritetest.txt.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eldenring::physwrite {

inline constexpr float kHeightsM[] = {1.f, 5.f, 10.f, 20.f};
inline constexpr int kHeightCount = 4;

inline bool validIndex(int index) { return index >= 0 && index < kHeightCount; }

// A one-shot write is reverted by the character controller on the next frame.
// Creative flight works because it writes every frame, so the test can hold the
// target for a few frames before releasing it to gravity.
struct HoldState {
    bool active{false};
    uint64_t until_ms{0};
    float target[3]{};
    bool glide{false};
    float speed_mps{10.f};
    uint64_t last_ms{0};
};

inline void startHold(HoldState& h, const float pos[3], float dy, uint64_t now_ms, uint64_t hold_ms) {
    h.active = hold_ms > 0;
    h.until_ms = now_ms + hold_ms;
    h.target[0] = pos[0];
    h.target[1] = pos[1] + dy;
    h.target[2] = pos[2];
    h.last_ms = now_ms;
}

// Moving hold: advance the target along the camera's horizontal forward at
// `speed_mps`. Vertical stays fixed, so the player is airborne the whole time.
inline void stepGlide(HoldState& h, const float forward[3], uint64_t now_ms) {
    if (!h.active) return;
    const float dt = h.last_ms == 0 ? 0.f : static_cast<float>(now_ms - h.last_ms) / 1000.f;
    h.last_ms = now_ms;
    const float len = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    if (len < 1e-3f || dt <= 0.f) return;
    const float step = h.speed_mps * dt;
    h.target[0] += forward[0] / len * step;
    h.target[2] += forward[2] / len * step;
}

inline bool holdActive(const HoldState& h, uint64_t now_ms) { return h.active && now_ms < h.until_ms; }

inline void stopHold(HoldState& h) {
    h.active = false;
    h.until_ms = 0;
}

inline std::string formatRequest(int index) {
    if (!validIndex(index)) return "physwrite: invalid request";
    char buf[96];
    std::snprintf(buf, sizeof(buf), "physwrite: request +%.0f m (Numpad%d)", static_cast<double>(kHeightsM[index]), index);
    return buf;
}

inline std::string formatResult(int index, const float before[3], const float after[3], const float read_back[3],
                                bool wrote, bool synced, int ground, int falling, float fall_t, int hp, int max_hp) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "physwrite: key=%d delta=+%.0f wrote=%d sync=%d before=(%.3f %.3f %.3f) after=(%.3f %.3f %.3f) "
                  "read=(%.3f %.3f %.3f) ground=%d falling=%d fall_t=%.3f hp=%d/%d",
                  index, validIndex(index) ? static_cast<double>(kHeightsM[index]) : 0.0, wrote ? 1 : 0,
                  synced ? 1 : 0, static_cast<double>(before[0]), static_cast<double>(before[1]),
                  static_cast<double>(before[2]), static_cast<double>(after[0]), static_cast<double>(after[1]),
                  static_cast<double>(after[2]), static_cast<double>(read_back[0]),
                  static_cast<double>(read_back[1]), static_cast<double>(read_back[2]), ground, falling,
                  static_cast<double>(fall_t), hp, max_hp);
    return buf;
}

} // namespace eldenring::physwrite
