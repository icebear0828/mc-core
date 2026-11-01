#pragma once

// Owns the Steve root signature and the three graphics pipelines (figure, debug depth view, blob shadow).
// SteveRenderer calls Create() once and then only binds the states it needs.

#include <d3d12.h>

#include "render/steve_types.hpp"

namespace erov::steve {

struct Pipeline {
    ID3D12RootSignature* root{nullptr};
    ID3D12PipelineState* figure{nullptr};
    ID3D12PipelineState* depth_view{nullptr};
    ID3D12PipelineState* shadow{nullptr};

    // Returns false only when the root signature or the figure pipeline cannot be created; the debug depth
    // view and the shadow pipeline are optional, as before.
    [[nodiscard]] bool Create(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log);
    void Release();
};

} // namespace erov::steve
