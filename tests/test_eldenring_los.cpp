#include <gtest/gtest.h>

#include "eldenring_los.hpp"

using namespace eldenring::live;

namespace {

RayCastFn wallAt(float distance_from_start) {
    return [distance_from_start](const float*, const float* disp) {
        const float len = std::sqrt(disp[0] * disp[0] + disp[1] * disp[1] + disp[2] * disp[2]);
        RayResult r;
        r.ok = true;
        r.hit = distance_from_start < len;
        r.fraction = distance_from_start / len;
        return r;
    };
}

} // namespace

TEST(EldenRingLos, ClearPathIsVisible) {
    const float a[3] = {0.f, 1.4f, 0.f}, b[3] = {0.f, 1.f, 3.f};
    RayCastFn none = [](const float*, const float*) { return RayResult{true, false, 1.f}; };
    EXPECT_TRUE(hasLineOfSight(none, a, b));
}

TEST(EldenRingLos, WallInTheMiddleBlocks) {
    const float a[3] = {0.f, 1.4f, 0.f}, b[3] = {0.f, 1.4f, 4.f};
    EXPECT_FALSE(hasLineOfSight(wallAt(2.f), a, b));
}

TEST(EldenRingLos, HitsRightAtTheEndsAreNotObstructions) {
    const float a[3] = {0.f, 1.4f, 0.f}, b[3] = {0.f, 1.4f, 4.f};
    EXPECT_TRUE(hasLineOfSight(wallAt(0.1f), a, b));  // the player's own floor / a wall at the feet
    EXPECT_TRUE(hasLineOfSight(wallAt(3.8f), a, b));  // the ground or wall the target stands against
    EXPECT_FALSE(hasLineOfSight(wallAt(3.5f), a, b)); // clearly before the target
    EXPECT_FALSE(hasLineOfSight(wallAt(0.5f), a, b)); // clearly after the start
}

TEST(EldenRingLos, FailsOpenWhenTheRayCannotBeCast) {
    const float a[3] = {0.f, 1.4f, 0.f}, b[3] = {0.f, 1.4f, 4.f};
    RayCastFn broken = [](const float*, const float*) { return RayResult{false, true, 0.5f}; };
    EXPECT_TRUE(hasLineOfSight(broken, a, b));
    EXPECT_TRUE(hasLineOfSight(RayCastFn{}, a, b));
}

TEST(EldenRingLos, ZeroLengthIsVisible) {
    const float a[3] = {1.f, 1.f, 1.f};
    EXPECT_TRUE(hasLineOfSight(wallAt(0.f), a, a));
}

TEST(EldenRingLos, PassesTheDisplacementNotTheEndPoint) {
    const float a[3] = {1.f, 2.f, 3.f}, b[3] = {4.f, 6.f, 3.f};
    float seen_from[3] = {}, seen_disp[3] = {};
    RayCastFn spy = [&](const float* f, const float* d) {
        for (int i = 0; i < 3; ++i) {
            seen_from[i] = f[i];
            seen_disp[i] = d[i];
        }
        return RayResult{true, false, 1.f};
    };
    hasLineOfSight(spy, a, b);
    EXPECT_FLOAT_EQ(seen_from[1], 2.f);
    EXPECT_FLOAT_EQ(seen_disp[0], 3.f);
    EXPECT_FLOAT_EQ(seen_disp[1], 4.f);
    EXPECT_FLOAT_EQ(seen_disp[2], 0.f);
}
