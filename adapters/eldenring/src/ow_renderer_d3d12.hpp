#pragma once

// D3D12 renderer for the Overwatch (Doomfist) first-person gauntlet.
// Self-contained: its own root signature, PSO, depth target, vertex/index
// buffers and three texture slots. It shares no descriptor heap, no shader and
// no class with the CS2 viewmodel renderer.

#include <d3d12.h>

#include <cstdint>
#include <string>

#include "mc/rig.hpp"
#include "ow_viewmodel.hpp"

namespace erow {

class OwRenderer {
public:
    using LogFn = void (*)(const char* fmt, ...);

    bool init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void release();
    [[nodiscard]] bool ready() const { return pso_ != nullptr && vb_ != nullptr; }

    bool loadModel(ID3D12Device* device, const std::string& path);
    [[nodiscard]] bool modelLoaded() const { return model_loaded_; }

    // t0 albedo, t1 normal (DirectX, G inverted), t2 ORM (R=AO, G=Roughness,
    // B=Metallic). The three descriptors are written starting at `first_slot`.
    bool loadTextures(ID3D12Device* device, const std::string& albedo_png, const std::string& normal_png,
                      const std::string& pbr_png, D3D12_CPU_DESCRIPTOR_HANDLE first_slot);

    void setConfig(const eldenring::ow::OwViewmodelConfig& cfg) { cfg_ = cfg; }
    [[nodiscard]] const eldenring::ow::OwViewmodelConfig& config() const { return cfg_; }
    [[nodiscard]] mc::rig::Mat4 computeProjection(unsigned width, unsigned height) const {
        return eldenring::ow::computeOwProjection(width, height, cfg_);
    }

    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0) {
        animator_.tick(dt, state, anim_time, variant);
    }
    [[nodiscard]] const eldenring::ow::OwAnimator& animator() const { return animator_; }

    void draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
              D3D12_GPU_DESCRIPTOR_HANDLE tex_table, unsigned width, unsigned height,
              const mc::rig::Mat4& projection, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
              D3D12_CPU_DESCRIPTOR_HANDLE dsv);

private:
    struct TextureGpu {
        ID3D12Resource* tex{nullptr};
        ID3D12Resource* upload{nullptr};
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        bool pending{false};

        void release();
        bool create(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height,
                    D3D12_CPU_DESCRIPTOR_HANDLE slot, bool srgb = false);
        void recordUpload(ID3D12GraphicsCommandList* list);
    };

    ID3D12RootSignature* root_{nullptr};
    ID3D12PipelineState* pso_{nullptr};
    ID3D12Resource* vb_{nullptr};
    ID3D12Resource* ib_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    ID3D12Device* device_{nullptr};
    ID3D12Resource* depth_tex_{nullptr};
    ID3D12DescriptorHeap* dsv_heap_{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_{};
    unsigned depth_w_{0};
    unsigned depth_h_{0};

    TextureGpu albedo_tex_;
    TextureGpu normal_tex_;
    TextureGpu pbr_tex_;

    eldenring::ow::OwModel model_;
    eldenring::ow::OwAnimator animator_;
    eldenring::ow::OwViewmodelConfig cfg_{};
    bool model_loaded_{false};
    LogFn log_{nullptr};

    bool ensureDepth(ID3D12Device* device, unsigned width, unsigned height);
};

} // namespace erow
