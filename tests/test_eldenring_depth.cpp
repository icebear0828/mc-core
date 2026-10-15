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

using eldenring::depth::classifyDepthFormat;
using eldenring::depth::depthResourceToSrvFormat;
using eldenring::depth::isEligibleDepthCandidate;
using eldenring::depth::isTypelessDepthFormat;
using eldenring::depth::linearZToReverseZ;
using eldenring::depth::reverseZToLinearZ;
using eldenring::depth::shouldDiscardOccluded;
using eldenring::depth::DepthCandidateCriteria;
using eldenring::depth::kEldenRingNearPlane;

TEST(EldenRingDepth, ReverseZMatchesCameraProjectionParams) {
    EXPECT_NEAR(linearZToReverseZ(0.050067f), 1.0f, 1e-4f);
    EXPECT_NEAR(linearZToReverseZ(10000.0f), 0.0f, 1e-6f);
    for (float z : {0.5f, 1.0f, 2.0f, 4.0f, 10.0f, 25.0f, 50.0f}) {
        const float gd = linearZToReverseZ(z);
        const float recon_z = reverseZToLinearZ(gd);
        EXPECT_NEAR(recon_z, z, z * 0.01f);
        EXPECT_NEAR(gd * z, kEldenRingNearPlane, 0.0005f);
    }
}

TEST(EldenRingDepth, OcclusionDiscardsWhenWallIsNearerThanBlock) {
    const float block_z = 5.0f;
    const float wall_z = 2.5f;
    const float gd_wall = linearZToReverseZ(wall_z);
    EXPECT_TRUE(shouldDiscardOccluded(gd_wall, block_z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, OcclusionKeepsBlockWhenBlockIsNearerThanScene) {
    const float block_z = 3.0f;
    const float bg_z = 10.0f;
    const float gd_bg = linearZToReverseZ(bg_z);
    EXPECT_FALSE(shouldDiscardOccluded(gd_bg, block_z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, OcclusionPreventsZFightingWhenBlockIsFlush) {
    const float z = 4.0f;
    const float gd = linearZToReverseZ(z);
    EXPECT_FALSE(shouldDiscardOccluded(gd, z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, DemonstratesWhyOldBugCausedPermanentBlockVisibility) {
    const float real_block_z = 4.0f;
    const float buggy_steve_z = 1.0f / real_block_z;
    const float buggy_depth_const = 0.00456f;
    const float gd_wall = linearZToReverseZ(2.5f);
    const bool buggy_result = shouldDiscardOccluded(gd_wall, buggy_steve_z, buggy_depth_const, 0.05f, 0.05f);
    EXPECT_FALSE(buggy_result);
    const bool fixed_result = shouldDiscardOccluded(gd_wall, real_block_z, kEldenRingNearPlane, 0.05f, 0.05f);
    EXPECT_TRUE(fixed_result);
}
