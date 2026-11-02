// Validates the custom Blockbench recreations of the elytra wings and the trident (issues #38 / #39).
//
// The data lives in assets/source/models/entities/{trident,elytra_wings}.geo.json. It is a custom
// recreation extracted from the bundled Blockbench preview
// (re_data_cache/model_data.js), not a Mojang asset. These tests only check the geometry tables; the
// renderer attachment, atlas regions and in-game screenshots are still open (see the draft PR).

#include "mc/entity_model.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>

namespace mc::model {
namespace {

std::string readAsset(const std::string& relative) {
    std::ifstream file(std::string(MC_ASSET_DIR) + relative, std::ios::binary);
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

struct Bounds {
    Vec3 min{1e9f, 1e9f, 1e9f};
    Vec3 max{-1e9f, -1e9f, -1e9f};
};

Bounds boundsOf(const Bone& bone) {
    Bounds b;
    for (const Cube& c : bone.cubes) {
        b.min.x = std::min(b.min.x, c.origin.x);
        b.min.y = std::min(b.min.y, c.origin.y);
        b.min.z = std::min(b.min.z, c.origin.z);
        b.max.x = std::max(b.max.x, c.origin.x + c.size.x);
        b.max.y = std::max(b.max.y, c.origin.y + c.size.y);
        b.max.z = std::max(b.max.z, c.origin.z + c.size.z);
    }
    return b;
}

rig::HostBasis identityBasis() {
    return {.forward = {1.f, 0.f, 0.f}, .left = {0.f, 1.f, 0.f}, .up = {0.f, 0.f, 1.f}, .units_per_cm = 1.0f};
}

} // namespace

TEST(ElytraTridentModels, TridentFileParsesWithAnchorAndPropBones) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const EntityModel& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.trident");
    ASSERT_EQ(model.bones.size(), 2u);

    const Bone* arm = model.findBone("rightArm");
    ASSERT_NE(arm, nullptr);
    EXPECT_TRUE(arm->cubes.empty());
    EXPECT_FLOAT_EQ(arm->pivot.x, -5.0f);
    EXPECT_FLOAT_EQ(arm->pivot.y, 22.0f);

    const Bone* trident = model.findBone("trident");
    ASSERT_NE(trident, nullptr);
    EXPECT_EQ(trident->parent, "rightArm");
    EXPECT_FLOAT_EQ(trident->pivot.x, -6.0f);
    EXPECT_FLOAT_EQ(trident->pivot.y, 13.0f);
    EXPECT_FLOAT_EQ(trident->pivot.z, 0.0f);
    ASSERT_EQ(trident->cubes.size(), 5u);
}

TEST(ElytraTridentModels, TridentShaftAndProngsMatchTheExtractedData) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const Bone* trident = opt->findBone("trident");
    ASSERT_NE(trident, nullptr);

    // shaft: from [-6.5, 12.5, -15] to [-5.5, 13.5, 10] (the prongs point to -Z, the model's front)
    const Cube& shaft = trident->cubes[0];
    EXPECT_FLOAT_EQ(shaft.origin.x, -6.5f);
    EXPECT_FLOAT_EQ(shaft.origin.y, 12.5f);
    EXPECT_FLOAT_EQ(shaft.origin.z, -15.0f);
    EXPECT_FLOAT_EQ(shaft.size.x, 1.0f);
    EXPECT_FLOAT_EQ(shaft.size.y, 1.0f);
    EXPECT_FLOAT_EQ(shaft.size.z, 25.0f);

    // prongs reach z = -20 (centre prong), the side prongs stop at z = -18.5
    const Cube& centre = trident->cubes[2];
    EXPECT_FLOAT_EQ(centre.origin.z, -20.0f);
    EXPECT_FLOAT_EQ(trident->cubes[3].origin.z, -18.5f);
    EXPECT_FLOAT_EQ(trident->cubes[4].origin.z, -18.5f);

    // overall bounding box: cross bar widens to x -9..-3, prongs reach z = -20, the shaft butt stops at z = 10
    const Bounds b = boundsOf(*trident);
    EXPECT_FLOAT_EQ(b.min.x, -9.0f);
    EXPECT_FLOAT_EQ(b.max.x, -3.0f);
    EXPECT_FLOAT_EQ(b.min.y, 12.5f);
    EXPECT_FLOAT_EQ(b.max.y, 13.5f);
    EXPECT_FLOAT_EQ(b.min.z, -20.0f);
    EXPECT_FLOAT_EQ(b.max.z, 10.0f);
}

