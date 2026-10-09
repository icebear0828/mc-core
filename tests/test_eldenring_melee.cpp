#include <gtest/gtest.h>

#include "eldenring_melee.hpp"

#include "mc/contracts/combat_adapter.hpp"

using namespace eldenring::live;

namespace {

class NullCombat : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent&) override { return true; }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}
};

} // namespace

TEST(EldenRingMelee, HotbarStartsOnSwordAndWrapsOnScroll) {
    MeleeController m;
    EXPECT_EQ(m.selectedSlot(), 0);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondSword);
    m.scroll(1);
    EXPECT_EQ(m.selectedSlot(), 1);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondPickaxe);
    m.scroll(-2);
    EXPECT_EQ(m.selectedSlot(), 8); // wraps
    m.select(4);
    EXPECT_EQ(m.selectedSlot(), 4);
    m.select(9); // out of range: ignored
    m.select(-1);
    EXPECT_EQ(m.selectedSlot(), 4);
}

TEST(EldenRingMelee, SwingStartsAnimationAndResetsCooldown) {
    MeleeController m;
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f);
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f);
    const float before = m.startSwing();
    EXPECT_FLOAT_EQ(before, 1.f);
    EXPECT_FLOAT_EQ(m.cooldown(), 0.f);
    m.tick(mc::Session::kSwingDurationSec * 0.5f);
    EXPECT_NEAR(m.swingProgress(), 0.5f, 1e-3f);
    m.tick(mc::Session::kSwingDurationSec); // past the end
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f);
}

TEST(EldenRingMelee, CooldownRechargesToOne) {
    MeleeController m;
    m.startSwing();
    m.tick(mc::Session::kAttackRechargeSec * 0.5f);
    EXPECT_NEAR(m.cooldown(), 0.5f, 1e-3f);
    m.tick(10.f);
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f);
}

TEST(EldenRingMelee, DamageDependsOnHeldItemAndCooldown) {
    NullCombat adapter;
    mc::CombatEngine engine(adapter);
    MeleeController m;
    const mc::Vec3 dir{1.f, 0.f, 0.f};

    mc::HitIntent sword = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    m.select(1);
    mc::HitIntent pick = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    m.select(2); // dirt block: bare hand
    mc::HitIntent hand = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    EXPECT_GT(sword.damage, pick.damage);
    EXPECT_GT(pick.damage, hand.damage);

    m.select(0);
    mc::HitIntent weak = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 0.f, false, true, {}, dir);
    EXPECT_LT(weak.damage, sword.damage * 0.3f); // an uncharged swing is a poor hit
}

TEST(EldenRingMelee, FallingFullChargeIsCritical) {
    NullCombat adapter;
    mc::CombatEngine engine(adapter);
    MeleeController m;
    const mc::HitIntent crit = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, true, false, {}, {1.f, 0.f, 0.f});
    EXPECT_TRUE(crit.is_critical);
    const mc::HitIntent normal = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, false, {}, {1.f, 0.f, 0.f});
    EXPECT_FALSE(normal.is_critical);
    EXPECT_GT(crit.damage, normal.damage);
}

TEST(EldenRingMelee, ErDamageScalesWithEnemyMaxHp) {
    mc::HitIntent sword;
    sword.damage = 7.f;
    sword.max_hp_percent = 0.05f;
    EXPECT_EQ(erDamage(sword, 1000), 50);   // a full sword hit = 5% of max HP
    EXPECT_EQ(erDamage(sword, 20000), 1000);

    mc::HitIntent half = sword;
    half.damage = 3.5f; // half charge
    EXPECT_EQ(erDamage(half, 1000), 25);

    mc::HitIntent tiny = sword;
    tiny.damage = 0.1f;
    EXPECT_EQ(erDamage(tiny, 100), 1); // never below 1 for a landed hit
    EXPECT_EQ(erDamage(sword, 0), 0);  // unknown max HP: nothing to scale
}
