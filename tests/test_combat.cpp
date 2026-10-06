#include <gtest/gtest.h>
#include "mc/combat.hpp"
#include "mc/contracts/combat_adapter.hpp"

class MockCombatAdapter : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent& intent) override {
        last_intent_ = intent;
        return true;
    }
    float getMaxHealth(uint64_t) override { return 100.0f; }
    void triggerStaggerOrRagdoll(uint64_t, const mc::Vec3&, float force) override {
        last_force_ = force;
    }

    mc::HitIntent last_intent_{};
    float last_force_{0.f};
};

TEST(CombatEngineTest, CriticalHitOnFalling) {
    MockCombatAdapter adapter;
    mc::CombatEngine combat(adapter);

    auto intent = combat.calculateMeleeHit(
        1, 2, mc::ItemId::DiamondSword,
        1.0f,  // full cooldown
        true,  // is_falling
        false, // not on ground
        {0, 0, 0}, {0, 1, 0}
    );

    EXPECT_TRUE(intent.is_critical);
    EXPECT_GT(intent.damage, 7.0f); // 7.0 * 1.5 = 10.5
    EXPECT_NEAR(intent.damage, 10.5f, 0.01f);

    EXPECT_TRUE(combat.executeHit(intent));
    EXPECT_GT(adapter.last_force_, 1000.f);
}

TEST(CombatEngineTest, SweepingEdgeOnGround) {
    MockCombatAdapter adapter;
    mc::CombatEngine combat(adapter);

    auto intent = combat.calculateMeleeHit(
        1, 2, mc::ItemId::DiamondSword,
        1.0f,  // full cooldown
        false, // not falling
        true,  // on ground
        {0, 0, 0}, {0, 1, 0}
    );

    EXPECT_FALSE(intent.is_critical);
    EXPECT_TRUE(intent.is_sweeping);
    EXPECT_NEAR(intent.damage, 7.0f, 0.01f);
}
