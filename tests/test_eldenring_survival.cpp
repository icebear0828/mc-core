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
    EXPECT_TRUE(m.has(mc::ItemId::TotemOfUndying));
    EXPECT_TRUE(m.consumeFirst(mc::ItemId::TotemOfUndying));
    EXPECT_FALSE(m.has(mc::ItemId::TotemOfUndying));
    EXPECT_EQ(m.itemAt(8), mc::ItemId::None); // the emptied slot is empty
    EXPECT_FALSE(m.consumeFirst(mc::ItemId::TotemOfUndying));
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
