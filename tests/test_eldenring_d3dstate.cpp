#include <gtest/gtest.h>

#include "eldenring_d3dstate.hpp"

using eldenring::d3d::describeResourceState;

TEST(EldenRingD3dState, NamesTheCommonStates) {
    EXPECT_EQ(describeResourceState(0x0), "COMMON/PRESENT");
    EXPECT_EQ(describeResourceState(0x10), "DEPTH_WRITE");
    EXPECT_EQ(describeResourceState(0x20), "DEPTH_READ");
    EXPECT_EQ(describeResourceState(0x4), "RENDER_TARGET");
    EXPECT_EQ(describeResourceState(0x800), "COPY_SOURCE");
    EXPECT_EQ(describeResourceState(0x400), "COPY_DEST");
}

TEST(EldenRingD3dState, CombinedReadStatesListEveryBit) {
    // the depth buffer read by the lighting pass: depth read plus both shader resource kinds
    EXPECT_EQ(describeResourceState(0x20 | 0x40 | 0x80), "DEPTH_READ|NON_PIXEL_SHADER_RESOURCE|PIXEL_SHADER_RESOURCE");
    EXPECT_EQ(describeResourceState(0xC0), "NON_PIXEL_SHADER_RESOURCE|PIXEL_SHADER_RESOURCE");
}

TEST(EldenRingD3dState, AnUnknownBitIsShownInHexInsteadOfBeingDropped) {
    EXPECT_EQ(describeResourceState(0x10 | 0x80000), "DEPTH_WRITE|0x80000");
}
