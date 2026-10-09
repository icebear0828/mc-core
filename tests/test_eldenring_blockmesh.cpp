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
