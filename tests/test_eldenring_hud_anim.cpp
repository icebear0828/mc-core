#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "eldenring_heart_anim.hpp"
#include "eldenring_hunger.hpp"
#include "eldenring_xp.hpp"

using namespace eldenring::live;

// ---- hunger -----------------------------------------------------------------------------------------------------------------

TEST(Hunger, StartsFull) {
    HungerModel h;
    EXPECT_EQ(h.food(), 20);
    EXPECT_FLOAT_EQ(h.saturation(), 5.f);
}

TEST(Hunger, FourExhaustionCostsSaturationFirstThenFood) {
    HungerModel h;
    h.addExhaustion(4.f);
    EXPECT_FLOAT_EQ(h.saturation(), 4.f);
    EXPECT_EQ(h.food(), 20);
    h.addExhaustion(3.9f);
    h.addExhaustion(0.2f); // crosses 4 again
    h.addExhaustion(16.f);
    h.addExhaustion(4.f);
    EXPECT_FLOAT_EQ(h.saturation(), 0.f);
    EXPECT_LT(h.food(), 20);
}

TEST(Hunger, FoodNeverGoesBelowZero) {
    HungerModel h;
    for (int i = 0; i < 100; ++i) h.addExhaustion(4.f);
    EXPECT_EQ(h.food(), 0);
    EXPECT_FLOAT_EQ(h.saturation(), 0.f);
}

TEST(Hunger, EatingRestoresFoodAndSaturationUpToTheFoodLevel) {
    HungerModel h;
    for (int i = 0; i < 60; ++i) h.addExhaustion(4.f); // empty
    h.eat(mc::ItemId::Bread); // 5 food, 6.0 saturation
    EXPECT_EQ(h.food(), 5);
    EXPECT_FLOAT_EQ(h.saturation(), 5.f); // capped at the food level
    h.eat(mc::ItemId::CookedBeef); // 8 food, 12.8 saturation
    EXPECT_EQ(h.food(), 13);
    EXPECT_FLOAT_EQ(h.saturation(), 13.f);
    h.eat(mc::ItemId::CookedBeef);
    EXPECT_EQ(h.food(), 20);
}

TEST(Hunger, OnlyTheGoldenAppleCanBeEatenWhenFull) {
    HungerModel h;
    EXPECT_FALSE(h.canEat(mc::ItemId::Bread));
    EXPECT_FALSE(h.canEat(mc::ItemId::CookedBeef));
    EXPECT_TRUE(h.canEat(mc::ItemId::GoldenApple));
    EXPECT_TRUE(h.canEat(mc::ItemId::EnchantedGoldenApple));
    EXPECT_FALSE(h.canEat(mc::ItemId::DiamondSword)); // not food at all
    h.addExhaustion(40.f);
    EXPECT_TRUE(h.canEat(mc::ItemId::Bread));
}

TEST(Hunger, FastRegenerationNeedsFullFoodAndSaturation) {
    HungerModel h; // food 20, saturation 5
    float healed = 0.f;
    healed += h.tick(0.5f, true); // 10 ticks: min(5, 6) / 6 hit points, and that much exhaustion
    EXPECT_NEAR(healed, 5.f / 6.f, 1e-4f);
    EXPECT_LT(h.saturation(), 5.f + 1e-4f);
}

TEST(Hunger, SlowRegenerationFromEighteenFoodHealsOnePointEveryFourSeconds) {
    HungerModel h;
    h.addExhaustion(20.f); // saturation gone, food drops
    // bring the food to exactly 18 with no saturation
    HungerModel g;
    for (int i = 0; i < 2 + 5; ++i) g.addExhaustion(4.f); // 5 saturation + 2 food
    EXPECT_EQ(g.food(), 18);
    EXPECT_FLOAT_EQ(g.saturation(), 0.f);
    EXPECT_FLOAT_EQ(g.tick(3.9f, true), 0.f);
    EXPECT_NEAR(g.tick(0.2f, true), 1.f, 1e-4f);
}

TEST(Hunger, NoRegenerationWhenNotHurtOrTooHungry) {
    HungerModel h;
    EXPECT_FLOAT_EQ(h.tick(10.f, false), 0.f);
    HungerModel g;
    for (int i = 0; i < 12; ++i) g.addExhaustion(4.f); // food 13
    EXPECT_FLOAT_EQ(g.tick(20.f, true), 0.f);
}

TEST(Hunger, StarvingHurtsEveryFourSeconds) {
    HungerModel h;
    for (int i = 0; i < 60; ++i) h.addExhaustion(4.f);
    ASSERT_EQ(h.food(), 0);
    h.tick(8.1f, true);
    EXPECT_EQ(h.takeStarvationHits(), 2);
    EXPECT_EQ(h.takeStarvationHits(), 0);
}

TEST(Hunger, TheIconsShakeOnlyWithoutSaturation) {
    HungerModel h;
    EXPECT_FALSE(h.shaking());
    for (int i = 0; i < 4; ++i) h.addExhaustion(4.f);
    for (int i = 0; i < 3; ++i) h.addExhaustion(4.f);
    EXPECT_TRUE(h.shaking());
}

// ---- experience ---------------------------------------------------------------------------------------------------------------

