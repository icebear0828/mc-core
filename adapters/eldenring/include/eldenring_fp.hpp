#pragma once

// First-person hand and held item, following Minecraft's ItemInHandRenderer: the pose of the item (or the bare arm) for
// the current swing, the equip/lower animation, eating, view sway and walking bob. Everything is Minecraft camera space
// (right handed, +X right, +Y up, -Z forward, blocks) with column vectors and a pose stack that multiplies on the right,
// exactly as the original does. toHost() converts a pose to the row-vector, left-handed (+Z forward) matrix the D3D12
// renderer wants. Pure logic, unit-tested.

#include "mc/hand_items.hpp"
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

// The same swing for the left arm (ItemInHandRenderer, l = -1): the x translation and the y/z
// rotations are mirrored, the x rotation is not.
inline M4 itemPoseLeft(float swing, float equipped) {
    const float sq = std::sqrt(swing);
    M4 p = translate(0.4f * std::sin(sq * kPi), 0.2f * std::sin(sq * 2.f * kPi), -0.2f * std::sin(swing * kPi));
    p = mul(p, translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f));
    const float f = std::sin(swing * swing * kPi);
    const float f1 = std::sin(sq * kPi);
    p = mul(p, rotY(-45.f + f * 20.f));
    p = mul(p, rotZ(f1 * 20.f));
    p = mul(p, rotX(f1 * -80.f));
    p = mul(p, rotY(45.f));
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

// The offhand eat: the same curve with the x offsets and the y/z turns mirrored.
inline M4 eatPoseLeft(float eaten, float equipped, float use_ticks = 32.f) {
    const float remaining = std::clamp(1.f - eaten, 0.f, 1.f);
    const float f = remaining * use_ticks;
    M4 p;
    if (remaining < 0.8f) p = mul(p, translate(0.f, std::fabs(std::cos(f / 4.f * kPi) * 0.1f), 0.f));
    const float f3 = 1.f - std::pow(remaining, 27.f);
    p = mul(p, translate(-f3 * 0.6f, f3 * -0.5f, 0.f));
    p = mul(p, rotY(-f3 * 90.f));
    p = mul(p, rotX(f3 * 10.f));
    p = mul(p, rotZ(-f3 * 30.f));
    return mul(p, translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f));
}

// Drawing the bow (ItemInHandRenderer.renderArmWithItem, UseAnim.BOW, right hand): the arm transform, then the bow-specific turn and offset, then the pull
// of the draw. `ticks` is how many ticks the bow has been drawn; the power curve is the same as the arrow's ((f*f + f*2) / 3, capped at 1).
inline M4 bowPose(float ticks, float equipped) {
    M4 p = translate(0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(-0.2785682f, 0.18344387f, 0.15731531f));
    p = mul(p, rotX(-13.935f));
    p = mul(p, rotY(35.3f));
    p = mul(p, rotZ(-9.785f));
    float f12 = std::max(0.f, ticks) / 20.f;
    f12 = std::min(1.f, (f12 * f12 + f12 * 2.f) / 3.f);
    if (f12 > 0.1f) {
        const float f15 = std::sin((ticks - 0.1f) * 1.3f);
        const float f20 = f15 * (f12 - 0.1f);
        p = mul(p, translate(f20 * 0.f, f20 * 0.004f, f20 * 0.f)); // the small shake once the draw has some power
    }
    p = mul(p, translate(f12 * 0.f, f12 * 0.f, f12 * 0.04f));
    M4 stretch; // scale(1, 1, 1 + f12 * 0.2)
    stretch.m[10] = 1.f + f12 * 0.2f;
    p = mul(p, stretch);
    return mul(p, rotY(-45.f));
}

