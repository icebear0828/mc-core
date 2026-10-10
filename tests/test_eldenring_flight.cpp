#include <gtest/gtest.h>

#include "eldenring_flight.hpp"
#include "eldenring_world.hpp"

#include <algorithm>
#include <cmath>

using namespace eldenring::flight;

namespace {
Heading northHeading() {
    Heading h; // forward +z, right +x
    const float f[3] = {0.f, 0.f, 1.f}, r[3] = {1.f, 0.f, 0.f};
    EXPECT_TRUE(headingFromCamera(f, r, h));
    return h;
}
} // namespace

TEST(EldenRingFlight, DoubleTapNeedsTwoPressesInTime) {
    DoubleTap t;
    EXPECT_FALSE(t.update(true, 1000));  // first press
    EXPECT_FALSE(t.update(false, 1050)); // release
    EXPECT_TRUE(t.update(true, 1200));   // second press 200 ms after the first
    EXPECT_FALSE(t.update(false, 1250));
    EXPECT_FALSE(t.update(true, 1300));  // a third press begins a new pair, it does not toggle again
}

TEST(EldenRingFlight, DoubleTapIgnoresSlowPressesAndHeldKeys) {
    DoubleTap t;
    EXPECT_FALSE(t.update(true, 1000));
    EXPECT_FALSE(t.update(false, 1100));
    EXPECT_FALSE(t.update(true, 1000 + kDoubleTapMs + 1)); // too slow: only a new first press
    DoubleTap held;
    EXPECT_FALSE(held.update(true, 0));
    EXPECT_FALSE(held.update(true, 50)); // holding the key is not a second press
    EXPECT_FALSE(held.update(true, 100));
}

TEST(EldenRingFlight, DoubleTapResetForgetsThePair) {
    DoubleTap t;
    t.update(true, 1000);
    t.update(false, 1010);
    t.reset();
    EXPECT_FALSE(t.update(true, 1100));
}

TEST(EldenRingFlight, HeadingIgnoresPitchAndRejectsStraightUpDown) {
    Heading h;
    const float up_look[3] = {0.f, 0.9f, 0.436f}, right[3] = {1.f, 0.f, 0.f}; // looking steeply up, still facing +z
    ASSERT_TRUE(headingFromCamera(up_look, right, h));
    EXPECT_NEAR(h.fwd[0], 0.f, 1e-5f);
    EXPECT_NEAR(h.fwd[1], 1.f, 1e-5f);
    const float straight_up[3] = {0.f, 1.f, 0.f};
    EXPECT_FALSE(headingFromCamera(straight_up, right, h));
}

TEST(EldenRingFlight, ForwardMovesAlongTheHeadingAtFlySpeed) {
    float pos[3] = {0.f, 10.f, 0.f};
    Keys k;
    k.forward = true;
    EXPECT_TRUE(step(pos, k, northHeading(), 0.02f));
    EXPECT_NEAR(pos[2], kHorizontalSpeed * 0.02f, 1e-5f);
    EXPECT_NEAR(pos[0], 0.f, 1e-6f);
    EXPECT_FLOAT_EQ(pos[1], 10.f);
}

TEST(EldenRingFlight, DiagonalIsNotFasterThanStraight) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = k.right = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(std::sqrt(pos[0] * pos[0] + pos[2] * pos[2]), kHorizontalSpeed * 0.02f, 1e-5f);
}

TEST(EldenRingFlight, OppositeKeysCancelAndVerticalIsIndependent) {
    float pos[3] = {1.f, 1.f, 1.f};
    Keys k;
    k.forward = k.back = true;
    EXPECT_FALSE(step(pos, k, northHeading(), 0.02f));
    EXPECT_FLOAT_EQ(pos[2], 1.f);
    k.up = true;
    EXPECT_TRUE(step(pos, k, northHeading(), 0.02f));
    EXPECT_NEAR(pos[1], 1.f + kVerticalSpeed * 0.02f, 1e-5f);
    k.up = false;
    k.down = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(pos[1], 1.f, 1e-5f);
}

TEST(EldenRingFlight, VerticalAddsToHorizontalWithoutSlowingIt) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = k.up = true;
    step(pos, k, northHeading(), 0.02f);
    EXPECT_NEAR(pos[2], kHorizontalSpeed * 0.02f, 1e-5f);
    EXPECT_NEAR(pos[1], kVerticalSpeed * 0.02f, 1e-5f);
}

