#include <gtest/gtest.h>

#include "eldenring_bow.hpp"

#include <cmath>
#include <cstring>
#include <vector>

using namespace eldenring::bow;

namespace {
// Runs the controller for `seconds` with the button held and the bow usable.
Update hold(BowController& c, float seconds, float step = 0.05f) {
    Update last;
    for (float t = 0.f; t < seconds - 1e-4f; t += step) last = c.update(step, true, true);
    return last;
}
} // namespace

TEST(EldenRingBow, MinecraftPowerCurveFromTheTicksOfTheDraw) {
    EXPECT_NEAR(powerForTicks(0), 0.f, 1e-6f);
    EXPECT_NEAR(powerForTicks(10), (0.25f + 1.f) / 3.f, 1e-5f); // f = 0.5: (f*f + f*2) / 3
    EXPECT_NEAR(powerForTicks(20), 1.f, 1e-6f);
    EXPECT_NEAR(powerForTicks(60), 1.f, 1e-6f); // capped: a longer draw is no stronger
    EXPECT_GT(powerForTicks(15), powerForTicks(10));
}

TEST(EldenRingBow, ADrawBelowTenPercentPowerDoesNotFire) {
    EXPECT_LT(powerForTicks(2), kMinPower);
    EXPECT_GE(powerForTicks(3), kMinPower); // Minecraft: three ticks is the shortest shot
}

TEST(EldenRingBow, DamageIsSixAtFullDrawPlusTheAverageCritAndScalesWithPower) {
    EXPECT_NEAR(damageForPower(1.f), 9.f, 1e-5f);        // 6 + 3: a full draw is a critical arrow
    EXPECT_NEAR(damageForPower(0.5f), 3.f, 1e-5f);       // 6 * power, no crit
    EXPECT_NEAR(damageForPower(0.1f), 0.6f, 1e-5f);
    EXPECT_LT(damageForPower(0.99f), 6.f + 1e-4f);
}

TEST(EldenRingBow, PressStartsTheDrawAndReleaseFiresWithThePowerOfTheTimeHeld) {
    BowController c;
    Update u = c.update(0.05f, true, true);
    EXPECT_TRUE(u.started);
    EXPECT_TRUE(c.drawing());
    u = hold(c, 1.0f); // 20 ticks more
    EXPECT_FALSE(u.released);
    EXPECT_NEAR(c.power(), 1.f, 1e-3f);
    u = c.update(0.05f, false, true);
    EXPECT_TRUE(u.released);
    EXPECT_NEAR(u.power, 1.f, 1e-3f);
    EXPECT_FALSE(c.drawing());
}

TEST(EldenRingBow, ATapIsCancelledNotFired) {
    BowController c;
    c.update(0.05f, true, true);              // 1 tick
    const Update u = c.update(0.05f, false, true); // 2 ticks in total: below the shortest shot
    EXPECT_FALSE(u.released);
    EXPECT_TRUE(u.cancelled);
    EXPECT_FALSE(c.drawing());
}

TEST(EldenRingBow, HoldingFromBeforeTheBowWasUsableDoesNotStartADraw) {
    BowController c;
    c.update(0.05f, true, false);  // the button is already down but the bow is not in hand / no arrows
    const Update u = c.update(0.05f, true, true);
    EXPECT_FALSE(u.started);       // only a fresh press starts it
    EXPECT_FALSE(c.drawing());
}

TEST(EldenRingBow, LosingTheBowMidDrawCancelsItWithoutAShot) {
    BowController c;
    hold(c, 0.5f);
    ASSERT_TRUE(c.drawing());
    const Update u = c.update(0.05f, true, false); // switched slot, opened the inventory, died...
    EXPECT_TRUE(u.cancelled);
    EXPECT_FALSE(u.released);
    EXPECT_FALSE(c.drawing());
    EXPECT_NEAR(c.power(), 0.f, 1e-6f);
}

