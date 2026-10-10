#include <gtest/gtest.h>

#include "eldenring_fp.hpp"

using namespace eldenring::fp;

namespace {
mc::Vec3 origin() { return {0.f, 0.f, 0.f}; }
}

TEST(EldenRingFp, MatrixPrimitivesFollowTheRightHandedConvention) {
    const mc::Vec3 x{1.f, 0.f, 0.f};
    const mc::Vec3 ry = apply(rotY(90.f), x);
    EXPECT_NEAR(ry.x, 0.f, 1e-5f);
    EXPECT_NEAR(ry.z, -1.f, 1e-5f); // +90 degrees about Y turns +X towards -Z
    const mc::Vec3 rz = apply(rotZ(90.f), x);
    EXPECT_NEAR(rz.y, 1.f, 1e-5f);  // +X towards +Y
    const mc::Vec3 rx = apply(rotX(90.f), mc::Vec3{0.f, 1.f, 0.f});
    EXPECT_NEAR(rx.z, 1.f, 1e-5f);  // +Y towards +Z
    const mc::Vec3 t = apply(mul(translate(1.f, 2.f, 3.f), scale(2.f)), mc::Vec3{1.f, 1.f, 1.f});
    EXPECT_NEAR(t.x, 3.f, 1e-5f);   // scaled first, then moved (the pose stack multiplies on the right)
    EXPECT_NEAR(t.y, 4.f, 1e-5f);
    EXPECT_NEAR(t.z, 5.f, 1e-5f);
}

TEST(EldenRingFp, IdleItemSitsLowerRightInFrontOfTheCamera) {
    const mc::Vec3 p = apply(itemPose(0.f, 0.f), origin());
    EXPECT_NEAR(p.x, 0.56f, 1e-4f);
    EXPECT_NEAR(p.y, -0.52f, 1e-4f);
    EXPECT_NEAR(p.z, -0.72f, 1e-4f);
}

TEST(EldenRingFp, LoweredHandMovesTheItemDownOutOfView) {
    const mc::Vec3 up = apply(itemPose(0.f, 0.f), origin());
    const mc::Vec3 down = apply(itemPose(0.f, 1.f), origin());
    EXPECT_NEAR(up.y - down.y, 0.6f, 1e-4f);
}

TEST(EldenRingFp, SwingSweepsTheItemAcrossTheView) {
    const mc::Vec3 mid = apply(itemPose(0.5f, 0.f), origin());
    EXPECT_LT(mid.x, 0.56f - 0.2f); // moves towards the middle of the screen
    EXPECT_LT(mid.y, -0.52f);       // and dips (0.2 * sin(2 * pi * sqrt(t)) is negative mid swing)
    EXPECT_LT(mid.z, -0.72f);       // and away
    const mc::Vec3 end = apply(itemPose(1.f, 0.f), origin());
    EXPECT_NEAR(end.x, 0.56f, 1e-3f); // the swing returns to the rest pose
    EXPECT_NEAR(end.y, -0.52f, 1e-3f);
    EXPECT_NEAR(end.z, -0.72f, 1e-3f);
}

TEST(EldenRingFp, TheBareArmEndsUpLowerRightInFront) {
    // The arm model's pivot (-5, 2, 0) / 16 and the middle of the cube below it (-1, 4, 0) / 16 more, in the model's y-down space.
    const mc::Vec3 shoulder = apply(bareArmPose(0.f, 0.f), mc::Vec3{-5.f / 16.f, 2.f / 16.f, 0.f});
    const mc::Vec3 hand = apply(bareArmPose(0.f, 0.f), mc::Vec3{-5.f / 16.f - 0.25f / 16.f * 0.f, 2.f / 16.f + 12.f / 16.f, 0.f});
    EXPECT_GT(shoulder.x, 0.1f);
    EXPECT_LT(shoulder.y, 0.f);
    EXPECT_LT(shoulder.z, -0.2f);
    EXPECT_GT(hand.x, 0.f);
    EXPECT_LT(hand.z, -0.2f);
    EXPECT_GT(shoulder.z, -3.f);
    EXPECT_GT(hand.z, -3.f);
}

TEST(EldenRingFp, EatingBringsTheItemUpToTheMouth) {
    const mc::Vec3 start = apply(eatPose(0.f, 0.f), origin());
    const mc::Vec3 done = apply(eatPose(0.99f, 0.f), origin());
    EXPECT_NEAR(start.x, 0.56f, 1e-3f);
    EXPECT_LT(done.x, start.x);      // towards the middle
    EXPECT_GT(done.y, start.y - 0.5f);
}

