#include <gtest/gtest.h>

#include "eldenring_pixel.hpp"

using namespace eldenring::pixel;

TEST(EldenRingPixel, R10G10B10A2UnpacksTheThreeTenBitChannelsToEightBits) {
    // R in bits 0-9, G in 10-19, B in 20-29, A in 30-31 (DXGI_FORMAT_R10G10B10A2_UNORM)
    auto pack = [](uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return r | (g << 10) | (b << 20) | (a << 30); };
    uint8_t rgb[3];
    unpackR10G10B10A2(pack(0, 0, 0, 3), rgb);
    EXPECT_EQ(rgb[0], 0); EXPECT_EQ(rgb[1], 0); EXPECT_EQ(rgb[2], 0);
    unpackR10G10B10A2(pack(1023, 1023, 1023, 0), rgb);
    EXPECT_EQ(rgb[0], 255); EXPECT_EQ(rgb[1], 255); EXPECT_EQ(rgb[2], 255);
    unpackR10G10B10A2(pack(1023, 0, 0, 3), rgb);
    EXPECT_EQ(rgb[0], 255); EXPECT_EQ(rgb[1], 0); EXPECT_EQ(rgb[2], 0);    // the red channel is the lowest bits
    unpackR10G10B10A2(pack(0, 0, 1023, 3), rgb);
    EXPECT_EQ(rgb[0], 0); EXPECT_EQ(rgb[1], 0); EXPECT_EQ(rgb[2], 255);    // the blue channel is bits 20-29
    unpackR10G10B10A2(pack(512, 256, 128, 1), rgb);
    EXPECT_NEAR(rgb[0], 128, 1); EXPECT_NEAR(rgb[1], 64, 1); EXPECT_NEAR(rgb[2], 32, 1); // linear scaling by 255 / 1023
}
