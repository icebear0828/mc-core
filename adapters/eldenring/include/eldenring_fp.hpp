#pragma once

// First-person hand and held item, following Minecraft's ItemInHandRenderer: the pose of the item (or the bare arm) for
// the current swing, the equip/lower animation, eating, view sway and walking bob. Everything is Minecraft camera space
// (right handed, +X right, +Y up, -Z forward, blocks) with column vectors and a pose stack that multiplies on the right,
// exactly as the original does. toHost() converts a pose to the row-vector, left-handed (+Z forward) matrix the D3D12
// renderer wants. Pure logic, unit-tested.

#include "mc/rig.hpp"
#include "mc/types.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace eldenring::fp {

inline constexpr float kPi = 3.14159265358979f;
inline constexpr float kDeg = kPi / 180.f;

struct M4 {
    std::array<float, 16> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; // m[row * 4 + col]; v' = M * v
};

inline M4 mul(const M4& a, const M4& b) {
    M4 r;
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float s = 0.f;
            for (int k = 0; k < 4; ++k) s += a.m[static_cast<size_t>(i * 4 + k)] * b.m[static_cast<size_t>(k * 4 + j)];
            r.m[static_cast<size_t>(i * 4 + j)] = s;
        }
    }
    return r;
}
inline M4 translate(float x, float y, float z) {
    M4 r;
    r.m[3] = x;
    r.m[7] = y;
    r.m[11] = z;
    return r;
}
inline M4 scale(float s) {
    M4 r;
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}
inline M4 rotX(float deg) {
    const float c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r;
    r.m[5] = c;
    r.m[6] = -s;
    r.m[9] = s;
    r.m[10] = c;
    return r;
}
inline M4 rotY(float deg) {
    const float c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r;
    r.m[0] = c;
    r.m[2] = s;
    r.m[8] = -s;
    r.m[10] = c;
    return r;
}
inline M4 rotZ(float deg) {
    const float c = std::cos(deg * kDeg), s = std::sin(deg * kDeg);
    M4 r;
    r.m[0] = c;
    r.m[1] = -s;
    r.m[4] = s;
    r.m[5] = c;
    return r;
}
inline mc::Vec3 apply(const M4& a, const mc::Vec3& v) {
    return {a.m[0] * v.x + a.m[1] * v.y + a.m[2] * v.z + a.m[3], a.m[4] * v.x + a.m[5] * v.y + a.m[6] * v.z + a.m[7],
            a.m[8] * v.x + a.m[9] * v.y + a.m[10] * v.z + a.m[11]};
}

// The right-hand item pose before the item's own display transform (ItemInHandRenderer.renderArmWithItem, right arm).
// `swing` 0..1 (the attack animation), `equipped` 0..1 (1 = fully lowered out of view; this is 1 - mainHandHeight).
inline M4 itemPose(float swing, float equipped) {
    const float sq = std::sqrt(swing);
    // translate(l * f, f6, f10), l = 1 for the right hand
    M4 p = translate(-0.4f * std::sin(sq * kPi), 0.2f * std::sin(sq * 2.f * kPi), -0.2f * std::sin(swing * kPi));
    // applyItemArmTransform
    p = mul(p, translate(0.56f, -0.52f + equipped * -0.6f, -0.72f));
    // applyItemArmAttackTransform
    const float f = std::sin(swing * swing * kPi);
    const float f1 = std::sin(sq * kPi);
    p = mul(p, rotY(45.f + f * -20.f));
    p = mul(p, rotZ(f1 * -20.f));
    p = mul(p, rotX(f1 * -80.f));
    p = mul(p, rotY(-45.f));
    return p;
}

// Eating: the item rises towards the mouth as the meal is finished (applyEatTransform + applyItemArmTransform).
// `eaten` 0..1 is the progress of the meal, `use_ticks` its length in ticks (32).
inline M4 eatPose(float eaten, float equipped, float use_ticks = 32.f) {
    const float remaining = std::clamp(1.f - eaten, 0.f, 1.f); // f1 in the original: remaining / duration
    const float f = remaining * use_ticks;
    M4 p;
    if (remaining < 0.8f) p = mul(p, translate(0.f, std::fabs(std::cos(f / 4.f * kPi) * 0.1f), 0.f));
    const float f3 = 1.f - std::pow(remaining, 27.f);
    p = mul(p, translate(f3 * 0.6f, f3 * -0.5f, 0.f));
    p = mul(p, rotY(f3 * 90.f));
    p = mul(p, rotX(f3 * 10.f));
    p = mul(p, rotZ(f3 * 30.f));
    return mul(p, translate(0.56f, -0.52f + equipped * -0.6f, -0.72f));
}

