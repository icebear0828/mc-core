#include <gtest/gtest.h>

#include "mc/ground_mining.hpp"

#include <cstdint>

using namespace mc;

namespace {

// Holds the dig button and stops on the tick the ground breaks, so the caller observes the drop
// instead of the next dig cycle.
bool digFor(GroundMiningController& controller, uint64_t& rng, float seconds, ItemId held, LootZone zone,
            float pitch = 90.0f, bool creative = false) {
    constexpr float kStep = 0.05f;
    for (float t = 0.0f; t < seconds; t += kStep) {
        controller.update(kStep, true, pitch, held, zone, creative, rng);
        if (controller.state().broken) return true;
    }
    return false;
}

} // namespace

TEST(GroundMiningTest, GatingRequiresDownwardAimAndEmptyHandOrPickaxe) {
    EXPECT_TRUE(GroundMiningController::canDig(ItemId::None, 60.0f));
    EXPECT_TRUE(GroundMiningController::canDig(ItemId::DiamondPickaxe, 90.0f));
    EXPECT_TRUE(GroundMiningController::canDig(ItemId::WoodenPickaxe, 75.0f));
    EXPECT_FALSE(GroundMiningController::canDig(ItemId::DiamondSword, 90.0f));
    EXPECT_FALSE(GroundMiningController::canDig(ItemId::Bow, 90.0f));
    EXPECT_FALSE(GroundMiningController::canDig(ItemId::None, 59.9f));
    EXPECT_FALSE(GroundMiningController::canDig(ItemId::DiamondPickaxe, 30.0f));

    GroundMiningController controller;
    uint64_t rng = 1;
    controller.update(0.05f, true, 90.0f, ItemId::DiamondSword, LootZone::Cave, false, rng);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, -1);

    controller.update(0.05f, true, 45.0f, ItemId::DiamondPickaxe, LootZone::Cave, false, rng);
    EXPECT_FALSE(controller.state().is_mining);
}

TEST(GroundMiningTest, OnlyUpwardSurfacesCountAsGround) {
    EXPECT_TRUE(GroundMiningController::canDigSurface(1.0f));
    EXPECT_TRUE(GroundMiningController::canDigSurface(0.5f));
    EXPECT_FALSE(GroundMiningController::canDigSurface(0.49f));
    EXPECT_FALSE(GroundMiningController::canDigSurface(0.0f));
    EXPECT_FALSE(GroundMiningController::canDigSurface(-1.0f));
}

TEST(GroundMiningTest, BreakTimeScalesWithToolAndZone) {
    EXPECT_FLOAT_EQ(GroundMiningController::breakTime(ItemId::None, LootZone::Grassland, false), 2.5f);
    EXPECT_FLOAT_EQ(GroundMiningController::breakTime(ItemId::DiamondPickaxe, LootZone::Grassland, false), 1.0f);
    EXPECT_FLOAT_EQ(GroundMiningController::breakTime(ItemId::DiamondPickaxe, LootZone::Cave, false), 1.5f);
    EXPECT_FLOAT_EQ(GroundMiningController::breakTime(ItemId::NetheritePickaxe, LootZone::Volcano, false), 0.9f * 1.8f);
    EXPECT_FLOAT_EQ(GroundMiningController::breakTime(ItemId::None, LootZone::Cave, true), 0.0f);
}

TEST(GroundMiningTest, CrackStagesAdvanceAndReleaseResets) {
    GroundMiningController controller;
    uint64_t rng = 2;

    controller.update(0.05f, true, 90.0f, ItemId::None, LootZone::Grassland, false, rng);
    EXPECT_TRUE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, 0);
    EXPECT_TRUE(controller.state().hit_sound);

    digFor(controller, rng, 1.0f, ItemId::None, LootZone::Grassland);
    EXPECT_GT(controller.state().stage, 0);
    EXPECT_LT(controller.state().stage, 9);
    EXPECT_FALSE(controller.state().broken);

    controller.update(0.05f, false, 90.0f, ItemId::None, LootZone::Grassland, false, rng);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, -1);
    EXPECT_FLOAT_EQ(controller.state().progress, 0.0f);
}

TEST(GroundMiningTest, LookingUpMidDigDropsTheCrack) {
    GroundMiningController controller;
    uint64_t rng = 3;
    digFor(controller, rng, 1.0f, ItemId::DiamondPickaxe, LootZone::Cave);
    EXPECT_TRUE(controller.state().is_mining);

    controller.update(0.05f, true, 20.0f, ItemId::DiamondPickaxe, LootZone::Cave, false, rng);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, -1);
}