// The offhand (left hand) bow draw: the arm transform and the bow-specific offsets/turns mirror x, y and z.
inline M4 bowPoseLeft(float ticks, float equipped) {
    M4 p = translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(0.2785682f, 0.18344387f, 0.15731531f));
    p = mul(p, rotX(-13.935f));
    p = mul(p, rotY(-35.3f));
    p = mul(p, rotZ(9.785f));
    float f12 = std::max(0.f, ticks) / 20.f;
    f12 = std::min(1.f, (f12 * f12 + f12 * 2.f) / 3.f);
    if (f12 > 0.1f) {
        const float f15 = std::sin((ticks - 0.1f) * 1.3f);
        const float f20 = f15 * (f12 - 0.1f);
        p = mul(p, translate(0.f, f20 * 0.004f, 0.f));
    }
    p = mul(p, translate(0.f, 0.f, f12 * 0.04f));
    M4 stretch;
    stretch.m[10] = 1.f + f12 * 0.2f;
    p = mul(p, stretch);
    return mul(p, rotY(45.f));
}

// The trident's first-person display: models/item/trident_in_hand.json firstperson_righthand (translation (-3, 17,
// 1)/16, rotation (0, -90, 25), scale 1), then the special renderer's scale(1, -1, -1) and translate(-0.5, -0.5, -0.5).
// The prop geometry is Bedrock (x runs the other way, Java y is flipped to geo y = 27.5 - java_y, and the boxes are
// shifted 6 px along x), so the mapping from kFirstPersonItemBasis to the item space is translate(-1/8, -71/32, -1/2)
// with x mirrored. The item pose places the grip.
inline M4 tridentFpDisplay() {
    M4 p = translate(-3.f / 16.f, 17.f / 16.f, 1.f / 16.f);
    p = mul(p, mul(rotX(0.f), mul(rotY(-90.f), rotZ(25.f))));
    p = mul(p, translate(-0.125f, -2.21875f, -0.5f));
    M4 mirror; // the Bedrock model's x axis runs the other way
    mirror.m[0] = -1.f;
    return mul(p, mirror);
}

// The offhand (left hand) variant: trident_in_hand.json firstperson_lefthand is translation (13, 17, 1)/16 and
// rotation (0, 90, -25); ItemTransform.apply's left-hand rule negates the y/z rotation and the x translation, so it
// reduces to the right-hand rotation with x = -13/16. The special-renderer mapping is the same.
inline M4 tridentFpDisplayLeft() {
    M4 p = translate(-13.f / 16.f, 17.f / 16.f, 1.f / 16.f);
    p = mul(p, mul(rotX(0.f), mul(rotY(-90.f), rotZ(25.f))));
    p = mul(p, translate(-0.125f, -2.21875f, -0.5f));
    M4 mirror;
    mirror.m[0] = -1.f;
    return mul(p, mirror);
}

// Trident charge (ItemInHandRenderer.renderArmWithItem, UseAnim.SPEAR, right hand): the spear turn, the 0..1
// charge extension/shake and the final -45 turn. `charge` is ticks/10, clamped to 1.
inline M4 tridentChargePose(float charge, float equipped) {
    M4 p = translate(0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(-0.5f, 0.7f, 0.1f));
    p = mul(p, rotX(-55.f));
    p = mul(p, rotY(35.3f));
    p = mul(p, rotZ(-9.785f));
    const float f = std::clamp(charge, 0.f, 1.f);
    if (f > 0.1f) {
        const float shake = std::sin((f * 10.f - 0.1f) * 1.3f) * (f - 0.1f);
        p = mul(p, translate(0.f, shake * 0.004f, 0.f));
    }
    p = mul(p, translate(0.f, 0.f, f * 0.2f));
    M4 stretch;
    stretch.m[10] = 1.f + f * 0.2f;
    p = mul(p, stretch);
    return mul(p, rotY(-45.f));
}

// Riptide auto-spin (ItemInHandRenderer.renderArmWithItem, isAutoSpinAttack): the trident held out while the
// camera/body spins in vanilla; the pose itself is static.
inline M4 riptidePose(float equipped) {
    M4 p = translate(0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(-0.4f, 0.8f, 0.3f));
    p = mul(p, rotY(65.f));
    return mul(p, rotZ(-85.f));
}