// Vanilla ElytraModel (net.minecraft.client.model.ElytraModel#createLayer):
//   left_wing  addBox(-10, 0, 0, 10, 20, 2) at PartPose.offsetAndRotation( 5, 0, 0, 15deg, 0, -15deg)
//   right_wing addBox(  0, 0, 0, 10, 20, 2) mirror at (-5, 0, 0, 15deg, 0,  15deg)
// with CubeDeformation(1) and texOffs(22, 0) on a 64x32 sheet. ElytraLayer draws it at pose.translate(0, 0, 0.125),
// so the slabs sit 2 px behind the torso's back face. Java's model y runs down from the neck, so Bedrock y = 24 - java y.
TEST(ElytraTridentModels, ElytraFileHasTwoWingsHangingFromTheUpperBody) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const EntityModel& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.elytra_wings");
    ASSERT_EQ(model.bones.size(), 3u);

    const Bone* upper = model.findBone("upper");
    ASSERT_NE(upper, nullptr);
    EXPECT_TRUE(upper->cubes.empty());

    const Bone* left = model.findBone("leftWing");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->parent, "upper");
    EXPECT_FLOAT_EQ(left->pivot.x, 5.0f);
    EXPECT_FLOAT_EQ(left->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(left->pivot.z, 2.0f);
    EXPECT_FLOAT_EQ(left->rotation.x, 15.0f);
    EXPECT_FLOAT_EQ(left->rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(left->rotation.z, -15.0f);
    ASSERT_EQ(left->cubes.size(), 1u);
    EXPECT_FALSE(left->cubes[0].mirror);

    const Bone* right = model.findBone("rightWing");
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->parent, "upper");
    EXPECT_FLOAT_EQ(right->pivot.x, -5.0f);
    EXPECT_FLOAT_EQ(right->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(right->pivot.z, 2.0f);
    EXPECT_FLOAT_EQ(right->rotation.x, 15.0f);
    EXPECT_FLOAT_EQ(right->rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(right->rotation.z, 15.0f);
    ASSERT_EQ(right->cubes.size(), 1u);
    // Vanilla's CubeListBuilder.mirror() only reverses the winding and flips the normal in ModelPart$Polygon; it
    // does not mirror the UVs, so both slabs sample the same rectangle.
    EXPECT_FALSE(right->cubes[0].mirror);
}

TEST(ElytraTridentModels, WingSlabsAreTenByTwentyPixelsAndSitBehindTheTorso) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());

    const Bone* right = opt->findBone("rightWing");
    const Bone* left = opt->findBone("leftWing");
    ASSERT_NE(right, nullptr);
    ASSERT_NE(left, nullptr);

    // Both slabs cover the whole 10 px torso width; only the pivot and the depth nudge differ.
    for (const Bone* wing : {left, right}) {
        const Cube& slab = wing->cubes[0];
        EXPECT_FLOAT_EQ(slab.origin.x, -5.0f);
        EXPECT_FLOAT_EQ(slab.origin.y, 4.0f);
        EXPECT_FLOAT_EQ(slab.size.x, 10.0f);
        EXPECT_FLOAT_EQ(slab.size.y, 20.0f);
        EXPECT_FLOAT_EQ(slab.size.z, 2.0f);
        EXPECT_FLOAT_EQ(slab.inflate, 1.0f);
        EXPECT_FLOAT_EQ(slab.u, 22.0f);
        EXPECT_FLOAT_EQ(slab.v, 0.0f);
        const Bounds b = boundsOf(*wing);
        EXPECT_FLOAT_EQ(b.min.x, -5.0f);
        EXPECT_FLOAT_EQ(b.max.x, 5.0f);
        EXPECT_FLOAT_EQ(b.min.y, 4.0f);
        EXPECT_FLOAT_EQ(b.max.y, 24.0f);
        // The ElytraLayer offset puts the slab behind the torso (whose back face is z = +2), so the arms, which
        // stay at z -2..2, never cut through it.
        EXPECT_GE(b.min.z, 1.0f);
    }
    // Both wings have the same xRot and opposite zRot, so they lie in exactly the same plane and overlap over the
    // whole back. Vanilla lets draw order settle it; with a float depth buffer that is a z-fight, so the right wing
    // (the one vanilla draws last) is nudged 0.25 px back to win deterministically.
    EXPECT_FLOAT_EQ(left->cubes[0].origin.z, 2.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].origin.z, 2.25f);
}

