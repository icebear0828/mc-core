#include <gtest/gtest.h>

// The backend-neutral rig seam (mc/rig/rig_renderer.hpp). Everything here runs without a GPU: the
// point of the seam is that the core can describe a rig frame through IRigRenderer alone, and that
// a host backend (Elden Ring's ErRigRenderer) satisfies the same contract at compile time.

#include "mc/rig/rig_renderer.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include "render/er_rig_renderer.hpp"
#endif

namespace {

// The calls the seam can make, in the order a frame makes them.
enum class Call {
    Init,
    SetSkin,
    SetSceneDepth,
    SetDepthConvention,
    SetHeldItem,
    SetBlocks,
    Draw,
    DrawFirstPerson,
    AmbientProbe,
    HurtTint,
    GroundShadow,
};

// Records every call so the test can prove the whole interface is reachable through IRigRenderer
// alone, with opaque handles and no host type.
class RecordingRigRenderer final : public mc::rig::IRigRenderer {
public:
    bool init(mc::rig::HostHandle device) override {
        calls.push_back(Call::Init);
        device_seen = device;
        return init_ok;
    }

    bool setSkin(const uint8_t* rgba, unsigned width, unsigned height, mc::rig::GpuHandle slot) override {
        calls.push_back(Call::SetSkin);
        skin_bytes = rgba;
        skin_width = width;
        skin_height = height;
        skin_slot = slot;
        return true;
    }

    void setSceneDepth(mc::rig::HostHandle depth_texture, mc::rig::GpuHandle slot) override {
        calls.push_back(Call::SetSceneDepth);
        depth_seen = depth_texture;
        depth_slot = slot;
    }

    void setDepthConvention(const mc::rig::DepthConvention& convention) override {
        calls.push_back(Call::SetDepthConvention);
        convention_seen = convention;
    }

    void setHeldItem(const mc::HandItems& hands, mc::rig::GpuHandle atlas_table) override {
        calls.push_back(Call::SetHeldItem);
        hands_seen = hands;
        atlas_table_seen = atlas_table;
    }

    void setBlocks(const mc::rig::RigMesh& mesh) override {
        calls.push_back(Call::SetBlocks);
        block_vertices = mesh.vertices.size();
    }

    void draw(const mc::rig::RigDraw& request) override {
        calls.push_back(Call::Draw);
        last_draw = request;
    }

    void drawFirstPerson(const mc::rig::RigFirstPersonDraw& request) override {
        calls.push_back(Call::DrawFirstPerson);
        last_first_person = request;
    }

    void setAmbientProbe(float u, float v) override {
        calls.push_back(Call::AmbientProbe);
        probe = {u, v};
    }

    void setHurtTint(float amount) override {
        calls.push_back(Call::HurtTint);
        hurt_tint = amount;
    }

    void setGroundShadow(const mc::rig::RigMesh& mesh) override {
        calls.push_back(Call::GroundShadow);
        shadow_indices = mesh.indices.size();
    }

    bool init_ok{true};
    std::vector<Call> calls;
    mc::rig::HostHandle device_seen{nullptr};
    const uint8_t* skin_bytes{nullptr};
    unsigned skin_width{0};
    unsigned skin_height{0};
    mc::rig::GpuHandle skin_slot{0};
    mc::rig::HostHandle depth_seen{nullptr};
    mc::rig::GpuHandle depth_slot{0};
    mc::rig::DepthConvention convention_seen{};
    mc::HandItems hands_seen{};
    mc::rig::GpuHandle atlas_table_seen{0};
    size_t block_vertices{0};
    size_t shadow_indices{0};
    mc::rig::RigDraw last_draw{};
    mc::rig::RigFirstPersonDraw last_first_person{};
    std::pair<float, float> probe{0.f, 0.f};
    float hurt_tint{0.f};
};

// A backend that can only draw: the optional hooks must stay no-ops, so it still implements the seam.
class MinimalRigRenderer final : public mc::rig::IRigRenderer {
public:
    bool init(mc::rig::HostHandle) override { return true; }
    bool setSkin(const uint8_t*, unsigned, unsigned, mc::rig::GpuHandle) override { return true; }
    void setSceneDepth(mc::rig::HostHandle, mc::rig::GpuHandle) override {}
    void setDepthConvention(const mc::rig::DepthConvention&) override {}
    void setHeldItem(const mc::HandItems&, mc::rig::GpuHandle) override {}
    void setBlocks(const mc::rig::RigMesh&) override {}
    void draw(const mc::rig::RigDraw&) override {}
    void drawFirstPerson(const mc::rig::RigFirstPersonDraw&) override {}
};

static_assert(!std::is_abstract_v<MinimalRigRenderer>, "the optional hooks must have defaults");
static_assert(std::has_virtual_destructor_v<mc::rig::IRigRenderer>,
              "a backend must be destroyable through the seam");

int* OpaquePtr(uintptr_t value) { return reinterpret_cast<int*>(value); }

} // namespace

