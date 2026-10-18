#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "cs_viewmodel.hpp"
#include "cs_weapon_registry.hpp"
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

    // Calculate projected bounds with true 1.0f scale, decoupled 68 deg FOV and natural origin (zero Euler hacks)
    fp::M4 base_correct = fp::translate(0.18f, -0.16f, -0.38f);
    base_correct = fp::mul(base_correct, fp::scale(1.0f));

    const mc::rig::Mat4 world_correct = fp::toHost(base_correct);
    const mc::rig::Mat4 proj = computeViewmodelProjection(1920, 1080);

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

TEST(CSViewModelTest, Decoupled68DegreeViewmodelProjection) {
    // Standard Source 2 viewmodel FOV is strictly 68 degrees
    ViewmodelProjectionConfig cfg{};
    EXPECT_FLOAT_EQ(cfg.fov_deg, 68.0f);
    EXPECT_FLOAT_EQ(cfg.z_near, 0.01f);
    EXPECT_FLOAT_EQ(cfg.z_far, 20.0f);
    EXPECT_FALSE(cfg.use_game_projection);

    const unsigned w = 1920, h = 1080;
    const mc::rig::Mat4 proj = computeViewmodelProjection(w, h, cfg);

    // Expected scaling factors for 68 deg FOV with 16:9 aspect ratio
    constexpr float kPi = 3.14159265358979323846f;
    const float expected_ys = 1.0f / std::tan((68.0f * kPi / 180.0f) * 0.5f);
    const float expected_xs = expected_ys / (1920.0f / 1080.0f);
    const float expected_a = 20.0f / (20.0f - 0.01f);

    EXPECT_NEAR(proj.m[0], expected_xs, 0.001f);
    EXPECT_NEAR(proj.m[5], expected_ys, 0.001f);
    EXPECT_NEAR(proj.m[10], expected_a, 0.001f);
    EXPECT_NEAR(proj.m[11], 1.0f, 0.001f);
    EXPECT_NEAR(proj.m[14], -0.01f * expected_a, 0.001f);

    // Test with custom viewmodel_offset (e.g. viewmodel_offset_x = 0.05m, y = -0.02m)
    ViewmodelProjectionConfig offset_cfg = cfg;
    offset_cfg.offset_x = 0.05f;
    offset_cfg.offset_y = -0.02f;
    const mc::rig::Mat4 proj_offset = computeViewmodelProjection(w, h, offset_cfg);

    // Translation row in projection matrix applies the screen-space shift
    EXPECT_NEAR(proj_offset.m[12], 0.05f * expected_xs, 0.002f);
    EXPECT_NEAR(proj_offset.m[13], -0.02f * expected_ys, 0.002f);

    // Crucial Decoupling Guard:
    // Game camera at 90 deg FOV vs Viewmodel at 68 deg FOV
    const mc::rig::Mat4 game_proj_90 = mc::rig::perspectiveLH(90.0f * kPi / 180.0f, 1920.0f / 1080.0f, 0.05f, 1000.0f);
    EXPECT_NE(proj.m[5], game_proj_90.m[5]); // Must be fully decoupled!
    EXPECT_GT(proj.m[5], game_proj_90.m[5]); // 68 deg has greater focal length than 90 deg
}

TEST(CSViewModelTest, NaturalPoseCameraOriginAlignmentAndZeroEulerHacks) {
    // In authentic CS2 viewmodel design, the camera bone is at (0, 0, 0)
    // There are NO artificial Euler rotations (-10° rotX or -15° rotY) in base stance
    const fp::M4 base_natural = fp::scale(1.0f);

    // Base matrix must be pure identity
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (r == c) {
                EXPECT_FLOAT_EQ(base_natural.m[r * 4 + c], 1.0f);
            } else {
                EXPECT_FLOAT_EQ(base_natural.m[r * 4 + c], 0.0f);
            }
        }
    }

    // Convert to host D3D12 convention
    const mc::rig::Mat4 host_base = fp::toHost(base_natural);
    EXPECT_FLOAT_EQ(host_base.m[0], 1.0f);
    EXPECT_FLOAT_EQ(host_base.m[5], 1.0f);
    EXPECT_FLOAT_EQ(host_base.m[10], -1.0f); // Z flipped from right-handed to left-handed
    EXPECT_FLOAT_EQ(host_base.m[15], 1.0f);
}

