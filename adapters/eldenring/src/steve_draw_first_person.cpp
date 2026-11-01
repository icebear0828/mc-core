#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "steve_renderer_d3d12.hpp"

// First-person view model draw calls. Split out of steve_renderer_d3d12.cpp to keep that translation unit
// focused on the third-person figure and the shared pass plumbing.

namespace erov {

void SteveRenderer::drawFirstPerson(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE skin_table,
                                    D3D12_GPU_DESCRIPTOR_HANDLE atlas_table, unsigned width, unsigned height, const mc::rig::Mat4& projection,
                                    const mc::rig::Mat4& arm_world, const mc::rig::Mat4& item_world, uint16_t item, D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!ready() || !fp_vertices_ || !own_depth_ || !dsv_heap_) return;
    const auto it = fp_items_.find(item);
    if (item != 0 && it == fp_items_.end()) return; // an item without a sprite: nothing is shown
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, true); // the view model is drawn over everything
    list->IASetVertexBuffers(0, 1, &fp_vbv_);
    list->IASetIndexBuffer(&fp_ibv_);
    list->SetGraphicsRoot32BitConstants(0, 16, projection.m.data(), 0);
    const float extra[8] = {1.f, 0.f, 0.f, 0.f /* mode 0: no scene occlusion */, 1.f, 1.f, static_cast<float>(width), static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
    list->SetGraphicsRoot32BitConstants(0, 4, no_tint, 40);
    if (item == 0) {
        list->SetGraphicsRootDescriptorTable(1, skin_table); // the arm is skinned
        list->SetGraphicsRoot32BitConstants(0, 16, arm_world.m.data(), 16);
        for (const FpDraw& d : fp_arm_) list->DrawIndexedInstanced(d.index_count, 1, d.first_index, d.base_vertex, 0);
    } else {
        list->SetGraphicsRootDescriptorTable(1, atlas_table); // the item is a cell of the atlas
        list->SetGraphicsRoot32BitConstants(0, 16, item_world.m.data(), 16);
        const FpDraw& d = it->second;
        list->DrawIndexedInstanced(d.index_count, 1, d.first_index, d.base_vertex, 0);
    }
}

void SteveRenderer::drawFirstPersonItem(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                                        D3D12_GPU_DESCRIPTOR_HANDLE atlas_table, unsigned width, unsigned height,
                                        const mc::rig::Mat4& projection, const mc::rig::Mat4& item_world, uint16_t item,
                                        D3D12_CPU_DESCRIPTOR_HANDLE rtv) {
    if (!ready() || !fp_vertices_ || !own_depth_ || !dsv_heap_) return;
    const auto it = fp_items_.find(item);
    if (item == 0 || it == fp_items_.end()) return;
    // Preserves the depth buffer so offhand and mainhand items depth-test against each other correctly.
    beginPass(list, srv_heap, rtv, width, height, pipeline_.figure, false);
    list->IASetVertexBuffers(0, 1, &fp_vbv_);
    list->IASetIndexBuffer(&fp_ibv_);
    list->SetGraphicsRoot32BitConstants(0, 16, projection.m.data(), 0);
    const float extra[8] = {1.f, 0.f, 0.f, 0.f, 1.f, 1.f, static_cast<float>(width), static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    const float no_tint[4] = {0.f, 0.f, 0.f, 0.f};
    list->SetGraphicsRoot32BitConstants(0, 4, no_tint, 40);
    list->SetGraphicsRootDescriptorTable(1, atlas_table);
    list->SetGraphicsRoot32BitConstants(0, 16, item_world.m.data(), 16);
    const FpDraw& d = it->second;
    list->DrawIndexedInstanced(d.index_count, 1, d.first_index, d.base_vertex, 0);
}

} // namespace erov