TEST(EldenRingFp, TheItemDisplayTransformScalesAndOffsetsTheModel) {
    const mc::Vec3 c = apply(itemDisplay(), origin());
    EXPECT_NEAR(c.x, 1.13f / 16.f, 1e-5f);
    EXPECT_NEAR(c.y, 3.2f / 16.f, 1e-5f);
    const mc::Vec3 edge = apply(itemDisplay(), mc::Vec3{0.5f, 0.f, 0.f});
    const mc::Vec3 d{edge.x - c.x, edge.y - c.y, edge.z - c.z};
    EXPECT_NEAR(std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z), 0.5f * 0.68f, 1e-4f);
}

TEST(EldenRingFp, BlockDisplayScalesBy04AndTurnsTheCubeBy45Degrees) {
    const mc::Vec3 c = apply(blockDisplay(), origin());
    EXPECT_NEAR(c.x, 0.f, 1e-6f); // block/block display has no translation
    EXPECT_NEAR(c.y, 0.f, 1e-6f);
    const mc::Vec3 edge = apply(blockDisplay(), mc::Vec3{0.5f, 0.f, 0.f});
    EXPECT_NEAR(std::sqrt(edge.x * edge.x + edge.y * edge.y + edge.z * edge.z), 0.5f * 0.4f, 1e-5f);
    // a quarter turn about Y would send +x to +/-z; 45 degrees splits it evenly
    EXPECT_NEAR(std::fabs(edge.x), std::fabs(edge.z), 1e-5f);
    EXPECT_NEAR(edge.y, 0.f, 1e-6f);
}

TEST(EldenRingFp, HostMatrixMirrorsZAndUsesRowVectors) {
    // a point 1 in front of the camera in Minecraft space (-Z) is +1 in front in the host's left-handed space
    const mc::rig::Mat4 m = toHost(translate(0.f, 0.f, -1.f));
    // row vector [0 0 0 1] * M -> the translation row
    EXPECT_NEAR(m.m[12], 0.f, 1e-6f);
    EXPECT_NEAR(m.m[13], 0.f, 1e-6f);
    EXPECT_NEAR(m.m[14], 1.f, 1e-6f);
    const mc::rig::Mat4 r = toHost(translate(0.56f, -0.52f, -0.72f));
    EXPECT_NEAR(r.m[12], 0.56f, 1e-6f);
    EXPECT_NEAR(r.m[13], -0.52f, 1e-6f);
    EXPECT_NEAR(r.m[14], 0.72f, 1e-6f);
}

TEST(EldenRingFp, WalkBobIsZeroWhenStanding) {
    const mc::Vec3 p = apply(walkBob(3.7f, 0.f), mc::Vec3{0.2f, 0.3f, -1.f});
    EXPECT_NEAR(p.x, 0.2f, 1e-5f);
    EXPECT_NEAR(p.y, 0.3f, 1e-5f);
    EXPECT_NEAR(p.z, -1.f, 1e-5f);
    const mc::Vec3 moving = apply(walkBob(0.5f, 0.1f), origin());
    EXPECT_GT(std::fabs(moving.x) + std::fabs(moving.y), 0.01f);
}

TEST(EldenRingFp, TheHandLowersOutAndTheNewItemRisesWhenYouSwitch) {
    HandAnimator h;
    for (int i = 0; i < 40; ++i) h.tick(0.05f, mc::ItemId::DiamondSword, 1.f);
    EXPECT_EQ(h.shownItem(), mc::ItemId::DiamondSword);
    EXPECT_NEAR(h.height(), 1.f, 1e-3f);
    h.tick(0.05f, mc::ItemId::DiamondPickaxe, 1.f);
    EXPECT_EQ(h.shownItem(), mc::ItemId::DiamondSword); // still the old one while it goes down
    EXPECT_LT(h.height(), 1.f);
    bool switched = false;
    for (int i = 0; i < 40 && !switched; ++i) {
        h.tick(0.05f, mc::ItemId::DiamondPickaxe, 1.f);
        switched = h.shownItem() == mc::ItemId::DiamondPickaxe;
    }
    EXPECT_TRUE(switched);
    for (int i = 0; i < 40; ++i) h.tick(0.05f, mc::ItemId::DiamondPickaxe, 1.f);
    EXPECT_NEAR(h.height(), 1.f, 1e-2f);                // back up
}