TEST(GroundMiningTest, MovingMidDigDropsTheCrack) {
    GroundMiningController controller;
    uint64_t rng = 9;
    digFor(controller, rng, 1.0f, ItemId::None, LootZone::Grassland);
    ASSERT_TRUE(controller.state().is_mining);
    ASSERT_GT(controller.state().progress, 0.0f);

    controller.update(0.05f, true, 90.0f, ItemId::None, LootZone::Grassland, false, rng, /*moving=*/true);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, -1);
    EXPECT_FLOAT_EQ(controller.state().progress, 0.0f);
    EXPECT_FALSE(controller.state().broken);
}

TEST(GroundMiningTest, MovingCannotStartADig) {
    GroundMiningController controller;
    uint64_t rng = 10;
    controller.update(0.05f, true, 90.0f, ItemId::DiamondPickaxe, LootZone::Cave, false, rng, /*moving=*/true);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, -1);

    controller.update(0.05f, true, 90.0f, ItemId::DiamondPickaxe, LootZone::Cave, false, rng, /*moving=*/false);
    EXPECT_TRUE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, 0);
}

TEST(GroundMiningTest, MovementKeepsTheLootStreak) {
    GroundMiningController controller;
    uint64_t rng = 11;
    digFor(controller, rng, 3.8f, ItemId::DiamondPickaxe, LootZone::Cave);
    ASSERT_TRUE(controller.state().broken);
    const int combo_before = controller.loot().combo();

    controller.update(0.05f, true, 90.0f, ItemId::DiamondPickaxe, LootZone::Cave, false, rng, /*moving=*/true);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_EQ(controller.loot().combo(), combo_before);
}

TEST(GroundMiningTest, ChangingToolRestartsTheCrack) {
    GroundMiningController controller;
    uint64_t rng = 4;
    digFor(controller, rng, 1.0f, ItemId::None, LootZone::Grassland);
    const float before = controller.state().progress;
    EXPECT_GT(before, 0.0f);

    controller.update(0.05f, true, 90.0f, ItemId::DiamondPickaxe, LootZone::Grassland, false, rng);
    EXPECT_TRUE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, 0);
    EXPECT_LT(controller.state().progress, before);
}

TEST(GroundMiningTest, BreakRollsAZoneDrop) {
    GroundMiningController controller;
    uint64_t rng = 5;
    digFor(controller, rng, 2.6f, ItemId::None, LootZone::Grassland);
    EXPECT_TRUE(controller.state().broken);
    EXPECT_EQ(controller.state().stage, 9);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_NE(controller.state().drop.item, ItemId::None);
}

TEST(GroundMiningTest, CreativeBreaksOnTheFirstTick) {
    GroundMiningController controller;
    uint64_t rng = 6;
    controller.update(0.05f, true, 90.0f, ItemId::DiamondPickaxe, LootZone::Cave, true, rng);
    EXPECT_TRUE(controller.state().broken);
    EXPECT_EQ(controller.state().stage, 9);
    EXPECT_FALSE(controller.state().is_mining);
}

TEST(GroundMiningTest, HandCannotHarvestCaveOreAndGetsRubble) {
    GroundMiningController controller;
    uint64_t rng = 7;
    for (int i = 0; i < 20; ++i) {
        digFor(controller, rng, 3.8f, ItemId::None, LootZone::Cave); // 2.5 s * 1.5 hardness
        ASSERT_TRUE(controller.state().broken);
        EXPECT_TRUE(controller.state().drop.item == ItemId::Coal ||
                    controller.state().drop.item == ItemId::BlockCobblestone);
        controller.update(0.01f, false, 90.0f, ItemId::None, LootZone::Cave, false, rng); // release between digs
    }
}

TEST(GroundMiningTest, PityAndComboSurviveCrackResets) {
    GroundMiningController controller;
    uint64_t rng = 8;
    digFor(controller, rng, 3.8f, ItemId::DiamondPickaxe, LootZone::Cave);
    const int combo_before = controller.loot().combo();
    controller.reset();
    EXPECT_EQ(controller.loot().combo(), combo_before);
    EXPECT_FALSE(controller.state().is_mining);
}