TEST(EldenRingBow, AfterAShotTheButtonStillHeldDoesNotDrawAgain) {
    BowController c;
    hold(c, 0.5f);
    c.update(0.05f, false, true);                  // released: shot
    const Update u = c.update(0.05f, false, true); // nothing pressed
    EXPECT_FALSE(u.started);
    EXPECT_FALSE(c.drawing());
    EXPECT_TRUE(c.update(0.05f, true, true).started); // a new press starts a new draw
}

TEST(EldenRingBow, TheDrawnTicksAreExposedForTheFirstPersonPose) {
    BowController c;
    EXPECT_FLOAT_EQ(c.ticks(), 0.f);
    c.update(0.05f, true, true);              // press
    c.update(0.5f, true, true);               // 10 ticks
    EXPECT_NEAR(c.ticks(), 10.f, 1e-4f);
    c.update(0.05f, false, true);             // release: reset
    EXPECT_FLOAT_EQ(c.ticks(), 0.f);
}

TEST(EldenRingBowAim, TheForwardOfAQuaternionIsRotatedZ) {
    const float identity[4] = {0.f, 0.f, 0.f, 1.f};
    float f[3];
    quatForward(identity, f);
    EXPECT_NEAR(f[0], 0.f, 1e-6f);
    EXPECT_NEAR(f[1], 0.f, 1e-6f);
    EXPECT_NEAR(f[2], 1.f, 1e-6f);
    // 90 degrees about +Y turns +z towards +x
    const float s = std::sqrt(0.5f);
    const float turn[4] = {0.f, s, 0.f, s};
    quatForward(turn, f);
    EXPECT_NEAR(f[0], 1.f, 1e-5f);
    EXPECT_NEAR(f[2], 0.f, 1e-5f);
    // the measured bolt (REVERSE 35.8): q = (-0.113, 0.447, 0.057, 0.885) flies along (0.778, 0.251, 0.575)
    const float bolt[4] = {-0.113f, 0.447f, 0.057f, 0.885f};
    quatForward(bolt, f);
    EXPECT_NEAR(f[0], 0.778f, 2e-3f);
    EXPECT_NEAR(f[1], 0.251f, 2e-3f);
    EXPECT_NEAR(f[2], 0.575f, 2e-3f);
}

TEST(EldenRingBowAim, TheErrorIsTheYawAndPitchTheFlightIsOffTheAim) {
    const float aim[3] = {0.f, 0.f, 1.f};
    const float flight[3] = {std::sin(10.f * 3.14159265f / 180.f), std::sin(5.f * 3.14159265f / 180.f), 1.f};
    const AimError e = aimError(aim, flight);
    EXPECT_NEAR(e.yaw_deg, 10.f, 0.3f);    // to the +x side
    EXPECT_NEAR(e.pitch_deg, 5.f, 0.3f);   // upwards
    const AimError none = aimError(aim, aim);
    EXPECT_NEAR(none.yaw_deg, 0.f, 1e-4f);
    EXPECT_NEAR(none.pitch_deg, 0.f, 1e-4f);
}

TEST(EldenRingBowAim, TheYawErrorWrapsAroundTheBackOfTheCircle) {
    const float aim[3] = {-0.05f, 0.f, -1.f};   // pointing almost -z (yaw about 177 or -177 degrees)
    const float flight[3] = {0.05f, 0.f, -1.f};
    EXPECT_LT(std::fabs(aimError(aim, flight).yaw_deg), 10.f);
}

TEST(EldenRingBowAim, TheBasisFromAForwardMatchesTheMatrixOfARealShot) {
    // A real request (REVERSE 35.1): right (0.857, -0.001, -0.516), up (0.001, 1.0, 0.0), forward (0.516, -0.001, 0.857).
    const float forward[3] = {0.516f, 0.f, 0.857f};
    float right[3], up[3];
    buildBasis(forward, right, up);
    EXPECT_NEAR(right[0], 0.857f, 2e-3f);
    EXPECT_NEAR(right[1], 0.f, 1e-5f);
    EXPECT_NEAR(right[2], -0.516f, 2e-3f);
    EXPECT_NEAR(up[0], 0.f, 1e-4f);
    EXPECT_NEAR(up[1], 1.f, 1e-4f);
    EXPECT_NEAR(up[2], 0.f, 1e-4f);
}

