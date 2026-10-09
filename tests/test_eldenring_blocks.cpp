#include <gtest/gtest.h>

#include "eldenring_blocks.hpp"

using namespace eldenring::blocks;

TEST(Blocks, CellOfFloorsEvenForNegativeCoordinates) {
    EXPECT_EQ(cellOf(0.2f, 5.9f, -0.1f), (Cell{0, 5, -1}));
    EXPECT_EQ(cellOf(-3.0f, 0.0f, 2.999f), (Cell{-3, 0, 2}));
}

TEST(Blocks, PlaceRemoveAndLookUp) {
    BlockGrid g;
    EXPECT_TRUE(g.place({1, 2, 3}, mc::BlockId::Dirt));
    EXPECT_FALSE(g.place({1, 2, 3}, mc::BlockId::Stone)); // occupied
    EXPECT_EQ(g.get({1, 2, 3}), mc::BlockId::Dirt);
    EXPECT_EQ(g.get({0, 0, 0}), mc::BlockId::Air);
    EXPECT_EQ(g.count(), 1u);
    EXPECT_TRUE(g.remove({1, 2, 3}));
    EXPECT_FALSE(g.remove({1, 2, 3}));
    EXPECT_EQ(g.count(), 0u);
}

TEST(Blocks, ThereIsACapOnTheNumberOfBlocks) {
    BlockGrid g;
    for (int i = 0; i < BlockGrid::kMaxBlocks; ++i) ASSERT_TRUE(g.place({i, 0, 0}, mc::BlockId::Dirt));
    EXPECT_FALSE(g.place({0, 1, 0}, mc::BlockId::Dirt));
}

TEST(Blocks, ChangeCounterTicksOnEveryChange) {
    BlockGrid g;
    const unsigned v0 = g.version();
    g.place({0, 0, 0}, mc::BlockId::Dirt);
    const unsigned v1 = g.version();
    EXPECT_NE(v0, v1);
    g.place({0, 0, 0}, mc::BlockId::Dirt); // refused: no change
    EXPECT_EQ(g.version(), v1);
    g.remove({0, 0, 0});
    EXPECT_NE(g.version(), v1);
}

TEST(Blocks, RayHitsTheNearestBlockAndReportsTheFace) {
    BlockGrid g;
    g.place({5, 0, 0}, mc::BlockId::Stone);
    g.place({8, 0, 0}, mc::BlockId::Stone);
    const float origin[3] = {0.5f, 0.5f, 0.5f};
    const float dir[3] = {1.f, 0.f, 0.f};
    const auto hit = raycastGrid(g, origin, dir, 20.f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->cell, (Cell{5, 0, 0}));
    EXPECT_NEAR(hit->distance, 4.5f, 1e-3f);
    EXPECT_EQ(hit->normal[0], -1); // entered through the -x face
    EXPECT_EQ(hit->normal[1], 0);
}

TEST(Blocks, RayRespectsTheReach) {
    BlockGrid g;
    g.place({10, 0, 0}, mc::BlockId::Dirt);
    const float origin[3] = {0.5f, 0.5f, 0.5f};
    const float dir[3] = {1.f, 0.f, 0.f};
    EXPECT_FALSE(raycastGrid(g, origin, dir, 5.f).has_value());
    EXPECT_TRUE(raycastGrid(g, origin, dir, 10.f).has_value());
}

TEST(Blocks, RayWorksDiagonallyAndDownwards) {
    BlockGrid g;
    g.place({3, -2, 3}, mc::BlockId::Dirt);
    const float origin[3] = {0.5f, 0.5f, 0.5f};
    float dir[3] = {1.f, -0.8f, 1.f};
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
    for (float& c : dir) c /= len;
    const auto hit = raycastGrid(g, origin, dir, 20.f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->cell, (Cell{3, -2, 3}));
}

TEST(Blocks, RayStartingInsideABlockHitsItAtZero) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Dirt);
    const float origin[3] = {0.5f, 0.5f, 0.5f};
    const float dir[3] = {0.f, 1.f, 0.f};
    const auto hit = raycastGrid(g, origin, dir, 5.f);
    ASSERT_TRUE(hit.has_value());
    EXPECT_FLOAT_EQ(hit->distance, 0.f);
}