TEST(RigRendererInterface, EveryCallReachesTheBackendThroughTheSeam) {
    RecordingRigRenderer backend;
    mc::rig::IRigRenderer& seam = backend;

    ASSERT_TRUE(seam.init(OpaquePtr(0x10)));

    const uint8_t skin[4] = {1, 2, 3, 4};
    ASSERT_TRUE(seam.setSkin(skin, 64, 64, 0x20));

    seam.setSceneDepth(OpaquePtr(0x30), 0x40);

    mc::rig::DepthConvention convention;
    convention.reverse_z = true;
    convention.depth_times_distance = 0.0501f;
    convention.relative_bias = 0.08f;
    convention.absolute_bias = 0.05f;
    seam.setDepthConvention(convention);

    mc::HandItems hands;
    hands.main.item = mc::ItemId::DiamondSword;
    hands.main.present = true;
    seam.setHeldItem(hands, 0x50);

    mc::rig::RigMesh blocks;
    blocks.vertices.push_back({0.f, 0.f, 0.f, 0.f, 0.f});
    seam.setBlocks(blocks);

    mc::rig::PartMatrices parts{};
    mc::rig::RigDraw draw;
    draw.command_list = OpaquePtr(0x60);
    draw.descriptor_heap = OpaquePtr(0x70);
    draw.depth_table = 0x80;
    draw.render_target = 0x90;
    draw.width = 1920;
    draw.height = 1080;
    draw.part_world = &parts;
    draw.keep_depth = true;
    seam.draw(draw);

    mc::rig::RigFirstPersonDraw first_person;
    first_person.command_list = OpaquePtr(0xA0);
    first_person.skin_table = 0xB0;
    first_person.atlas_table = 0xC0;
    first_person.item = 7;
    seam.drawFirstPerson(first_person);

    seam.setAmbientProbe(0.25f, 0.75f);
    seam.setHurtTint(0.4f);
    seam.setGroundShadow(blocks);

    const std::vector<Call> expected{
        Call::Init,          Call::SetSkin,     Call::SetSceneDepth, Call::SetDepthConvention,
        Call::SetHeldItem,   Call::SetBlocks,   Call::Draw,          Call::DrawFirstPerson,
        Call::AmbientProbe,  Call::HurtTint,    Call::GroundShadow,
    };
    EXPECT_EQ(backend.calls, expected);

    // The opaque handles arrive unchanged: no host type is involved in the core header.
    EXPECT_EQ(backend.device_seen, OpaquePtr(0x10));
    EXPECT_EQ(backend.skin_bytes, skin);
    EXPECT_EQ(backend.skin_width, 64u);
    EXPECT_EQ(backend.skin_height, 64u);
    EXPECT_EQ(backend.skin_slot, 0x20u);
    EXPECT_EQ(backend.depth_seen, OpaquePtr(0x30));
    EXPECT_EQ(backend.depth_slot, 0x40u);
    EXPECT_FLOAT_EQ(backend.convention_seen.depth_times_distance, 0.0501f);
    EXPECT_EQ(backend.hands_seen.main.item, mc::ItemId::DiamondSword);
    EXPECT_EQ(backend.atlas_table_seen, 0x50u);
    EXPECT_EQ(backend.block_vertices, 1u);
    EXPECT_EQ(backend.last_draw.command_list, OpaquePtr(0x60));
    EXPECT_EQ(backend.last_draw.part_world, &parts);
    EXPECT_TRUE(backend.last_draw.keep_depth);
    EXPECT_EQ(backend.last_first_person.item, 7u);
    EXPECT_FLOAT_EQ(backend.probe.first, 0.25f);
    EXPECT_FLOAT_EQ(backend.hurt_tint, 0.4f);
    EXPECT_EQ(backend.shadow_indices, 0u);
}

TEST(RigRendererInterface, OptionalHooksAreNoOpsForABackendThatCannotDoThem) {
    MinimalRigRenderer backend;
    mc::rig::IRigRenderer& seam = backend;

    seam.setAmbientProbe(0.5f, 0.5f);
    seam.setHurtTint(1.f);
    seam.setGroundShadow(mc::rig::RigMesh{});
    SUCCEED();
}

TEST(RigRendererInterface, ADrawRequestStartsWithoutHostHandles) {
    const mc::rig::RigDraw draw;
    EXPECT_EQ(draw.command_list, nullptr);
    EXPECT_EQ(draw.descriptor_heap, nullptr);
    EXPECT_EQ(draw.depth_table, 0u);
    EXPECT_EQ(draw.render_target, 0u);
    EXPECT_EQ(draw.width, 0u);
    EXPECT_EQ(draw.height, 0u);
    EXPECT_EQ(draw.part_world, nullptr);
    EXPECT_FALSE(draw.keep_depth);

    const mc::rig::RigFirstPersonDraw first_person;
    EXPECT_EQ(first_person.command_list, nullptr);
    EXPECT_EQ(first_person.descriptor_heap, nullptr);
    EXPECT_EQ(first_person.skin_table, 0u);
    EXPECT_EQ(first_person.atlas_table, 0u);
    EXPECT_EQ(first_person.render_target, 0u);
    EXPECT_EQ(first_person.width, 0u);
    EXPECT_EQ(first_person.height, 0u);
    EXPECT_EQ(first_person.item, 0u);
}

