#include <gtest/gtest.h>
#include "eldenring_mining.hpp"
#include "eldenring_blockmesh.hpp"
#include "mc/hud_atlas.hpp"

using namespace eldenring::mining;
using namespace eldenring::blocks;

TEST(EldenRingMiningTest, ComputeBreakTimeByBlockAndTool) {
    // Creative mode: instant break (0.0s)
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::None, true), 0.0f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Dirt, mc::ItemId::None, true), 0.0f);

    // Stone: 0.3s with Diamond Pickaxe, 7.5s bare hand / other tool
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false), 0.3f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::None, false), 7.5f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondSword, false), 7.5f);

    // Dirt: 0.75s bare hand, 0.4s with tool
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Dirt, mc::ItemId::None, false), 0.75f);

    // TNT: 0.05s
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Tnt, mc::ItemId::None, false), 0.05f);
}

TEST(EldenRingMiningTest, CanHarvestRules) {
    // Stone: requires pickaxe to drop
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false));
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::None, false));
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondSword, false));

    // Dirt: can be harvested by hand
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Dirt, mc::ItemId::None, false));
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Dirt, mc::ItemId::DiamondPickaxe, false));

    // Creative mode: no item drops
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, true));
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Dirt, mc::ItemId::None, true));
}

TEST(EldenRingMiningTest, AllPickaxeTiersHarvestStoneAndDropCobblestone) {
    const mc::ItemId picks[] = {mc::ItemId::WoodenPickaxe, mc::ItemId::StonePickaxe, mc::ItemId::IronPickaxe,
                                mc::ItemId::GoldenPickaxe, mc::ItemId::DiamondPickaxe, mc::ItemId::NetheritePickaxe};
    for (mc::ItemId tool : picks) {
        EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Stone, tool, false));
        EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Stone, tool, false), mc::ItemId::BlockCobblestone);
    }
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::None, false));
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Stone, mc::ItemId::None, false), mc::ItemId::None);
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondSword, false));
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Stone, mc::ItemId::DiamondSword, false), mc::ItemId::None);
}

TEST(EldenRingMiningTest, BlockDropsMatchVanilla) {
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Dirt, mc::ItemId::None, false), mc::ItemId::BlockDirt);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Tnt, mc::ItemId::None, false), mc::ItemId::BlockTnt);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Cobblestone, mc::ItemId::DiamondPickaxe, false), mc::ItemId::BlockCobblestone);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Cobblestone, mc::ItemId::None, false), mc::ItemId::None);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Wood, mc::ItemId::None, false), mc::ItemId::BlockWood);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Obsidian, mc::ItemId::DiamondPickaxe, false), mc::ItemId::BlockObsidian);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Obsidian, mc::ItemId::NetheritePickaxe, false), mc::ItemId::BlockObsidian);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Obsidian, mc::ItemId::StonePickaxe, false), mc::ItemId::None);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Obsidian, mc::ItemId::None, false), mc::ItemId::None);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, true), mc::ItemId::None);
}

TEST(EldenRingMiningTest, WoodAndObsidianRules) {
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Wood, mc::ItemId::None, false), 3.0f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Wood, mc::ItemId::DiamondPickaxe, false), 1.5f);
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Wood, mc::ItemId::None, false));
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::Wood, mc::ItemId::None, false), mc::ItemId::BlockWood);

    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Obsidian, mc::ItemId::DiamondPickaxe, false), 9.4f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Obsidian, mc::ItemId::NetheritePickaxe, false), 8.35f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Obsidian, mc::ItemId::None, false), 30.0f);
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Obsidian, mc::ItemId::DiamondPickaxe, false));
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::Obsidian, mc::ItemId::NetheritePickaxe, false));
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Obsidian, mc::ItemId::StonePickaxe, false));
    EXPECT_FALSE(MiningController::canHarvest(mc::BlockId::Obsidian, mc::ItemId::None, false));
}

TEST(EldenRingMiningTest, CraftingTableBreaksLikeWoodAndDropsItself) {
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::CraftingTable, mc::ItemId::None, false), 3.0f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::CraftingTable, mc::ItemId::DiamondPickaxe, false), 1.5f);
    EXPECT_TRUE(MiningController::canHarvest(mc::BlockId::CraftingTable, mc::ItemId::None, false));
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::CraftingTable, mc::ItemId::None, false), mc::ItemId::CraftingTable);
    EXPECT_EQ(MiningController::blockDrop(mc::BlockId::CraftingTable, mc::ItemId::None, true), mc::ItemId::None);
}

