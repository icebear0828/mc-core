#pragma once

#include <d3d12.h>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "cs_viewmodel.hpp"
#include "cs_weapon_registry.hpp"
#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

namespace erov {

namespace fp = eldenring::fp;

using ViewmodelProjectionConfig = eldenring::cs::ViewmodelProjectionConfig;
using eldenring::cs::computeViewmodelProjection;

// Frees the decoded-PNG cache. Called once the init uploads are done and on teardown;
// weapon switches decode on demand and keep their maps cached for later switches.
void ReleaseCsDecodeCache();

class CsRenderer {
public:
    using LogFn = void (*)(const char* fmt, ...);

    bool init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void release();
    [[nodiscard]] bool ready() const { return pso_ != nullptr && vb_ != nullptr; }

    // Dedicated D32_FLOAT depth buffer for the viewmodel. Keeping it separate
    // from the Steve/MC depth buffer guarantees the CS pass always has a valid
    // DSV and never clears another pass's depth.
    bool ensureDepth(ID3D12Device* device, unsigned width, unsigned height);

    bool loadModel(ID3D12Device* device, const std::string& path);

    // Textures:
    // t0: knife_albedo
    // t1: glove_albedo
    // t2: knife_normal (DirectX format: G inverted)
    // t3: glove_normal
    // t4: knife_pbr (ORM: R=AO, G=Roughness, B=Metallic)
    // t5: glove_pbr
    // t6: bare_arm_albedo
    // t7: bare_arm_normal
    // t8: bare_arm_pbr
    // t9: sleeve_albedo
    // t10: sleeve_normal
    // t11: sleeve_pbr
    bool setKnifeTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setGloveTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setKnifeNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setGloveNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setKnifePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setGlovePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    // Gun maps live in their own GPU resources. Reusing the knife resources
    // would release the texture the knife descriptor table still points at,
    // which makes the knife sample the gun's albedo.
    bool setGunTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setGunNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setGunPbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setBareArmTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setBareArmNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setBareArmPbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setSleeveTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setSleeveNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);
    bool setSleevePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);

    bool loadTexturesFromFiles(ID3D12Device* device,
                               const std::string& knife_png,
                               const std::string& glove_png,
                               D3D12_CPU_DESCRIPTOR_HANDLE knife_slot,
                               D3D12_CPU_DESCRIPTOR_HANDLE glove_slot,
                               const std::string& knife_norm_png = "",
                               const std::string& glove_norm_png = "",
                               const std::string& knife_pbr_png = "",
                               const std::string& glove_pbr_png = "",
                               const std::string& bare_png = "",
                               const std::string& bare_norm_png = "",
                               const std::string& bare_pbr_png = "",
                               const std::string& sleeve_png = "",
                               const std::string& sleeve_norm_png = "",
                               const std::string& sleeve_pbr_png = "");

    // Viewmodel projection & stance management. The input thread can retune
    // this at runtime (debug keys), so guard it against the render thread.
    void setViewmodelConfig(const ViewmodelProjectionConfig& cfg) {
        std::lock_guard<std::mutex> lock(vm_config_mutex_);
        vm_config_ = cfg;
    }
    [[nodiscard]] ViewmodelProjectionConfig getViewmodelConfig() const {
        std::lock_guard<std::mutex> lock(vm_config_mutex_);
        return vm_config_;
    }
    [[nodiscard]] mc::rig::Mat4 computeViewmodelProjection(unsigned width, unsigned height) const {
        return eldenring::cs::computeViewmodelProjection(width, height, getViewmodelConfig());
    }

    // Pose getters for verification and rendering
    [[nodiscard]] const fp::M4& bladePose() const { return animator_.bladePose(); }
    [[nodiscard]] const fp::M4& handleFrontPose() const { return animator_.handleFrontPose(); }
    [[nodiscard]] const fp::M4& handleRearPose() const { return animator_.handleRearPose(); }
    [[nodiscard]] const fp::M4& lockPose() const { return animator_.lockPose(); }
    [[nodiscard]] const fp::M4& armsPose() const { return animator_.armsPose(); }
    [[nodiscard]] const fp::M4& basePose() const { return animator_.basePose(); }

    // Weapon registry pipeline
    bool loadWeapon(ID3D12Device* device, const eldenring::cs::CSWeaponDefinition& def);

    // Swap the gun albedo/normal/ORM maps into the weapon texture slots
    // (t0/t2/t4). The glove slots are left untouched.
    bool loadWeaponTextures(ID3D12Device* device,
                            const eldenring::cs::CSWeaponDefinition& def,
                            D3D12_CPU_DESCRIPTOR_HANDLE weapon_slot);

    [[nodiscard]] const eldenring::cs::CSWeaponDefinition* activeWeapon() const {
        return has_active_weapon_ ? &active_weapon_def_ : nullptr;
    }

    // Cook-Torrance PBR & normal map math verification (accessible for unit test suites)
    static float computeGgxD(float NdotH, float roughness) {
        return eldenring::cs::computeGgxD(NdotH, roughness);
    }
    static float computeSmithG(float NdotV, float NdotL, float roughness) {
        return eldenring::cs::computeSmithG(NdotV, NdotL, roughness);
    }
    static void computeFresnelSchlick(float cos_theta, const float f0[3], float out_f[3]) {
        eldenring::cs::computeFresnelSchlick(cos_theta, f0, out_f);
    }
    static void unpackDirectXNormal(uint8_t r, uint8_t g, uint8_t b, float out_n[3]) {
        eldenring::cs::unpackDirectXNormal(r, g, b, out_n);
    }

    void tick(float dt, uint8_t state, float anim_time, uint8_t variant = 0, bool hit = true);

    // Duration (seconds) of a clip in the currently loaded CSVM, or 0.
    // Thread safe: reads a snapshot published by loadModel.
    [[nodiscard]] float clipDuration(const char* name) const;

    // Screen-space position of the weapon's muzzle track under the viewmodel projection.
    // False when the loaded model has no "muzzle" track (the knife).
    bool muzzleScreen(unsigned width, unsigned height, float& out_x, float& out_y) const;

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

        void release() {
            if (tex) { tex->Release(); tex = nullptr; }
            if (upload) { upload->Release(); upload = nullptr; }
            pending = false;
        }

        bool create(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot, bool srgb = false);
        void recordUpload(ID3D12GraphicsCommandList* list);
    };

    ID3D12RootSignature* root_{nullptr};
    ID3D12PipelineState* pso_{nullptr};
    ID3D12PipelineState* pso_depth_flip_{nullptr}; // debug: reverse comparison
    ID3D12Resource* vb_{nullptr};
    ID3D12Resource* ib_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    // Dedicated viewmodel depth target.
    ID3D12Device* device_{nullptr};
    ID3D12Resource* depth_tex_{nullptr};
    ID3D12DescriptorHeap* dsv_heap_{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv_cpu_{};
    unsigned depth_w_{0};
    unsigned depth_h_{0};

    // CSVM v4 per-vertex skinning palette. Four 512-byte slots are rotated per
    // draw so a frame in flight never sees the next frame's bone matrices.
    static constexpr uint32_t kMaxSkinBones = 8;
    static constexpr uint32_t kBoneCbSlots = 4;
    static constexpr uint32_t kBoneCbSlotBytes = kMaxSkinBones * 64;
    ID3D12Resource* bone_cb_{nullptr};
    void* bone_cb_mapped_{nullptr};
    D3D12_GPU_VIRTUAL_ADDRESS bone_cb_gpu_{0};
    uint32_t bone_cb_slot_{0};

    TextureGpu knife_tex_;
    TextureGpu glove_tex_;
    TextureGpu knife_normal_tex_;
    TextureGpu glove_normal_tex_;
    TextureGpu knife_pbr_tex_;
    TextureGpu glove_pbr_tex_;
    TextureGpu gun_tex_;
    TextureGpu gun_normal_tex_;
    TextureGpu gun_pbr_tex_;
    TextureGpu bare_tex_;
    TextureGpu bare_normal_tex_;
    TextureGpu bare_pbr_tex_;
    TextureGpu sleeve_tex_;
    TextureGpu sleeve_normal_tex_;
    TextureGpu sleeve_pbr_tex_;

    // First-person sleeve tuning (user-tuned in game). The narrow viewmodel
    // FOV crops the open shoulder end of the agent sleeve; the small offsets
    // keep the gun centred. FOV 68 puts the shoulder back on screen.
    static ViewmodelProjectionConfig defaultCsViewConfig() {
        ViewmodelProjectionConfig cfg;
        cfg.fov_deg = 32.0f;
        cfg.offset_x = 0.000f;
        cfg.offset_y = -0.010f;
        cfg.offset_z = -0.030f;
        return cfg;
    }
    ViewmodelProjectionConfig vm_config_{defaultCsViewConfig()};
    mutable std::mutex vm_config_mutex_;
    eldenring::cs::CSWeaponDefinition active_weapon_def_{};
    bool has_active_weapon_{false};

    eldenring::cs::CSViewModel model_;
    eldenring::cs::CSViewModelAnimator animator_;
    // Published by loadModel, read by the input thread via CsClipDuration.
    mutable std::mutex clip_durations_mutex_;
    std::map<std::string, float> clip_durations_;
    LogFn log_{nullptr};

    using SubmeshType = eldenring::cs::SubmeshType;
    std::vector<SubmeshType> submesh_types_;
};

} // namespace erov