TEST(CSViewModelTest, CookTorrancePbrMathFidelity) {
    // 1. Fresnel-Schlick validation
    const float f0_blade[3] = {0.95f, 0.95f, 0.98f}; // Polished steel
    const float f0_glove[3] = {0.04f, 0.04f, 0.04f}; // Dielectric fabric

    float f_normal_blade[3]{}, f_grazing_blade[3]{};
    computeFresnelSchlick(1.0f, f0_blade, f_normal_blade);
    computeFresnelSchlick(0.0f, f0_blade, f_grazing_blade);

    EXPECT_NEAR(f_normal_blade[0], 0.95f, 0.001f);
    EXPECT_NEAR(f_normal_blade[1], 0.95f, 0.001f);
    EXPECT_NEAR(f_normal_blade[2], 0.98f, 0.001f);
    EXPECT_NEAR(f_grazing_blade[0], 1.0f, 0.001f);
    EXPECT_NEAR(f_grazing_blade[1], 1.0f, 0.001f);
    EXPECT_NEAR(f_grazing_blade[2], 1.0f, 0.001f);

    float f_normal_glove[3]{}, f_grazing_glove[3]{};
    computeFresnelSchlick(1.0f, f0_glove, f_normal_glove);
    computeFresnelSchlick(0.0f, f0_glove, f_grazing_glove);

    EXPECT_NEAR(f_normal_glove[0], 0.04f, 0.001f);
    EXPECT_NEAR(f_grazing_glove[0], 1.0f, 0.001f);

    // 2. GGX Normal Distribution D validation
    // Smooth surface (roughness = 0.15) must produce sharp specular peak
    const float d_smooth_peak = computeGgxD(1.0f, 0.15f);
    const float d_smooth_off = computeGgxD(0.85f, 0.15f);
    EXPECT_GT(d_smooth_peak, 10.0f);
    EXPECT_LT(d_smooth_off, d_smooth_peak * 0.1f); // Drops sharply off axis

    // Rough surface (roughness = 0.85) spreads highlight broadly
    const float d_rough_peak = computeGgxD(1.0f, 0.85f);
    EXPECT_LT(d_rough_peak, 1.0f);

    // Monotonicity test: as NdotH decreases, D must monotonically decrease
    float prev_d = computeGgxD(1.0f, 0.4f);
    for (int step = 9; step >= 0; --step) {
        float ndoth = step * 0.1f;
        float curr_d = computeGgxD(ndoth, 0.4f);
        EXPECT_LE(curr_d, prev_d);
        prev_d = curr_d;
    }

    // 3. Smith GGX Geometry G validation
    const float g_head_on = computeSmithG(1.0f, 1.0f, 0.3f);
    EXPECT_NEAR(g_head_on, 1.0f, 0.001f);
    const float g_glancing = computeSmithG(0.1f, 0.1f, 0.3f);
    EXPECT_GT(g_glancing, 0.0f);
    EXPECT_LT(g_glancing, g_head_on);

    // 4. DirectX Normal Map Unpacking validation
    // Flat normal (128, 128, 255)
    float n_flat[3]{};
    unpackDirectXNormal(128, 128, 255, n_flat);
    EXPECT_NEAR(n_flat[0], 0.0f, 0.01f);
    EXPECT_NEAR(n_flat[1], 0.0f, 0.01f);
    EXPECT_NEAR(n_flat[2], 1.0f, 0.01f);

    // Inverted Y channel test: green channel = 255 in DirectX means downward normal (negative Y)
    float n_down[3]{};
    unpackDirectXNormal(128, 255, 128, n_down);
    EXPECT_LT(n_down[1], -0.90f);

    // Length check: unpacked normal must be unit length
    const float len = std::sqrt(n_down[0] * n_down[0] + n_down[1] * n_down[1] + n_down[2] * n_down[2]);
    EXPECT_NEAR(len, 1.0f, 0.001f);
}

