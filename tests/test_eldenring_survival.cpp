#include <gtest/gtest.h>

#include "eldenring_melee.hpp"
#include "eldenring_survival.hpp"

using namespace eldenring::live;

TEST(EldenRingSurvival, MinecraftHealthScalesToTheCharactersMaximum) {
    EXPECT_EQ(scaleToEr(20.f, 1000), 1000);
    EXPECT_EQ(scaleToEr(4.f, 1000), 200);
    EXPECT_EQ(scaleToEr(1.f, 576), 29); // 28.8 rounds up
    EXPECT_EQ(scaleToEr(0.f, 1000), 0);
}

TEST(EldenRingSurvival, TotemOnlyActsOnAKillingHit) {
    TotemOutcome a = totemClamp(50, 100, true);
    EXPECT_FALSE(a.popped);
    EXPECT_EQ(a.damage, 50);
    TotemOutcome b = totemClamp(100, 100, true); // exactly lethal
    EXPECT_TRUE(b.popped);
    EXPECT_EQ(b.damage, 99); // leaves 1 HP
    TotemOutcome c = totemClamp(300, 100, true);
    EXPECT_TRUE(c.popped);
    EXPECT_EQ(c.damage, 99);
    TotemOutcome d = totemClamp(300, 100, false); // no totem: the hit kills
    EXPECT_FALSE(d.popped);
    EXPECT_EQ(d.damage, 300);
    TotemOutcome e = totemClamp(5, 1, true); // already on 1 HP: any hit would kill, damage becomes 0
    EXPECT_TRUE(e.popped);
    EXPECT_EQ(e.damage, 0);
    EXPECT_FALSE(totemClamp(300, 0, true).popped); // dead already
}

TEST(EldenRingSurvival, RegenerationIIHealsOneHpEvery1_25Seconds) {
    SurvivalState s;
    s.apply({mc::ActiveEffect{mc::EffectType::Regeneration, 2, 5.f}});
    EXPECT_TRUE(s.regenerating());
    int total = 0;
    for (int i = 0; i < 500; ++i) total += s.tick(0.01f, 1000); // 5 s
    EXPECT_NEAR(total, 4 * 50, 55);                              // 4 Minecraft HP = 4 * (1000 / 20)
    EXPECT_FALSE(s.regenerating());
    EXPECT_EQ(s.tick(1.f, 1000), 0);                             // nothing after it ends
}

TEST(EldenRingSurvival, RegenerationILevelOneIsSlower) {
    SurvivalState s;
    s.apply({mc::ActiveEffect{mc::EffectType::Regeneration, 1, 5.f}});
    int total = 0;
    for (int i = 0; i < 500; ++i) total += s.tick(0.01f, 1000);
    EXPECT_NEAR(total, 2 * 50, 55); // 5 s / 2.5 s = 2 HP
}

TEST(EldenRingSurvival, AbsorptionSoaksUpDamageFirstAndRunsOut) {
    SurvivalState s;
    s.apply({mc::ActiveEffect{mc::EffectType::Absorption, 1, 120.f}}); // 4 HP = 200 at max 1000
    EXPECT_EQ(s.absorb(150, 1000), 0);
    EXPECT_NEAR(s.absorptionMc(), 1.f, 0.01f); // 50 left = 1 MC HP
    EXPECT_EQ(s.absorb(100, 1000), 50);        // 50 soaked, 50 gets through
    EXPECT_FLOAT_EQ(s.absorptionMc(), 0.f);
    EXPECT_EQ(s.absorb(100, 1000), 100);       // nothing left
    SurvivalState t;
    t.apply({mc::ActiveEffect{mc::EffectType::Absorption, 1, 2.f}});
    t.tick(3.f, 1000);                         // expired
    EXPECT_EQ(t.absorb(100, 1000), 100);
}

TEST(EldenRingSurvival, MealHealOnlyWhenTheMealIsFinished) {
    mc::EatingEvent e;
    e.health_restored = 8.f;
    EXPECT_EQ(mealHeal(e, 1000), 0);
    e.completed = true;
    EXPECT_EQ(mealHeal(e, 1000), 400);
}

TEST(EldenRingSurvival, HotbarCountsAndTheTotemSlot) {
    MeleeController m;
    EXPECT_EQ(m.countAt(0), 1u);
    EXPECT_EQ(m.countAt(2), 64u);
    EXPECT_EQ(m.countAt(5), 8u);
    // Minecraft: a totem only works in the main hand (or off hand): carrying it elsewhere does nothing.
    EXPECT_FALSE(m.holds(mc::ItemId::TotemOfUndying)); // slot 0 (the sword) is selected, the totem sits in slot 8
    EXPECT_FALSE(m.consumeHeld(mc::ItemId::TotemOfUndying));
    EXPECT_EQ(m.itemAt(8), mc::ItemId::TotemOfUndying);
    m.select(8);
    EXPECT_TRUE(m.holds(mc::ItemId::TotemOfUndying));
    EXPECT_TRUE(m.consumeHeld(mc::ItemId::TotemOfUndying));
    EXPECT_FALSE(m.holds(mc::ItemId::TotemOfUndying));
    EXPECT_EQ(m.itemAt(8), mc::ItemId::None); // the emptied slot is empty
    EXPECT_FALSE(m.consumeHeld(mc::ItemId::TotemOfUndying));
    m.select(5);
    EXPECT_TRUE(m.consumeAt(5));
    EXPECT_EQ(m.countAt(5), 7u);
    EXPECT_EQ(m.heldItem(), mc::ItemId::GoldenApple);
}

