#include <gtest/gtest.h>

#include "eldenring_melee.hpp"
#include "mc/combat.hpp"
#include "mc/contracts/combat_adapter.hpp"

using namespace eldenring::live;

namespace {

class MockCombatAdapter : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent&) override { return true; }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}
};

} // namespace

TEST(EldenRingCombatBalanceTest, LowHpMobDoesNotSufferSpongeScaling) {
    // 150 HP mob (e.g. dog / rodent). Previously only took 7 HP (needed 20 hits to kill a dog!).
    // With balanced hybrid scaling, diamond sword (7.0 damage) must deal >= 140 damage (killing in 1-2 hits).
    mc::HitIntent sword;
    sword.damage = 7.0f;
    sword.max_hp_percent = 0.02f;

    const int dmg_sword = erDamage(sword, 150);
    EXPECT_GE(dmg_sword, 140);
    EXPECT_LE(dmg_sword, 160);

    // Bare hand (1.0 damage) deals ~20 damage on 150 HP mob
    mc::HitIntent hand;
    hand.damage = 1.0f;
    hand.max_hp_percent = 0.01f;
    const int dmg_hand = erDamage(hand, 150);
    EXPECT_GE(dmg_hand, 20);
    EXPECT_LE(dmg_hand, 25);
}

TEST(EldenRingCombatBalanceTest, StandardSoldierEliminatedInThreeHitsOrTwoCrits) {
    // 400 HP standard Godrick soldier (matches MC Zombie 20 HP behavior)
    mc::HitIntent sword_normal;
    sword_normal.damage = 7.0f;
    sword_normal.max_hp_percent = 0.02f;
    sword_normal.is_critical = false;

    const int normal_dmg = erDamage(sword_normal, 400);
    EXPECT_GE(normal_dmg, 140); // 3 hits * 148 = 444 > 400 (3-hit kill)
    EXPECT_LE(normal_dmg, 160);

    // Critical jump attack (1.5x)
    mc::HitIntent sword_crit = sword_normal;
    sword_crit.damage = 7.0f * 1.5f; // 10.5f
    sword_crit.is_critical = true;

    const int crit_dmg = erDamage(sword_crit, 400);
    EXPECT_GE(crit_dmg, 210); // 2 crits * 218 = 436 > 400 (2-crit kill)
    EXPECT_LE(crit_dmg, 240);
}

TEST(EldenRingCombatBalanceTest, BossScalingRemainsChallengingAndFair) {
    mc::HitIntent sword;
    sword.damage = 7.0f;
    sword.max_hp_percent = 0.02f;

    // Early Boss: Margit the Fell Omen (4,174 HP)
    // Should take ~200-250 per hit (around 16-20 hits to defeat)
    const int margit_dmg = erDamage(sword, 4174);
    EXPECT_GE(margit_dmg, 210);
    EXPECT_LE(margit_dmg, 240);

    // Endgame Boss: Malenia (18,487 HP)
    // Should scale smoothly to ~450-550 per hit
    const int malenia_dmg = erDamage(sword, 18487);
    EXPECT_GE(malenia_dmg, 480);
    EXPECT_LE(malenia_dmg, 530);
}

TEST(EldenRingCombatBalanceTest, UnknownMaxHpDealsBaseDamageInsteadOfZero) {
    mc::HitIntent sword;
    sword.damage = 7.0f;
    sword.max_hp_percent = 0.02f;

    // When max HP is unreadable or <= 0, base damage must still be dealt!
    const int fallback_dmg = erDamage(sword, 0);
    EXPECT_GE(fallback_dmg, 140);
    EXPECT_LE(fallback_dmg, 150);

    const int neg_dmg = erDamage(sword, -1);
    EXPECT_EQ(neg_dmg, fallback_dmg);
}

TEST(EldenRingCombatBalanceTest, ConfigurableScaleAndPercentOverrides) {
    mc::HitIntent sword;
    sword.damage = 7.0f;
    sword.max_hp_percent = 0.02f;

    // Custom multiplier: base scale 30.0f, hp_percent 0.03f
    // On 1,000 HP mob: Base = 7 * 30 = 210, HP = 1000 * 0.03 = 30 -> 240
    const int custom_dmg = erDamage(sword, 1000, 30.0f, 0.03f);
    EXPECT_EQ(custom_dmg, 240);
}

TEST(EldenRingCombatBalanceTest, SweepingEdgeCalculatesSecondarySplashDamage) {
    MockCombatAdapter adapter;
    mc::CombatEngine engine(adapter);

    // Full charge on ground with diamond sword activates Sweeping Edge
    const mc::HitIntent primary = engine.calculateMeleeHit(
        mc::EntityId::LocalPlayer, mc::EntityId::None,
        mc::ItemId::DiamondSword, /*attack_cooldown=*/1.0f,
        /*is_falling=*/false, /*is_on_ground=*/true,
        {}, {0.f, 0.f, 1.f}
    );

    EXPECT_TRUE(primary.is_sweeping);
    EXPECT_FALSE(primary.is_critical);

    const int primary_dmg = erDamage(primary, 400); // 400 HP soldier
    EXPECT_GE(primary_dmg, 140);

    // Secondary target hit by sweeping edge takes 40% damage
    mc::HitIntent secondary = primary;
    secondary.damage = std::max(1.0f, primary.damage * 0.4f);
    secondary.is_sweeping = true;

    const int secondary_dmg = erDamage(secondary, 400);
    EXPECT_GE(secondary_dmg, 50);
    EXPECT_LE(secondary_dmg, 70);
    EXPECT_LT(secondary_dmg, primary_dmg);
}