TEST(EldenRingMiningTest, StoneBreakTimeScalesWithPickaxeTier) {
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::WoodenPickaxe, false), 1.15f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::StonePickaxe, false), 0.6f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::IronPickaxe, false), 0.4f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::GoldenPickaxe, false), 0.2f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false), 0.3f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::NetheritePickaxe, false), 0.25f);
    EXPECT_FLOAT_EQ(MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::None, false), 7.5f);
}

TEST(EldenRingMiningTest, ProgressiveCrackStagesAndHitSound) {
    MiningController controller;
    const Cell stone_cell{10, 5, 20};

    // Frame 0: Start mining stone with Diamond Pickaxe (0.3s total)
    controller.update(0.016f, true, true, stone_cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    const MiningState& s0 = controller.state();
    EXPECT_TRUE(s0.is_mining);
    EXPECT_EQ(s0.target_cell, stone_cell);
    EXPECT_EQ(s0.stage, 0); // initial crack stage
    EXPECT_FALSE(s0.broken);
    EXPECT_TRUE(s0.hit_sound); // initial hit sound on click

    // Step 0.075s (total ~0.091s, ~30% progress -> stage 3)
    controller.update(0.075f, true, true, stone_cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_EQ(controller.state().stage, 3);
    EXPECT_FALSE(controller.state().broken);

    // Step 0.1s (total ~0.191s, ~63% progress -> stage 6)
    controller.update(0.1f, true, true, stone_cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_EQ(controller.state().stage, 6);
    EXPECT_FALSE(controller.state().broken);

    // Step 0.12s (total > 0.3s -> broken)
    controller.update(0.12f, true, true, stone_cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_TRUE(controller.state().broken);
    EXPECT_FALSE(controller.state().is_mining);
}

TEST(EldenRingMiningTest, TargetChangeOrReleaseResetsProgress) {
    MiningController controller;
    const Cell cell_a{1, 2, 3};
    const Cell cell_b{1, 2, 4};

    // Start mining cell_a
    controller.update(0.1f, true, true, cell_a, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_TRUE(controller.state().is_mining);
    EXPECT_GT(controller.state().progress, 0.0f);

    // Target switches to cell_b before cell_a is finished: should reset
    controller.update(0.05f, true, true, cell_b, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_EQ(controller.state().target_cell, cell_b);
    EXPECT_FLOAT_EQ(controller.state().progress, 0.05f / 0.3f);

    // Mouse released: should completely reset
    controller.update(0.016f, false, true, cell_b, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_FALSE(controller.state().is_mining);
    EXPECT_FLOAT_EQ(controller.state().progress, 0.0f);
    EXPECT_EQ(controller.state().stage, -1);
}

TEST(EldenRingMiningTest, BlockMeshIncludesCrackOverlayQuads) {
    BlockGrid grid;
    const Cell target{0, 0, 0};
    grid.place(target, mc::BlockId::Stone);

    // Mesh without mining overlay: 6 faces of 1 cube = 6 * 4 = 24 vertices
    mc::rig::RigMesh base_mesh = buildBlockMesh(grid);
    EXPECT_EQ(base_mesh.vertices.size(), 24u);
    EXPECT_EQ(base_mesh.indices.size(), 36u);

    // Mesh with mining overlay at stage 4
    MiningOverlay overlay;
    overlay.cell = target;
    overlay.stage = 4;
    mc::rig::RigMesh cracked_mesh = buildBlockMesh(grid, {}, overlay);

    // Should contain base faces + crack overlay quads on all 6 exposed faces (24 + 24 = 48 vertices)
    EXPECT_EQ(cracked_mesh.vertices.size(), 48u);
    EXPECT_EQ(cracked_mesh.indices.size(), 72u);

    // Verify crack vertices are slightly offset along normal (+0.001m)
    // Top face is at y=1.0f + 0.001f = 1.001f
    bool found_offset_top = false;
    for (size_t i = 24; i < cracked_mesh.vertices.size(); ++i) {
        if (std::abs(cracked_mesh.vertices[i].y - 1.001f) < 1e-4f) {
            found_offset_top = true;
            break;
        }
    }
    EXPECT_TRUE(found_offset_top);
}
