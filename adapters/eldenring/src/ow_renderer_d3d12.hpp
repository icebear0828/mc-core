#pragma once

// D3D12 renderer for the Overwatch (Doomfist) first-person gauntlet.
// Self-contained: its own root signature, PSO, depth target, vertex/index
// buffers and three texture slots. It shares no descriptor heap, no shader and
// no class with the CS2 viewmodel renderer.

#include <d3d12.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "mc/rig.hpp"
#include "ow_viewmodel.hpp"

namespace erow {

class OwRenderer {
public:
    using LogFn = void (*)(const char* fmt, ...);

    bool init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void release();
    [[nodiscard]] bool ready() const { return pso_ != nullptr && (vb_ != nullptr || skinned_vb_ != nullptr); }

    bool loadModel(ID3D12Device* device, const std::string& path);
    [[nodiscard]] bool modelLoaded() const { return model_loaded_; }

    // Rigid VFX mesh (OWVF): the muzzle flash sphere extracted from the PBZ
    // mod. Drawn with the additive PSO while since_shot() < kMuzzleFlashTime.
    bool loadVfxMesh(ID3D12Device* device, const std::string& path);
    [[nodiscard]] bool vfxLoaded() const { return vfx_index_count_ != 0; }
    void setSinceShot(float since_shot) { since_shot_ = since_shot; }
    // PageUp/PageDown in game: toggle the additive pass depth test and cycle
    // the flash size. The values live in RuntimeContext so both threads agree.
    void setVfxDebug(bool nodepth, float scale) {
        vfx_nodepth_ = nodepth;
        vfx_scale_ = scale;
    }
    static constexpr float kMuzzleFlashTime = 0.09f;

    // One texture set per material (t0 albedo, t1 normal, t2 ORM). Each set
    // occupies three consecutive descriptors, starting at `first_slot`.
    struct MaterialTextures {
        std::string albedo;
        std::string normal;
        std::string pbr;
    };
    static constexpr uint32_t kMaxMaterials = 4;
    bool loadTextures(ID3D12Device* device, const std::vector<MaterialTextures>& materials,
                      D3D12_CPU_DESCRIPTOR_HANDLE first_slot);

    void setConfig(const eldenring::ow::OwViewmodelConfig& cfg) { cfg_ = cfg; }
    [[nodiscard]] const eldenring::ow::OwViewmodelConfig& config() const { return cfg_; }
    [[nodiscard]] mc::rig::Mat4 computeProjection(unsigned width, unsigned height) const {
        return eldenring::ow::computeOwProjection(width, height, cfg_);
    }

    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0) {
        animator_.tick(dt, state, anim_time, variant, cfg_.blend_seconds);
    }
    [[nodiscard]] const eldenring::ow::OwAnimator& animator() const { return animator_; }

    // Duration of an authored clip in seconds, or 0 when the asset has not
    // loaded (the dispatcher then uses the baked fallback constants). Safe to
    // call from the input thread while the render thread loads the asset.
    [[nodiscard]] float clipDuration(const char* name) const;

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
    ID3D12PipelineState* vfx_pso_{nullptr};
    ID3D12PipelineState* vfx_pso_nodepth_{nullptr};
    ID3D12Resource* vb_{nullptr};
    ID3D12Resource* ib_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    // Muzzle flash (OWVF): rigid sphere from SM_火花球, drawn additively at the
    // right index fingertip while since_shot_ < kMuzzleFlashTime.
    ID3D12Resource* vfx_vb_{nullptr};
    ID3D12Resource* vfx_ib_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vfx_vbv_{};
    D3D12_INDEX_BUFFER_VIEW vfx_ibv_{};
    UINT vfx_index_count_{0};
    int muzzle_bone_{-1};
    // Bind-space centroid of the vertices the muzzle bone actually drives. The
    // palette translation is the bind-space origin, which is far from the hand;
    // transforming this centroid by the live palette gives the fingertip.
    float muzzle_bind_[3]{0.0f, 0.0f, 0.0f};
    float since_shot_{1.0f};
    // Live tuning while comparing in game (PageUp / PageDown, forwarded by
    // overlay_scene from RuntimeContext).
    bool vfx_nodepth_{false};
    float vfx_scale_{0.004f};

    // CPU skinning output: a small ring of upload vertex buffers written every
    // frame (`3 * floats` per vertex), rotated so the GPU never reads a slot
    // the CPU is rewriting. Three slots cover a frame or two of latency.
    static constexpr uint32_t kSkinnedSlots = 4;
    ID3D12Resource* skinned_vb_{nullptr};
    void* skinned_vb_mapped_{nullptr};
    UINT skinned_slot_stride_{0};
    uint32_t skinned_slot_{0};

    ID3D12Device* device_{nullptr};
    ID3D12Resource* depth_tex_{nullptr};
    ID3D12DescriptorHeap* dsv_heap_{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_{};
    unsigned depth_w_{0};
    unsigned depth_h_{0};

    std::array<TextureGpu, kMaxMaterials> albedo_tex_{};
    std::array<TextureGpu, kMaxMaterials> normal_tex_{};
    std::array<TextureGpu, kMaxMaterials> pbr_tex_{};
    uint32_t material_count_{0};
    UINT srv_inc_{0};

    eldenring::ow::OwModel model_;
    mutable std::mutex model_mutex_; // guards model_.clips against the input thread
    eldenring::ow::OwAnimator animator_;
    std::vector<eldenring::fp::M4> effective_palette_; // shoot damping scratch
    eldenring::ow::OwViewmodelConfig cfg_{};
    bool model_loaded_{false};
    LogFn log_{nullptr};

    bool ensureDepth(ID3D12Device* device, unsigned width, unsigned height);
};

} // namespace erow
