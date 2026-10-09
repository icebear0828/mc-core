#include <gtest/gtest.h>

#include "eldenring_blockicon.hpp"

using namespace eldenring::blocks;

TEST(BlockIcon, ACubeShowsThreeFacesTopAndTwoSides) {
    const IconCube icon = blockIcon(mc::BlockId::Stone);
    ASSERT_EQ(icon.count, 3);
    int tops = 0;
    for (int i = 0; i < icon.count; ++i) tops += icon.faces[i].is_top ? 1 : 0;
    EXPECT_EQ(tops, 1);
}

TEST(BlockIcon, TheIconFitsTheUnitSquareAndIsCentred) {
    const IconCube icon = blockIcon(mc::BlockId::Dirt);
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (int i = 0; i < icon.count; ++i) {
        for (int k = 0; k < 4; ++k) {
            const float x = icon.faces[i].p[k][0], y = icon.faces[i].p[k][1];
            lo_x = std::min(lo_x, x);
            hi_x = std::max(hi_x, x);
            lo_y = std::min(lo_y, y);
            hi_y = std::max(hi_y, y);
        }
    }
    EXPECT_GE(lo_x, 0.f);
    EXPECT_LE(hi_x, 1.f);
    EXPECT_GE(lo_y, 0.f);
    EXPECT_LE(hi_y, 1.f);
    EXPECT_NEAR((lo_x + hi_x) * 0.5f, 0.5f, 0.02f);
    EXPECT_NEAR((lo_y + hi_y) * 0.5f, 0.5f, 0.02f);
    EXPECT_GT(hi_x - lo_x, 0.75f); // fills the slot like the vanilla icon (the hexagon is taller than wide)
}

TEST(BlockIcon, TheTopIsBrightestAndTheSidesAreShaded) {
    const IconCube icon = blockIcon(mc::BlockId::Stone);
    float top = 0.f, lo = 2.f, hi = 0.f;
    for (int i = 0; i < icon.count; ++i) {
        if (icon.faces[i].is_top) top = icon.faces[i].shade;
        else {
            lo = std::min(lo, icon.faces[i].shade);
            hi = std::max(hi, icon.faces[i].shade);
        }
    }
    EXPECT_FLOAT_EQ(top, 1.0f);
    EXPECT_LT(hi, 1.0f);
    EXPECT_LT(lo, hi); // the two sides differ
}

TEST(BlockIcon, TntTopUsesTheTopCellAndTheSidesUseTheSideCell) {
    const IconCube icon = blockIcon(mc::BlockId::Tnt);
    for (int i = 0; i < icon.count; ++i) {
        const mc::hud::HudUV& want = icon.faces[i].is_top ? mc::hud::kUV_BLOCK_TNT_TOP : mc::hud::kUV_BLOCK_TNT_SIDE;
        EXPECT_FLOAT_EQ(icon.faces[i].uv.u0, want.u0);
        EXPECT_FLOAT_EQ(icon.faces[i].uv.v1, want.v1);
    }
}

TEST(BlockIcon, TheFacesDoNotOverlapMuch) {
    // the three parallelograms tile a hexagon: their areas add up to the hexagon's area (no gap, no double cover)
    const IconCube icon = blockIcon(mc::BlockId::Dirt);
    float total = 0.f;
    for (int i = 0; i < icon.count; ++i) {
        const auto& p = icon.faces[i].p;
        float a = 0.f;
        for (int k = 0; k < 4; ++k) a += p[k][0] * p[(k + 1) % 4][1] - p[(k + 1) % 4][0] * p[k][1];
        total += std::fabs(a) * 0.5f;
    }
    float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
    for (int i = 0; i < icon.count; ++i)
        for (int k = 0; k < 4; ++k) {
            lo_x = std::min(lo_x, icon.faces[i].p[k][0]);
            hi_x = std::max(hi_x, icon.faces[i].p[k][0]);
            lo_y = std::min(lo_y, icon.faces[i].p[k][1]);
            hi_y = std::max(hi_y, icon.faces[i].p[k][1]);
        }
    const float box = (hi_x - lo_x) * (hi_y - lo_y);
    EXPECT_GT(total, 0.6f * box);  // a cube's hexagon fills about 3/4 of its bounding box
    EXPECT_LT(total, 0.85f * box);
}