TEST(RigRendererInterface, OneMatrixPerStevePart) {
    EXPECT_EQ(mc::rig::kRigPartCount, static_cast<size_t>(mc::StevePart::Count));
    EXPECT_EQ(mc::rig::kRigPartCount, 12u);
    EXPECT_EQ(sizeof(mc::rig::PartMatrices), mc::rig::kRigPartCount * sizeof(mc::rig::Mat4));
}

#ifdef _WIN32
// The adapter's SteveParams mapping, tested without a GPU: MakeSteveParams is what buildParams()
// delegates to, and it is header-inline so the test target needs no D3D12 objects.
TEST(RigRendererInterface, SteveParamsCarryTheSceneDepthTextureSize) {
    // steve.hlsl maps a back-buffer pixel onto the scene depth with
    // `int2(i.pos.xy / dims.zw * dims.xy)`: dims.zw is the depth texture's own size, which a
    // RigDraw does not carry (it only knows the back buffer). Leaving it at 0 samples texel (0,0)
    // for every pixel, so one corner of the scene depth decides the whole figure.
    mc::rig::DepthConvention convention;
    convention.reverse_z = true;
    convention.depth_times_distance = 0.0501f;
    convention.relative_bias = 0.08f;
    convention.absolute_bias = 0.05f;

    const erov::SteveParams params = erov::MakeSteveParams(convention, /*has_scene_depth=*/true,
                                                           2560, 1440, mc::HandItems{}, 0, 0.f);
    EXPECT_FLOAT_EQ(params.mode, 1.f);
    EXPECT_FLOAT_EQ(params.depth_w, 2560.f);
    EXPECT_FLOAT_EQ(params.depth_h, 1440.f);
    EXPECT_FLOAT_EQ(params.depth_const, 0.0501f);
    EXPECT_FLOAT_EQ(params.rel_bias, 0.08f);
    EXPECT_FLOAT_EQ(params.abs_bias, 0.05f);
}

TEST(RigRendererInterface, SteveParamsWithoutASceneDepthDrawOverEverything) {
    mc::rig::DepthConvention convention;
    convention.reverse_z = true;

    const erov::SteveParams params =
        erov::MakeSteveParams(convention, /*has_scene_depth=*/false, 0, 0, mc::HandItems{}, 0, 0.f);
    EXPECT_FLOAT_EQ(params.mode, 0.f); // no depth SRV: the shader must not compare
    EXPECT_FLOAT_EQ(params.depth_w, 0.f);
    EXPECT_FLOAT_EQ(params.depth_h, 0.f);
}

TEST(RigRendererInterface, SteveParamsWithoutReverseZDrawOverEverything) {
    // Only reverse-Z is understood; any other convention would occlude the wrong pixels.
    mc::rig::DepthConvention convention;
    convention.reverse_z = false;

    const erov::SteveParams params = erov::MakeSteveParams(convention, /*has_scene_depth=*/true,
                                                           1920, 1080, mc::HandItems{}, 0, 0.f);
    EXPECT_FLOAT_EQ(params.mode, 0.f);
}

TEST(RigRendererInterface, SteveParamsCarryTheHandsTheHeldTableAndTheHurtTint) {
    mc::HandItems hands;
    hands.main.item = mc::ItemId::DiamondSword;
    hands.main.present = true;

    const erov::SteveParams params = erov::MakeSteveParams(
        mc::rig::DepthConvention{}, /*has_scene_depth=*/false, 0, 0, hands, 0x1234, 0.4f);
    EXPECT_EQ(params.hands.main.item, mc::ItemId::DiamondSword);
    EXPECT_TRUE(params.hands.main.present);
    EXPECT_EQ(params.held_table.ptr, 0x1234ull);
    EXPECT_FLOAT_EQ(params.tint[0], 1.f); // Minecraft's hurt flash is red over the lit skin
    EXPECT_FLOAT_EQ(params.tint[3], 0.4f);
}

TEST(RigRendererInterface, ErRigRendererImplementsTheSeam) {
    // Compile-time conformance: the Elden Ring backend derives from the seam and overrides every
    // pure virtual (otherwise it would be abstract and the static_assert below would fail).
    static_assert(std::is_base_of_v<mc::rig::IRigRenderer, erov::ErRigRenderer>,
                  "ErRigRenderer must implement mc::rig::IRigRenderer");
    static_assert(!std::is_abstract_v<erov::ErRigRenderer>,
                  "ErRigRenderer must override every pure virtual of the seam");
    static_assert(std::has_virtual_destructor_v<erov::ErRigRenderer>);

    EXPECT_TRUE((std::is_base_of_v<mc::rig::IRigRenderer, erov::ErRigRenderer>));
}
#endif
