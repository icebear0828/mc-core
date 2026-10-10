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

TEST(EldenRingBowProbe, TriplesNearAPointAreFoundAtTheirByteOffsets) {
    std::vector<uint8_t> b(0x100, 0);
    const float p[3] = {10.f, 5.f, -3.f};
    std::memcpy(b.data() + 0x40, p, 12);
    const float far[3] = {10.f, 5.f, 30.f};
    std::memcpy(b.data() + 0x80, far, 12);
    const float center[3] = {10.01f, 5.f, -3.f};
    const std::vector<int> hits = findTriples(b.data(), b.size(), center, 0.1f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], 0x40);
}

TEST(EldenRingBowProbe, ZeroedMemoryIsNotATripleNearAnOriginThatIsNotZero) {
    std::vector<uint8_t> b(0x100, 0);
    const float center[3] = {10.f, 5.f, -3.f};
    EXPECT_TRUE(findTriples(b.data(), b.size(), center, 0.5f).empty());
}

TEST(EldenRingBowProbe, NonFiniteFloatsAreSkipped) {
    std::vector<uint8_t> b(0x40, 0xFF); // NaN patterns
    const float center[3] = {0.f, 0.f, 0.f};
    EXPECT_TRUE(findTriples(b.data(), b.size(), center, 1000.f).empty());
}

TEST(EldenRingBowProbe, AlongAimFindsAPointFartherOnTheAimAndSkipsEverythingElse) {
    std::vector<uint8_t> b(0x200, 0);
    auto put = [&](size_t off, float x, float y, float z) {
        const float v[3] = {x, y, z};
        std::memcpy(b.data() + off, v, 12);
    };
    const float p0[3] = {10.f, 5.f, 0.f}, aim[3] = {0.f, 0.f, 1.f};
    put(0x10, 10.f, 5.f, 8.f);    // 8 m ahead on the aim: the bullet
    put(0x40, 10.f, 5.f, 0.f);    // the spawn point itself: not moved
    put(0x70, 18.f, 5.f, 0.5f);   // off the aim (almost perpendicular)
    put(0xA0, 10.f, 5.f, 500.f);  // beyond the maximum distance
    put(0xD0, 10.f, 5.f, -8.f);   // behind the muzzle
    const std::vector<int> hits = findAlongAim(b.data(), b.size(), p0, aim, 0.3f, 80.f, 0.9f);
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0], 0x10);
}

TEST(EldenRingBowProbe, AlongAimToleratesTheDropOfAFallingBolt) {
    std::vector<uint8_t> b(0x40, 0);
    const float v[3] = {10.f, 3.5f, 20.f}; // 20 m ahead, 1.5 m lower than the muzzle (gravity)
    std::memcpy(b.data() + 0x8, v, 12);
    const float p0[3] = {10.f, 5.f, 0.f}, aim[3] = {0.f, 0.f, 1.f};
    EXPECT_EQ(findAlongAim(b.data(), b.size(), p0, aim, 0.3f, 80.f, 0.9f).size(), 1u);
}

TEST(EldenRingBowProbe, MoversAreTriplesWhoseChangeRunsAlongTheAimWhateverTheFrame) {
    std::vector<uint8_t> a(0x100, 0), b(0x100, 0);
    auto put = [](std::vector<uint8_t>& v, size_t off, float x, float y, float z) {
        const float t[3] = {x, y, z};
        std::memcpy(v.data() + off, t, 12);
    };
    const float aim[3] = {0.f, 0.f, 1.f};
    put(a, 0x10, 100.f, 5.f, 40.f);  put(b, 0x10, 100.f, 5.f, 47.5f);  // moved 7.5 m along the aim, in some other coordinate frame
    put(a, 0x40, 1.f, 2.f, 3.f);     put(b, 0x40, 1.f, 2.f, 3.f);      // did not move
    put(a, 0x70, 0.f, 0.f, 0.f);     put(b, 0x70, 6.f, 0.f, 0.f);      // moved sideways
    put(a, 0xA0, 0.f, 0.f, 10.f);    put(b, 0xA0, 0.f, 0.f, 9.f);      // moved backwards
    const std::vector<Mover> m = findMovers(a.data(), b.data(), a.size(), aim, 0.5f, 100.f, 0.9f);
    // A triple read 8 bytes early can borrow the x of a neighbour that moved sideways (here 0x68 for the one at 0x70): such overlaps are real noise in
    // aligned scans, so the test only asks for the true mover to be there and the still / sideways / backward ones to be reported at their own offsets.
    bool found = false;
    for (const Mover& x : m) {
        if (x.offset == 0x10) {
            found = true;
            EXPECT_NEAR(x.moved, 7.5f, 1e-4f);
            EXPECT_FLOAT_EQ(x.v2[2], 47.5f);
        }
        EXPECT_NE(x.offset, 0x40); // did not move
        EXPECT_NE(x.offset, 0x70); // moved sideways
        EXPECT_NE(x.offset, 0xA0); // moved backwards
    }
    EXPECT_TRUE(found);
}

TEST(EldenRingBowProbe, MoversIgnoreNonFiniteAndHugeJumps) {
    std::vector<uint8_t> a(0x40, 0), b(0x40, 0xFF);
    const float aim[3] = {0.f, 0.f, 1.f};
    EXPECT_TRUE(findMovers(a.data(), b.data(), a.size(), aim, 0.5f, 100.f, 0.9f).empty());
    const float x[3] = {0.f, 0.f, 0.f}, y[3] = {0.f, 0.f, 5000.f};
    std::memcpy(a.data(), x, 12);
    std::memcpy(b.data(), y, 12);
    EXPECT_TRUE(findMovers(a.data(), b.data(), 12, aim, 0.5f, 100.f, 0.9f).empty());
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

TEST(EldenRingBowAim, RotatingAboutTheVerticalAxisChangesTheYawAndKeepsPitchAndLength) {
    const float v[3] = {0.f, 0.f, 1.f};
    float r[3];
    rotateYaw(v, 90.f, r);
    EXPECT_NEAR(r[0], 1.f, 1e-5f);     // yaw = atan2(x, z): +90 turns +z into +x
    EXPECT_NEAR(r[1], 0.f, 1e-5f);
    EXPECT_NEAR(r[2], 0.f, 1e-5f);
    const float tilted[3] = {0.554f, 0.2f, 0.831f};
    rotateYaw(tilted, -31.f, r);
    EXPECT_NEAR(r[1], 0.2f, 1e-6f);   // the height does not change
    EXPECT_NEAR(std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]), std::sqrt(0.554f * 0.554f + 0.04f + 0.831f * 0.831f), 1e-5f);
    const AimError e = aimError(tilted, r);
    EXPECT_NEAR(e.yaw_deg, -31.f, 1e-3f);
    EXPECT_NEAR(e.pitch_deg, 0.f, 1e-3f);
}

TEST(EldenRingBowAim, TheMeasuredOffsetOfTheGameCancelsOut) {
    // The game turns the flight by +31 degrees (REVERSE 35.9). We give the aim turned by -31: the bolt flies where we meant.
    const float aim[3] = {0.554f, 0.043f, 0.831f};
    float given[3], flown[3];
    rotateYaw(aim, -kBoltYawOffsetDeg, given);
    rotateYaw(given, kBoltYawOffsetDeg, flown);   // what the game does to it
    const AimError e = aimError(aim, flown);
    EXPECT_NEAR(e.yaw_deg, 0.f, 1e-3f);
    EXPECT_NEAR(e.pitch_deg, 0.f, 1e-3f);
}