TEST(EldenRingFeedback, TotemPopFadesOverAboutASecond) {
    HitFeedback f;
    EXPECT_FLOAT_EQ(f.totem(), 0.f);
    f.onTotem();
    EXPECT_FLOAT_EQ(f.totem(), 1.f);
    f.tick(HitFeedback::kTotemSec * 0.5f);
    EXPECT_NEAR(f.totem(), 0.5f, 1e-3f);
    f.tick(HitFeedback::kTotemSec);
    EXPECT_FLOAT_EQ(f.totem(), 0.f);
}

TEST(EldenRingFeedback, HurtLastsHalfASecondAndKeepsItsSide) {
    HitFeedback f;
    EXPECT_FLOAT_EQ(f.hurt(), 0.f);
    f.onHurt(-1.f);
    EXPECT_FLOAT_EQ(f.hurt(), 1.f);
    EXPECT_FLOAT_EQ(f.hurtSide(), -1.f);
    f.tick(HitFeedback::kHurtSec * 0.5f);
    EXPECT_NEAR(f.hurt(), 0.5f, 1e-3f);
    f.tick(HitFeedback::kHurtSec);
    EXPECT_FLOAT_EQ(f.hurt(), 0.f);
}

TEST(EldenRingSurvival, ComputeArmorDamageReductionFullSuite) {
    // 0 points: full damage passes through
    auto r0 = computeArmorDamageReduction(100, 100, 0, 0.0f);
    EXPECT_EQ(r0.raw_damage, 100);
    EXPECT_EQ(r0.final_damage, 100);
    EXPECT_FLOAT_EQ(r0.reduction_ratio, 0.0f);

    // Full diamond set (20 points, 80% reduction)
    auto r20 = computeArmorDamageReduction(120, 100, 20, 0.80f);
    EXPECT_EQ(r20.raw_damage, 120);
    EXPECT_EQ(r20.damage_after_absorb, 100);
    EXPECT_EQ(r20.armor_points, 20);
    EXPECT_EQ(r20.final_damage, 20); // 100 * (1 - 0.8) = 20

    // Minimum 1 damage clamped even if reduction would round to 0
    auto r_min = computeArmorDamageReduction(2, 2, 20, 0.80f);
    EXPECT_EQ(r_min.final_damage, 1);
}

TEST(EldenRingFeedback, ArmorDamageToastTracksValuesAndFades) {
    HitFeedback f;
    EXPECT_FLOAT_EQ(f.armorToast().timer, 0.f);
    f.onArmorDamage(150, 30, 20, 0.80f);
    EXPECT_FLOAT_EQ(f.armorToast().timer, 3.0f);
    EXPECT_EQ(f.armorToast().raw, 150);
    EXPECT_EQ(f.armorToast().final_dmg, 30);
    EXPECT_EQ(f.armorToast().armor_pts, 20);
    EXPECT_FLOAT_EQ(f.armorToast().ratio, 0.80f);

    f.tick(1.5f);
    EXPECT_NEAR(f.armorToast().timer, 1.5f, 1e-3f);
    f.tick(2.0f);
    EXPECT_FLOAT_EQ(f.armorToast().timer, 0.f);
}

TEST(EldenRingSurvival, InventoryArmorPointsAlwaysReflectsEquippedItems) {
    mc::Inventory inv;
    EXPECT_EQ(inv.totalArmorPoints(), 0);

    // Equip diamond helmet (3 pts)
    inv.setArmor(0, {mc::ItemId::DiamondHelmet, 1});
    EXPECT_EQ(inv.totalArmorPoints(), 3);

    // Equip diamond chestplate (8 pts), leggings (6 pts), boots (3 pts)
    inv.setArmor(1, {mc::ItemId::DiamondChestplate, 1});
    inv.setArmor(2, {mc::ItemId::DiamondLeggings, 1});
    inv.setArmor(3, {mc::ItemId::DiamondBoots, 1});
    EXPECT_EQ(inv.totalArmorPoints(), 20);

    // Unequip chestplate -> immediately drops by 8 pts
    inv.setArmor(1, {});
    EXPECT_EQ(inv.totalArmorPoints(), 12);
}

TEST(EldenRingSurvival, InventoryDefenseReductionRatioMatchesFormula) {
    mc::Inventory inv;
    EXPECT_FLOAT_EQ(inv.defenseReductionRatio(), 0.0f);

    inv.setArmor(0, {mc::ItemId::DiamondHelmet, 1}); // 3 pts -> 12%
    EXPECT_FLOAT_EQ(inv.defenseReductionRatio(), 0.12f);

    inv.setArmor(1, {mc::ItemId::DiamondChestplate, 1}); // +8 = 11 pts -> 44%
    inv.setArmor(2, {mc::ItemId::DiamondLeggings, 1});   // +6 = 17 pts -> 68%
    inv.setArmor(3, {mc::ItemId::DiamondBoots, 1});      // +3 = 20 pts -> 80% (cap)
    EXPECT_FLOAT_EQ(inv.defenseReductionRatio(), 0.80f);
}

