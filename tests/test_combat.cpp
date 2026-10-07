#include <gtest/gtest.h>
#include "mc/combat.hpp"
#include "mc/contracts/combat_adapter.hpp"

class MockCombatAdapter : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent& intent) override {
        last_intent_ = intent;
        return true;
    }
    float getMaxHealth(mc::EntityId) override { return 100.0f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float force) override {
        last_force_ = force;
    }

    mc::HitIntent last_intent_{};
    float last_force_{0.f};
};

TEST(CombatEngineTest, CriticalHitOnFalling) {
    MockCombatAdapter adapter;
    mc::CombatEngine combat(adapter);

    auto intent = combat.calculateMeleeHit(
        mc::EntityId{1}, mc::EntityId{2}, mc::ItemId::DiamondSword,
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
        mc::EntityId{1}, mc::EntityId{2}, mc::ItemId::DiamondSword,
        1.0f,  // full cooldown
        false, // not falling
        true,  // on ground
        {0, 0, 0}, {0, 1, 0}
    );

    EXPECT_FALSE(intent.is_critical);
    EXPECT_TRUE(intent.is_sweeping);
    EXPECT_NEAR(intent.damage, 7.0f, 0.01f);
}

namespace {

class CountingCombatAdapter : public mc::ICombatAdapter {
public:
    bool process_result{true};
    int process_calls{0};
    int stagger_calls{0};
    mc::EntityId stagger_victim{mc::EntityId::None};
    mc::Vec3 stagger_dir{};
    float stagger_force{0.f};

    bool processHit(const mc::HitIntent&) override {
        ++process_calls;
        return process_result;
    }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId id, const mc::Vec3& dir, float force) override {
        ++stagger_calls;
        stagger_victim = id;
        stagger_dir = dir;
        stagger_force = force;
    }
};

mc::HitIntent sampleIntent() {
    mc::HitIntent i;
    i.victim_id = mc::EntityId{9};
    i.damage = 5.f;
    i.knockback_vector = {1.f, 0.f, 0.f};
    i.knockback_force = 800.f;
    return i;
}

} // namespace

TEST(CombatEngineTest, ExecuteHitTriggersReactionExactlyOnceWithIntentKnockback) {
    CountingCombatAdapter adapter;
    mc::CombatEngine combat(adapter);

    EXPECT_TRUE(combat.executeHit(sampleIntent()));
    EXPECT_EQ(adapter.process_calls, 1);
    EXPECT_EQ(adapter.stagger_calls, 1);
    EXPECT_EQ(adapter.stagger_victim, mc::EntityId{9});
    EXPECT_FLOAT_EQ(adapter.stagger_dir.x, 1.f);
    EXPECT_FLOAT_EQ(adapter.stagger_force, 800.f);
}

TEST(CombatEngineTest, RejectedHitDoesNotTriggerReaction) {
    CountingCombatAdapter adapter;
    adapter.process_result = false;
    mc::CombatEngine combat(adapter);

    EXPECT_FALSE(combat.executeHit(sampleIntent()));
    EXPECT_EQ(adapter.stagger_calls, 0);
}

TEST(CombatEngineTest, NoKnockbackNoReaction) {
    CountingCombatAdapter adapter;
    mc::CombatEngine combat(adapter);
    mc::HitIntent i = sampleIntent();
    i.knockback_force = 0.f;

    EXPECT_TRUE(combat.executeHit(i));
    EXPECT_EQ(adapter.stagger_calls, 0);
}

TEST(CombatEngineTest, ExecuteHitRejectsNoEntityVictim) {
    CountingCombatAdapter adapter;
    mc::CombatEngine combat(adapter);
    mc::HitIntent i = sampleIntent();
    i.victim_id = mc::EntityId::None;

    EXPECT_FALSE(combat.executeHit(i));
    EXPECT_EQ(adapter.process_calls, 0);
    EXPECT_EQ(adapter.stagger_calls, 0);
}

TEST(CombatEngineTest, LocalPlayerIsAValidVictim) {
    CountingCombatAdapter adapter;
    mc::CombatEngine combat(adapter);
    mc::HitIntent i = sampleIntent();
    i.victim_id = mc::EntityId::LocalPlayer;

    EXPECT_TRUE(combat.executeHit(i));
    EXPECT_EQ(adapter.process_calls, 1);
    EXPECT_EQ(adapter.stagger_victim, mc::EntityId::LocalPlayer);
}