// The offhand mirrors of the two poses above: the arm's x, the offsets' x and the Y/Z turns flip; the X turn and the
// forward stretch stay.
inline M4 tridentChargePoseLeft(float charge, float equipped) {
    M4 p = translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(0.5f, 0.7f, 0.1f));
    p = mul(p, rotX(-55.f));
    p = mul(p, rotY(-35.3f));
    p = mul(p, rotZ(9.785f));
    const float f = std::clamp(charge, 0.f, 1.f);
    if (f > 0.1f) {
        const float shake = std::sin((f * 10.f - 0.1f) * 1.3f) * (f - 0.1f);
        p = mul(p, translate(0.f, shake * 0.004f, 0.f));
    }
    p = mul(p, translate(0.f, 0.f, f * 0.2f));
    M4 stretch;
    stretch.m[10] = 1.f + f * 0.2f;
    p = mul(p, stretch);
    return mul(p, rotY(45.f));
}

inline M4 riptidePoseLeft(float equipped) {
    M4 p = translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f);
    p = mul(p, translate(0.4f, 0.8f, 0.3f));
    p = mul(p, rotY(-65.f));
    return mul(p, rotZ(85.f));
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

// The item's model display transform in the first-person hand (item/generated and item/handheld share it): applied to a
// model that is centred on the origin, 1 block = the sprite's width. The left hand mirrors the y/z rotation and the x
// translation (ItemTransform.apply).
inline M4 itemDisplay(bool main_hand = true) {
    const float tx = (main_hand ? 1.13f : -1.13f) / 16.f;
    const float ry = main_hand ? -90.f : 90.f;
    const float rz = main_hand ? 25.f : -25.f;
    M4 p = translate(tx, 3.2f / 16.f, 1.13f / 16.f);
    p = mul(p, mul(rotX(0.f), mul(rotY(ry), rotZ(rz)))); // rotationXYZ(0, ry, rz) = Rx * Ry * Rz
    return mul(p, scale(0.68f));
}

// The bow's first-person display (models/item/bow.json). Unlike item/generated it defines
// firstperson_lefthand explicitly: [0, 90, -25] / [1.13, 3.2, 1.13]. ItemTransform.apply(leftHand=true)
// then negates the x translation and the y/z rotation, so the applied left-hand transform is the
// right-hand rotation (-90, 25) with x = -1.13/16. itemDisplay(false) would use (+90, -25) and turn
// the offhand bow the wrong way.
inline M4 bowDisplay(bool main_hand = true) {
    const float tx = (main_hand ? 1.13f : -1.13f) / 16.f;
    M4 p = translate(tx, 3.2f / 16.f, 1.13f / 16.f);
    p = mul(p, mul(rotX(0.f), mul(rotY(-90.f), rotZ(25.f))));
    return mul(p, scale(0.68f));
}

// The display transform of the shield in first person: models/item/shield.json (idle) and shield_blocking.json (`blocking`, the use animation),
// firstperson_righthand / firstperson_lefthand (1.21.8), as ItemTransform.apply does it: the left hand negates the y and z rotation and the x
// translation, then translate * rotationXYZ (= Rx * Ry * Rz) * scale. It is applied to the shield model of buildShieldFirstPersonMesh, which
// already includes the shield renderer's scale(1, -1, -1) and the item renderer's translate(-0.5).
inline M4 shieldFpDisplay(bool left_hand, bool blocking) {
    struct J { float rot[3]; float trans[3]; };
    static constexpr J kRight[2] = {{{0.f, 180.f, 5.f}, {-10.f, 2.f, -10.f}}, {{0.f, 180.f, -5.f}, {-15.f, 5.f, -11.f}}};
    static constexpr J kLeft[2] = {{{0.f, 180.f, 5.f}, {10.f, 0.f, -10.f}}, {{0.f, 180.f, -5.f}, {5.f, 5.f, -11.f}}};
    const J& j = (left_hand ? kLeft : kRight)[blocking ? 1 : 0];
    const float ry = left_hand ? -j.rot[1] : j.rot[1], rz = left_hand ? -j.rot[2] : j.rot[2];
    const float side = left_hand ? -1.f : 1.f;
    M4 p = translate(side * j.trans[0] / 16.f, j.trans[1] / 16.f, j.trans[2] / 16.f);
    p = mul(p, mul(rotX(j.rot[0]), mul(rotY(ry), rotZ(rz))));
    return mul(p, scale(1.25f));
}