TEST(EldenRingFp, ASwingDipsTheHandUntilTheAttackIsCharged) {
    HandAnimator h;
    for (int i = 0; i < 40; ++i) h.tick(0.05f, mc::ItemId::DiamondSword, 1.f);
    for (int i = 0; i < 4; ++i) h.tick(0.05f, mc::ItemId::DiamondSword, 0.1f); // just attacked: the cooldown is nearly empty
    EXPECT_LT(h.height(), 0.5f);
    for (int i = 0; i < 40; ++i) h.tick(0.05f, mc::ItemId::DiamondSword, 1.f);
    EXPECT_NEAR(h.height(), 1.f, 1e-2f);
}

TEST(EldenRingFp, SwayFollowsTheViewWithALag) {
    SwayFilter s;
    float dp = 0.f, dy = 0.f;
    s.update(0.05f, 0.f, 0.f, dp, dy);
    EXPECT_FLOAT_EQ(dp, 0.f);
    s.update(0.05f, 10.f, 30.f, dp, dy); // the view turned: the hand is behind it
    EXPECT_GT(dp, 0.f);
    EXPECT_GT(dy, 0.f);
    for (int i = 0; i < 40; ++i) s.update(0.05f, 10.f, 30.f, dp, dy);
    EXPECT_NEAR(dp, 0.f, 0.05f);                        // it catches up
    s.update(0.05f, 0.f, 359.f, dp, dy);                // yaw wraps: 30 -> 359 is a small turn the other way... not a jump of 329
    EXPECT_LT(std::fabs(dy), 180.f);
}

TEST(EldenRingFp, TheBowRestsLowerAndFurtherInTowardsTheMiddleThanTheSwordAndPullsBackWhileDrawn) {
    const mc::Vec3 rest = apply(bowPose(0.f, 0.f), origin());
    // translate(0.56, -0.52, -0.72) then translate(-0.2785682, 0.18344387, 0.15731531), both applied to the origin
    EXPECT_NEAR(rest.x, 0.56f - 0.2785682f, 1e-4f);
    EXPECT_NEAR(rest.y, -0.52f + 0.18344387f, 1e-4f);
    EXPECT_NEAR(rest.z, -0.72f + 0.15731531f, 1e-4f);
    // a full draw pulls the bow back 0.04 along its own (rotated) z: a small, but measurable, move
    const mc::Vec3 full = apply(bowPose(20.f, 0.f), origin());
    const float d = std::sqrt((full.x - rest.x) * (full.x - rest.x) + (full.y - rest.y) * (full.y - rest.y) + (full.z - rest.z) * (full.z - rest.z));
    EXPECT_GT(d, 0.03f);
    EXPECT_LT(d, 0.06f);
}

TEST(EldenRingFp, TheBowStretchesAlongItsDepthAsItIsDrawn) {
    // The stretch (scale 1, 1, 1 + 0.2 * power) is applied before the last turn of 45 degrees about Y, so the axis it stretches is the local one that this
    // turn maps onto z: (sin 45, 0, cos 45) in the item's own coordinates (this code's rotY turns +z towards +x).
    const float s45 = std::sin(45.f * kDeg), c45 = std::cos(45.f * kDeg);
    auto depth = [&](float ticks) {
        const M4 p = bowPose(ticks, 0.f);
        const mc::Vec3 a = apply(p, origin()), b = apply(p, mc::Vec3{s45, 0.f, c45});
        return std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y) + (b.z - a.z) * (b.z - a.z));
    };
    EXPECT_NEAR(depth(0.f), 1.f, 1e-4f);
    EXPECT_NEAR(depth(20.f), 1.2f, 1e-3f);   // power 1: 1 + 0.2
    EXPECT_NEAR(depth(60.f), 1.2f, 1e-3f);   // capped like the power
    EXPECT_GT(depth(10.f), 1.f);
    EXPECT_LT(depth(10.f), depth(20.f));
}

TEST(EldenRingFp, TheBowFollowsTheHandWhenItIsLowered) {
    const mc::Vec3 up = apply(bowPose(5.f, 0.f), origin());
    const mc::Vec3 down = apply(bowPose(5.f, 1.f), origin());
    EXPECT_NEAR(up.y - down.y, 0.6f, 1e-4f);
}
