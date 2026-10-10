#pragma once

// Minecraft's bow: hold the right button to draw, release to shoot. Pure logic (no game, no clock): the caller says every frame whether the button is
// down and whether the bow can be used right now (in hand, arrows or creative, no screen open, alive). Also the pure helper that finds a float triple
// near a point in a block of memory, used by the read-only probe that looks for where the game keeps a bullet's position.

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

// Byte offsets (4-aligned) in `data` where three consecutive floats are all finite and within `radius` of `center` on every axis.
inline std::vector<int> findTriples(const uint8_t* data, size_t size, const float center[3], float radius) {
    std::vector<int> out;
    for (size_t at = 0; at + 12 <= size; at += 4) {
        float v[3];
        std::memcpy(v, data + at, 12);
        bool ok = true;
        for (int i = 0; i < 3; ++i) ok = ok && std::isfinite(v[i]) && std::fabs(v[i] - center[i]) <= radius;
        if (ok) out.push_back(static_cast<int>(at));
    }
    return out;
}

} // namespace eldenring::bow