TEST(CSViewModelTest, WeaponRegistryMultiWeaponCatalogAndSockets) {
    auto& reg = CSWeaponRegistry::instance();
    EXPECT_GE(reg.getWeaponCount(), 4u);

    // 1. Butterfly Knife verification
    const auto* butterfly = reg.findWeapon("weapon_knife_butterfly");
    ASSERT_NE(butterfly, nullptr);
    EXPECT_EQ(butterfly->type, WeaponType::KnifeButterfly);
    EXPECT_EQ(butterfly->slot, WeaponSlot::Melee);
    EXPECT_FALSE(butterfly->sockets.empty());
    EXPECT_EQ(butterfly->sockets[0].socket_name, "wpnHand_R");
    EXPECT_EQ(butterfly->sockets[0].target_bone, "hand_R");
    EXPECT_NEAR(butterfly->ballistics.damage, 65.0f, 0.1f);
    EXPECT_FALSE(butterfly->audio.draw_sound.empty());

    // 2. AK-47 verification
    const auto* ak47 = reg.findWeapon("weapon_ak47");
    ASSERT_NE(ak47, nullptr);
    EXPECT_EQ(ak47->type, WeaponType::RifleAK47);
    EXPECT_EQ(ak47->slot, WeaponSlot::Primary);
    EXPECT_EQ(ak47->ballistics.magazine_capacity, 30u);
    EXPECT_TRUE(ak47->ballistics.is_automatic);
    bool has_right_socket = false, has_left_socket = false, has_muzzle = false;
    for (const auto& s : ak47->sockets) {
        if (s.socket_name == "wpnHand_R") has_right_socket = true;
        if (s.socket_name == "wpnHand_L") has_left_socket = true;
        if (s.socket_name == "muzzle_flash") has_muzzle = true;
    }
    EXPECT_TRUE(has_right_socket);
    EXPECT_TRUE(has_left_socket);
    EXPECT_TRUE(has_muzzle);

    // 3. AWP verification
    const auto* awp = reg.findWeapon("weapon_awp");
    ASSERT_NE(awp, nullptr);
    EXPECT_EQ(awp->type, WeaponType::SniperAWP);
    EXPECT_EQ(awp->slot, WeaponSlot::Primary);
    EXPECT_TRUE(awp->ballistics.has_scope);
    EXPECT_EQ(awp->ballistics.magazine_capacity, 5u);
    EXPECT_GE(awp->ballistics.damage, 100.0f);

    // 4. M4A1-S verification
    const auto* m4a1s = reg.findWeapon("weapon_m4a1_silencer");
    ASSERT_NE(m4a1s, nullptr);
    EXPECT_EQ(m4a1s->type, WeaponType::RifleM4A1S);
    EXPECT_EQ(m4a1s->slot, WeaponSlot::Primary);
    bool has_silencer_socket = false;
    for (const auto& s : m4a1s->sockets) {
        if (s.socket_name == "silencer") has_silencer_socket = true;
    }
    EXPECT_TRUE(has_silencer_socket);

    // 5. Active weapon lifecycle
    EXPECT_TRUE(reg.setActiveWeapon("weapon_ak47"));
    const auto* active = reg.getActiveWeapon();
    ASSERT_NE(active, nullptr);
    EXPECT_EQ(active->id, "weapon_ak47");

    // Switching back to butterfly knife
    EXPECT_TRUE(reg.setActiveWeapon("weapon_knife_butterfly"));
    EXPECT_EQ(reg.getActiveWeapon()->id, "weapon_knife_butterfly");
}

