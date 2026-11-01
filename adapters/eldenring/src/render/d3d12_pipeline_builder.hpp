#pragma once

// Builds the Steve root signature and graphics pipelines from plain descriptors, so SteveRenderer only
// submits draws. The descriptor assembly here is pure (no device, no allocation) and unit-tested; the
// device-touching helpers live in the .cpp.

#include <d3d12.h>

#include <cstdint>

#include "render/steve_types.hpp"

namespace erov::d3d12 {

// Descriptor value types for the Steve root signature:
//   0: 44 root constants (view_proj, world, scene, dims, tint)
//   1: descriptor table (t0 scene depth, t1 skin), pixel visible
//   2: UAV u1 (stats), pixel visible
// plus one static point/clamp sampler s0.
struct SteveRootDesc {
    D3D12_ROOT_PARAMETER params[3]{};
    D3D12_DESCRIPTOR_RANGE srv_range{};
    D3D12_STATIC_SAMPLER_DESC sampler{};
};

[[nodiscard]] inline SteveRootDesc MakeSteveRootDesc() noexcept {
    SteveRootDesc d{};
    d.srv_range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    d.srv_range.NumDescriptors = 2; // t0: scene depth, t1: skin
    d.srv_range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    d.params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    d.params[0].Constants.Num32BitValues = 44;
    d.params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    d.params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    d.params[1].DescriptorTable.NumDescriptorRanges = 1;
    d.params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    d.params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    d.params[2].Descriptor.ShaderRegister = 1;
    d.params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    d.sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    d.sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    d.sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    d.sampler.MaxLOD = D3D12_FLOAT32_MAX;
    d.sampler.ShaderRegister = 0;
    d.sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    return d;
}

// Fills a D3D12_ROOT_SIGNATURE_DESC whose pointers refer into `d`; keep `d` alive until serialization is
// done. Call this only after `d` is in its final storage (do not copy the result across a move of `d`).
[[nodiscard]] inline D3D12_ROOT_SIGNATURE_DESC SteveRootSignatureDesc(SteveRootDesc& d) noexcept {
    d.params[1].DescriptorTable.pDescriptorRanges = &d.srv_range;
    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 3;
    desc.pParameters = d.params;
    desc.NumStaticSamplers = 1;
    desc.pStaticSamplers = &d.sampler;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    return desc;
}

// The figure/shadow input layout: POSITION (R32G32B32_FLOAT, offset 0) and TEXCOORD (R32G32_FLOAT, offset 12).
[[nodiscard]] inline const D3D12_INPUT_ELEMENT_DESC* SteveInputLayout(UINT& count) noexcept {
    static const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    count = 2;
    return layout;
}

struct PsoOptions {
    ID3D12RootSignature* root{nullptr};
    D3D12_SHADER_BYTECODE vs{};
    D3D12_SHADER_BYTECODE ps{};
    const D3D12_INPUT_ELEMENT_DESC* input{nullptr};
    UINT input_count{0};
    DXGI_FORMAT rtv_format{DXGI_FORMAT_UNKNOWN};
    DXGI_FORMAT dsv_format{DXGI_FORMAT_D32_FLOAT};
    bool depth_enable{true};
    D3D12_DEPTH_WRITE_MASK depth_write{D3D12_DEPTH_WRITE_MASK_ALL};
    D3D12_COMPARISON_FUNC depth_func{D3D12_COMPARISON_FUNC_LESS};
    bool alpha_blend{false};
    D3D12_CULL_MODE cull{D3D12_CULL_MODE_NONE};
};

// Pure descriptor assembly for a Steve graphics pipeline (no device call).
[[nodiscard]] inline D3D12_GRAPHICS_PIPELINE_STATE_DESC MakePsoDesc(const PsoOptions& o) noexcept {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = o.root;
    pd.VS = o.vs;
    pd.PS = o.ps;
    pd.InputLayout = {o.input, o.input_count};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = o.cull;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = o.depth_enable ? TRUE : FALSE;
    pd.DepthStencilState.DepthWriteMask = o.depth_write;
    pd.DepthStencilState.DepthFunc = o.depth_func;
    pd.DSVFormat = o.dsv_format;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = o.rtv_format;
    pd.SampleDesc.Count = 1;
    if (o.alpha_blend) {
        auto& b = pd.BlendState.RenderTarget[0];
        b.BlendEnable = TRUE;
        b.SrcBlend = D3D12_BLEND_SRC_ALPHA;
        b.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        b.BlendOp = D3D12_BLEND_OP_ADD;
        b.SrcBlendAlpha = D3D12_BLEND_ONE;
        b.DestBlendAlpha = D3D12_BLEND_ZERO;
        b.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }
    return pd;
}

// Common per-pass setup: viewport/scissor, render target + depth, optional clear, pipeline, root signature,
// descriptor heaps and triangle-list topology.
struct PassDesc {
    ID3D12GraphicsCommandList* list{nullptr};
    ID3D12DescriptorHeap* srv_heap{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
    unsigned width{0}, height{0};
    ID3D12PipelineState* pso{nullptr};
    ID3D12RootSignature* root{nullptr};
    bool clear_depth{true};
    const D3D12_VIEWPORT* viewport{nullptr}; // null = full back buffer
    const D3D12_RECT* scissor{nullptr};      // null = full back buffer
    const D3D12_RECT* clear_rect{nullptr};   // null = clear the whole depth view
};

void BeginPass(const PassDesc& pass);

[[nodiscard]] ID3D12RootSignature* CreateSteveRootSignature(ID3D12Device* device, LogFn log);
[[nodiscard]] ID3D12PipelineState* CreatePipeline(ID3D12Device* device, const PsoOptions& options, LogFn log);

} // namespace erov::d3d12
