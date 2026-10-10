#pragma once

// Minecraft's bow: hold the right button to draw, release to shoot. Pure logic (no game, no clock): the caller says every frame whether the button is
// down and whether the bow can be used right now (in hand, arrows or creative, no screen open, alive). Also the aim calibration helpers (quaternion forward,
// yaw / pitch error) and the request-matrix basis.

#include "mc/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace eldenring::bow {

inline constexpr float kTicksPerSecond = 20.f; // Minecraft ticks
inline constexpr float kMinPower = 0.1f;      // below this the arrow is not fired (a draw of less than three ticks)

// Minecraft: f = ticks / 20; f = (f * f + f * 2) / 3; capped at 1.
inline float powerForTicks(float ticks) {
    const float f = ticks / kTicksPerSecond;
    return std::min(1.f, (f * f + f * 2.f) / 3.f);
}

// An arrow does 6 at full power (speed 3 blocks per tick * 2). A full draw is a critical arrow: add the average of Minecraft's random bonus (+3).
inline float damageForPower(float power) { return power * 6.f + (power >= 1.f ? 3.f : 0.f); }

struct Update {
    bool started{false};
    bool released{false};  // fire an arrow with `power`
    bool cancelled{false}; // the draw ended without a shot (too short, or the bow was lost)
    float power{0.f};
};

class BowController {
public:
    // Every frame. `usable`: the bow is in the main hand, there is an arrow (or creative mode), no screen is open, the player is alive.
    Update update(float dt, bool right_down, bool usable) {
        Update u;
        if (!usable) {
            if (drawing_) u.cancelled = true;
            reset();
            prev_down_ = right_down;
            return u;
        }
        if (!drawing_) {
            if (right_down && !prev_down_) { // only a fresh press starts a draw
                drawing_ = true;
                ticks_ = 0.f;
                u.started = true;
            }
        } else {
            ticks_ += dt * kTicksPerSecond;
            if (!right_down) {
                const float p = powerForTicks(ticks_);
                if (p >= kMinPower) {
                    u.released = true;
                    u.power = p;
                } else {
                    u.cancelled = true;
                }
                reset();
            }
        }
        prev_down_ = right_down;
        return u;
    }

    [[nodiscard]] bool drawing() const { return drawing_; }
    // Ticks the bow has been drawn (0 when not drawing): the first-person pose animates with it.
    [[nodiscard]] float ticks() const { return drawing_ ? ticks_ : 0.f; }
    // 0 when not drawing; for the pose of the arms and the sound.
    [[nodiscard]] float power() const { return drawing_ ? powerForTicks(ticks_) : 0.f; }

private:
    void reset() {
        drawing_ = false;
        ticks_ = 0.f;
    }
    bool drawing_{false};
    bool prev_down_{false};
    float ticks_{0.f};
};


// ---- aim calibration: where does a bolt really fly compared with the aim we gave? ---------------------------------------------------------------------
// The forward (+z) of a unit quaternion (x, y, z, w): the bullet's CSBulletIns keeps its flight direction there (REVERSE 35.8).
inline void quatForward(const float q[4], float out[3]) {
    const float x = q[0], y = q[1], z = q[2], w = q[3];
    out[0] = 2.f * (x * z + w * y);
    out[1] = 2.f * (y * z - w * x);
    out[2] = 1.f - 2.f * (x * x + y * y);
}

struct AimError {
    float yaw_deg{0.f};   // + = the flight is turned towards +x (about +y), -180..180
    float pitch_deg{0.f}; // + = the flight is above the aim
};

inline AimError aimError(const float aim[3], const float flight[3]) {
    auto yawPitch = [](const float v[3], float& yaw, float& pitch) {
        const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        yaw = std::atan2(v[0], v[2]) * 180.f / 3.14159265f;
        pitch = n > 1e-6f ? std::asin(std::max(-1.f, std::min(1.f, v[1] / n))) * 180.f / 3.14159265f : 0.f;
    };
    float ay, ap, fy, fp;
    yawPitch(aim, ay, ap);
    yawPitch(flight, fy, fp);
    float dy = fy - ay;
    while (dy > 180.f) dy -= 360.f;
    while (dy < -180.f) dy += 360.f;
    return {dy, fp - ap};
}


// Which of the three drawn-bow sprites to show for a draw power (Minecraft's item properties: pull > 0 -> bow_pulling_0, >= 0.65 -> _1, >= 0.9 -> _2); -1 = the plain bow.
inline int pullingStage(float power) {
    if (power >= 0.9f) return 2;
    if (power >= 0.65f) return 1;
    if (power > 0.f) return 0;
    return -1;
}

// The item to draw in the hand: the drawn-bow sprite for the bow being drawn, the item itself otherwise.
inline mc::ItemId shownBow(mc::ItemId item, float power) {
    if (item != mc::ItemId::Bow) return item;
    const int stage = pullingStage(power);
    return stage < 0 ? item : static_cast<mc::ItemId>(static_cast<int>(mc::ItemId::BowPulling0) + stage);
}

// determinant of the rows (right, up, forward): +1 for a proper rotation in which right x up = forward (a real request's matrix), -1 when one axis is mirrored.
inline float determinant(const float r[3], const float u[3], const float f[3]) {
    return r[0] * (u[1] * f[2] - u[2] * f[1]) - r[1] * (u[0] * f[2] - u[2] * f[0]) + r[2] * (u[0] * f[1] - u[1] * f[0]);
}

// A roll-free basis from a forward vector, built the way a real request's matrix is: up is the world up as far as the forward allows,
// right = up x forward, then up = forward x right (so right x up = forward). Looking straight up or down falls back to a fixed right.
inline void buildBasis(const float forward_in[3], float right[3], float up[3]) {
    float f[3] = {forward_in[0], forward_in[1], forward_in[2]};
    const float fn = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (fn > 1e-6f) {
        for (float& c : f) c /= fn;
    } else {
        f[0] = 0.f;
        f[1] = 0.f;
        f[2] = 1.f;
    }
    float r[3] = {f[2], 0.f, -f[0]}; // (0, 1, 0) x forward
    const float rn = std::sqrt(r[0] * r[0] + r[2] * r[2]);
    if (rn < 1e-4f) { // straight up or down
        r[0] = 1.f;
        r[1] = 0.f;
        r[2] = 0.f;
    } else {
        r[0] /= rn;
        r[2] /= rn;
    }
    right[0] = r[0];
    right[1] = r[1];
    right[2] = r[2];
    up[0] = f[1] * r[2] - f[2] * r[1]; // forward x right
    up[1] = f[2] * r[0] - f[0] * r[2];
    up[2] = f[0] * r[1] - f[1] * r[0];
}

} // namespace eldenring::bow
