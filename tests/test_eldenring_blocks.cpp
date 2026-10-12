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

TEST(Blocks, SneakEdgeProtectionAllowsMovementWhenFullySupported) {
    BlockGrid g;
    // 3x3 platform at y = 0 (top at y = 1)
    for (int x = -1; x <= 1; ++x) {
        for (int z = -1; z <= 1; ++z) {
            g.place({x, 0, z}, mc::BlockId::Stone);
        }
    }
    const float feet[3] = {0.f, 1.0f, 0.f};
    const auto res = clampSneakEdgeMovement(g, feet, 0.1f, 0.1f);
    EXPECT_FALSE(res.clamped_x);
    EXPECT_FALSE(res.clamped_z);
    EXPECT_NEAR(res.dx, 0.1f, 1e-4f);
    EXPECT_NEAR(res.dz, 0.1f, 1e-4f);
}

TEST(Blocks, SneakEdgeProtectionStopsPlayerAtBlockEdge) {
    BlockGrid g;
    // Single block at (0, 0, 0), top at y = 1
    g.place({0, 0, 0}, mc::BlockId::Stone);
    const float feet[3] = {0.5f, 1.0f, 0.5f};
    // Trying to walk +1.0m in X towards the void
    const auto res = clampSneakEdgeMovement(g, feet, 1.0f, 0.f);
    EXPECT_TRUE(res.clamped_x);
    EXPECT_FALSE(res.clamped_z);
    EXPECT_LT(res.dx, 1.0f);
    EXPECT_GT(res.dx, 0.f);
    // The resulting position must still be supported on the block ledge!
    const float new_feet[3] = {feet[0] + res.dx, feet[1], feet[2] + res.dz};
    EXPECT_TRUE(supportedByBlock(g, new_feet));
    // And advancing even 2cm further would fall off
    const float over_edge[3] = {new_feet[0] + 0.05f, new_feet[1], new_feet[2]};
    EXPECT_FALSE(supportedByBlock(g, over_edge));
}

TEST(Blocks, SneakEdgeProtectionSlidesAlongEdgeWhenMovingDiagonally) {
    BlockGrid g;
    // 1-block wide bridge along Z: (0, 0, 0), (0, 0, 1), (0, 0, 2)
    g.place({0, 0, 0}, mc::BlockId::Stone);
    g.place({0, 0, 1}, mc::BlockId::Stone);
    g.place({0, 0, 2}, mc::BlockId::Stone);

    const float feet[3] = {0.5f, 1.0f, 0.5f};
    // Trying to move diagonally (+0.8 in X towards void, +0.3 in Z along bridge)
    const auto res = clampSneakEdgeMovement(g, feet, 0.8f, 0.3f);
    EXPECT_TRUE(res.clamped_x);
    EXPECT_FALSE(res.clamped_z); // Z is unhindered along the bridge
    EXPECT_NEAR(res.dz, 0.3f, 1e-4f);
    const float new_feet[3] = {feet[0] + res.dx, feet[1], feet[2] + res.dz};
    EXPECT_TRUE(supportedByBlock(g, new_feet));
}

TEST(Blocks, SneakEdgeProtectionDoesNotClampWhenAlreadyInAir) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    // Player is 5m in the air above the block
    const float feet[3] = {0.5f, 5.0f, 0.5f};
    const auto res = clampSneakEdgeMovement(g, feet, 0.5f, -0.5f);
    EXPECT_FALSE(res.clamped_x);
    EXPECT_FALSE(res.clamped_z);
    EXPECT_FLOAT_EQ(res.dx, 0.5f);
    EXPECT_FLOAT_EQ(res.dz, -0.5f);
}

TEST(Blocks, SneakEdgeProtectionCatchesPlayerWhoSteppedIntoAirFromPreviousBlock) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone); // Block from [0, 1] x [0, 1], top y = 1
    // Player was at (0.9, 1.0, 0.5) on the block
    const float prev[3] = {0.9f, 1.0f, 0.5f};
    EXPECT_TRUE(supportedByBlock(g, prev));

    // Next frame, Havok moved player by +0.5m in X to (1.4, 1.0, 0.5), which is in empty air!
    const float over_feet[3] = {1.4f, 1.0f, 0.5f};
    EXPECT_FALSE(supportedByBlock(g, over_feet)); // stepped off!

    // Clamping movement from prev restores to safe ledge:
    const float desired_dx = over_feet[0] - prev[0];
    const float desired_dz = over_feet[2] - prev[2];
    const auto res = clampSneakEdgeMovement(g, prev, desired_dx, desired_dz);
    EXPECT_TRUE(res.clamped_x);
    EXPECT_LT(res.dx, desired_dx);

    const float clamped_feet[3] = {prev[0] + res.dx, prev[1], prev[2] + res.dz};
    EXPECT_TRUE(supportedByBlock(g, clamped_feet)); // Restored to safe ledge!
}

