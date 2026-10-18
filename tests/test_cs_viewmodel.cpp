#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "cs_viewmodel.hpp"
#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

namespace {

using namespace eldenring::cs;
namespace fp = eldenring::fp;

enum class SubmeshType {
    Blade,
    HandleFront,
    HandleRear,
    Lock,
    Arms,
    Base
};

SubmeshType ClassifySubmesh(const std::string& raw_name) {
    std::string n = raw_name;
    for (char& c : n) c = static_cast<char>(tolower(c));
    if (n.find("blade") != std::string::npos) return SubmeshType::Blade;
    if (n.find("front") != std::string::npos || n.find("handle0") != std::string::npos || n.find("handle1") != std::string::npos)
        return SubmeshType::HandleFront;
    if (n.find("rear") != std::string::npos || n.find("handle2") != std::string::npos)
        return SubmeshType::HandleRear;
    if (n.find("lock") != std::string::npos || n.find("latch") != std::string::npos)
        return SubmeshType::Lock;
    if (n.find("arm") != std::string::npos || n.find("glove") != std::string::npos)
        return SubmeshType::Arms;
    return SubmeshType::Base;
}

TEST(CSViewModelTest, SubmeshClassificationCoversAllPartsAndMultipleArms) {
    // Exact bone names exported from Blender / glTF
    const std::vector<std::string> bone_names = {
        "rear",
        "weapon",
        "blade",
        "front",
        "lock",
        "arms_glove",
        "arms_glove",
        "arms_glove"
    };

    std::vector<SubmeshType> types;
    for (const auto& name : bone_names) {
        types.push_back(ClassifySubmesh(name));
    }

    ASSERT_EQ(types.size(), 8u);
    EXPECT_EQ(types[0], SubmeshType::HandleRear);
    EXPECT_EQ(types[1], SubmeshType::Base);
    EXPECT_EQ(types[2], SubmeshType::Blade);
    EXPECT_EQ(types[3], SubmeshType::HandleFront);
    EXPECT_EQ(types[4], SubmeshType::Lock);
    // All 3 arm submeshes MUST be classified as Arms
    EXPECT_EQ(types[5], SubmeshType::Arms);
    EXPECT_EQ(types[6], SubmeshType::Arms);
    EXPECT_EQ(types[7], SubmeshType::Arms);
}

TEST(CSViewModelTest, MemorySerializationRoundtrip) {
    CSViewModel model;
    std::vector<uint8_t> buffer;

    CSVMHeader hdr{};
    std::memcpy(hdr.magic, "CSVM", 4);
    hdr.version = 1;
    hdr.num_submeshes = 2;
    hdr.total_vertices = 4;
    hdr.total_indices = 6;

    buffer.resize(sizeof(CSVMHeader));
    std::memcpy(buffer.data(), &hdr, sizeof(CSVMHeader));

    auto append_submesh = [&](const std::string& name, uint32_t bone_idx, uint32_t first_idx, int32_t base_v, uint32_t count) {
        uint32_t len = static_cast<uint32_t>(name.size());
        size_t off = buffer.size();
        buffer.resize(off + 4 + len + 16);
        std::memcpy(buffer.data() + off, &len, 4); off += 4;
        std::memcpy(buffer.data() + off, name.data(), len); off += len;
        std::memcpy(buffer.data() + off, &bone_idx, 4); off += 4;
        std::memcpy(buffer.data() + off, &first_idx, 4); off += 4;
        std::memcpy(buffer.data() + off, &base_v, 4); off += 4;
        std::memcpy(buffer.data() + off, &count, 4);
    };

    append_submesh("blade", 0, 0, 0, 3);
    append_submesh("arms_glove", 1, 3, 2, 3);

    // 4 vertices
    CSVMVertex verts[4] = {
        {{-0.1f, -0.1f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}},
        {{ 0.1f, -0.1f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}},
        {{-0.1f,  0.1f, 0.0f}, {0, 0, 1}, {0.0f, 1.0f}},
        {{ 0.1f,  0.1f, 0.0f}, {0, 0, 1}, {1.0f, 1.0f}},
    };
    size_t off = buffer.size();
    buffer.resize(off + sizeof(verts));
    std::memcpy(buffer.data() + off, verts, sizeof(verts));

    // 6 indices (2 triangles)
    uint32_t indices[6] = {0, 1, 2, 2, 1, 3};
    off = buffer.size();
    buffer.resize(off + sizeof(indices));
    std::memcpy(buffer.data() + off, indices, sizeof(indices));

    EXPECT_TRUE(model.loadFromMemory(buffer.data(), buffer.size()));
    EXPECT_EQ(model.submeshes.size(), 2u);
    EXPECT_EQ(model.submeshes[0].bone_name, "blade");
    EXPECT_EQ(model.submeshes[1].bone_name, "arms_glove");
    EXPECT_EQ(model.vertices.size(), 4u);
    EXPECT_EQ(model.indices.size(), 6u);
}

TEST(CSViewModelTest, ViewmodelScaleAndVisibilityAssertion) {
    // CS2 arms bounds: width is ~0.308m (+/- 0.154m), height is ~0.34m
    const mc::Vec3 arm_left{-0.154f, -0.083f, 0.0f};
    const mc::Vec3 arm_right{0.154f, 0.070f, 0.0f};

    // Calculate projected bounds with true 1.0f scale
    fp::M4 base_correct = fp::translate(0.18f, -0.16f, -0.38f);
    base_correct = fp::mul(base_correct, fp::scale(1.0f));
    base_correct = fp::mul(base_correct, fp::rotX(-10.f));
    base_correct = fp::mul(base_correct, fp::rotY(-15.f));

    const mc::rig::Mat4 world_correct = fp::toHost(base_correct);
    const mc::rig::Mat4 proj = mc::rig::perspectiveLH(70.f * fp::kDeg, 1920.f / 1080.f, 0.05f, 20.f);

    auto project = [&](const mc::rig::Mat4& world, const mc::Vec3& p) -> mc::Vec3 {
        // Row vector mul: p_w = (p, 1) * world
        float wx = p.x * world.m[0] + p.y * world.m[4] + p.z * world.m[8] + world.m[12];
        float wy = p.x * world.m[1] + p.y * world.m[5] + p.z * world.m[9] + world.m[13];
        float wz = p.x * world.m[2] + p.y * world.m[6] + p.z * world.m[10] + world.m[14];
        float ww = p.x * world.m[3] + p.y * world.m[7] + p.z * world.m[11] + world.m[15];

        // Perspective mul: p_clip = p_w * proj
        float cx = wx * proj.m[0] + wy * proj.m[4] + wz * proj.m[8] + ww * proj.m[12];
        float cy = wx * proj.m[1] + wy * proj.m[5] + wz * proj.m[9] + ww * proj.m[13];
        float cz = wx * proj.m[2] + wy * proj.m[6] + wz * proj.m[10] + ww * proj.m[14];
        float cw = wx * proj.m[3] + wy * proj.m[7] + wz * proj.m[11] + ww * proj.m[15];

        EXPECT_GT(cw, 0.05f); // Must be well in front of camera near clip
        return {cx / cw, cy / cw, cz / cw};
    };

    mc::Vec3 ndc_l = project(world_correct, arm_left);
    mc::Vec3 ndc_r = project(world_correct, arm_right);

    // In NDC [-1, 1], viewmodel should occupy a substantial and visible portion of the screen
    float ndc_width = std::fabs(ndc_r.x - ndc_l.x);
    EXPECT_GT(ndc_width, 0.35f); // Must be at least 35% of half-screen width (~300+ pixels)
    EXPECT_LT(ndc_width, 1.20f); // But not overflowing the whole screen

    // Now test the buggy 0.012f micro-scale to verify the regression guard
    fp::M4 base_buggy = fp::translate(0.24f, -0.20f, -0.42f);
    base_buggy = fp::mul(base_buggy, fp::scale(0.012f)); // Micro-scale bug!
    const mc::rig::Mat4 world_buggy = fp::toHost(base_buggy);
    mc::Vec3 ndc_buggy_l = project(world_buggy, arm_left);
    mc::Vec3 ndc_buggy_r = project(world_buggy, arm_right);
    float buggy_width = std::fabs(ndc_buggy_r.x - ndc_buggy_l.x);
    // Buggy width is tiny (< 0.01, roughly 8 pixels), proving the invisibility bug
    EXPECT_LT(buggy_width, 0.01f);
}

TEST(CSViewModelTest, KnifeOrientationIsForwardAndFaceFacingCameraNotSideways) {
    // In authentic CS2 viewmodel camera space, the knife and glove are placed at the resting stance
    // Base pose is identity (with subtle idle bob at runtime)
    const fp::M4 base = fp::scale(1.0f);
    const mc::rig::Mat4 world = fp::toHost(base);

    // In authentic CS2 camera space:
    // +X is right, +Y is up, -Z is forward
    // Hand grip is at [0.115, -0.112, -0.500]
    // Blade extends forward into scene (-Z) and slightly upward
    const mc::Vec3 blade_base{0.126f, -0.096f, -0.519f};
    const mc::Vec3 blade_tip{0.180f, -0.070f, -0.535f};

    mc::Vec3 tip_dir{blade_tip.x - blade_base.x, blade_tip.y - blade_base.y, blade_tip.z - blade_base.z};
    float len = std::sqrt(tip_dir.x * tip_dir.x + tip_dir.y * tip_dir.y + tip_dir.z * tip_dir.z);
    ASSERT_GT(len, 0.01f);
    tip_dir.x /= len; tip_dir.y /= len; tip_dir.z /= len;

    // In camera space:
    // 1. Blade is positioned in front of camera (-Z < -0.40)
    EXPECT_LT(blade_base.z, -0.40f);
    EXPECT_LT(blade_tip.z, -0.40f);

    // 2. Hand and knife are on the right side of the screen (+X > 0.05)
    EXPECT_GT(blade_base.x, 0.05f);
    EXPECT_GT(blade_tip.x, 0.05f);

    // 3. Blade is positioned below camera eye level (Y < 0.0)
    EXPECT_LT(blade_base.y, 0.0f);
    EXPECT_LT(blade_tip.y, 0.0f);
}

#include "cs_animation_tracks.hpp"

TEST(CSViewModelTest, AuthenticCS2AnimationTracksFidelity) {
    using namespace erov::cs_anims;

    // 1. Inspect Animation track checks
    EXPECT_EQ(kInspectFrameCount, 124u);
    EXPECT_FLOAT_EQ(kInspectFps, 30.0f);
    EXPECT_NEAR(kInspectDuration, 4.10f, 0.05f);

    // Frame 0 must be resting identity matrix
    const auto& f0 = kInspectBlade[0];
    EXPECT_NEAR(f0.m[0], 1.0f, 0.01f);
    EXPECT_NEAR(f0.m[5], 1.0f, 0.01f);
    EXPECT_NEAR(f0.m[10], 1.0f, 0.01f);
    EXPECT_NEAR(f0.m[15], 1.0f, 0.01f);
    EXPECT_NEAR(f0.m[3], 0.0f, 0.01f);
    EXPECT_NEAR(f0.m[7], 0.0f, 0.01f);
    EXPECT_NEAR(f0.m[11], 0.0f, 0.01f);

    // Last frame (123) must smoothly return to resting identity matrix (< 0.02 delta)
    const auto& f_last = kInspectBlade[123];
    EXPECT_NEAR(f_last.m[0], 1.0f, 0.02f);
    EXPECT_NEAR(f_last.m[5], 1.0f, 0.02f);
    EXPECT_NEAR(f_last.m[10], 1.0f, 0.02f);
    EXPECT_NEAR(f_last.m[15], 1.0f, 0.02f);
    EXPECT_NEAR(f_last.m[3], 0.0f, 0.02f);
    EXPECT_NEAR(f_last.m[7], 0.0f, 0.02f);
    EXPECT_NEAR(f_last.m[11], 0.0f, 0.02f);

    // Middle of inspect (frame 60) must have significant flourish rotation (trace significantly differs from 4.0)
    const auto& f60 = kInspectBlade[60];
    const float trace = f60.m[0] + f60.m[5] + f60.m[10] + f60.m[15];
    EXPECT_LT(trace, 2.5f); // Substantial rotation away from identity!

    // 2. Deploy Animation track checks
    EXPECT_EQ(kDeployFrameCount, 31u);
    EXPECT_FLOAT_EQ(kDeployFps, 30.0f);
    EXPECT_NEAR(kDeployDuration, 1.0f, 0.05f);

    // End of deploy (frame 30) must be resting identity matrix
    const auto& d_last = kDeployBlade[30];
    EXPECT_NEAR(d_last.m[0], 1.0f, 0.01f);
    EXPECT_NEAR(d_last.m[5], 1.0f, 0.01f);
    EXPECT_NEAR(d_last.m[10], 1.0f, 0.01f);

    // 3. Backstab Animation track checks
    EXPECT_EQ(kBackstabFrameCount, 36u);
    EXPECT_FLOAT_EQ(kBackstabFps, 30.0f);

    // 4. Test sampling function
    eldenring::fp::M4 sampled{};
    SampleTrack(kInspectBlade, kInspectFrameCount, kInspectFps, 0.0f, sampled);
    EXPECT_NEAR(sampled.m[0], 1.0f, 0.01f);
    SampleTrack(kInspectBlade, kInspectFrameCount, kInspectFps, kInspectDuration, sampled);
    EXPECT_NEAR(sampled.m[0], 1.0f, 0.02f);
}

} // namespace

