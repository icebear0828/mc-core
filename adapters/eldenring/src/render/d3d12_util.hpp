#pragma once

// Small D3D12 resource helpers shared by the Steve renderer, its pipeline builder and its texture cache.

#include <d3d12.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mc/rig.hpp"
#include "render/steve_types.hpp"

namespace erov::d3d12 {

// Releases a COM pointer and clears it.
template <typename T>
void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

// Creates an upload-heap buffer holding a copy of `data`.
[[nodiscard]] ID3D12Resource* UploadBuffer(ID3D12Device* device, const void* data, size_t size);

// Uploads a rig mesh and fills its views. The caller releases any previous buffers first. On failure the
// outputs are left null and false is returned.
[[nodiscard]] bool UploadMesh(ID3D12Device* device, const std::vector<mc::rig::RigVertex>& vertices,
                              const std::vector<uint16_t>& indices, ID3D12Resource*& vb, ID3D12Resource*& ib,
                              D3D12_VERTEX_BUFFER_VIEW& vbv, D3D12_INDEX_BUFFER_VIEW& ibv, unsigned& index_count);

} // namespace erov::d3d12