TEST(Blocks, AnchorBlocksPositionOnlyWhenNoMovementKeysPressed) {
    // When any movement key is pressed (W, A, S, D, Space), anchor must be released (return false)
    EXPECT_FALSE(shouldAnchorBlockPosition(true, true, false, false, false, false)); // W
    EXPECT_FALSE(shouldAnchorBlockPosition(true, false, true, false, false, false)); // A
    EXPECT_FALSE(shouldAnchorBlockPosition(true, false, false, true, false, false)); // S
    EXPECT_FALSE(shouldAnchorBlockPosition(true, false, false, false, true, false)); // D
    EXPECT_FALSE(shouldAnchorBlockPosition(true, false, false, false, false, true)); // Space

    // When supported and no keys pressed in foreground, should anchor (return true)
    EXPECT_TRUE(shouldAnchorBlockPosition(true, false, false, false, false, false));

    // When not supported by block, must not anchor regardless of keys
    EXPECT_FALSE(shouldAnchorBlockPosition(false, false, false, false, false, false));
}

TEST(Blocks, StepBlockMovementAdvancesPositionWhenMoving) {
    const float feet[3] = {10.0f, 1.0f, 20.0f};
    const float dir[2] = {1.0f, 0.0f}; // moving +X
    const float speed = 4.317f;
    const float dt = 0.0166667f;

    float out_feet[3] = {};
    const bool moved = stepBlockMovement(feet, dir, speed, dt, true, out_feet);
    EXPECT_TRUE(moved);
    EXPECT_NEAR(out_feet[0], 10.0f + speed * dt, 1e-4f);
    EXPECT_FLOAT_EQ(out_feet[1], 1.0f);
    EXPECT_FLOAT_EQ(out_feet[2], 20.0f);
}

TEST(Blocks, StepBlockMovementStationaryWhenNoDirectionOrZeroSpeed) {
    const float feet[3] = {10.0f, 1.0f, 20.0f};
    const float dir[2] = {0.0f, 0.0f};
    float out_feet[3] = {};
    const bool moved = stepBlockMovement(feet, dir, 4.317f, 0.0166667f, false, out_feet);
    EXPECT_FALSE(moved);
    EXPECT_FLOAT_EQ(out_feet[0], 10.0f);
    EXPECT_FLOAT_EQ(out_feet[1], 1.0f);
    EXPECT_FLOAT_EQ(out_feet[2], 20.0f);
}

TEST(Blocks, ShouldSyncPhysicsProxyTriggersOnMovedOrDirectDriven) {
    EXPECT_TRUE(shouldSyncPhysicsProxy(true, false, false, false)); // collision pushed
    EXPECT_TRUE(shouldSyncPhysicsProxy(false, true, false, false)); // direct driven
    EXPECT_TRUE(shouldSyncPhysicsProxy(false, false, true, false)); // sneak clamped
    EXPECT_TRUE(shouldSyncPhysicsProxy(false, false, false, true)); // pinned
    EXPECT_FALSE(shouldSyncPhysicsProxy(false, false, false, false));
}

TEST(Blocks, StepBlockMovementAdvancesDiagonally) {
    const float feet[3] = {5.0f, 2.0f, 5.0f};
    const float dir[2] = {0.7071f, 0.7071f};
    const float speed = 5.612f; // sprint
    const float dt = 0.016f;

    float out_feet[3] = {};
    const bool moved = stepBlockMovement(feet, dir, speed, dt, true, out_feet);
    EXPECT_TRUE(moved);
    EXPECT_NEAR(out_feet[0], 5.0f + 0.7071f * speed * dt, 1e-4f);
    EXPECT_FLOAT_EQ(out_feet[1], 2.0f);
    EXPECT_NEAR(out_feet[2], 5.0f + 0.7071f * speed * dt, 1e-4f);
}




