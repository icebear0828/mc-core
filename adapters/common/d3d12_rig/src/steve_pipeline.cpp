#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "d3d12_rig/steve_pipeline.hpp"

#include "d3d12_rig/d3d12_pipeline_builder.hpp"
#include "d3d12_rig/d3d12_shader_lib.hpp"
#include "d3d12_rig/d3d12_util.hpp"

namespace mc::rig::d3d12 {

bool Pipeline::Create(ID3D12Device* device, DXGI_FORMAT rtv_format, const char* hlsl, size_t hlsl_size, LogFn log) {
    Release();
    root = CreateSteveRootSignature(device, log);
    if (!root) return false;

    UINT input_count = 0;
    const D3D12_INPUT_ELEMENT_DESC* input = SteveInputLayout(input_count);

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!Compile(hlsl, hlsl_size, "VSMain", "vs_5_0", &vs, log) || !Compile(hlsl, hlsl_size, "PSMain", "ps_5_0", &ps, log)) {
        Rel(vs);
        Rel(ps);
        return false;
    }
    PsoOptions opts;
    opts.root = root;
    opts.vs = {vs->GetBufferPointer(), vs->GetBufferSize()};
    opts.ps = {ps->GetBufferPointer(), ps->GetBufferSize()};
    opts.input = input;
    opts.input_count = input_count;
    opts.rtv_format = rtv_format;
    figure = CreatePipeline(device, opts, log);
    Rel(vs);
    Rel(ps);
    if (!figure) return false;

    // Debug depth view: no input layout, no depth buffer, alpha blended.
    ID3DBlob* vs_full = nullptr;
    ID3DBlob* ps_full = nullptr;
    if (Compile(hlsl, hlsl_size, "VSFull", "vs_5_0", &vs_full, log) && Compile(hlsl, hlsl_size, "PSDepthView", "ps_5_0", &ps_full, log)) {
        PsoOptions dv;
        dv.root = root;
        dv.vs = {vs_full->GetBufferPointer(), vs_full->GetBufferSize()};
        dv.ps = {ps_full->GetBufferPointer(), ps_full->GetBufferSize()};
        dv.rtv_format = rtv_format;
        dv.dsv_format = DXGI_FORMAT_UNKNOWN;
        dv.depth_enable = false;
        dv.alpha_blend = true;
        depth_view = CreatePipeline(device, dv, log);
    }
    Rel(vs_full);
    Rel(ps_full);

    // Blob shadow: same vertex layout as the figure, tests against the depth buffer but writes none, blended.
    ID3DBlob* vs_shadow = nullptr;
    ID3DBlob* ps_shadow = nullptr;
    if (Compile(hlsl, hlsl_size, "VSMain", "vs_5_0", &vs_shadow, log) && Compile(hlsl, hlsl_size, "PSShadow", "ps_5_0", &ps_shadow, log)) {
        PsoOptions sd;
        sd.root = root;
        sd.vs = {vs_shadow->GetBufferPointer(), vs_shadow->GetBufferSize()};
        sd.ps = {ps_shadow->GetBufferPointer(), ps_shadow->GetBufferSize()};
        sd.input = input;
        sd.input_count = input_count;
        sd.rtv_format = rtv_format;
        sd.depth_write = D3D12_DEPTH_WRITE_MASK_ZERO;
        sd.alpha_blend = true;
        shadow = CreatePipeline(device, sd, log);
    }
    Rel(vs_shadow);
    Rel(ps_shadow);
    return true;
}

void Pipeline::Release() {
    Rel(shadow);
    Rel(depth_view);
    Rel(figure);
    Rel(root);
}

} // namespace mc::rig::d3d12
