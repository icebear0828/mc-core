#include <gtest/gtest.h>

#include "mc/fluid.hpp"

#include <cmath>
#include <cstddef>

namespace {

using mc::BlockId;
using mc::Cell;
using mc::FluidType;

// A flat stone floor: every cell below `first_air_y` is solid, everything above is air.
mc::FluidSimulation::BlockLookup floorAt(int first_air_y) {
    return [first_air_y](const Cell& cell) {
        return cell.y < first_air_y ? BlockId::Stone : BlockId::Air;
    };
}

} // namespace

TEST(FluidSimulationTest, WaterSourceSpreadsSevenBlocksWithOneLevelPerStep) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));

    const mc::FluidTickResult result = sim.step();
    EXPECT_FALSE(result.updates.empty());

    for (int d = 1; d <= 7; ++d) {
        const mc::FluidCell* east = sim.get(Cell{d, 0, 0});
        ASSERT_NE(east, nullptr) << "east d=" << d;
        EXPECT_EQ(east->type, FluidType::Water);
        EXPECT_EQ(static_cast<int>(east->level), d);
        EXPECT_FALSE(east->falling);
    }
    EXPECT_EQ(sim.get(Cell{8, 0, 0}), nullptr);
    EXPECT_EQ(sim.get(Cell{-8, 0, 0}), nullptr);
    EXPECT_EQ(sim.get(Cell{0, 0, 8}), nullptr);
    EXPECT_EQ(sim.get(Cell{0, 0, -8}), nullptr);

    // A diamond of radius 7: 1 + 4 * (1 + 2 + ... + 7).
    EXPECT_EQ(sim.size(), static_cast<std::size_t>(113));

    // A diagonal step costs two levels, so the corner of the diamond is level 7 too.
    const mc::FluidCell* corner = sim.get(Cell{4, 0, 3});
    ASSERT_NE(corner, nullptr);
    EXPECT_EQ(static_cast<int>(corner->level), 7);
    EXPECT_EQ(sim.get(Cell{4, 0, 4}), nullptr);
}

TEST(FluidSimulationTest, SourceIsLevelZeroAndNotFalling) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    const mc::FluidCell* source = sim.get(Cell{0, 0, 0});
    ASSERT_NE(source, nullptr);
    EXPECT_EQ(static_cast<int>(source->level), 0);
    EXPECT_FALSE(source->falling);
    EXPECT_TRUE(source->isSource());
}

TEST(FluidSimulationTest, LavaSpreadsThreeBlocksInTheOverworld) {
    mc::FluidRules rules;
    ASSERT_FALSE(rules.in_nether);
    mc::FluidSimulation sim(rules, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Lava));
    sim.step();

    const mc::FluidCell* third = sim.get(Cell{3, 0, 0});
    ASSERT_NE(third, nullptr);
    EXPECT_EQ(static_cast<int>(third->level), 3);
    EXPECT_EQ(sim.get(Cell{4, 0, 0}), nullptr);
    EXPECT_EQ(sim.size(), static_cast<std::size_t>(25));
}

TEST(FluidSimulationTest, LavaSpreadsSevenBlocksInTheNether) {
    mc::FluidRules rules;
    rules.in_nether = true;
    mc::FluidSimulation sim(rules, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Lava));
    sim.step();

    const mc::FluidCell* seventh = sim.get(Cell{7, 0, 0});
    ASSERT_NE(seventh, nullptr);
    EXPECT_EQ(static_cast<int>(seventh->level), 7);
    EXPECT_EQ(sim.get(Cell{8, 0, 0}), nullptr);
    EXPECT_EQ(sim.size(), static_cast<std::size_t>(113));
}

TEST(FluidSimulationTest, FallingColumnFallsAndRelevelsOnLanding) {
    // Stone at y <= -6, air above: the source at y = 0 has nothing under it.
    mc::FluidSimulation sim({}, floorAt(-5));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    sim.step();

    for (int y = -1; y >= -5; --y) {
        const mc::FluidCell* cell = sim.get(Cell{0, y, 0});
        ASSERT_NE(cell, nullptr) << "y=" << y;
        EXPECT_TRUE(cell->falling) << "y=" << y;
        EXPECT_EQ(static_cast<int>(cell->level), 0) << "y=" << y;
    }
    EXPECT_EQ(sim.get(Cell{0, -6, 0}), nullptr);

    // The column lands at y = -5 and runs its full seven blocks from there.
    const mc::FluidCell* landing = sim.get(Cell{7, -5, 0});
    ASSERT_NE(landing, nullptr);
    EXPECT_FALSE(landing->falling);
    EXPECT_EQ(static_cast<int>(landing->level), 7);
    EXPECT_EQ(sim.get(Cell{8, -5, 0}), nullptr);
}

