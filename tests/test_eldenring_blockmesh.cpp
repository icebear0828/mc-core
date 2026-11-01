#include <gtest/gtest.h>

#include "eldenring_blockmesh.hpp"

using namespace eldenring::blocks;

namespace {
bool inside(const mc::hud::HudUV& uv, const mc::rig::RigVertex& v) {
    return v.u >= uv.u0 && v.u <= uv.u1 && v.v >= uv.v0 && v.v <= uv.v1;
}
} // namespace

TEST(BlockMesh, ASingleBlockHasSixFaces) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Dirt);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    EXPECT_EQ(m.vertices.size(), 24u);
    EXPECT_EQ(m.indices.size(), 36u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_GE(v.x, 0.f);
        EXPECT_LE(v.x, 1.f);
        EXPECT_GE(v.y, 0.f);
        EXPECT_LE(v.y, 1.f);
        EXPECT_TRUE(inside(mc::hud::kUV_BLOCK_DIRT, v));
    }
}

TEST(BlockMesh, FacesBetweenNeighboursAreNotBuilt) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Stone);
    g.place({1, 0, 0}, mc::BlockId::Stone);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    EXPECT_EQ(m.vertices.size(), 10u * 4u); // 12 faces minus the two touching ones
    EXPECT_EQ(m.indices.size(), 10u * 6u);
}

TEST(BlockMesh, TntHasDifferentTopSideAndBottom) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Tnt);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    int top = 0, side = 0, bottom = 0;
    for (const mc::rig::RigVertex& v : m.vertices) {
        if (inside(mc::hud::kUV_BLOCK_TNT_TOP, v)) ++top;
        else if (inside(mc::hud::kUV_BLOCK_TNT_BOTTOM, v)) ++bottom;
        else if (inside(mc::hud::kUV_BLOCK_TNT_SIDE, v)) ++side;
    }
    EXPECT_EQ(top, 4);
    EXPECT_EQ(bottom, 4);
    EXPECT_EQ(side, 16);
}

TEST(BlockMesh, CraftingTableUsesItsOwnTopAndFrontFaces) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::CraftingTable);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    EXPECT_EQ(m.vertices.size(), 24u);
    int top = 0, front = 0;
    for (const mc::rig::RigVertex& v : m.vertices) {
        if (inside(mc::hud::kUV_BLOCK_CRAFTING_TABLE_TOP, v)) ++top;
        else if (inside(mc::hud::kUV_BLOCK_CRAFTING_TABLE_FRONT, v)) ++front;
    }
    EXPECT_EQ(top, 8);   // top and bottom
    EXPECT_EQ(front, 16); // four sides
}

TEST(BlockMesh, BuildableBlocksUseTheirOwnFaceCells) {
    struct Case {
        mc::BlockId id;
        const mc::hud::HudUV* uv;
    };
    const Case cases[] = {{mc::BlockId::Sand, &mc::hud::kUV_BLOCK_SAND},
                          {mc::BlockId::Gravel, &mc::hud::kUV_BLOCK_GRAVEL},
                          {mc::BlockId::WoodPlanks, &mc::hud::kUV_BLOCK_PLANKS},
                          {mc::BlockId::StoneBricks, &mc::hud::kUV_BLOCK_STONE_BRICKS},
                          {mc::BlockId::Bricks, &mc::hud::kUV_BLOCK_BRICKS},
                          {mc::BlockId::WhiteWool, &mc::hud::kUV_BLOCK_WHITE_WOOL}};
    for (const Case& c : cases) {
        BlockGrid g;
        g.place({0, 0, 0}, c.id);
        const mc::rig::RigMesh m = buildBlockMesh(g);
        EXPECT_EQ(m.vertices.size(), 24u) << static_cast<int>(c.id);
        for (const mc::rig::RigVertex& v : m.vertices) EXPECT_TRUE(inside(*c.uv, v)) << static_cast<int>(c.id);
    }
}

TEST(BlockMesh, AnvilUsesItsTopAndSideFaces) {
    BlockGrid g;
    g.place({0, 0, 0}, mc::BlockId::Anvil);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    EXPECT_EQ(m.vertices.size(), 24u);
    int top = 0, side = 0;
    for (const mc::rig::RigVertex& v : m.vertices) {
        if (inside(mc::hud::kUV_BLOCK_ANVIL_TOP, v)) ++top;
        else if (inside(mc::hud::kUV_BLOCK_ANVIL_SIDE, v)) ++side;
    }
    EXPECT_EQ(top, 8);   // top and bottom
    EXPECT_EQ(side, 16); // four sides
}

TEST(BlockMesh, TheTopFaceIsAtTheTopOfTheCell) {
    BlockGrid g;
    g.place({3, 7, -2}, mc::BlockId::Dirt);
    const mc::rig::RigMesh m = buildBlockMesh(g);
    int at_top = 0;
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_GE(v.x, 3.f);
        EXPECT_LE(v.x, 4.f);
        EXPECT_GE(v.z, -2.f);
        EXPECT_LE(v.z, -1.f);
        if (v.y == 8.f) ++at_top;
    }
    EXPECT_GE(at_top, 4);
}

