#include <gtest/gtest.h>

#include "eldenring_depth.hpp"

using eldenring::depth::isSceneDepth;

TEST(EldenRingDepth, OnlyADepthBufferAsWideAsTheBackBufferIsTheSceneDepth) {
    EXPECT_TRUE(isSceneDepth(1920, 1080, 1920, 1080));
    // the game also creates 1024x512 depth textures (shadow passes) after a load: they must never replace the scene depth
    EXPECT_FALSE(isSceneDepth(1024, 512, 1920, 1080));
    EXPECT_FALSE(isSceneDepth(2560, 1440, 1920, 1080));
}

TEST(EldenRingDepth, AnEngineThatRoundsTheHeightUpIsStillTheSceneDepth) {
    EXPECT_TRUE(isSceneDepth(1920, 1088, 1920, 1080));
}

TEST(EldenRingDepth, WithoutAKnownBackBufferTheLargeTexturesAreKept) {
    EXPECT_TRUE(isSceneDepth(1920, 1080, 0, 0));   // the overlay is not ready yet: nothing to compare with
    EXPECT_TRUE(isSceneDepth(1024, 512, 0, 0));
}

TEST(EldenRingDepth, AResizedBackBufferChangesWhichDepthMatches) {
    EXPECT_TRUE(isSceneDepth(2560, 1440, 2560, 1440));
    EXPECT_FALSE(isSceneDepth(1920, 1080, 2560, 1440));
}
