#pragma once

// Elden Ring's implementation of the backend-neutral rig seam (mc/rig/rig_renderer.hpp).
// Every call forwards to SteveRenderer, which owns the D3D12 pipelines, meshes and depth buffer;
// this class only converts the seam's opaque handles back into D3D12 objects and keeps the state
// (depth convention, held items, hurt tint) that SteveParams wants per draw.

// SteveRenderer pulls windows.h (through d3d12.h); keep its min/max macros out of the core headers.
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "mc/rig/rig_renderer.hpp"

#include "steve_renderer_d3d12.hpp"

#include <cstdint>
#include <vector>

namespace erov {

// The SteveParams a frame is drawn with, derived from the state the seam set. `depth_width` and
// `depth_height` are the scene depth texture's own size, not the back buffer's: steve.hlsl maps a
// back-buffer pixel onto the depth texture with `int2(i.pos.xy / dims.zw * dims.xy)`, so a draw that
// leaves them at 0 samples texel (0,0) for every pixel and one corner decides the whole figure.
// Header-inline so tests can cover the mapping without a D3D12 device.
[[nodiscard]] inline SteveParams MakeSteveParams(const mc::rig::DepthConvention& depth, bool has_scene_depth,
                                                 unsigned depth_width, unsigned depth_height, const mc::HandItems& hands,
                                                 mc::rig::GpuHandle held_table, float hurt_tint) {
    SteveParams params;
    // Mode 1 hides the figure behind the scene; without a calibrated reverse-Z depth buffer the
    // shader cannot compare and the figure is drawn over everything instead.
    params.mode = (has_scene_depth && depth.reverse_z) ? 1.f : 0.f;
    params.depth_const = depth.depth_times_distance;
    params.rel_bias = depth.relative_bias;
    params.abs_bias = depth.absolute_bias;
    params.depth_w = static_cast<float>(depth_width);
    params.depth_h = static_cast<float>(depth_height);
    params.held_table.ptr = static_cast<UINT64>(held_table);
    params.hands = hands;
    params.tint[0] = 1.f; // Minecraft's hurt flash: red over the lit skin
    params.tint[3] = hurt_tint;
    return params;
}

class ErRigRenderer final : public mc::rig::IRigRenderer {
public:
    using LogFn = SteveRenderer::LogFn;

    explicit ErRigRenderer(DXGI_FORMAT rtv_format = DXGI_FORMAT_R8G8B8A8_UNORM, LogFn log = {});

    bool init(mc::rig::HostHandle device) override;
    bool setSkin(const uint8_t* rgba, unsigned width, unsigned height, mc::rig::GpuHandle slot) override;
    void setSceneDepth(mc::rig::HostHandle depth_texture, mc::rig::GpuHandle slot) override;
    void setDepthConvention(const mc::rig::DepthConvention& convention) override;
    void setHeldItem(const mc::HandItems& hands, mc::rig::GpuHandle atlas_table) override;
    void setBlocks(const mc::rig::RigMesh& mesh) override;
    void draw(const mc::rig::RigDraw& request) override;
    void drawFirstPerson(const mc::rig::RigFirstPersonDraw& request) override;
    void setHurtTint(float amount) override;

    // First person needs the item atlas, which the seam's init() does not carry: the host builds
    // these once from its own asset pipeline. They forward to SteveRenderer unchanged.
    bool initFirstPerson(mc::rig::HostHandle device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                         const std::vector<SteveRenderer::FpItemCell>& cells);
    bool initHeldItems(mc::rig::HostHandle device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h,
                       const std::vector<SteveRenderer::FpItemCell>& cells);

    [[nodiscard]] SteveRenderer& steve() { return renderer_; }
    [[nodiscard]] const SteveRenderer& steve() const { return renderer_; }

private:
    // The SteveParams SteveRenderer draws with, built from the state the seam set.
    [[nodiscard]] SteveParams buildParams() const;

    SteveRenderer renderer_;
    DXGI_FORMAT rtv_format_;
    LogFn log_;
    ID3D12Device* device_{nullptr};
    ID3D12Resource* scene_depth_{nullptr};
    // The scene depth texture's own size, kept because RigDraw only carries the back-buffer size and
    // the shader needs both (see MakeSteveParams).
    unsigned scene_depth_w_{0};
    unsigned scene_depth_h_{0};
    mc::rig::DepthConvention depth_{};
    mc::HandItems hands_{};
    mc::rig::GpuHandle held_table_{0};
    float hurt_tint_{0.f};
};

} // namespace erov
