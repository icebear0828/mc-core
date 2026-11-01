#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <vector>

#include "render/steve_mesh_factory.hpp"

namespace {

// A small synthetic atlas: fully opaque so sprite extrusion always produces geometry.
std::vector<uint8_t> MakeAtlas(unsigned w, unsigned h) {
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 255);
    return rgba;
}

std::vector<erov::steve::FpItemCell> MakeCells() {
    return {
        {static_cast<uint16_t>(mc::ItemId::Shield), 0, 0, 16, 16},
        {static_cast<uint16_t>(mc::ItemId::Arrow), 16, 0, 16, 16},
        {static_cast<uint16_t>(mc::ItemId::DiamondSword), 32, 0, 16, 16},
    };
}

void ExpectRangesDisjoint(const std::map<uint16_t, erov::steve::DrawRange>& draws, size_t index_count) {
    std::vector<std::pair<size_t, size_t>> spans;
    for (const auto& [item, d] : draws) {
        (void)item;
        ASSERT_GT(d.index_count, 0u);
        ASSERT_LE(static_cast<size_t>(d.first_index) + d.index_count, index_count);
        spans.emplace_back(d.first_index, static_cast<size_t>(d.first_index) + d.index_count);
    }
    for (size_t i = 0; i < spans.size(); ++i) {
        for (size_t j = i + 1; j < spans.size(); ++j) {
            const bool disjoint = spans[i].second <= spans[j].first || spans[j].second <= spans[i].first;
            EXPECT_TRUE(disjoint);
        }
    }
}

mc::model::EntityModel TwoBoneModel() {
    mc::model::EntityModel model;
    model.identifier = "test";
    model.texture_width = 64.f;
    model.texture_height = 64.f;
    for (const char* name : {"right_arm", "left_arm"}) {
        mc::model::Bone bone;
        bone.name = name;
        mc::model::Cube cube;
        cube.origin = {0.f, 0.f, 0.f};
        cube.size = {4.f, 12.f, 4.f};
        cube.u = 0.f;
        cube.v = 0.f;
        bone.cubes.push_back(cube);
        model.bones.push_back(bone);
    }
    return model;
}

} // namespace

TEST(EldenRingSteveMesh, FirstPersonArmHasTwoRanges) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::FirstPersonMesh mesh = erov::steve::BuildFirstPerson(atlas.data(), 64, 64, MakeCells());
    ASSERT_FALSE(mesh.vertices.empty());
    for (int i = 0; i < 2; ++i) {
        EXPECT_GT(mesh.arm_ranges[i].index_count, 0u);
        EXPECT_LE(static_cast<size_t>(mesh.arm_ranges[i].first_index) + mesh.arm_ranges[i].index_count, mesh.indices.size());
    }
    // The sleeve range starts after the bare arm in the shared buffer.
    EXPECT_GT(mesh.arm_ranges[1].base_vertex, mesh.arm_ranges[0].base_vertex);
    EXPECT_EQ(mesh.arm_ranges[1].first_index, mesh.arm_ranges[0].first_index + mesh.arm_ranges[0].index_count);
}

TEST(EldenRingSteveMesh, FirstPersonItemsContainShieldAndSword) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::FirstPersonMesh mesh = erov::steve::BuildFirstPerson(atlas.data(), 64, 64, MakeCells());
    EXPECT_NE(mesh.items.find(static_cast<uint16_t>(mc::ItemId::Shield)), mesh.items.end());
    EXPECT_NE(mesh.items.find(static_cast<uint16_t>(mc::ItemId::DiamondSword)), mesh.items.end());
    ExpectRangesDisjoint(mesh.items, mesh.indices.size());
}

