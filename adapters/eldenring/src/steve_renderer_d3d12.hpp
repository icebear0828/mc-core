#pragma once

// Draws the 12-part Steve rig into the game's back buffer with D3D12 (milestone 2). Textured with the locally extracted Minecraft skin (flat grey-brown when it is missing).
// The game's depth buffer is read in the pixel shader to hide the figure behind the scene (reverse-Z, same rule as
// the Sekiro D3D11 renderer: mc::rig::sceneOccludes).

#include <d3d12.h>

#include <cstdint>
#include <map>
#include <vector>

#include "eldenring_steve.hpp"
#include "mc/rig.hpp"

namespace erov {

struct SteveParams {
    float mode{1.f}; // 0: no depth test, 1: hide behind the scene, 2: calibration colours
    float depth_const{0.0501f}; // depth * view z = near plane for a reverse-Z projection with an infinite far plane
    float rel_bias{0.08f};
    float abs_bias{0.05f};
    float depth_w{0.f}, depth_h{0.f}; // size of the depth texture, to map back buffer pixels onto it
    float tint[4]{0.f, 0.f, 0.f, 0.f}; // rgb + amount: mixed over the lit skin (hurt flash)
    uint16_t held_item{0};             // mc::ItemId in the right hand (0 or an item without a sprite: nothing is drawn)
    D3D12_GPU_DESCRIPTOR_HANDLE held_table{}; // descriptor table (scene depth, atlas) the held item is drawn with
    bool keep_depth{false};            // the blocks were drawn just before: do not clear the figure's depth buffer
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

    // The figure's own depth buffer (D32_FLOAT, standard 0..1 depth, cleared by draw()). Recreated when the size changes.
    bool ensureDepth(ID3D12Device* device, unsigned width, unsigned height);

    // (Re)creates the depth SRV at `slot`; a null resource writes a null descriptor (occlusion must then be off).
    void setDepthView(ID3D12Device* device, ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE slot);

    // Calibration (mode 3): histogram of K = depth * view z over the figure's pixels, 64 bins, bin = 8 * log2(K / 0.0005).
    bool readStats(uint32_t out[64]);

    // Debug: the scene depth as a grey full-screen overlay (see PSDepthView).
    void drawDepthView(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
                       unsigned width, unsigned height, float gain, float depth_w, float depth_h);

    // Records the draw calls. `srv_heap` must be the heap that holds the depth SRV at `depth_table` (GPU handle).
    // First person: the bare right arm (with its sleeve) and the held item as a flat extruded sprite, drawn in front of the camera
    // in Minecraft's view-model space. `cells` are the atlas cells of the items, in atlas pixels.
    struct FpItemCell {
        uint16_t item;
        int x, y, w, h;
    };
    bool initFirstPerson(ID3D12Device* device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h, const std::vector<FpItemCell>& cells);
    // `skin_table`: descriptor table (depth, skin); `atlas_table`: (any, atlas). `item` 0 draws the bare arm, otherwise that item.
    void drawFirstPerson(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE skin_table,
                         D3D12_GPU_DESCRIPTOR_HANDLE atlas_table, unsigned width, unsigned height, const mc::rig::Mat4& projection,
                         const mc::rig::Mat4& arm_world, const mc::rig::Mat4& item_world, uint16_t item, D3D12_CPU_DESCRIPTOR_HANDLE rtv);
    [[nodiscard]] bool firstPersonReady() const { return fp_vertices_ != nullptr; }

    // Third person: the held item as Minecraft draws it (a flat sprite extruded one pixel, ItemInHandLayer transform), built from the
    // atlas cells for every item that is held as a sprite. It is drawn with the right arm's matrix so it follows the swing.
    // The placed blocks: one world-space mesh (see eldenring_blockmesh.hpp) drawn textured from the atlas, with the scene's depth for
    // occlusion. `table` is the (scene depth, atlas) descriptor table; `frame` is the back buffer index (each frame slot keeps its own
    // copy of the mesh, so a rebuild never touches a buffer the GPU may still read). Returns whether anything was drawn.
    void setBlocks(const mc::rig::RigMesh& mesh);
    bool drawBlocks(ID3D12Device* device, ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE table,
                    unsigned width, unsigned height, const mc::rig::Mat4& view_proj, const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                    unsigned frame);
    bool initHeldItems(ID3D12Device* device, const uint8_t* atlas_rgba, unsigned atlas_w, unsigned atlas_h, const std::vector<FpItemCell>& cells);

    // `rtv` is the back buffer view: the figure is drawn with its own depth buffer (so the faces of the boxes and the parts
    // sort correctly), then the caller must bind its render target again without a depth view.
    void draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
              unsigned width, unsigned height, const mc::rig::Mat4& view_proj, const eldenring::render::PartMatrices& parts,
              const SteveParams& params, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

private:
    ID3D12RootSignature* root_{nullptr};
    ID3D12PipelineState* pso_{nullptr};
    ID3D12PipelineState* pso_depthview_{nullptr};
    ID3D12Resource* stats_{nullptr};
    ID3D12Resource* stats_init_{nullptr};
    ID3D12Resource* stats_readback_{nullptr};
    struct FpDraw {
        unsigned index_count{0}, first_index{0};
        int base_vertex{0};
    };
    ID3D12Resource* fp_vertices_{nullptr};
    ID3D12Resource* fp_indices_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW fp_vbv_{};
    D3D12_INDEX_BUFFER_VIEW fp_ibv_{};
    FpDraw fp_arm_[2]{};
    std::map<uint16_t, FpDraw> fp_items_;
    ID3D12Resource* held_vertices_{nullptr};
    ID3D12Resource* held_indices_{nullptr};
    D3D12_VERTEX_BUFFER_VIEW held_vbv_{};
    D3D12_INDEX_BUFFER_VIEW held_ibv_{};
    std::map<uint16_t, FpDraw> held_items_;
    struct BlockSlot {
        ID3D12Resource* vb{nullptr};
        ID3D12Resource* ib{nullptr};
        unsigned version{0};
        unsigned index_count{0};
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        D3D12_INDEX_BUFFER_VIEW ibv{};
    };
    BlockSlot block_slots_[4]{};
    std::vector<mc::rig::RigVertex> block_vertices_cpu_;
    std::vector<uint16_t> block_indices_cpu_;
    unsigned block_version_{0};
    ID3D12Resource* own_depth_{nullptr};
    ID3D12DescriptorHeap* dsv_heap_{nullptr};
    unsigned own_depth_w_{0}, own_depth_h_{0};
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