// ElytraModel.setupAnim only changes the rotations (and, while sneaking, drops the pivots 3 px).
TEST(ElytraTridentModels, GlideAndCrouchModelsOnlyDifferInPose) {
    const auto glide = EntityModel::fromJson(readAsset("/models/entities/elytra_wings_glide.geo.json"));
    const auto crouch = EntityModel::fromJson(readAsset("/models/entities/elytra_wings_crouch.geo.json"));
    ASSERT_TRUE(glide.has_value());
    ASSERT_TRUE(crouch.has_value());

    const Bone* glide_left = glide->findBone("leftWing");
    const Bone* glide_right = glide->findBone("rightWing");
    ASSERT_NE(glide_left, nullptr);
    ASSERT_NE(glide_right, nullptr);
    EXPECT_FLOAT_EQ(glide_left->rotation.x, 20.0f);
    EXPECT_FLOAT_EQ(glide_left->rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(glide_left->rotation.z, -90.0f);
    EXPECT_FLOAT_EQ(glide_right->rotation.z, 90.0f);
    EXPECT_FLOAT_EQ(glide_left->pivot.y, 24.0f);
    EXPECT_FLOAT_EQ(glide_left->cubes[0].origin.y, 4.0f);

    const Bone* crouch_left = crouch->findBone("leftWing");
    const Bone* crouch_right = crouch->findBone("rightWing");
    ASSERT_NE(crouch_left, nullptr);
    ASSERT_NE(crouch_right, nullptr);
    EXPECT_FLOAT_EQ(crouch_left->rotation.x, 40.0f);
    EXPECT_FLOAT_EQ(crouch_left->rotation.y, 5.0f);
    EXPECT_FLOAT_EQ(crouch_left->rotation.z, -45.0f);
    EXPECT_FLOAT_EQ(crouch_right->rotation.y, -5.0f);
    EXPECT_FLOAT_EQ(crouch_right->rotation.z, 45.0f);
    // sneaking drops the wing 3 px: java pivot y 3 -> bedrock 21, and the slab follows to y 1..21
    EXPECT_FLOAT_EQ(crouch_left->pivot.y, 21.0f);
    EXPECT_FLOAT_EQ(crouch_left->cubes[0].origin.y, 1.0f);
    EXPECT_FLOAT_EQ(crouch_left->pivot.z, 2.0f);

    for (const EntityModel* m : {&*glide, &*crouch}) {
        EXPECT_FLOAT_EQ(m->findBone("leftWing")->cubes[0].origin.z, 2.0f);
        EXPECT_FLOAT_EQ(m->findBone("rightWing")->cubes[0].origin.z, 2.25f);
        EXPECT_FALSE(m->findBone("rightWing")->cubes[0].mirror);
        EXPECT_FALSE(m->findBone("leftWing")->cubes[0].mirror);
    }
}

TEST(ElytraTridentModels, BothModelsBuildMeshesWithoutTouchingTheSteveRig) {
    const auto trident = EntityModel::fromJson(readAsset("/models/entities/trident.geo.json"));
    const auto wings = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(trident.has_value());
    ASSERT_TRUE(wings.has_value());

    // 5 cubes -> 5 * 24 vertices / 5 * 36 indices; 2 wing cubes -> 48 / 72.
    EXPECT_EQ(trident->buildCombinedMesh(identityBasis()).vertices.size(), 5u * 24u);
    EXPECT_EQ(trident->buildCombinedMesh(identityBasis()).indices.size(), 5u * 36u);
    EXPECT_EQ(wings->buildCombinedMesh(identityBasis()).vertices.size(), 2u * 24u);
    EXPECT_EQ(wings->buildCombinedMesh(identityBasis()).indices.size(), 2u * 36u);

    // The prop models are separate data: the 12-part Steve rig is untouched.
    EXPECT_EQ(static_cast<size_t>(StevePart::Count), 12u);
}

} // namespace mc::model
