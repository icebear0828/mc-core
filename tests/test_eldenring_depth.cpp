#include <gtest/gtest.h>
#include "eldenring_depth.hpp"

using namespace eldenring::depth;

TEST(EldenRingDepth, MapsR32G8X24TypelessToFloatX8X24Typeless) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R32G8X24_TYPELESS), DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_D32_FLOAT_S8X24_UINT), DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS), DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
}

TEST(EldenRingDepth, MapsD32FloatAndR32TypelessToR32Float) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_D32_FLOAT), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R32_TYPELESS), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R32_FLOAT), DXGI_FORMAT_R32_FLOAT);
}

TEST(EldenRingDepth, MapsR24G8AndD24S8ToR24UnormX8Typeless) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R24G8_TYPELESS), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_D24_UNORM_S8_UINT), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R24_UNORM_X8_TYPELESS), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
}

TEST(EldenRingDepth, MapsD16UnormAndR16TypelessToR16Unorm) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_D16_UNORM), DXGI_FORMAT_R16_UNORM);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R16_TYPELESS), DXGI_FORMAT_R16_UNORM);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_R16_UNORM), DXGI_FORMAT_R16_UNORM);
}

TEST(EldenRingDepth, FallsBackToDsvFormatWhenResourceFormatIsUnknown) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT), DXGI_FORMAT_R32_FLOAT);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D32_FLOAT_S8X24_UINT), DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS);
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_UNKNOWN, DXGI_FORMAT_D24_UNORM_S8_UINT), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
}

TEST(EldenRingDepth, RejectsNonDepthFormats) {
    EXPECT_EQ(depthResourceToSrvFormat(DXGI_FORMAT_UNKNOWN), DXGI_FORMAT_UNKNOWN);
    // Standard backbuffer / color formats must return UNKNOWN (cannot be depth)
    EXPECT_EQ(depthResourceToSrvFormat(static_cast<DXGI_FORMAT>(28)), DXGI_FORMAT_UNKNOWN); // R8G8B8A8_UNORM
    EXPECT_EQ(depthResourceToSrvFormat(static_cast<DXGI_FORMAT>(87)), DXGI_FORMAT_UNKNOWN); // B8G8R8A8_UNORM
    EXPECT_EQ(depthResourceToSrvFormat(static_cast<DXGI_FORMAT>(24)), DXGI_FORMAT_UNKNOWN); // R10G10B10A2_UNORM
}

TEST(EldenRingDepth, EligibleCandidateAllowsTypelessDepthFormatsAtScreenResolution) {
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1920, 1080, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, 1280, 720, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_D16_UNORM, 1920, 1080, 1, 1, 0));
}

TEST(EldenRingDepth, RejectsNonTypelessDepthResourcesBecauseD3D12CannotCreateSRVOnTypedDSV) {
    // In D3D12, CreateShaderResourceView fails / produces null view when called on typed depth formats like D32_FLOAT.
    // Therefore, only typeless resources can be accepted as depth candidates for sampling.
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 1, 1, 0));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D24_UNORM_S8_UINT, DXGI_FORMAT_D24_UNORM_S8_UINT, 1920, 1080, 1, 1, 0));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 1920, 1080, 1, 1, 0));
}

TEST(EldenRingDepth, RejectsCandidateWithDenyShaderResourceFlag) {
    constexpr unsigned int kDenyShaderResource = 0x4; // D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 1, 1, kDenyShaderResource));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1920, 1080, 1, 1, kDenyShaderResource));
}

TEST(EldenRingDepth, RejectsMultiSampledOrArrayOrSmallTextures) {
    // Array size > 1
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 2, 1, 0));
    // Sample count > 1 (MSAA)
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 1, 4, 0));
    // Small texture (e.g. 256x256 shadow map or probe)
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D32_FLOAT, DXGI_FORMAT_D32_FLOAT, 256, 256, 1, 1, 0));
}

TEST(EldenRingDepth, StrictMatchingRejectsOversizedShadowMaps) {
    DepthCandidateCriteria criteria;
    criteria.match_width = 1920;
    criteria.match_height = 1080;

    // 1920x1080 screen candidate must pass
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_D32_FLOAT, 1920, 1080, 1, 1, 0, criteria));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1920, 1080, 1, 1, 0, criteria));

    // Shadow maps of all dimensions must be strictly rejected
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 8192, 4096, 1, 1, 0, criteria));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 4096, 4096, 1, 1, 0, criteria));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 6144, 4096, 1, 1, 0, criteria));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 2048, 1024, 1, 1, 0, criteria));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 512, 512, 1, 1, 0, criteria));
}