TEST(Blocks, PlacementOnABlockFaceIsTheNeighbourCell) {
    GridHit h;
    h.cell = {5, 0, 0};
    h.normal[0] = -1;
    EXPECT_EQ(placementCellForBlockHit(h), (Cell{4, 0, 0}));
}

TEST(Blocks, PlacementOnTerrainSitsOnTheSurface) {
    // the floor at y = 5.0 with an upward normal: the block occupies 5..6
    const float pos[3] = {2.3f, 5.0f, 7.8f}, normal[3] = {0.f, 1.f, 0.f};
    EXPECT_EQ(placementCellForWorldHit(pos, normal), (Cell{2, 5, 7}));
    // a wall at x = 3.0 facing -x: the block is on the near side, 2..3
    const float wall[3] = {3.0f, 1.4f, 0.5f}, wn[3] = {-1.f, 0.f, 0.f};
    EXPECT_EQ(placementCellForWorldHit(wall, wn), (Cell{2, 1, 0}));
    // a hit on a slope: the dominant normal axis decides
    const float slope[3] = {1.2f, 3.0f, 1.2f}, sn[3] = {0.3f, 0.9f, 0.3f};
    EXPECT_EQ(placementCellForWorldHit(slope, sn).y, 3);
}

TEST(Blocks, ACellInsideThePlayerCannotBeBuilt) {
    const float feet[3] = {0.5f, 0.0f, 0.5f};
    EXPECT_TRUE(cellTouchesPlayer({0, 0, 0}, feet));
    EXPECT_TRUE(cellTouchesPlayer({0, 1, 0}, feet));   // the upper body
    EXPECT_FALSE(cellTouchesPlayer({0, 2, 0}, feet));  // above the head
    EXPECT_FALSE(cellTouchesPlayer({2, 0, 0}, feet));
    EXPECT_FALSE(cellTouchesPlayer({0, -1, 0}, feet)); // the ground under the feet is fine
}

TEST(Blocks, PlayerStandingOnABlockIsLiftedToItsTop) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    float feet[3] = {0.5f, 0.9f, 0.5f}; // sunk 10 cm into the block (top at y = 1)
    const Resolve r = resolvePlayer(g, feet);
    EXPECT_TRUE(r.moved);
    EXPECT_TRUE(r.standing);
    EXPECT_NEAR(r.feet[1], 1.0f, 1e-4f);
    EXPECT_NEAR(r.feet[0], 0.5f, 1e-4f);
}

TEST(Blocks, PlayerWalkingIntoAWallIsPushedBack) {
    BlockGrid g;
    for (int y = 0; y < 3; ++y) g.place({2, y, 0}, mc::BlockId::Stone);
    float feet[3] = {1.8f, 0.0f, 0.5f}; // half width 0.3: reaches x = 2.1, 10 cm into the wall
    const Resolve r = resolvePlayer(g, feet);
    EXPECT_TRUE(r.moved);
    EXPECT_FALSE(r.standing);
    EXPECT_NEAR(r.feet[0], 1.7f, 1e-3f);
    EXPECT_NEAR(r.feet[1], 0.0f, 1e-4f);
}

TEST(Blocks, FreePlayerIsNotTouched) {
    BlockGrid g;
    g.place({5, 5, 5}, mc::BlockId::Stone);
    float feet[3] = {0.5f, 0.0f, 0.5f};
    const Resolve r = resolvePlayer(g, feet);
    EXPECT_FALSE(r.moved);
    EXPECT_EQ(r.feet[0], feet[0]);
}

TEST(Blocks, HeadHittingABlockPushesDown) {
    BlockGrid g;
    g.place({0, 2, 0}, mc::BlockId::Stone); // underside at y = 2, the head is at 1.8
    float feet[3] = {0.5f, 0.3f, 0.5f};     // head at 2.1: 10 cm into the block
    const Resolve r = resolvePlayer(g, feet);
    EXPECT_TRUE(r.moved);
    EXPECT_NEAR(r.feet[1], 0.2f, 1e-3f);
    EXPECT_FALSE(r.standing);
}

TEST(Blocks, MovedFarIsRejectedAsATeleportGuard) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    float feet[3] = {0.5f, 0.05f, 0.5f}; // would need 0.95 up: allowed (standing on a ledge)
    EXPECT_TRUE(resolvePlayer(g, feet).moved);
    // deep inside a 3x3x3 solid: refuse to teleport the player out
    BlockGrid solid;
    for (int x = -1; x <= 1; ++x)
        for (int y = 0; y <= 2; ++y)
            for (int z = -1; z <= 1; ++z) solid.place({x, y, z}, mc::BlockId::Stone);
    float inside[3] = {0.5f, 0.5f, 0.5f};
    EXPECT_FALSE(resolvePlayer(solid, inside).moved);
}

