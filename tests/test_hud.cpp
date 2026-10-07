#include <gtest/gtest.h>
#include "mc/hud.hpp"

using namespace mc;

TEST(HudEngineTest, DefaultStateInitialization) {
    HudEngine hud;
    EXPECT_EQ(hud.getSelectedSlot(), 0);
    EXPECT_EQ(hud.getSelectedItem(), ItemId::None);
    EXPECT_FLOAT_EQ(hud.getHealth(), 20.0f);
    EXPECT_FLOAT_EQ(hud.getMaxHealth(), 20.0f);
    EXPECT_FLOAT_EQ(hud.getHunger(), 20.0f);
    EXPECT_TRUE(hud.isCrosshairVisible());
}

TEST(HudEngineTest, HotbarSlotSelectionAndScroll) {
    HudEngine hud;

    // Direct slot selection
    hud.selectSlot(4);
    EXPECT_EQ(hud.getSelectedSlot(), 4);

    hud.selectSlot(8);
    EXPECT_EQ(hud.getSelectedSlot(), 8);

    // Out of bound slot selection clamps to 0..8
    hud.selectSlot(15);
    EXPECT_EQ(hud.getSelectedSlot(), 8);

    hud.selectSlot(-5);
    EXPECT_EQ(hud.getSelectedSlot(), 0);

    // Scroll delta cycling (0 -> 1 -> 2 ... -> 8 -> 0)
    hud.selectSlot(0);
    hud.scrollSlot(1);
    EXPECT_EQ(hud.getSelectedSlot(), 1);

    hud.scrollSlot(-1);
    EXPECT_EQ(hud.getSelectedSlot(), 0);

    // Wrap around backwards: 0 - 1 -> 8
    hud.scrollSlot(-1);
    EXPECT_EQ(hud.getSelectedSlot(), 8);

    // Wrap around forwards: 8 + 1 -> 0
    hud.scrollSlot(1);
    EXPECT_EQ(hud.getSelectedSlot(), 0);
}

TEST(HudEngineTest, ItemSlotsAndDisplayNames) {
    HudEngine hud;
    hud.setSlot(0, ItemId::DiamondSword, 1);
    hud.setSlot(1, ItemId::BlockDirt, 64);
    hud.setSlot(2, ItemId::GoldenApple, 16);

    hud.selectSlot(0);
    EXPECT_EQ(hud.getSelectedItem(), ItemId::DiamondSword);
    EXPECT_EQ(hud.getSelectedSlotCount(), 1u);
    EXPECT_STREQ(hud.getSelectedItemDisplayName(), "Diamond Sword");

    hud.selectSlot(1);
    EXPECT_EQ(hud.getSelectedItem(), ItemId::BlockDirt);
    EXPECT_EQ(hud.getSelectedSlotCount(), 64u);
    EXPECT_STREQ(hud.getSelectedItemDisplayName(), "Dirt");

    hud.selectSlot(2);
    EXPECT_EQ(hud.getSelectedItem(), ItemId::GoldenApple);
    EXPECT_STREQ(hud.getSelectedItemDisplayName(), "Golden Apple");

    hud.selectSlot(3);
    EXPECT_EQ(hud.getSelectedItem(), ItemId::None);
    EXPECT_STREQ(hud.getSelectedItemDisplayName(), "");
}

TEST(HudEngineTest, HeartContainersCalculation) {
    HudEngine hud;

    // Full 20 HP -> 10 full hearts, 0 half, 0 empty
    hud.setHealth(20.0f);
    auto h20 = hud.computeHearts();
    EXPECT_EQ(h20.full_hearts, 10);
    EXPECT_EQ(h20.has_half_heart, false);
    EXPECT_EQ(h20.empty_hearts, 0);

    // 19 HP -> 9 full hearts, 1 half, 0 empty
    hud.setHealth(19.0f);
    auto h19 = hud.computeHearts();
    EXPECT_EQ(h19.full_hearts, 9);
    EXPECT_EQ(h19.has_half_heart, true);
    EXPECT_EQ(h19.empty_hearts, 0);

    // 15 HP -> 7 full hearts, 1 half, 2 empty (total 10 containers)
    hud.setHealth(15.0f);
    auto h15 = hud.computeHearts();
    EXPECT_EQ(h15.full_hearts, 7);
    EXPECT_EQ(h15.has_half_heart, true);
    EXPECT_EQ(h15.empty_hearts, 2);

    // 0 HP -> 0 full hearts, 0 half, 10 empty
    hud.setHealth(0.0f);
    auto h0 = hud.computeHearts();
    EXPECT_EQ(h0.full_hearts, 0);
    EXPECT_EQ(h0.has_half_heart, false);
    EXPECT_EQ(h0.empty_hearts, 10);
}

TEST(HudEngineTest, HungerDrumsticksCalculation) {
    HudEngine hud;

    // 20 Hunger -> 10 full drumsticks
    hud.setHunger(20.0f);
    auto f20 = hud.computeHunger();
    EXPECT_EQ(f20.full_drumsticks, 10);
    EXPECT_EQ(f20.has_half_drumstick, false);
    EXPECT_EQ(f20.empty_drumsticks, 0);

    // 17 Hunger -> 8 full drumsticks, 1 half, 1 empty
    hud.setHunger(17.0f);
    auto f17 = hud.computeHunger();
    EXPECT_EQ(f17.full_drumsticks, 8);
    EXPECT_EQ(f17.has_half_drumstick, true);
    EXPECT_EQ(f17.empty_drumsticks, 1);
}