TEST(Xp, PointsNeededPerLevelFollowMinecraft) {
    EXPECT_EQ(XpModel::pointsForNextLevel(0), 7);
    EXPECT_EQ(XpModel::pointsForNextLevel(1), 9);
    EXPECT_EQ(XpModel::pointsForNextLevel(15), 37);
    EXPECT_EQ(XpModel::pointsForNextLevel(16), 42);
    EXPECT_EQ(XpModel::pointsForNextLevel(30), 112);
    EXPECT_EQ(XpModel::pointsForNextLevel(31), 121);
}

TEST(Xp, LevelsAndProgress) {
    XpModel x;
    EXPECT_EQ(x.level(), 0);
    EXPECT_FLOAT_EQ(x.progress(), 0.f);
    x.add(5);
    EXPECT_EQ(x.level(), 0);
    EXPECT_NEAR(x.progress(), 5.f / 7.f, 1e-5f);
    x.add(2); // exactly level 1
    EXPECT_EQ(x.level(), 1);
    EXPECT_FLOAT_EQ(x.progress(), 0.f);
    x.add(10); // 9 more make level 2, 1 left over in a level that needs 11
    EXPECT_EQ(x.level(), 2);
    EXPECT_NEAR(x.progress(), 1.f / 11.f, 1e-5f);
}

TEST(Xp, AddingReportsLevelUps) {
    XpModel x;
    EXPECT_EQ(x.add(5), 0);
    EXPECT_EQ(x.add(2), 1);
    EXPECT_EQ(x.add(100), 6); // several levels at once: 9+11+13+15+17+19 = 84 points, 16 left towards the 21 of level 7
    EXPECT_EQ(x.add(0), 0);
    EXPECT_EQ(x.add(-5), 0);
}

TEST(Xp, KillsGiveMoreForTougherEnemies) {
    EXPECT_EQ(xpForKill(300), 5);
    EXPECT_EQ(xpForKill(3000), 20);
    EXPECT_EQ(xpForKill(20000), 50);
    EXPECT_EQ(xpForKill(0), 5);
}

// ---- heart animation ----------------------------------------------------------------------------------------------------------

TEST(HeartAnim, NoBlinkWhileTheHealthIsSteady) {
    HeartAnimator a;
    for (int i = 0; i < 100; ++i) a.update(0.05f, 20, 0, false);
    EXPECT_FALSE(a.blink());
    EXPECT_EQ(a.displayHealth(), 20);
}

TEST(HeartAnim, DamageBlinksForTwentyTicksAndFlipsEveryThree) {
    HeartAnimator a;
    a.update(0.05f, 20, 0, false);
    a.update(0.05f, 14, 0, false); // hit: the blink window opens
    int on = 0, flips = 0;
    bool prev = a.blink();
    int ticks = 0;
    for (; ticks < 40; ++ticks) {
        a.update(0.05f, 14, 0, false);
        if (a.blink()) ++on;
        if (a.blink() != prev) ++flips;
        prev = a.blink();
    }
    EXPECT_GT(on, 4);
    EXPECT_LT(on, 12);
    EXPECT_GE(flips, 4); // alternates, then stops after 20 ticks
    for (int i = 0; i < 5; ++i) {
        a.update(0.05f, 14, 0, false);
        EXPECT_FALSE(a.blink());
    }
}

TEST(HeartAnim, TheOldHealthStaysOnShowForAboutASecond) {
    HeartAnimator a;
    a.update(0.05f, 20, 0, false);
    a.update(0.05f, 12, 0, false);
    EXPECT_EQ(a.displayHealth(), 20); // the white hearts show what was lost
    for (int i = 0; i < 10; ++i) a.update(0.05f, 12, 0, false); // half a second
    EXPECT_EQ(a.displayHealth(), 20);
    for (int i = 0; i < 12; ++i) a.update(0.05f, 12, 0, false); // past a second
    EXPECT_EQ(a.displayHealth(), 12);
}

TEST(HeartAnim, HealingDoesNotBlink) {
    HeartAnimator a;
    a.update(0.05f, 10, 0, false);
    for (int i = 0; i < 40; ++i) a.update(0.05f, 10, 0, false);
    a.update(0.05f, 12, 0, false);
    for (int i = 0; i < 10; ++i) {
        a.update(0.05f, 12, 0, false);
        EXPECT_FALSE(a.blink());
    }
}

TEST(HeartAnim, RegenerationMakesOneHeartJumpAndTheJumpTravels) {
    HeartAnimator a;
    std::vector<int> seen;
    for (int i = 0; i < 30; ++i) {
        a.update(0.05f, 20, 0, true);
        seen.push_back(a.regenIndex());
    }
    EXPECT_GE(seen.front(), 0);
    EXPECT_NE(seen[0], seen[1]);
    EXPECT_LT(*std::max_element(seen.begin(), seen.end()), 25); // ceil(max health + 5) = 25 positions
    HeartAnimator b;
    b.update(0.05f, 20, 0, false);
    EXPECT_EQ(b.regenIndex(), -1);
}

TEST(HeartAnim, LowHealthShakesTheHearts) {
    HeartAnimator a;
    int shaken = 0;
    for (int i = 0; i < 40; ++i) {
        a.update(0.05f, 4, 0, false);
        for (int h = 0; h < 10; ++h) shaken += a.shake(h);
    }
    EXPECT_GT(shaken, 0);
    HeartAnimator b;
    int calm = 0;
    for (int i = 0; i < 40; ++i) {
        b.update(0.05f, 12, 0, false);
        for (int h = 0; h < 10; ++h) calm += b.shake(h);
    }
    EXPECT_EQ(calm, 0);
}
