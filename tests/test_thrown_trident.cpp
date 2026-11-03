#include <gtest/gtest.h>

#include "mc/thrown_trident.hpp"

namespace {
constexpr float kNoJitter[6] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
}

TEST(ThrownTridentTest, LaunchVelocityIsLookTimesTwoPointFiveWithoutJitter) {
    const mc::Vec3 v = mc::ThrownTrident::launchVelocity({0.f, 0.f, 1.f}, 2.5f, 0.0172275f, kNoJitter);
    EXPECT_NEAR(v.x, 0.f, 1e-6f);
    EXPECT_NEAR(v.y, 0.f, 1e-6f);
    EXPECT_NEAR(v.z, 2.5f, 1e-6f);
}

TEST(ThrownTridentTest, LaunchInheritsOwnerVelocityXZAndAirborneY) {
    mc::ThrownTrident t;
    ASSERT_TRUE(t.launch({1.f, 2.f, 3.f}, {0.f, 0.f, 1.f}, {0.2f, -0.4f, 0.1f}, false, kNoJitter));
    EXPECT_NEAR(t.state().velocity.x, 0.2f, 1e-6f);
    EXPECT_NEAR(t.state().velocity.y, -0.4f, 1e-6f);
    EXPECT_NEAR(t.state().velocity.z, 2.6f, 1e-6f);

    mc::ThrownTrident grounded;
    ASSERT_TRUE(grounded.launch({}, {0.f, 0.f, 1.f}, {0.2f, -0.4f, 0.1f}, true, kNoJitter));
    EXPECT_NEAR(grounded.state().velocity.y, 0.f, 1e-6f);
}

TEST(ThrownTridentTest, TickAppliesGravityThenDragThenMoves) {
    mc::ThrownTrident t;
    ASSERT_TRUE(t.launch({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {}, false, kNoJitter));
    t.updateTick();
    EXPECT_NEAR(t.state().velocity.y, -0.05f * 0.99f, 1e-6f);
    EXPECT_NEAR(t.state().velocity.z, 2.5f * 0.99f, 1e-6f);
    EXPECT_NEAR(t.state().position.z, 2.5f * 0.99f, 1e-6f);
}

TEST(ThrownTridentTest, AdapterPathCommitsTheResolvedHit) {
    mc::ThrownTrident t;
    ASSERT_TRUE(t.launch({}, {0.f, 0.f, 1.f}, {}, false, kNoJitter));
    t.beginTick();
    const mc::Vec3 move = t.moveVector();
    EXPECT_NEAR(move.z, 2.475f, 1e-6f);
    t.commit({0.f, 0.f, move.z * 0.25f});
    EXPECT_NEAR(t.state().position.z, move.z * 0.25f, 1e-6f);
}

TEST(ThrownTridentTest, SafetyCapAndDiscard) {
    mc::ThrownTridentConfig cfg;
    cfg.max_ticks = 2;
    mc::ThrownTrident t(cfg);
    ASSERT_TRUE(t.launch({}, {0.f, 0.f, 1.f}, {}, false, kNoJitter));
    t.updateTick();
    EXPECT_TRUE(t.active());
    t.updateTick();
    EXPECT_FALSE(t.active());

    mc::ThrownTrident other;
    ASSERT_TRUE(other.launch({}, {0.f, 0.f, 1.f}, {}, false, kNoJitter));
    other.discard();
    EXPECT_FALSE(other.active());
}

TEST(ThrownTridentTest, RejectsADegenerateLook) {
    mc::ThrownTrident t;
    EXPECT_FALSE(t.launch({}, {0.f, 0.f, 0.f}, {}, false, kNoJitter));
    EXPECT_FALSE(t.active());
}