// The main-hand pose of a shield: raised (use animation BLOCK) it is only the arm transform, idle it follows the swing like any item.
inline M4 shieldMainHandPose(bool blocking, float swing, float equipped) {
    return blocking ? translate(0.56f, -0.52f + equipped * -0.6f, -0.72f) : itemPose(swing, equipped);
}

// The left-hand (offhand) item pose before display transform (ItemInHandRenderer.renderArmWithItem, left hand).
inline M4 offhandItemPose(float equipped) {
    return translate(-0.56f, -0.52f + equipped * -0.6f, -0.72f);
}

// The display transform of a block held in the first-person hand (block/block: rotation (0, 45, 0), no translation, scale 0.4),
// applied to the unit cube centred on the origin. The left hand mirrors the rotation.
inline M4 blockDisplay(bool main_hand = true) { return mul(rotY(main_hand ? 45.f : -45.f), scale(0.4f)); }

// The first-person view of one hand: the arm/item pose, the item's display transform, and what to draw.
// This is the one place the item presentation is chosen; main and offhand share it.
struct HandView {
    M4 pose;
    M4 display;
    mc::ItemId shown{mc::ItemId::None};
    bool draw_3d{false}; // draw the trident model instead of a sprite
};

inline HandView handView(const mc::HandItem& item, bool main_hand, float swing, float equipped, float bow_ticks) {
    HandView view;
    view.shown = item.shown;
    view.draw_3d = item.model == mc::HandModel::Trident;
    switch (item.use) {
        case mc::HandUse::BowDraw:
            view.pose = main_hand ? bowPose(bow_ticks, equipped) : bowPoseLeft(bow_ticks, equipped);
            break;
        case mc::HandUse::Block:
            view.pose = main_hand ? shieldMainHandPose(item.raised, swing, equipped) : offhandItemPose(equipped);
            break;
        case mc::HandUse::TridentCharge:
            view.pose = main_hand ? tridentChargePose(item.progress, equipped) : tridentChargePoseLeft(item.progress, equipped);
            break;
        case mc::HandUse::Riptide:
            view.pose = main_hand ? riptidePose(equipped) : riptidePoseLeft(equipped);
            break;
        case mc::HandUse::Eat:
            view.pose = main_hand ? eatPose(item.progress, equipped) : eatPoseLeft(item.progress, equipped);
            break;
        default:
            // Only the hand that owns the swing follows it; the other one stays at rest.
            if (main_hand) view.pose = itemPose(item.use == mc::HandUse::Swing ? swing : 0.f, equipped);
            else if (item.use == mc::HandUse::Swing) view.pose = itemPoseLeft(swing, equipped);
            else view.pose = offhandItemPose(equipped);
            break;
    }
    switch (item.model) {
        case mc::HandModel::Shield: view.display = shieldFpDisplay(!main_hand, item.raised); break;
        case mc::HandModel::Trident: view.display = main_hand ? tridentFpDisplay() : tridentFpDisplayLeft(); break;
        case mc::HandModel::Block: view.display = blockDisplay(main_hand); break;
        default: view.display = item.item == mc::ItemId::Bow ? bowDisplay(main_hand) : itemDisplay(main_hand); break;
    }
    return view;
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
// Clamped to [-kMaxSwayDeg, kMaxSwayDeg] to prevent viewmodel lag / stutter appearance during rapid mouse turns.
class SwayFilter {
public:
    static constexpr float kMaxSwayDeg = 4.0f;

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
        d_pitch = std::clamp(pitch_deg - pitch_bob_, -kMaxSwayDeg, kMaxSwayDeg);
        float out_yaw = yaw_deg - yaw_bob_;
        while (out_yaw > 180.f) out_yaw -= 360.f;
        while (out_yaw < -180.f) out_yaw += 360.f;
        d_yaw = std::clamp(out_yaw, -kMaxSwayDeg, kMaxSwayDeg);
    }

private:
    bool seeded_{false};
    float pitch_bob_{0.f}, yaw_bob_{0.f};
};

} // namespace eldenring::fp
