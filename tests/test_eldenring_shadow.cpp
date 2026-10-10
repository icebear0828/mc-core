#include <gtest/gtest.h>

#include "eldenring_shadow.hpp"

#include <cmath>

using namespace eldenring::shadow;
using eldenring::live::RayCastFn;
using eldenring::live::RayResult;

namespace {
// A flat floor at height `y`: the ray hits it when it reaches that height.
RayCastFn floorAt(float y) {
    return [y](const float from[3], const float disp[3]) {
        RayResult r;
        r.ok = true;
        const float end = from[1] + disp[1];
        r.hit = from[1] >= y && end < y;
        r.fraction = r.hit ? (from[1] - y) / (from[1] - end) : 1.f;
        r.pos[0] = from[0];
        r.pos[1] = y;
        r.pos[2] = from[2];
        return r;
    };
}
} // namespace

TEST(EldenRingShadow, GroundIsWhereTheRayHits) {
    const float feet[3] = {1.f, 10.f, 2.f};
    const auto g = groundBelow(floorAt(7.f), {}, feet);
    ASSERT_TRUE(g.has_value());
    EXPECT_NEAR(*g, 7.f, 1e-3f);
}

TEST(EldenRingShadow, GroundNearTheFeetIsFoundEvenWhenTheFeetSinkALittleIntoIt) {
    const float feet[3] = {0.f, 10.f, 0.f};
    const auto g = groundBelow(floorAt(10.1f), {}, feet); // the ray starts a little above the feet, so a floor 10 cm above them is still found
    ASSERT_TRUE(g.has_value());
    EXPECT_NEAR(*g, 10.1f, 1e-3f);
}

TEST(EldenRingShadow, NoGroundWhenTheRayMissesOrCannotBeCast) {
    const float feet[3] = {0.f, 10.f, 0.f};
    EXPECT_FALSE(groundBelow(floorAt(-100.f), {}, feet).has_value());                         // farther than the search distance
    EXPECT_FALSE(groundBelow(RayCastFn{}, {}, feet).has_value());
    RayCastFn broken = [](const float*, const float*) { return RayResult{false, true, 0.5f}; };
    EXPECT_FALSE(groundBelow(broken, {}, feet).has_value());
}

TEST(EldenRingShadow, APlacedBlockTopHigherThanTheTerrainWins) {
    const float feet[3] = {0.f, 10.f, 0.f};
    BlockTopFn top = [](const float, const float, const float, const float) { return std::optional<float>(9.f); };
    const auto g = groundBelow(floorAt(5.f), top, feet);
    ASSERT_TRUE(g.has_value());
    EXPECT_NEAR(*g, 9.f, 1e-3f);
    // a block top below the terrain does not matter
    BlockTopFn low = [](const float, const float, const float, const float) { return std::optional<float>(2.f); };
    EXPECT_NEAR(*groundBelow(floorAt(5.f), low, feet), 5.f, 1e-3f);
    // blocks alone are enough when the terrain ray fails
    EXPECT_NEAR(*groundBelow(RayCastFn{}, top, feet), 9.f, 1e-3f);
}

TEST(EldenRingShadow, StandingOnTheGroundGivesTheFullShadow) {
    const Shadow s = shadowAt(3.f, 4.f, 5.f, 4.f);
    EXPECT_TRUE(s.visible);
    EXPECT_FLOAT_EQ(s.x, 3.f);
    EXPECT_FLOAT_EQ(s.z, 5.f);
    EXPECT_NEAR(s.y, 4.f + kLift, 1e-6f);
    EXPECT_NEAR(s.radius, kBaseRadius, 1e-6f);
    EXPECT_NEAR(s.strength, kMaxStrength, 1e-6f);
}

TEST(EldenRingShadow, TheShadowFadesAndShrinksWithHeightAndVanishesFarUp) {
    const Shadow low = shadowAt(0.f, 6.f, 0.f, 4.f);
    const Shadow high = shadowAt(0.f, 12.f, 0.f, 4.f);
    EXPECT_TRUE(low.visible);
    EXPECT_TRUE(high.visible);
    EXPECT_LT(low.strength, kMaxStrength);
    EXPECT_LT(high.strength, low.strength);
    EXPECT_LT(high.radius, low.radius);
    EXPECT_GT(high.radius, 0.f);
    EXPECT_FALSE(shadowAt(0.f, 4.f + kFadeHeight, 0.f, 4.f).visible);
    EXPECT_FALSE(shadowAt(0.f, 100.f, 0.f, 4.f).visible);
}

TEST(EldenRingShadow, FeetBelowTheGroundByTheSinkDepthStillCountAsStanding) {
    const Shadow s = shadowAt(0.f, 3.95f, 0.f, 4.f); // the feet sunk 5 cm into a slope
    EXPECT_TRUE(s.visible);
    EXPECT_NEAR(s.strength, kMaxStrength, 1e-6f);
}

TEST(EldenRingShadow, ScaleBigMobsShadowsByTheirRadius) {
    const Shadow big = shadowAt(0.f, 1.f, 0.f, 1.f, 2.f);
    EXPECT_NEAR(big.radius, 2.f * kBaseRadius, 1e-6f);
}

TEST(EldenRingShadow, TheWorldMatrixScalesTheUnitDiscAndMovesItUnderTheFeet) {
    const Shadow s = shadowAt(3.f, 4.f, 5.f, 4.f);
    const mc::rig::Mat4 m = shadowMatrix(s);
    const mc::Vec3 rim = mc::rig::transformPoint(m, {1.f, 0.f, 0.f});
    EXPECT_NEAR(rim.x, 3.f + s.radius, 1e-5f);
    EXPECT_NEAR(rim.y, s.y, 1e-5f);
    EXPECT_NEAR(rim.z, 5.f, 1e-5f);
    const mc::Vec3 centre = mc::rig::transformPoint(m, {0.f, 0.f, 0.f});
    EXPECT_NEAR(centre.x, 3.f, 1e-5f);
    EXPECT_NEAR(centre.z, 5.f, 1e-5f);
}

TEST(EldenRingShadow, TheDiscIsAFanWithRadialCoordinatesInItsUv) {
    const mc::rig::RigMesh d = buildShadowDisc(16);
    ASSERT_EQ(d.vertices.size(), 17u);
    ASSERT_EQ(d.indices.size(), 16u * 3u);
    EXPECT_FLOAT_EQ(d.vertices[0].u, 0.f);
    EXPECT_FLOAT_EQ(d.vertices[0].v, 0.f);
    for (size_t i = 1; i < d.vertices.size(); ++i) {
        const auto& v = d.vertices[i];
        EXPECT_FLOAT_EQ(v.y, 0.f);                                   // flat
        EXPECT_NEAR(std::hypot(v.x, v.z), 1.f, 1e-5f);               // unit circle
        EXPECT_NEAR(std::hypot(v.u, v.v), 1.f, 1e-5f);               // the shader reads the distance from the centre
    }
    for (uint16_t i : d.indices) EXPECT_LT(i, d.vertices.size());
}

TEST(EldenRingShadow, TheFalloffIsFullAtTheCentreAndZeroAtTheRim) {
    EXPECT_NEAR(falloff(0.f), 1.f, 1e-6f);
    EXPECT_NEAR(falloff(1.f), 0.f, 1e-6f);
    EXPECT_GT(falloff(0.3f), falloff(0.7f));
    EXPECT_FLOAT_EQ(falloff(1.5f), 0.f);
}