TEST(Blocks, ItemsMapToBlocks) {
    EXPECT_EQ(blockForItem(mc::ItemId::BlockDirt), mc::BlockId::Dirt);
    EXPECT_EQ(blockForItem(mc::ItemId::BlockStone), mc::BlockId::Stone);
    EXPECT_EQ(blockForItem(mc::ItemId::BlockTnt), mc::BlockId::Tnt);
    EXPECT_EQ(blockForItem(mc::ItemId::DiamondSword), mc::BlockId::Air);
    EXPECT_EQ(itemForBlock(mc::BlockId::Stone), mc::ItemId::BlockStone);
}

TEST(Blocks, FallingThroughABlockTopIsCaughtAndStandsOnIt) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone); // top at y = 1
    const float prev[3] = {0.5f, 3.0f, 0.5f};
    const float cur[3] = {0.5f, -4.0f, 0.5f}; // a hitch: seven metres in one frame, far below the block
    const Resolve r = resolvePlayerSwept(g, prev, cur);
    EXPECT_TRUE(r.moved);
    EXPECT_TRUE(r.standing);
    EXPECT_NEAR(r.feet[1], 1.0f, 1e-4f);
}

TEST(Blocks, FallingBesideABlockIsNotCaught) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    const float prev[3] = {2.5f, 3.0f, 0.5f};
    const float cur[3] = {2.5f, -4.0f, 0.5f};
    EXPECT_FALSE(resolvePlayerSwept(g, prev, cur).moved);
}

TEST(Blocks, FallingOntoTheHighestBlockOfAColumnStopsOnTheFirstOne) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    g.place({0, 2, 0}, mc::BlockId::Stone); // top at 3
    const float prev[3] = {0.5f, 5.0f, 0.5f};
    const float cur[3] = {0.5f, -1.0f, 0.5f};
    const Resolve r = resolvePlayerSwept(g, prev, cur);
    EXPECT_NEAR(r.feet[1], 3.0f, 1e-4f);
}

TEST(Blocks, SweptFallsBackToTheNormalPushWhenNotFalling) {
    BlockGrid g;
    for (int y = 0; y < 3; ++y) g.place({2, y, 0}, mc::BlockId::Stone);
    const float prev[3] = {1.0f, 0.0f, 0.5f};
    const float cur[3] = {1.8f, 0.0f, 0.5f};
    const Resolve r = resolvePlayerSwept(g, prev, cur);
    EXPECT_TRUE(r.moved);
    EXPECT_NEAR(r.feet[0], 1.7f, 1e-3f);
}

TEST(Blocks, ACeilingRightAboveTheBlockTopDoesNotTrapThePlayer) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    g.place({0, 2, 0}, mc::BlockId::Stone); // only 1 m of room above the first block: the 1.8 m player does not fit there
    const float prev[3] = {0.5f, 1.0f, 0.5f};
    const float cur[3] = {0.5f, 0.2f, 0.5f};
    const Resolve r = resolvePlayerSwept(g, prev, cur);
    EXPECT_FALSE(r.standing && std::fabs(r.feet[1] - 1.0f) < 1e-3f); // not placed into the pocket
}

TEST(Blocks, StandingOnABlockTopCountsAsSupported) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone); // top at y = 1
    const float on[3] = {0.5f, 1.0f, 0.5f};
    const float just_above[3] = {0.5f, 1.03f, 0.5f};
    const float jumping[3] = {0.5f, 1.4f, 0.5f};
    const float beside[3] = {2.5f, 1.0f, 0.5f};
    const float inside[3] = {0.5f, 0.7f, 0.5f};
    EXPECT_TRUE(supportedByBlock(g, on));
    EXPECT_TRUE(supportedByBlock(g, just_above));
    EXPECT_FALSE(supportedByBlock(g, jumping));
    EXPECT_FALSE(supportedByBlock(g, beside));
    EXPECT_FALSE(supportedByBlock(g, inside)); // sunk: the push handles this, it is not "standing"
}
