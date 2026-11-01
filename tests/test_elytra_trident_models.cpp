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

TEST(ElytraTridentModels, ElytraFileHasTwoWingsHangingFromTheUpperBody) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());
    const EntityModel& model = *opt;

    EXPECT_EQ(model.identifier, "geometry.elytra_wings");
    ASSERT_EQ(model.bones.size(), 3u);

    const Bone* upper = model.findBone("upper");
    ASSERT_NE(upper, nullptr);
    EXPECT_TRUE(upper->cubes.empty());

    const Bone* right = model.findBone("rightWing");
    ASSERT_NE(right, nullptr);
    EXPECT_EQ(right->parent, "upper");
    EXPECT_FLOAT_EQ(right->pivot.x, -1.0f);
    EXPECT_FLOAT_EQ(right->pivot.y, 23.0f);
    EXPECT_FLOAT_EQ(right->pivot.z, 3.0f);
    ASSERT_EQ(right->cubes.size(), 1u);

    const Bone* left = model.findBone("leftWing");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->parent, "upper");
    EXPECT_FLOAT_EQ(left->pivot.x, 1.0f);
    EXPECT_FLOAT_EQ(left->pivot.y, 23.0f);
    EXPECT_FLOAT_EQ(left->pivot.z, 3.0f);
    ASSERT_EQ(left->cubes.size(), 1u);
}

TEST(ElytraTridentModels, WingSlabsAreTenByTwentyPixelsAndMirrored) {
    const auto opt = EntityModel::fromJson(readAsset("/models/entities/elytra_wings.geo.json"));
    ASSERT_TRUE(opt.has_value());

    const Bone* right = opt->findBone("rightWing");
    const Bone* left = opt->findBone("leftWing");
    ASSERT_NE(right, nullptr);
    ASSERT_NE(left, nullptr);

    // one slab per wing: the vanilla 10x20x2 elytra box, from [-11, 3, 2.6] to [-1, 23, 4.6]
    EXPECT_FLOAT_EQ(right->cubes[0].origin.x, -11.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].origin.y, 3.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].size.x, 10.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].size.y, 20.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].size.z, 2.0f);
    // the vanilla elytra texture: both wings sample texOffs (22,0), the right one mirrored
    EXPECT_FLOAT_EQ(right->cubes[0].u, 22.0f);
    EXPECT_FLOAT_EQ(right->cubes[0].v, 0.0f);
    EXPECT_TRUE(right->cubes[0].mirror);

    // left wing mirrors the right one about x = 0
    EXPECT_FLOAT_EQ(left->cubes[0].origin.x, 1.0f);
    EXPECT_FLOAT_EQ(left->cubes[0].size.x, 10.0f);
    EXPECT_FLOAT_EQ(left->cubes[0].size.y, 20.0f);
    EXPECT_FLOAT_EQ(left->cubes[0].size.z, 2.0f);
    EXPECT_FLOAT_EQ(left->cubes[0].u, 22.0f);
    EXPECT_FLOAT_EQ(left->cubes[0].v, 0.0f);
    EXPECT_FALSE(left->cubes[0].mirror);

    const Bounds r = boundsOf(*right);
    EXPECT_FLOAT_EQ(r.min.y, 3.0f);
    EXPECT_FLOAT_EQ(r.max.y, 23.0f);
    const Bounds l = boundsOf(*left);
    EXPECT_FLOAT_EQ(l.min.y, 3.0f);
    EXPECT_FLOAT_EQ(l.max.y, 23.0f);
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
