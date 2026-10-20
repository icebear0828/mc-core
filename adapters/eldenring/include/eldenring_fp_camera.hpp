#pragma once

// First-person camera pitch extension.
//
// Elden Ring's camera controller clamps its pitch: the third-person spring arm must not go under the ground,
// so first person (which reuses the game's rotation and only moves the camera to the eyes) cannot look 90
// degrees down. We keep the game's yaw and roll and add an offset to the pitch after the camera step:
//
//   offset += deg_per_count * raw_dy - (game_down_now - game_down_prev)
//
// Inside the game's own range the two mouse terms cancel and the offset stays put. At a clamp the game stops
// responding, so the same mouse motion keeps growing the offset. Coming back, the game's own delta is
// subtracted again, so the view returns at the normal speed. Calibrating deg_per_count from the game's own
// response also handles the inverted-Y setting: the ratio simply becomes negative.
//
// Pure logic, no Windows headers. Tested in tests/test_eldenring_fp_camera.cpp.

#include <algorithm>
#include <cmath>

namespace eldenring::fpcam {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kDeg = kPi / 180.f;

inline constexpr float kMinDegPerCount = 0.001f;      // reject a near-zero sample (a clamped frame)
inline constexpr float kMaxDegPerCount = 0.5f;        // reject an absurd sample
inline constexpr float kDefaultDegPerCount = 0.1f;    // only before the first calibration sample
inline constexpr float kCalibrationAlpha = 0.1f;      // EMA weight of a new sample
inline constexpr float kAutoCentreResyncDeg = 1.0f;   // the game moved the pitch with no mouse input
inline constexpr float kMaxFrameCounts = 2000.f;      // a bigger delta is a discontinuity (menu, alt-tab)
inline constexpr float kDefaultMaxPitchDeg = 89.9f;   // never exactly 90: movement direction projects the camera forward
inline constexpr float kMinMaxPitchDeg = 30.f;
inline constexpr float kMaxMaxPitchDeg = 89.9f;       // onto the ground plane, and a vertical forward has no heading

// Down-positive pitch of a forward vector: 0 = level, +90 = straight down, -90 = straight up.
inline float downPitchDeg(const float forward[3]) {
    const float y = std::clamp(forward[1], -1.f, 1.f);
    return -std::asin(y) / kDeg;
}

// Rotate the camera basis around its right axis by `down_delta_deg` (positive = look further down).
// Only up and forward are used, so roll (already in the basis) is preserved; both stay unit length.
inline void rotateDown(float up[3], float forward[3], float down_delta_deg) {
    if (down_delta_deg == 0.f) return;
    const float c = std::cos(down_delta_deg * kDeg);
    const float s = std::sin(down_delta_deg * kDeg);
    float f[3], u[3];
    for (int i = 0; i < 3; ++i) {
        f[i] = forward[i] * c - up[i] * s;
        u[i] = up[i] * c + forward[i] * s;
    }
    for (int i = 0; i < 3; ++i) {
        forward[i] = f[i];
        up[i] = u[i];
    }
}

class PitchOffset {
public:
    void reset() {
        offset_deg_ = 0.f;
        prev_down_deg_ = 0.f;
        seeded_ = false;
    }

    // A fixed degrees-per-count disables auto-calibration (0 restores it).
    void setDegPerCount(float value) {
        deg_per_count_ = std::clamp(value, 0.f, kMaxDegPerCount);
        auto_calibrate_ = value == 0.f;
    }

    void setMaxPitchDeg(float value) { max_pitch_deg_ = std::clamp(value, kMinMaxPitchDeg, kMaxMaxPitchDeg); }

    // raw_dy: DirectInput mouse Y delta since the last camera step (positive = mouse moved down).
    // game_down_deg: the game's own pitch this frame, down-positive.
    // Returns the down-positive pitch the render camera should use.
    float update(float raw_dy, float game_down_deg) {
        if (!seeded_) {
            prev_down_deg_ = game_down_deg;
            seeded_ = true;
            return game_down_deg;
        }

        const float game_delta = game_down_deg - prev_down_deg_;
        prev_down_deg_ = game_down_deg;

        // A jump this large is a discontinuity (menu, alt-tab, a stalled camera thread), not mouse input.
        if (std::fabs(raw_dy) > kMaxFrameCounts) {
            reset();
            prev_down_deg_ = game_down_deg;
            seeded_ = true;
            return game_down_deg;
        }

        // The game moved the pitch without mouse input (lock-on, cutscene, auto-centre): follow it.
        if (raw_dy == 0.f && std::fabs(game_delta) > kAutoCentreResyncDeg) {
            offset_deg_ = 0.f;
            return game_down_deg;
        }

        calibrate(raw_dy, game_delta);

        const float k = deg_per_count_ != 0.f ? deg_per_count_ : kDefaultDegPerCount;
        offset_deg_ += k * raw_dy - game_delta;

        float effective = game_down_deg + offset_deg_;
        if (effective > max_pitch_deg_) {
            effective = max_pitch_deg_;
            offset_deg_ = effective - game_down_deg;
        } else if (effective < -max_pitch_deg_) {
            effective = -max_pitch_deg_;
            offset_deg_ = effective - game_down_deg;
        }
        return effective;
    }

    [[nodiscard]] float offsetDeg() const { return offset_deg_; }
    [[nodiscard]] float degPerCount() const { return deg_per_count_; }
    [[nodiscard]] float maxPitchDeg() const { return max_pitch_deg_; }
    [[nodiscard]] bool seeded() const { return seeded_; }

private:
    void calibrate(float raw_dy, float game_delta) {
        if (!auto_calibrate_ || raw_dy == 0.f) return;
        // A clamped frame has game_delta ~ 0 while the mouse moved: no sensitivity information.
        if (std::fabs(game_delta) < 0.01f) return;
        const float sample = game_delta / raw_dy;
        if (std::fabs(sample) < kMinDegPerCount || std::fabs(sample) > kMaxDegPerCount) return;
        if (deg_per_count_ == 0.f || (sample > 0.f) != (deg_per_count_ > 0.f)) {
            // First sample, or the player changed the invert-Y setting: start over.
            deg_per_count_ = sample;
        } else {
            deg_per_count_ += (sample - deg_per_count_) * kCalibrationAlpha;
        }
    }

    float offset_deg_{0.f};
    float prev_down_deg_{0.f};
    float deg_per_count_{0.f};
    float max_pitch_deg_{kDefaultMaxPitchDeg};
    bool auto_calibrate_{true};
    bool seeded_{false};
};

} // namespace eldenring::fpcam
