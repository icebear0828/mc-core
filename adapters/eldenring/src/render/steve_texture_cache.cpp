#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstring>

#include "render/steve_texture_cache.hpp"

#include "render/d3d12_util.hpp"

namespace erov::steve {

bool CreateSkinTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height,
                       D3D12_CPU_DESCRIPTOR_HANDLE slot, SkinGpu& out) {
    if (!device || !rgba || width == 0 || height == 0) return false;
    d3d12::Rel(out.tex);
    d3d12::Rel(out.upload);
    out.pending = false;
    D3D12_HEAP_PROPERTIES hd{};
    hd.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = width;
    td.Height = height;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&out.tex)))) {
        return false;
    }
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &out.footprint, &rows, &row_bytes, &total);
    D3D12_HEAP_PROPERTIES hu{};
    hu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC ud{};
    ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    ud.Width = total;
    ud.Height = 1;
    ud.DepthOrArraySize = 1;
    ud.MipLevels = 1;
    ud.SampleDesc.Count = 1;
    ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    void* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out.upload))) ||
        FAILED(out.upload->Map(0, &none, &mapped))) {
        d3d12::Rel(out.tex);
        d3d12::Rel(out.upload);
        return false;
    }
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped) + out.footprint.Offset + static_cast<size_t>(y) * out.footprint.Footprint.RowPitch,
                    rgba + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
    }
    out.upload->Unmap(0, nullptr);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(out.tex, &sd, slot);
    out.pending = true; // the copy is recorded into the first command list that draws the figure
    return true;
}

void RecordUpload(ID3D12GraphicsCommandList* list, SkinGpu& skin) {
    if (!skin.pending || !skin.tex || !skin.upload) return;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = skin.tex;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = skin.upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = skin.footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = skin.tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &b);
    skin.pending = false;
}

} // namespace erov::steve