TEST(EldenRingDepth, RejectsNonWidescreenAspectRatiosByDefault) {
    // Square shadow maps (1:1)
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 4096, 4096, 1, 1, 0));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 1024, 1024, 1, 1, 0));

    // 2:1 shadow atlases
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 8192, 4096, 1, 1, 0));
    EXPECT_FALSE(isEligibleDepthCandidate(DXGI_FORMAT_D16_UNORM, DXGI_FORMAT_D16_UNORM, 2048, 1024, 1, 1, 0));

    // Standard 16:9 / 16:10 resolutions must pass
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1920, 1080, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 2560, 1440, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 3840, 2160, 1, 1, 0));
    EXPECT_TRUE(isEligibleDepthCandidate(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, 1920, 1200, 1, 1, 0));
}

TEST(EldenRingDepth, ReverseZMatchesCameraProjectionParams) {
    // Elden Ring projection: near = 0.050067 m, far = 10000.0 m
    // At near plane: gd should be 1.0
    EXPECT_NEAR(linearZToReverseZ(0.050067f), 1.0f, 1e-4f);
    // At far plane: gd should be 0.0
    EXPECT_NEAR(linearZToReverseZ(10000.0f), 0.0f, 1e-6f);
    // For any typical view distance (1m to 50m): relative reconstruction error should be < 1% (due to finite far plane)
    for (float z : {0.5f, 1.0f, 2.0f, 4.0f, 10.0f, 25.0f, 50.0f}) {
        const float gd = linearZToReverseZ(z);
        const float recon_z = reverseZToLinearZ(gd);
        EXPECT_NEAR(recon_z, z, z * 0.01f);
        EXPECT_NEAR(gd * z, kEldenRingNearPlane, 0.0005f);
    }
}

TEST(EldenRingDepth, OcclusionDiscardsWhenWallIsNearerThanBlock) {
    // A placed block is at 5.0 m.
    // A wall or terrain is in front of it at 2.5 m.
    const float block_z = 5.0f;
    const float wall_z = 2.5f;
    const float gd_wall = linearZToReverseZ(wall_z);

    // With correct parameters (depth_const = 0.050067), the occluded block must be discarded
    EXPECT_TRUE(shouldDiscardOccluded(gd_wall, block_z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, OcclusionKeepsBlockWhenBlockIsNearerThanScene) {
    // A placed block is in front at 3.0 m.
    // The background terrain is at 10.0 m.
    const float block_z = 3.0f;
    const float bg_z = 10.0f;
    const float gd_bg = linearZToReverseZ(bg_z);

    // The block is visible in front of the background, so should NOT discard
    EXPECT_FALSE(shouldDiscardOccluded(gd_bg, block_z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, OcclusionPreventsZFightingWhenBlockIsFlush) {
    // When a block is placed touching a surface at 4.0 m, biases prevent z-fighting
    const float z = 4.0f;
    const float gd = linearZToReverseZ(z);
    EXPECT_FALSE(shouldDiscardOccluded(gd, z, kEldenRingNearPlane, 0.05f, 0.05f));
}

TEST(EldenRingDepth, DemonstratesWhyOldBugCausedPermanentBlockVisibility) {
    // Reproduction of the bug:
    // 1. The shader previously did `float steve_z = rcp(i.pos.w);`
    // In D3D12 HLSL, SV_POSITION.w is already the view-space distance z (e.g. 4.0 m).
    // `rcp` inverted it to 1 / 4.0 = 0.25 m!
    // 2. The config had `depth_const = 0.00456` instead of `0.050067`.
    const float real_block_z = 4.0f;
    const float buggy_steve_z = 1.0f / real_block_z; // 0.25 m
    const float buggy_depth_const = 0.00456f;

    // A wall is at 2.5 m (in front of the 4.0 m block)
    const float gd_wall = linearZToReverseZ(2.5f);

    // Under the buggy shader logic, the block NEVER discarded (returned false)!
    const bool buggy_result = shouldDiscardOccluded(gd_wall, buggy_steve_z, buggy_depth_const, 0.05f, 0.05f);
    EXPECT_FALSE(buggy_result); // The bug caused permanent visibility!

    // Under the fixed shader logic (real_block_z and kEldenRingNearPlane), it correctly discards!
    const bool fixed_result = shouldDiscardOccluded(gd_wall, real_block_z, kEldenRingNearPlane, 0.05f, 0.05f);
    EXPECT_TRUE(fixed_result); // Fixed!
}

