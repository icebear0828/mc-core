#pragma once

// Skin textures for the rig renderer: an RGBA8 texture plus the upload buffer that carries its pixels to
// the GPU on the first draw of the frame.

#include <d3d12.h>

#include <cstdint>

namespace mc::rig::d3d12 {

// A skin texture, its upload buffer and whether the pixels still have to be copied to the GPU.
struct SkinGpu {
    ID3D12Resource* tex{nullptr};
    ID3D12Resource* upload{nullptr};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    bool pending{false};
};

// (Re)creates `out` as an RGBA8 texture of the given size with its SRV at `slot`; the pixels are copied by
// the first RecordUpload().
[[nodiscard]] bool CreateSkinTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height,
                                     D3D12_CPU_DESCRIPTOR_HANDLE slot, SkinGpu& out);

// Records the pending upload of `skin` into `list` (once).
void RecordUpload(ID3D12GraphicsCommandList* list, SkinGpu& skin);

} // namespace mc::rig::d3d12

// Compatibility spelling used by the Elden Ring adapter.
namespace erov::steve {
using mc::rig::d3d12::CreateSkinTexture;
using mc::rig::d3d12::RecordUpload;
using mc::rig::d3d12::SkinGpu;
} // namespace erov::steve