TEST(EldenRingBowAim, TheBasisIsOrthonormalAndProperEvenWhenLookingUpAndDown) {
    for (float pitch : {-80.f, -30.f, 0.f, 45.f, 85.f}) {
        const float p = pitch * 3.14159265f / 180.f;
        const float forward[3] = {0.6f * std::cos(p), std::sin(p), 0.8f * std::cos(p)};
        float right[3], up[3];
        buildBasis(forward, right, up);
        auto dot = [](const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; };
        EXPECT_NEAR(dot(right, right), 1.f, 1e-5f);
        EXPECT_NEAR(dot(up, up), 1.f, 1e-5f);
        EXPECT_NEAR(dot(right, up), 0.f, 1e-5f);
        EXPECT_NEAR(dot(right, forward), 0.f, 1e-5f);
        EXPECT_NEAR(dot(up, forward), 0.f, 1e-5f);
        EXPECT_NEAR(determinant(right, up, forward), 1.f, 1e-4f);   // proper: right x up = forward
        EXPECT_GT(up[1], 0.f);                                      // never upside down
    }
}

TEST(EldenRingBowAim, ALookStraightUpOrDownStillGivesAUsableBasis) {
    const float forward[3] = {0.f, 1.f, 0.f};
    float right[3], up[3];
    buildBasis(forward, right, up);
    EXPECT_TRUE(std::isfinite(right[0]) && std::isfinite(right[2]) && std::isfinite(up[1]));
    EXPECT_NEAR(std::sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]), 1.f, 1e-4f);
}

TEST(EldenRingBowAim, AMirroredBasisHasADeterminantOfMinusOne) {
    const float right[3] = {-1.f, 0.f, 0.f}, up[3] = {0.f, 1.f, 0.f}, forward[3] = {0.f, 0.f, 1.f};
    EXPECT_NEAR(determinant(right, up, forward), -1.f, 1e-6f);
    const float right2[3] = {1.f, 0.f, 0.f};
    EXPECT_NEAR(determinant(right2, up, forward), 1.f, 1e-6f);
}

TEST(EldenRingBow, TheDrawnSpriteStageFollowsMinecraftsThresholds) {
    EXPECT_EQ(pullingStage(0.f), -1);      // not drawing: the plain bow
    EXPECT_EQ(pullingStage(0.05f), 0);     // pull > 0: bow_pulling_0
    EXPECT_EQ(pullingStage(0.64f), 0);
    EXPECT_EQ(pullingStage(0.65f), 1);     // pull >= 0.65: bow_pulling_1
    EXPECT_EQ(pullingStage(0.89f), 1);
    EXPECT_EQ(pullingStage(0.9f), 2);      // pull >= 0.9: bow_pulling_2
    EXPECT_EQ(pullingStage(1.f), 2);
}

TEST(EldenRingBow, TheBowInHandShowsTheDrawnSpriteOnlyWhileItIsBeingDrawn) {
    EXPECT_EQ(shownBow(mc::ItemId::Bow, 0.f), mc::ItemId::Bow);
    EXPECT_EQ(shownBow(mc::ItemId::Bow, 0.3f), mc::ItemId::BowPulling0);
    EXPECT_EQ(shownBow(mc::ItemId::Bow, 0.7f), mc::ItemId::BowPulling1);
    EXPECT_EQ(shownBow(mc::ItemId::Bow, 1.f), mc::ItemId::BowPulling2);
    EXPECT_EQ(shownBow(mc::ItemId::DiamondSword, 1.f), mc::ItemId::DiamondSword);   // another item is never swapped
    EXPECT_EQ(shownBow(mc::ItemId::None, 0.5f), mc::ItemId::None);
}