TEST(EldenRingSteveMesh, HeldItemsContainShieldAndBlockingVariant) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::HeldItemsMesh mesh = erov::steve::BuildHeldItems(atlas.data(), 64, 64, MakeCells());
    const uint16_t shield = static_cast<uint16_t>(mc::ItemId::Shield);
    const uint16_t raised = static_cast<uint16_t>(shield | erov::kBlockingKeyBit);
    const auto idle = mesh.draws.find(shield);
    const auto blocking = mesh.draws.find(raised);
    ASSERT_NE(idle, mesh.draws.end());
    ASSERT_NE(blocking, mesh.draws.end());
    EXPECT_NE(idle->second.first_index, blocking->second.first_index);
    ExpectRangesDisjoint(mesh.draws, mesh.indices.size());
}

TEST(EldenRingSteveMesh, HeldOffhandRangesDoNotOverlap) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::HeldItemsMesh mesh = erov::steve::BuildHeldItems(atlas.data(), 64, 64, MakeCells());
    ASSERT_FALSE(mesh.off_vertices.empty());
    ExpectRangesDisjoint(mesh.off_draws, mesh.off_indices.size());
}

TEST(EldenRingSteveMesh, ArrowMeshHasFourPlanes) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::HeldItemsMesh mesh = erov::steve::BuildHeldItems(atlas.data(), 64, 64, MakeCells());
    ASSERT_TRUE(mesh.arrow_valid);
    // Two extruded profiles (each the flat sprite's index count) plus two billboard quads (6 indices each).
    const mc::rig::ItemSprite sprite{atlas.data(), 64, 64, 16, 0, 16, 16};
    const mc::rig::RigMesh base = mc::rig::buildFlatItemMesh(sprite);
    ASSERT_FALSE(base.indices.empty());
    EXPECT_EQ(mesh.arrow_indices.size(), 2 * base.indices.size() + 12);
    EXPECT_EQ(mesh.arrow_vertices.size(), 2 * base.vertices.size() + 8);
}

TEST(EldenRingSteveMesh, MobMeshHasOneRangePerBone) {
    const mc::model::EntityModel model = TwoBoneModel();
    const erov::steve::MobCpuMesh mesh = erov::steve::BuildMobMesh(model);
    ASSERT_EQ(mesh.bone_ranges.size(), 2u);
    EXPECT_EQ(mesh.right_arm_ix, 0);
    EXPECT_EQ(mesh.left_arm_ix, 1);
    EXPECT_FALSE(mesh.vertices.empty());
    for (const auto& r : mesh.bone_ranges) {
        EXPECT_GT(r.index_count, 0u);
        EXPECT_LE(static_cast<size_t>(r.first_index) + r.index_count, mesh.indices.size());
    }
}

TEST(EldenRingSteveMesh, ShadowDiscClosesTheFan) {
    const mc::rig::RigMesh disc = erov::steve::BuildShadowDisc(24);
    EXPECT_EQ(disc.vertices.size(), 25u);
    EXPECT_EQ(disc.indices.size(), 72u);
}

TEST(EldenRingSteveMesh, MeshGeometryIsFinite) {
    const std::vector<uint8_t> atlas = MakeAtlas(64, 64);
    const erov::steve::FirstPersonMesh fp = erov::steve::BuildFirstPerson(atlas.data(), 64, 64, MakeCells());
    const erov::steve::HeldItemsMesh held = erov::steve::BuildHeldItems(atlas.data(), 64, 64, MakeCells());
    const erov::steve::MobCpuMesh mob = erov::steve::BuildMobMesh(TwoBoneModel());
    for (const auto* mesh : {&fp.vertices, &held.vertices, &held.off_vertices, &held.arrow_vertices, &mob.vertices}) {
        for (const mc::rig::RigVertex& v : *mesh) {
            EXPECT_TRUE(std::isfinite(v.x));
            EXPECT_TRUE(std::isfinite(v.y));
            EXPECT_TRUE(std::isfinite(v.z));
            EXPECT_TRUE(std::isfinite(v.u));
            EXPECT_TRUE(std::isfinite(v.v));
        }
    }
}
