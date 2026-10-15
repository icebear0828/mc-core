#pragma once

#include <cmath>
#include <cstdint>

namespace mc {

inline constexpr uint64_t kDoubleTapMs = 300;
inline constexpr float kFlightHorizontalSpeed = 10.9f; // m/s, creative flying speed (0.05 * 20 * 10.89 blocks/s with the default)
inline constexpr float kFlightVerticalSpeed = 7.5f;    // m/s
inline constexpr float kFlightMaxStep = 0.05f;         // s: a hitch (loading, alt-tab) must not throw the player across the map

inline constexpr float kHorizontalSpeed = kFlightHorizontalSpeed;
inline constexpr float kVerticalSpeed = kFlightVerticalSpeed;
inline constexpr float kMaxStep = kFlightMaxStep;

// Two presses of the same key within kDoubleTapMs. update() is fed the key state every tick and returns true on the second press.
class DoubleTap {
public:
    bool update(bool down, uint64_t now_ms) {
        const bool edge = down && !prev_;
        prev_ = down;
        if (!edge) return false;
        if (have_ && now_ms - last_ms_ <= kDoubleTapMs) {
            have_ = false; // a third quick press starts a new pair
            return true;
        }
        have_ = true;
        last_ms_ = now_ms;
        return false;
    }
    void reset() {
        prev_ = false;
        have_ = false;
    }

private:
    bool prev_{false};
    bool have_{false};
    uint64_t last_ms_{0};
};

struct FlightKeys {
    bool forward{false}, back{false}, left{false}, right{false}, up{false}, down{false};
};
using Keys = FlightKeys;

// The camera's horizontal heading: forward and right projected on the ground plane and normalised. False when looking straight up/down
// (no heading); the caller then does not move horizontally this frame.
struct FlightHeading {
    float fwd[2]{0.f, 1.f};
    float right[2]{1.f, 0.f};
};
using Heading = FlightHeading;

inline bool headingFromCamera(const float forward[3], const float right[3], FlightHeading& out) {
    const float fl = std::sqrt(forward[0] * forward[0] + forward[2] * forward[2]);
    const float rl = std::sqrt(right[0] * right[0] + right[2] * right[2]);
    if (!(fl > 1e-3f) || !(rl > 1e-3f)) return false;
    out.fwd[0] = forward[0] / fl;
    out.fwd[1] = forward[2] / fl;
    out.right[0] = right[0] / rl;
    out.right[1] = right[2] / rl;
    return true;
}

// One frame of flight: pos (x, y up, z) is advanced by dt seconds. Diagonal keys do not move faster than a single key; vertical
// movement is independent of the horizontal one (Minecraft). Returns true when the position changed.
inline bool flightStep(float pos[3], const FlightKeys& k, const FlightHeading& h, float dt,
                       float h_speed = kFlightHorizontalSpeed, float v_speed = kFlightVerticalSpeed) {
    if (!(dt > 0.f)) return false;
    if (dt > kFlightMaxStep) dt = kFlightMaxStep;
    const float f = (k.forward ? 1.f : 0.f) - (k.back ? 1.f : 0.f);
    const float r = (k.right ? 1.f : 0.f) - (k.left ? 1.f : 0.f);
    float wx = f * h.fwd[0] + r * h.right[0];
    float wz = f * h.fwd[1] + r * h.right[1];
    const float wl = std::sqrt(wx * wx + wz * wz);
    bool moved = false;
    if (wl > 1e-3f) {
        pos[0] += wx / wl * h_speed * dt;
        pos[2] += wz / wl * h_speed * dt;
        moved = true;
    }
    const float v = (k.up ? 1.f : 0.f) - (k.down ? 1.f : 0.f);
    if (v != 0.f) {
        pos[1] += v * v_speed * dt;
        moved = true;
    }
    return moved;
}

inline bool step(float pos[3], const FlightKeys& k, const FlightHeading& h, float dt,
                 float h_speed = kFlightHorizontalSpeed, float v_speed = kFlightVerticalSpeed) {
    return flightStep(pos, k, h, dt, h_speed, v_speed);
}

// True when the position read back differs from what we wrote last frame by more than tol on any axis: something else moved the player.
inline bool positionOverwritten(const float wrote[3], const float read[3], float tol) {
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(read[i] - wrote[i]) > tol) return true;
    }
    return false;
}

} // namespace mc