// The bare right arm (ItemInHandRenderer.renderPlayerArm): the pose the arm model is drawn in.
inline M4 bareArmPose(float swing, float equipped) {
    const float sq = std::sqrt(swing);
    M4 p = translate(-0.3f * std::sin(sq * kPi) + 0.64000005f, 0.4f * std::sin(sq * 2.f * kPi) + -0.6f + equipped * -0.6f,
                     -0.4f * std::sin(swing * kPi) + -0.71999997f);
    p = mul(p, rotY(45.f));
    const float f5 = std::sin(swing * swing * kPi), f6 = std::sin(sq * kPi);
    p = mul(p, rotY(f6 * 70.f));
    p = mul(p, rotZ(f5 * -20.f));
    p = mul(p, translate(-1.f, 3.6f, 3.5f));
    p = mul(p, rotZ(120.f));
    p = mul(p, rotX(200.f));
    p = mul(p, rotY(-135.f));
    p = mul(p, translate(5.6f, 0.f, 0.f));
    return p;
}

// The item's model display transform in the first-person right hand (item/generated and item/handheld share it): applied to a
// model that is centred on the origin, 1 block = the sprite's width.
inline M4 itemDisplay() {
    M4 p = translate(1.13f / 16.f, 3.2f / 16.f, 1.13f / 16.f);
    p = mul(p, mul(rotX(0.f), mul(rotY(-90.f), rotZ(25.f)))); // rotationXYZ(0, -90, 25) = Rx * Ry * Rz
    return mul(p, scale(0.68f));
}

// View bobbing while walking (GameRenderer.bobView). `walk` is the walked distance in Minecraft's units, `bob` the amplitude
// (0 standing .. 0.1 walking).
inline M4 walkBob(float walk, float bob) {
    const float f1 = -walk;
    M4 p = translate(std::sin(f1 * kPi) * bob * 0.5f, -std::fabs(std::cos(f1 * kPi) * bob), 0.f);
    p = mul(p, rotZ(std::sin(f1 * kPi) * bob * 3.f));
    return mul(p, rotX(std::fabs(std::cos(f1 * kPi - 0.2f) * bob) * 5.f));
}

// The hand lags a little behind the view when it turns (ItemInHandRenderer.renderHandsWithItems), degrees.
inline M4 handSway(float pitch_minus_bob_deg, float yaw_minus_bob_deg) {
    return mul(rotX(pitch_minus_bob_deg * 0.1f), rotY(yaw_minus_bob_deg * 0.1f));
}

// Camera space (Minecraft: right handed, -Z forward) to the renderer's row-vector, left-handed (+Z forward) matrix.
inline mc::rig::Mat4 toHost(const M4& pose) {
    mc::rig::Mat4 r;
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            float v = pose.m[static_cast<size_t>(col * 4 + row)]; // transpose: column vectors -> row vectors
            if (col == 2) v = -v;                                 // the z mirror, applied to the result of the pose
            r.m[static_cast<size_t>(row * 4 + col)] = v;
        }
    }
    return r;
}

// The raised/lowered hand: Minecraft keeps `mainHandHeight` (0 = down, 1 = up), moving towards (cooldown^3 for the item that
// is shown, 0 for a different one) by at most 0.4 per tick; the shown item changes when the hand is nearly down.
class HandAnimator {
public:
    void tick(float dt, mc::ItemId wanted, float attack_cooldown) {
        const float target = (wanted == shown_) ? attack_cooldown * attack_cooldown * attack_cooldown : 0.f;
        const float step = std::clamp(target - height_, -0.4f, 0.4f) * std::min(1.f, dt * 20.f);
        height_ = std::clamp(height_ + step, 0.f, 1.f);
        if (wanted != shown_ && height_ < 0.1f) shown_ = wanted;
    }
    [[nodiscard]] mc::ItemId shownItem() const { return shown_; }
    [[nodiscard]] float equipped() const { return 1.f - height_; } // what the pose functions take
    [[nodiscard]] float height() const { return height_; }

private:
    mc::ItemId shown_{mc::ItemId::DiamondSword};
    float height_{1.f};
};

// Sways the hand behind the view: bob follows the angle with half the gap closed per tick.
class SwayFilter {
public:
    // angles in degrees; returns (pitch - bob, yaw - bob)
    void update(float dt, float pitch_deg, float yaw_deg, float& d_pitch, float& d_yaw) {
        if (!seeded_) {
            pitch_bob_ = pitch_deg;
            yaw_bob_ = yaw_deg;
            seeded_ = true;
        }
        const float k = 1.f - std::pow(0.5f, dt * 20.f); // half per tick
        pitch_bob_ += (pitch_deg - pitch_bob_) * k;
        float dy = yaw_deg - yaw_bob_;
        while (dy > 180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;
        yaw_bob_ += dy * k;
        d_pitch = pitch_deg - pitch_bob_;
        float out_yaw = yaw_deg - yaw_bob_;
        while (out_yaw > 180.f) out_yaw -= 360.f;
        while (out_yaw < -180.f) out_yaw += 360.f;
        d_yaw = out_yaw;
    }

private:
    bool seeded_{false};
    float pitch_bob_{0.f}, yaw_bob_{0.f};
};

} // namespace eldenring::fp