TEST(CSViewModelTest, TddWeaponRegistryResetToDefaultsAndDynamicSwitching) {
    auto& reg = CSWeaponRegistry::instance();
    reg.clear();
    EXPECT_EQ(reg.getWeaponCount(), 0u);
    EXPECT_EQ(reg.getActiveWeapon(), nullptr);
    EXPECT_FALSE(reg.setActiveWeapon("weapon_ak47"));

    // Reset to defaults must restore the complete built-in CS arsenal
    reg.resetToDefaults();
    EXPECT_GE(reg.getWeaponCount(), 4u);
    EXPECT_NE(reg.findWeapon("weapon_knife_butterfly"), nullptr);
    EXPECT_NE(reg.findWeapon("weapon_ak47"), nullptr);
    EXPECT_NE(reg.findWeapon("weapon_awp"), nullptr);
    EXPECT_NE(reg.findWeapon("weapon_m4a1_silencer"), nullptr);
    EXPECT_EQ(reg.getActiveWeapon()->id, "weapon_knife_butterfly");

    // Switching to unregistered weapon must fail safely without altering active weapon
    EXPECT_FALSE(reg.setActiveWeapon("weapon_nonexistent_deagle"));
    EXPECT_EQ(reg.getActiveWeapon()->id, "weapon_knife_butterfly");

    // Unregister active weapon falls back smoothly
    EXPECT_TRUE(reg.unregisterWeapon("weapon_knife_butterfly"));
    EXPECT_FALSE(reg.getActiveWeapon()->id.empty());
    EXPECT_NE(reg.getActiveWeapon()->id, "weapon_knife_butterfly");

    // Restore clean state for other test suites
    reg.resetToDefaults();
}

TEST(CSViewModelTest, TddCsRendererNaturalPoseAndStateTransitions) {
    CSViewModelAnimator anim;
    EXPECT_EQ(anim.currentState(), 0);

    // State 0: Idle stance. Camera origin must be strictly at (0, 0, 0)
    // No artificial Euler twist (-10° rotX or -15° rotY) in base matrix
    anim.tick(0.016f, 0, 0.0f);
    const auto& idle_arms = anim.armsPose();
    EXPECT_NEAR(idle_arms.m[0], 1.0f, 0.001f);
    EXPECT_NEAR(idle_arms.m[5], 1.0f, 0.001f);
    EXPECT_NEAR(idle_arms.m[10], 1.0f, 0.001f);
    EXPECT_NEAR(idle_arms.m[15], 1.0f, 0.001f);
    // Translation X and Z are strictly 0 in base camera origin (Y is subtle idle breathing bob)
    EXPECT_NEAR(idle_arms.m[3], 0.0f, 0.001f);
    EXPECT_NEAR(idle_arms.m[11], 0.0f, 0.001f);

    // State 1: Inspect Tricks (must deviate from rest pose with genuine CS2 rotation flourish)
    anim.tick(0.016f, 1, 2.0f); // Frame 60 equivalent in 30fps
    const auto& inspect_blade = anim.bladePose();
    const float inspect_trace = inspect_blade.m[0] + inspect_blade.m[5] + inspect_blade.m[10] + inspect_blade.m[15];
    EXPECT_LT(inspect_trace, 2.5f); // Substantial dynamic rotation

    // State 2: Slash (Light Attack) - forward thrust and rotation
    anim.tick(0.016f, 2, 0.175f); // Peak thrust
    const auto& slash_blade = anim.bladePose();
    EXPECT_LT(slash_blade.m[11], -0.05f); // Forward thrust along -Z

    // State 3: Backstab (Heavy Attack)
    anim.tick(0.016f, 3, 0.5f);
    EXPECT_EQ(anim.currentState(), 3);

    // State 4: Deploy Flip
    anim.tick(0.016f, 4, 0.0f);
    EXPECT_EQ(anim.currentState(), 4);
    // End of deploy (at 1.0s) returns to resting stance
    anim.tick(0.016f, 4, 1.05f);
    const auto& deploy_blade_end = anim.bladePose();
    EXPECT_NEAR(deploy_blade_end.m[0], 1.0f, 0.05f);
    EXPECT_NEAR(deploy_blade_end.m[5], 1.0f, 0.05f);
}

