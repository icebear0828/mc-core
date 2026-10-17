#pragma once

#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include "cs_viewmodel.hpp"
#include "eldenring_fp.hpp"
#include "mc/rig.hpp"

namespace erov {

namespace fp = eldenring::fp;

class CsRenderer {
public:
    using LogFn = void (*)(const char* fmt, ...);

    bool init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void release();
    [[nodiscard]] bool ready() const { return pso_ != nullptr && vb_ != nullptr; }

    bool loadModel(ID3D12Device* device, const std::string& path);
    bool setTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);

    void tick(float dt, uint8_t state, float anim_time);

    void draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
              D3D12_GPU_DESCRIPTOR_HANDLE tex_table, unsigned width, unsigned height,
              const mc::rig::Mat4& projection, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
              D3D12_CPU_DESCRIPTOR_HANDLE dsv);

private:
    ID3D12RootSignature* root_{nullptr};
    ID3D12PipelineState* pso_{nullptr};
    ID3D12Resource* vb_{nullptr};
    ID3D12Resource* ib_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};

    ID3D12Resource* tex_{nullptr};
    ID3D12Resource* upload_{nullptr};

    eldenring::cs::CSViewModel model_;
    LogFn log_{nullptr};

    // Submesh indices
    int blade_idx_{-1};
    int handle_front_idx_{-1};
    int handle_rear_idx_{-1};
    int lock_idx_{-1};
    int arms_idx_{-1};

    // Animation pose matrices (Minecraft camera space)
    fp::M4 blade_pose_{};
    fp::M4 handle_front_pose_{};
    fp::M4 handle_rear_pose_{};
    fp::M4 lock_pose_{};
    fp::M4 arms_pose_{};
    fp::M4 base_pose_{};
};

} // namespace erov
