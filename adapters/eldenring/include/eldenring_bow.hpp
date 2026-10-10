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


// Byte offsets (4-aligned) of float triples that lie ahead of `p0` along `aim`: finite, `min_d`..`max_d` metres from p0, and the angle between
// (triple - p0) and `aim` has a cosine of at least `min_cos`. A moving bolt that has not dropped far matches; the spawn point (distance 0) and anything
// behind or beside the muzzle does not.
inline std::vector<int> findAlongAim(const uint8_t* data, size_t size, const float p0[3], const float aim[3], float min_d, float max_d, float min_cos) {
    std::vector<int> out;
    const float an = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (!(an > 1e-6f)) return out;
    for (size_t at = 0; at + 12 <= size; at += 4) {
        float v[3];
        std::memcpy(v, data + at, 12);
        if (!(std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]))) continue;
        const float d[3] = {v[0] - p0[0], v[1] - p0[1], v[2] - p0[2]};
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (dist < min_d || dist > max_d) continue;
        const float cosine = (d[0] * aim[0] + d[1] * aim[1] + d[2] * aim[2]) / (dist * an);
        if (cosine >= min_cos) out.push_back(static_cast<int>(at));
    }
    return out;
}


// A float triple that moved between two snapshots of the same block of memory, along the aim: finds the position of a flying bullet whatever the
// coordinate frame it is stored in (the snapshots are taken a few hundred milliseconds apart).
struct Mover {
    int offset{0};
    float v1[3]{}, v2[3]{};
    float moved{0.f};
};

inline std::vector<Mover> findMovers(const uint8_t* a, const uint8_t* b, size_t size, const float aim[3], float min_move, float max_move, float min_cos) {
    std::vector<Mover> out;
    const float an = std::sqrt(aim[0] * aim[0] + aim[1] * aim[1] + aim[2] * aim[2]);
    if (!(an > 1e-6f)) return out;
    for (size_t at = 0; at + 12 <= size; at += 4) {
        float p[3], q[3];
        std::memcpy(p, a + at, 12);
        std::memcpy(q, b + at, 12);
        if (!(std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]) && std::isfinite(q[0]) && std::isfinite(q[1]) && std::isfinite(q[2]))) continue;
        const float d[3] = {q[0] - p[0], q[1] - p[1], q[2] - p[2]};
        const float moved = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (moved < min_move || moved > max_move) continue;
        if ((d[0] * aim[0] + d[1] * aim[1] + d[2] * aim[2]) / (moved * an) < min_cos) continue;
        Mover m;
        m.offset = static_cast<int>(at);
        std::memcpy(m.v1, p, 12);
        std::memcpy(m.v2, q, 12);
        m.moved = moved;
        out.push_back(m);
    }
    return out;
}


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


// The game turns the flight of a bolt we spawn from a replayed request by this much about the vertical axis (6 shots measured: +32.0, +30.6, +28.9 isolated,
// +32.9, +30.5, +29.7 overlapping; REVERSE 35.9). We give the aim turned by the opposite angle. mc_er_steve.txt: bolt_yaw_offset_deg.
inline constexpr float kBoltYawOffsetDeg = 31.f;

// Turns `v` about the vertical (+y) axis so that its yaw (atan2(x, z)) grows by `deg`; the height and the length do not change.
inline void rotateYaw(const float v[3], float deg, float out[3]) {
    const float r = deg * 3.14159265f / 180.f;
    const float c = std::cos(r), s = std::sin(r);
    const float x = v[0], y = v[1], z = v[2];
    out[0] = x * c + z * s;
    out[1] = y;
    out[2] = z * c - x * s;
}

} // namespace eldenring::bow