TEST(EldenRingFlight, ALongHitchIsClampedAndZeroOrNegativeTimeDoesNothing) {
    float pos[3] = {0.f, 0.f, 0.f};
    Keys k;
    k.forward = true;
    step(pos, k, northHeading(), 5.f); // 5 s hitch
    EXPECT_NEAR(pos[2], kHorizontalSpeed * kMaxStep, 1e-5f);
    float still[3] = {0.f, 0.f, 0.f};
    EXPECT_FALSE(step(still, k, northHeading(), 0.f));
    EXPECT_FALSE(step(still, k, northHeading(), -0.1f));
    EXPECT_FLOAT_EQ(still[2], 0.f);
}

TEST(EldenRingFlight, TheGameDoesNotSeeMovementJumpOrCrouchKeys) {
    for (int dik : {0x11, 0x1E, 0x1F, 0x20, 0x39, 0x2A}) {
        EXPECT_NE(std::find(std::begin(kHiddenKeys), std::end(kHiddenKeys), dik), std::end(kHiddenKeys)) << dik;
    }
}

TEST(EldenRingFlight, ThePhysicsPositionIsSyncedToTheProxiesThroughTheRequestByte) {
    // 0x14045C910 reads [phys+0x91] and clears it after copying [phys+0x70] into the Havok proxies.
    EXPECT_EQ(eldenring::live::layout::kPhysicsProxySyncRequest, 0x91u);
    EXPECT_NE(eldenring::live::layout::kPhysicsProxySyncRequest, eldenring::live::layout::kStandingOnGroundInPhysics); // not the ground flag
}

TEST(EldenRingFlight, OverwrittenPositionIsDetectedAgainstWhatWeWroteLastFrame) {
    const float wrote[3] = {30.f, 5.f, 2.f};
    const float same[3] = {30.f, 5.f, 2.f};
    const float tiny[3] = {30.05f, 5.f, 2.f};
    const float snapped[3] = {2.f, 5.f, 2.f};
    EXPECT_FALSE(positionOverwritten(wrote, same, 0.5f));
    EXPECT_FALSE(positionOverwritten(wrote, tiny, 0.5f)); // the game may move the player a little
    EXPECT_TRUE(positionOverwritten(wrote, snapped, 0.5f));
    const float high[3] = {30.f, 25.f, 2.f};
    EXPECT_TRUE(positionOverwritten(wrote, high, 0.5f)); // any axis
}

TEST(EldenRingFlight, FollowsTheGamesFloatingOriginRebaseInMultiplesOfEight) {
    // 2026-10-10 log: the game re-bases the physics coordinates by whole multiples of 8 m (the fraction is kept), it is not a teleport.
    float own[3] = {27.52f, 5.70f, 32.13f};
    const float wrote[3] = {27.52f, 5.70f, 32.13f};
    const float read[3] = {3.52f, 5.71f, 0.13f};
    followRebase(own, wrote, read);
    EXPECT_NEAR(own[0], 3.52f, 1e-4f);
    EXPECT_NEAR(own[1], 5.70f, 1e-4f); // no re-base on this axis: the tiny difference is the game's own physics
    EXPECT_NEAR(own[2], 0.13f, 1e-4f);
}

TEST(EldenRingFlight, RebaseRoundsAwayTheGravityDriftAndIgnoresSmallDifferences) {
    float own[3] = {32.21f, -32.77f, 3.49f};
    const float wrote[3] = {32.21f, -32.77f, 3.49f};
    const float read[3] = {0.21f, -1.17f, 3.49f}; // y moved by +31.6: 32 m of re-base minus 0.4 m of falling
    followRebase(own, wrote, read);
    EXPECT_NEAR(own[0], 0.21f, 1e-4f);
    EXPECT_NEAR(own[1], -0.77f, 1e-4f); // our own height, not the game's fallen one
    EXPECT_NEAR(own[2], 3.49f, 1e-4f);
    float still[3] = {1.f, 2.f, 3.f};
    const float w2[3] = {1.f, 2.f, 3.f};
    const float falling[3] = {1.f, 1.6f, 3.f}; // gravity pulled 0.4 m down in a frame: not a re-base, own height must stay
    followRebase(still, w2, falling);
    EXPECT_FLOAT_EQ(still[1], 2.f);
}

TEST(EldenRingFlight, TheBodyFacesTheCameraHeading) {
    Heading h;
    const float f0[3] = {0.f, 0.f, 1.f}, r0[3] = {1.f, 0.f, 0.f};
    ASSERT_TRUE(headingFromCamera(f0, r0, h));
    EXPECT_NEAR(bodyYaw(h), 0.f, 1e-5f);
    const float fx[3] = {1.f, 0.f, 0.f}, rx[3] = {0.f, 0.f, -1.f};
    ASSERT_TRUE(headingFromCamera(fx, rx, h));
    EXPECT_NEAR(bodyYaw(h), 1.5707963f, 1e-5f);
}