TEST(FluidSimulationTest, FluidsStopAtSolidBlocksFromTheCallback) {
    const auto lookup = [](const Cell& cell) {
        if (cell.y < 0) return BlockId::Stone;   // floor
        if (cell.x == 3) return BlockId::Stone;  // wall
        return BlockId::Air;
    };
    mc::FluidSimulation sim({}, lookup);
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    sim.step();

    const mc::FluidCell* before_wall = sim.get(Cell{2, 0, 0});
    ASSERT_NE(before_wall, nullptr);
    EXPECT_EQ(static_cast<int>(before_wall->level), 2);
    EXPECT_EQ(sim.get(Cell{3, 0, 0}), nullptr);
    EXPECT_EQ(sim.get(Cell{4, 0, 0}), nullptr);
    EXPECT_EQ(sim.get(Cell{3, 0, 5}), nullptr);
}

TEST(FluidSimulationTest, SourceInsideASolidCellIsRejected) {
    mc::FluidSimulation sim({}, floorAt(0));
    EXPECT_FALSE(sim.setSource({0, -1, 0}, FluidType::Water));
    EXPECT_TRUE(sim.empty());
}

TEST(FluidSimulationTest, WaterTurnsFlowingLavaIntoCobblestone) {
    mc::FluidRules rules;
    rules.water_tick_interval = 2;
    rules.lava_tick_interval = 1;
    mc::FluidSimulation sim(rules, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    ASSERT_TRUE(sim.setSource({2, 0, 0}, FluidType::Lava));

    sim.tick(1); // lava only: it reaches (1,0,0) at level 1
    const mc::FluidCell* flowing = sim.get(Cell{1, 0, 0});
    ASSERT_NE(flowing, nullptr);
    EXPECT_EQ(flowing->type, FluidType::Lava);
    EXPECT_EQ(static_cast<int>(flowing->level), 1);

    const mc::FluidTickResult result = sim.tick(1); // water runs and meets the flowing lava
    bool converted = false;
    for (const mc::FluidInteraction& interaction : result.interactions) {
        if (interaction.cell == Cell{1, 0, 0} && interaction.result == BlockId::Cobblestone) {
            converted = true;
        }
    }
    EXPECT_TRUE(converted);
    EXPECT_EQ(sim.get(Cell{1, 0, 0}), nullptr);
}

TEST(FluidSimulationTest, WaterTurnsALavaSourceIntoObsidian) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    ASSERT_TRUE(sim.setSource({1, 0, 0}, FluidType::Lava));

    const mc::FluidTickResult result = sim.step();
    ASSERT_EQ(result.interactions.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE((result.interactions[0].cell == Cell{1, 0, 0}));
    EXPECT_EQ(result.interactions[0].result, BlockId::Obsidian);
    EXPECT_EQ(sim.get(Cell{1, 0, 0}), nullptr);
}

TEST(FluidSimulationTest, LavaLandingOnWaterTurnsTheWaterToStone) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    ASSERT_TRUE(sim.setSource({0, 2, 0}, FluidType::Lava));

    sim.step();
    const mc::FluidCell* falling_lava = sim.get(Cell{0, 1, 0});
    ASSERT_NE(falling_lava, nullptr);
    EXPECT_EQ(falling_lava->type, FluidType::Lava);
    EXPECT_TRUE(falling_lava->falling);

    const mc::FluidTickResult result = sim.step();
    bool converted = false;
    for (const mc::FluidInteraction& interaction : result.interactions) {
        if (interaction.cell == Cell{0, 0, 0} && interaction.result == BlockId::Stone) {
            converted = true;
        }
    }
    EXPECT_TRUE(converted);
    EXPECT_EQ(sim.get(Cell{0, 0, 0}), nullptr);
}

