#pragma once

// Creative flight (feat/creative). Pure logic: the double-tap toggle, the direction from the keys and the camera, and the per-frame step.
//
// The game cannot be told to fly (no gravity switch is known, and writing its physics flags broke walking, see HANDOFF section 9), and it
// does not need to: the native body is hidden and our own Steve is drawn, so only the physics position matters. While flying the keys
// are hidden from the game (DirectInput) and the position is integrated here from the keys and the camera, exactly like the placed-block
// collision already writes it (PhysicsModule +0x70 and the previous position +0x80). Nothing but the position is written.
// Minecraft's creative flight: double-tap jump to start/stop, W/A/S/D along the view's horizontal heading, Space up, Shift down.

#include <cmath>
#include <cstdint>

namespace eldenring::flight {

inline constexpr uint64_t kDoubleTapMs = 300;
inline constexpr float kHorizontalSpeed = 10.9f; // m/s, creative flying speed (0.05 * 20 * 10.89 blocks/s with the default)
inline constexpr float kVerticalSpeed = 7.5f;    // m/s
inline constexpr float kMaxStep = 0.05f;         // s: a hitch (loading, alt-tab) must not throw the player across the map

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

struct Keys {
    bool forward{false}, back{false}, left{false}, right{false}, up{false}, down{false};
};

// The camera's horizontal heading: forward and right projected on the ground plane and normalised. False when looking straight up/down
// (no heading); the caller then does not move horizontally this frame.
struct Heading {
    float fwd[2]{0.f, 1.f};
    float right[2]{1.f, 0.f};
};
inline bool headingFromCamera(const float forward[3], const float right[3], Heading& out) {
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
inline bool step(float pos[3], const Keys& k, const Heading& h, float dt, float h_speed = kHorizontalSpeed, float v_speed = kVerticalSpeed) {
    if (!(dt > 0.f)) return false;
    if (dt > kMaxStep) dt = kMaxStep;
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

// True when the position read back differs from what we wrote last frame by more than tol on any axis: something else moved the player.
inline bool positionOverwritten(const float wrote[3], const float read[3], float tol) {
    for (int i = 0; i < 3; ++i) {
        if (std::fabs(read[i] - wrote[i]) > tol) return true;
    }
    return false;
}

// The game recentres its physics coordinates ("floating origin") by whole multiples of 8 m when the player is far from the origin, keeping
// the fraction (observed 2026-10-10: -24, -32, -16, -8, +32 on single axes). Flight keeps its own position (the game's gravity would pull
// a position read back every frame), so after each write the difference to what is read back is only taken over where it is such a
// re-base: whole grid steps on an axis whose difference is at least `threshold`. Gravity's tenths of a metre are left alone.
inline void followRebase(float own[3], const float wrote[3], const float read[3], float grid = 8.f, float threshold = 4.f) {
    for (int i = 0; i < 3; ++i) {
        const float d = read[i] - wrote[i];
        if (std::fabs(d) >= threshold) own[i] += std::round(d / grid) * grid;
    }
}

// The heading (rotation about +Y, like yawFromQuat) the body is drawn with while flying: the game no longer turns the character because it
// never sees the movement keys, so the figure faces where the camera looks.
inline float bodyYaw(const Heading& h) { return std::atan2(h.fwd[0], h.fwd[1]); }

// DirectInput scan codes the game must not see while flying: movement, jump/roll, crouch/sprint.
inline constexpr int kHiddenKeys[] = {0x11 /*W*/, 0x1E /*A*/, 0x1F /*S*/, 0x20 /*D*/, 0x39 /*Space*/, 0x2A /*LShift*/, 0x36 /*RShift*/, 0x1D /*LCtrl*/};

} // namespace eldenring::flight