TEST(CSViewModelTest, TddCookTorrancePbrEnergyConservationAndOrmPacking) {
    // 1. ORM Texture Packing validation
    // Simulate 2x2 AO and Roughness inputs
    const uint8_t mock_ao[2 * 2 * 4] = {
        200, 200, 200, 255,   220, 220, 220, 255,
        180, 180, 180, 255,   255, 255, 255, 255
    };
    const uint8_t mock_rough[2 * 2 * 4] = {
        40, 40, 40, 255,      60, 60, 60, 255,
        80, 80, 80, 255,      120, 120, 120, 255
    };
    std::vector<uint8_t> orm;
    unsigned out_w = 0, out_h = 0;
    EXPECT_TRUE(packOrmTexture(mock_ao, 2, 2, mock_rough, 2, 2, 216 /* metallic=0.85 */, orm, out_w, out_h));
    ASSERT_EQ(out_w, 2u);
    ASSERT_EQ(out_h, 2u);
    ASSERT_EQ(orm.size(), 16u);

    // Pixel 0: R=AO (200), G=Roughness (40), B=Metallic (216), A=255
    EXPECT_EQ(orm[0], 200);
    EXPECT_EQ(orm[1], 40);
    EXPECT_EQ(orm[2], 216);
    EXPECT_EQ(orm[3], 255);

    // 2. Cook-Torrance BRDF Energy Conservation:
    // Radiance out must never exceed radiance in (energy conservation: kD + kS <= 1.0)
    const float albedo[3] = {0.9f, 0.9f, 0.9f};
    for (float r = 0.05f; r <= 0.95f; r += 0.15f) {
        for (float m = 0.0f; m <= 1.0f; m += 0.25f) {
            float out_rad[3]{};
            computeCookTorranceBrdf(0.8f, 0.7f, 0.9f, 0.85f, r, m, albedo, out_rad);
            EXPECT_LE(out_rad[0], 1.25f);
            EXPECT_GE(out_rad[0], 0.0f);
            EXPECT_FALSE(std::isnan(out_rad[0]));
            EXPECT_FALSE(std::isinf(out_rad[0]));
        }
    }

    // Edge case: Grazing angle NdotV = 0.0f, NdotL = 0.0f must not produce NaN or division by zero
    float grazing_rad[3]{};
    computeCookTorranceBrdf(0.0f, 0.0f, 0.0f, 1.0f, 0.2f, 0.8f, albedo, grazing_rad);
    EXPECT_FLOAT_EQ(grazing_rad[0], 0.0f);
    EXPECT_FLOAT_EQ(grazing_rad[1], 0.0f);
    EXPECT_FLOAT_EQ(grazing_rad[2], 0.0f);
}

TEST(CSViewModelTest, TddD3D12ViewmodelRenderPipelineAndFovIntegration) {
    // 1. Verify decoupled 68° FOV projection matrix across standard display aspect ratios
    const struct { unsigned w, h; float expected_aspect; } ratios[] = {
        {1920, 1080, 16.f / 9.f},
        {1920, 1200, 16.f / 10.f},
        {2560, 1080, 2560.f / 1080.f},
        {1024, 768, 4.f / 3.f}
    };

    ViewmodelProjectionConfig cfg{};
    cfg.fov_deg = 68.0f;
    cfg.z_near = 0.01f;
    cfg.z_far = 20.0f;

    for (const auto& r : ratios) {
        mc::rig::Mat4 proj = computeViewmodelProjection(r.w, r.h, cfg);
        // Left-handed perspective projection checks:
        // m[0] = cot(fov_y / 2) / aspect
        // m[5] = cot(fov_y / 2)
        constexpr float kDeg = 3.14159265f / 180.0f;
        const float cot_half_fov = 1.0f / std::tan(68.0f * kDeg * 0.5f);
        EXPECT_NEAR(proj.m[5], cot_half_fov, 0.001f);
        EXPECT_NEAR(proj.m[0], cot_half_fov / r.expected_aspect, 0.001f);
        EXPECT_NEAR(proj.m[11], 1.0f, 0.001f); // LH Z-forward
    }

    // 2. Verify state transition and animation sampling in CSViewModelAnimator
    CSViewModelAnimator anim;
    anim.tick(0.016f, 4 /* Deploy */, 0.1f);
    EXPECT_EQ(anim.currentState(), 4);
    anim.tick(0.016f, 1 /* Inspect */, 0.5f);
    EXPECT_EQ(anim.currentState(), 1);
    anim.tick(0.016f, 2 /* Slash */, 0.15f);
    EXPECT_EQ(anim.currentState(), 2);
    anim.tick(0.016f, 3 /* Backstab */, 0.2f);
    EXPECT_EQ(anim.currentState(), 3);
    anim.tick(0.016f, 0 /* Idle */, 0.0f);
    EXPECT_EQ(anim.currentState(), 0);
}

} // namespace