TEST(FluidSimulationTest, RemovingTheSourceDrainsTheFlow) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    sim.step();
    ASSERT_GT(sim.size(), static_cast<std::size_t>(1));

    ASSERT_TRUE(sim.removeSource({0, 0, 0}));
    const mc::FluidTickResult result = sim.step();

    EXPECT_TRUE(sim.empty());
    EXPECT_EQ(sim.get(Cell{0, 0, 0}), nullptr);
    EXPECT_EQ(sim.get(Cell{3, 0, 0}), nullptr);
    EXPECT_FALSE(result.updates.empty());
    for (const mc::FluidUpdate& update : result.updates) {
        EXPECT_EQ(update.fluid.type, FluidType::None);
    }
}

TEST(FluidSimulationTest, WaterTicksEveryFiveGameTicksAndLavaEveryThirty) {
    mc::FluidSimulation water_sim({}, floorAt(0));
    ASSERT_TRUE(water_sim.setSource({0, 0, 0}, FluidType::Water));
    EXPECT_TRUE(water_sim.tick(4).empty());
    EXPECT_EQ(water_sim.size(), static_cast<std::size_t>(1));
    EXPECT_FALSE(water_sim.tick(1).empty());
    EXPECT_GT(water_sim.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(water_sim.tickCount(), 5u);

    mc::FluidSimulation lava_sim({}, floorAt(0));
    ASSERT_TRUE(lava_sim.setSource({0, 0, 0}, FluidType::Lava));
    EXPECT_TRUE(lava_sim.tick(29).empty());
    EXPECT_EQ(lava_sim.size(), static_cast<std::size_t>(1));
    EXPECT_FALSE(lava_sim.tick(1).empty());
    EXPECT_EQ(lava_sim.tickCount(), 30u);
}

TEST(FluidSimulationTest, TickIntervalsComeFromTheRules) {
    mc::FluidRules rules;
    rules.water_tick_interval = 1;
    mc::FluidSimulation sim(rules, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    EXPECT_FALSE(sim.tick(1).empty());

    rules.water_tick_interval = 0; // treated as every tick
    mc::FluidSimulation fast(rules, floorAt(0));
    ASSERT_TRUE(fast.setSource({0, 0, 0}, FluidType::Water));
    EXPECT_FALSE(fast.tick(1).empty());
}

TEST(FluidSimulationTest, SameInputsProduceTheSameOutputTwice) {
    const auto build = []() {
        mc::FluidSimulation sim({}, floorAt(0));
        sim.setSource({0, 0, 0}, FluidType::Water);
        sim.setSource({9, 0, 0}, FluidType::Lava);
        return sim;
    };

    mc::FluidSimulation first = build();
    mc::FluidSimulation second = build();
    const mc::FluidTickResult first_result = first.tick(40);
    const mc::FluidTickResult second_result = second.tick(40);

    ASSERT_EQ(first_result.updates.size(), second_result.updates.size());
    for (std::size_t i = 0; i < first_result.updates.size(); ++i) {
        EXPECT_TRUE(first_result.updates[i].cell == second_result.updates[i].cell);
        EXPECT_EQ(first_result.updates[i].fluid.type, second_result.updates[i].fluid.type);
        EXPECT_EQ(first_result.updates[i].fluid.level, second_result.updates[i].fluid.level);
        EXPECT_EQ(first_result.updates[i].fluid.falling, second_result.updates[i].fluid.falling);
    }
    ASSERT_EQ(first_result.interactions.size(), second_result.interactions.size());
    for (std::size_t i = 0; i < first_result.interactions.size(); ++i) {
        EXPECT_TRUE(first_result.interactions[i].cell == second_result.interactions[i].cell);
        EXPECT_EQ(first_result.interactions[i].result, second_result.interactions[i].result);
    }
    EXPECT_FALSE(first_result.interactions.empty());
    EXPECT_EQ(first.size(), second.size());
}

TEST(FluidSimulationTest, SteppingTwiceWithoutChangesIsIdempotent) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    EXPECT_FALSE(sim.step().empty());
    EXPECT_TRUE(sim.step().empty());
    EXPECT_TRUE(sim.step().empty());
}

TEST(FluidSimulationTest, FallingIntoTheVoidIsBounded) {
    mc::FluidRules rules;
    rules.max_cells = 64;
    mc::FluidSimulation sim(rules, [](const Cell&) { return BlockId::Air; });
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    sim.step();
    EXPECT_GT(sim.size(), static_cast<std::size_t>(1));
    EXPECT_LE(sim.size(), static_cast<std::size_t>(64));
}

TEST(FluidSimulationTest, ClearDropsEverything) {
    mc::FluidSimulation sim({}, floorAt(0));
    ASSERT_TRUE(sim.setSource({0, 0, 0}, FluidType::Water));
    sim.step();
    sim.clear();
    EXPECT_TRUE(sim.empty());
    EXPECT_EQ(sim.get(Cell{0, 0, 0}), nullptr);
    EXPECT_TRUE(sim.step().empty());
}

TEST(FluidWaterGridTest, SurfaceAtUsesTheOriginAndTheSentinel) {
    mc::HostWaterGrid grid;
    grid.world_id = 7;
    grid.origin_x = -8;
    grid.origin_z = 8;

    // Nothing reported yet: every column is the kNoWater sentinel.
    EXPECT_TRUE(std::isnan(grid.surfaceAt(-8, 8)));

    grid.set(0, 0, 62.5f);
    grid.set(15, 15, 61.0f);
    EXPECT_FLOAT_EQ(grid.surfaceAt(-8, 8), 62.5f);
    EXPECT_FLOAT_EQ(grid.surfaceAt(7, 23), 61.0f);
    EXPECT_EQ(grid.world_id, 7u);

    // Out of the 16x16 window.
    EXPECT_TRUE(std::isnan(grid.surfaceAt(-9, 8)));
    EXPECT_TRUE(std::isnan(grid.surfaceAt(-8, 7)));
    EXPECT_TRUE(std::isnan(grid.surfaceAt(8, 8)));
    EXPECT_TRUE(std::isnan(grid.surfaceAt(-8, 24)));

    // An explicitly cleared column reports no water again.
    grid.set(4, 4, mc::kNoWater);
    EXPECT_TRUE(std::isnan(grid.surfaceAt(-4, 12)));
}

TEST(FluidWaterGridTest, DepthAndUnderwaterAtAPoint) {
    mc::HostWaterGrid grid;
    grid.origin_x = 0;
    grid.origin_z = 0;
    grid.set(0, 0, 62.5f);

    EXPECT_FLOAT_EQ(grid.depthIn(Cell{0, 62, 0}), 0.5f);
    EXPECT_FLOAT_EQ(grid.depthIn(Cell{0, 61, 0}), 1.0f);   // clamped to the cell
    EXPECT_FLOAT_EQ(grid.depthIn(Cell{0, 63, 0}), 0.0f);   // above the surface
    EXPECT_FLOAT_EQ(grid.depthIn(Cell{40, 62, 0}), 0.0f);  // outside the grid

    EXPECT_TRUE(grid.isUnderwater(mc::Vec3{0.5f, 62.0f, 0.5f}));
    EXPECT_TRUE(grid.isUnderwater(mc::Vec3{0.5f, 0.0f, 0.5f}));
    EXPECT_FALSE(grid.isUnderwater(mc::Vec3{0.5f, 62.5f, 0.5f}));
    EXPECT_FALSE(grid.isUnderwater(mc::Vec3{0.5f, 63.0f, 0.5f}));
    EXPECT_FALSE(grid.isUnderwater(mc::Vec3{100.5f, 0.0f, 0.5f}));
}

TEST(FluidSwimTest, DragConstantsMatchMinecraftWater) {
    const mc::SwimConstants swim;
    EXPECT_FLOAT_EQ(swim.horizontal_drag, 0.8f);
    EXPECT_FLOAT_EQ(swim.vertical_drag, 0.9f);
    EXPECT_NEAR(mc::kEntityGravity * swim.gravity_scale, 0.02f, 1.0e-6f);
    EXPECT_FLOAT_EQ(swim.buoyancy, 0.04f);

    const mc::Vec3 dragged = mc::applyWaterDrag(mc::Vec3{1.0f, -1.0f, 2.0f});
    EXPECT_FLOAT_EQ(dragged.x, 0.8f);
    EXPECT_FLOAT_EQ(dragged.y, -0.9f);
    EXPECT_FLOAT_EQ(dragged.z, 1.6f);
}
