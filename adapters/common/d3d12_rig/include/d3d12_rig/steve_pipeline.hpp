#pragma once

// Owns the rig root signature and the three graphics pipelines (figure, debug depth view, blob shadow).
// The renderer calls Create() once and then only binds the states it needs. The HLSL source comes from the
// caller, so this layer stays game-neutral.

#include <d3d12.h>

#include <cstddef>

#include "d3d12_rig/steve_types.hpp"

namespace mc::rig::d3d12 {

struct Pipeline {
    ID3D12RootSignature* root{nullptr};
    ID3D12PipelineState* figure{nullptr};
    ID3D12PipelineState* depth_view{nullptr};
    ID3D12PipelineState* shadow{nullptr};

    // Returns false only when the root signature or the figure pipeline cannot be created; the debug depth
    // view and the shadow pipeline are optional, as before.
    [[nodiscard]] bool Create(ID3D12Device* device, DXGI_FORMAT rtv_format, const char* hlsl, size_t hlsl_size, LogFn log);
    void Release();
};

} // namespace mc::rig::d3d12

// Compatibility spelling used by the Elden Ring adapter.
namespace erov::steve {
using mc::rig::d3d12::Pipeline;
}