TEST(BlockMesh, IndicesStayInRangeForAFullGrid) {
    BlockGrid g;
    int n = 0;
    for (int x = 0; x < 20 && n < BlockGrid::kMaxBlocks; ++x)
        for (int z = 0; z < 20 && n < BlockGrid::kMaxBlocks; ++z)
            for (int y = 0; y < 6 && n < BlockGrid::kMaxBlocks; ++y, ++n) g.place({x * 2, y * 2, z * 2}, mc::BlockId::Stone); // all faces exposed
    const mc::rig::RigMesh m = buildBlockMesh(g);
    EXPECT_LE(m.vertices.size(), 65535u);
    for (uint16_t i : m.indices) EXPECT_LT(i, m.vertices.size());
}

TEST(BlockMesh, EmptyGridGivesAnEmptyMesh) {
    BlockGrid g;
    EXPECT_TRUE(buildBlockMesh(g).vertices.empty());
}

TEST(BlockMesh, TheHeldCubeIsCentredOnTheOrigin) {
    const mc::rig::RigMesh m = buildCubeMesh(mc::BlockId::Stone);
    ASSERT_EQ(m.vertices.size(), 24u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_GE(v.x, -0.5f);
        EXPECT_LE(v.x, 0.5f);
        EXPECT_GE(v.y, -0.5f);
        EXPECT_LE(v.y, 0.5f);
        EXPECT_TRUE(inside(mc::hud::kUV_BLOCK_STONE, v));
    }
}

TEST(BlockMesh, TheThirdPersonHeldCubeUsesTheBlockFaces) {
    const mc::rig::HostBasis basis{{0.f, 0.f, 1.f}, {-1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, 0.01f};
    const mc::rig::RigMesh m = buildHeldCubeMesh(mc::BlockId::Tnt, basis);
    ASSERT_EQ(m.vertices.size(), 24u);
    int top = 0, side = 0;
    for (const mc::rig::RigVertex& v : m.vertices) {
        if (inside(mc::hud::kUV_BLOCK_TNT_TOP, v)) ++top;
        if (inside(mc::hud::kUV_BLOCK_TNT_SIDE, v)) ++side;
    }
    EXPECT_EQ(top, 4);
    EXPECT_EQ(side, 16);
}

TEST(BlockMesh, TerrainMiningCrackQuadSitsOnTheSurface) {
    BlockGrid g;
    TerrainMiningOverlay o{};
    o.pos[0] = 1.f; o.pos[1] = 2.f; o.pos[2] = 3.f;
    o.normal[0] = 0.f; o.normal[1] = 1.f; o.normal[2] = 0.f;
    o.stage = 4;
    const mc::rig::RigMesh m = buildBlockMesh(g, {}, std::nullopt, o);
    ASSERT_EQ(m.vertices.size(), 4u);
    ASSERT_EQ(m.indices.size(), 6u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_NEAR(v.x, 1.f, 0.46f);
        EXPECT_NEAR(v.y, 2.01f, 1e-3f);
        EXPECT_NEAR(v.z, 3.f, 0.46f);
        EXPECT_TRUE(inside(mc::hud::kUV_DESTROY_STAGE_4, v));
    }
}

TEST(BlockMesh, TerrainMiningCrackFollowsAVerticalNormal) {
    BlockGrid g;
    TerrainMiningOverlay o{};
    o.pos[0] = 1.f; o.pos[1] = 2.f; o.pos[2] = 3.f;
    o.normal[0] = 0.f; o.normal[1] = 0.f; o.normal[2] = 1.f;
    o.stage = 9;
    const mc::rig::RigMesh m = buildBlockMesh(g, {}, std::nullopt, o);
    ASSERT_EQ(m.vertices.size(), 4u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_NEAR(v.x, 1.f, 0.46f);
        EXPECT_NEAR(v.y, 2.f, 0.46f);
        EXPECT_NEAR(v.z, 3.01f, 1e-3f);
    }
}

TEST(BlockMesh, TerrainMiningCrackIsAbsentWithoutAStage) {
    BlockGrid g;
    TerrainMiningOverlay o{};
    o.pos[0] = 1.f; o.pos[1] = 2.f; o.pos[2] = 3.f;
    o.stage = -1;
    EXPECT_TRUE(buildBlockMesh(g, {}, std::nullopt, o).vertices.empty());
}

TEST(BlockMesh, TerrainMiningCrackLiftAndSizeAreConfigurable) {
    BlockGrid g;
    TerrainMiningOverlay o{};
    o.pos[0] = 1.f; o.pos[1] = 2.f; o.pos[2] = 3.f;
    o.stage = 4;
    o.lift = -0.1f;
    o.half_size = 0.2f;
    const mc::rig::RigMesh m = buildBlockMesh(g, {}, std::nullopt, o);
    ASSERT_EQ(m.vertices.size(), 4u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_NEAR(v.y, 1.9f, 1e-3f);
        EXPECT_NEAR(v.x, 1.f, 0.21f);
        EXPECT_NEAR(v.z, 3.f, 0.21f);
    }
}

TEST(BlockMesh, TerrainCrackMeshIsSeparateAndUsesTheSameQuad) {
    TerrainMiningOverlay o{};
    o.pos[0] = 1.f; o.pos[1] = 2.f; o.pos[2] = 3.f;
    o.stage = 4;
    const mc::rig::RigMesh m = buildTerrainCrackMesh(o);
    ASSERT_EQ(m.vertices.size(), 4u);
    ASSERT_EQ(m.indices.size(), 6u);
    for (const mc::rig::RigVertex& v : m.vertices) {
        EXPECT_NEAR(v.y, 2.01f, 1e-3f);
        EXPECT_TRUE(inside(mc::hud::kUV_DESTROY_STAGE_4, v));
    }
}
