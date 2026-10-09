#pragma once

// Draws the 12-part Steve rig into the game's back buffer with D3D12 (milestone 2). Textured with the locally extracted Minecraft skin (flat grey-brown when it is missing).
// The game's depth buffer is read in the pixel shader to hide the figure behind the scene (reverse-Z, same rule as
// the Sekiro D3D11 renderer: mc::rig::sceneOccludes).

#include <d3d12.h>

#include <cstdint>

#include "eldenring_steve.hpp"
#include "mc/rig.hpp"

namespace erov {

struct SteveParams {
    float mode{1.f}; // 0: no depth test, 1: hide behind the scene, 2: calibration colours
    float depth_const{0.0501f}; // depth * view z = near plane for a reverse-Z projection with an infinite far plane
    float rel_bias{0.08f};
    float abs_bias{0.05f};
    float depth_w{0.f}, depth_h{0.f}; // size of the depth texture, to map back buffer pixels onto it
};

class SteveRenderer {
public:
    using LogFn = void (*)(const char* fmt, ...);

    bool init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void release();
    [[nodiscard]] bool ready() const { return pso_ != nullptr; }

    // Creates the skin texture (RGBA8, usually 64x64) and its SRV at `slot`, which must directly follow the depth SRV in the
    // heap (the two share one descriptor table: t0 depth, t1 skin). The pixels are copied to the GPU by the first draw().
    bool setSkin(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot);

    // (Re)creates the depth SRV at `slot`; a null resource writes a null descriptor (occlusion must then be off).
    void setDepthView(ID3D12Device* device, ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE slot);

    // Calibration (mode 3): histogram of K = depth * view z over the figure's pixels, 64 bins, bin = 8 * log2(K / 0.0005).
    bool readStats(uint32_t out[64]);

    // Debug: the scene depth as a grey full-screen overlay (see PSDepthView).
    void drawDepthView(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
                       unsigned width, unsigned height, float gain, float depth_w, float depth_h);

    // Records the draw calls. `srv_heap` must be the heap that holds the depth SRV at `depth_table` (GPU handle).
    void draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
              unsigned width, unsigned height, const mc::rig::Mat4& view_proj, const eldenring::render::PartMatrices& parts,
              const SteveParams& params);

private:
    ID3D12RootSignature* root_{nullptr};
    ID3D12PipelineState* pso_{nullptr};
    ID3D12PipelineState* pso_depthview_{nullptr};
    ID3D12Resource* stats_{nullptr};
    ID3D12Resource* stats_init_{nullptr};
    ID3D12Resource* stats_readback_{nullptr};
    ID3D12Resource* skin_tex_{nullptr};
    ID3D12Resource* skin_upload_{nullptr};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT skin_footprint_{};
    bool skin_pending_{false};
    ID3D12Resource* vertices_{nullptr};
    ID3D12Resource* indices_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW vbv_{};
    D3D12_INDEX_BUFFER_VIEW ibv_{};
    unsigned index_count_[static_cast<size_t>(mc::StevePart::Count)]{};
    unsigned first_index_[static_cast<size_t>(mc::StevePart::Count)]{};
    int base_vertex_[static_cast<size_t>(mc::StevePart::Count)]{};
};

} // namespace erov
