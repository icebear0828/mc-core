#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

#include "render/d3d12_util.hpp"

namespace erov::d3d12 {

ID3D12Resource* UploadBuffer(ID3D12Device* device, const void* data, size_t size) {
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = size;
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* res = nullptr;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&res)))) {
        return nullptr;
    }
    void* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(res->Map(0, &none, &mapped))) {
        res->Release();
        return nullptr;
    }
    std::memcpy(mapped, data, size);
    res->Unmap(0, nullptr);
    return res;
}

bool UploadMesh(ID3D12Device* device, const std::vector<mc::rig::RigVertex>& vertices,
                const std::vector<uint16_t>& indices, ID3D12Resource*& vb, ID3D12Resource*& ib,
                D3D12_VERTEX_BUFFER_VIEW& vbv, D3D12_INDEX_BUFFER_VIEW& ibv, unsigned& index_count) {
    vb = UploadBuffer(device, vertices.data(), vertices.size() * sizeof(mc::rig::RigVertex));
    ib = UploadBuffer(device, indices.data(), indices.size() * sizeof(uint16_t));
    if (!vb || !ib) {
        Rel(vb);
        Rel(ib);
        return false;
    }
    vbv = {vb->GetGPUVirtualAddress(), static_cast<UINT>(vertices.size() * sizeof(mc::rig::RigVertex)),
           static_cast<UINT>(sizeof(mc::rig::RigVertex))};
    ibv = {ib->GetGPUVirtualAddress(), static_cast<UINT>(indices.size() * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT};
    index_count = static_cast<unsigned>(indices.size());
    return true;
}

} // namespace erov::d3d12
