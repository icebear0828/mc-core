#include <gtest/gtest.h>

#include "cs_impact_decals.hpp"

#include <cmath>

using namespace eldenring::cs;

namespace {

float dot3(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

float length3(const float v[3]) {
    return std::sqrt(dot3(v, v));
}

} // namespace

TEST(CsImpactDecalsTest, NormalIsFlippedTowardTheShooter) {
    float out[3];
    const float shot_down[3] = {0.f, -1.f, 0.f};
    const float floor_up[3] = {0.f, 1.f, 0.f};
    orientDecalNormal(floor_up, shot_down, out);
    EXPECT_NEAR(out[1], 1.f, 1e-6f); // already faces the shooter

    const float shot_up[3] = {0.f, 1.f, 0.f};
    orientDecalNormal(floor_up, shot_up, out);
    EXPECT_NEAR(out[1], -1.f, 1e-6f); // flipped so the decal faces the shot

    const float zero[3] = {0.f, 0.f, 0.f};
    orientDecalNormal(zero, shot_down, out);
    EXPECT_NEAR(out[1], 1.f, 1e-6f); // degenerate input falls back to world up
}

TEST(CsImpactDecalsTest, TangentBasisIsOrthonormalOnFloorAndWall) {
    const float normals[2][3] = {{0.f, 1.f, 0.f}, {1.f, 0.f, 0.f}};
    for (const auto& n : normals) {
        float t[3], b[3];
        decalTangentBasis(n, t, b);
        EXPECT_NEAR(length3(t), 1.f, 1e-5f);
        EXPECT_NEAR(length3(b), 1.f, 1e-5f);
        EXPECT_NEAR(dot3(n, t), 0.f, 1e-5f);
        EXPECT_NEAR(dot3(n, b), 0.f, 1e-5f);
        EXPECT_NEAR(dot3(t, b), 0.f, 1e-5f);
    }
}

TEST(CsImpactDecalsTest, FadeStartsInTheLastFifthOfTheLifetime) {
    EXPECT_FLOAT_EQ(decalFade(0.f, 20.f), 1.f);
    EXPECT_FLOAT_EQ(decalFade(16.f, 20.f), 1.f);
    EXPECT_NEAR(decalFade(18.f, 20.f), 0.5f, 1e-5f);
    EXPECT_FLOAT_EQ(decalFade(20.f, 20.f), 0.f);
    EXPECT_FLOAT_EQ(decalFade(5.f, 0.f), 0.f);
}

TEST(CsImpactDecalsTest, RingRefreshesNearbyHitsAndCapsItsSize) {
    ImpactDecalRing ring;
    const float floor_normal[3] = {0.f, 1.f, 0.f};

    const float first[3] = {1.f, 0.f, 2.f};
    EXPECT_TRUE(ring.addOrRefresh(first, floor_normal, 1000u));
    EXPECT_EQ(ring.size(), 1u);

    // Same spot within 3 cm: refresh, do not stack.
    const float nearby[3] = {1.01f, 0.f, 2.01f};
    EXPECT_FALSE(ring.addOrRefresh(nearby, floor_normal, 2000u));
    EXPECT_EQ(ring.size(), 1u);
    EXPECT_EQ(ring.at(0).start_ms, 2000u);

    // A different spot adds a new decal.
    const float elsewhere[3] = {1.2f, 0.f, 2.f};
    EXPECT_TRUE(ring.addOrRefresh(elsewhere, floor_normal, 3000u));
    EXPECT_EQ(ring.size(), 2u);

    for (int i = 0; i < 400; ++i) {
        const float pos[3] = {static_cast<float>(i) * 0.5f, 0.f, 0.f};
        ring.addOrRefresh(pos, floor_normal, 10000u + static_cast<uint64_t>(i));
    }
    EXPECT_EQ(ring.size(), kMaxImpactDecals);
}

TEST(CsImpactDecalsTest, RingExpiresDecalsAfterTheirLifetime) {
    ImpactDecalRing ring;
    const float normal[3] = {0.f, 1.f, 0.f};
    const float short_lived[3] = {0.f, 0.f, 0.f};
    const float long_lived[3] = {5.f, 0.f, 0.f};
    ring.addOrRefresh(short_lived, normal, 1000u, 0.5f);
    ring.addOrRefresh(long_lived, normal, 1000u, 2.f);

    ring.expire(1200u);
    EXPECT_EQ(ring.size(), 2u);
    ring.expire(1600u);
    EXPECT_EQ(ring.size(), 1u); // the 0.5 s decal is gone, the 2 s one remains
    ring.expire(3100u);
    EXPECT_EQ(ring.size(), 0u);
}
