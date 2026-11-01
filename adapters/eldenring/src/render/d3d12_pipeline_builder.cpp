#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "render/d3d12_pipeline_builder.hpp"

#include "render/d3d12_util.hpp"

namespace erov::d3d12 {

void BeginPass(const PassDesc& pass) {
    D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(pass.width), static_cast<float>(pass.height), 0.f, 1.f};
    D3D12_RECT sc{0, 0, static_cast<LONG>(pass.width), static_cast<LONG>(pass.height)};
    if (pass.viewport) vp = *pass.viewport;
    if (pass.scissor) sc = *pass.scissor;
    pass.list->RSSetViewports(1, &vp);
    pass.list->RSSetScissorRects(1, &sc);
    pass.list->OMSetRenderTargets(1, &pass.rtv, FALSE, &pass.dsv);
    if (pass.clear_depth) {
        if (pass.clear_rect) {
            pass.list->ClearDepthStencilView(pass.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1, pass.clear_rect);
        } else {
            pass.list->ClearDepthStencilView(pass.dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
        }
    }
    pass.list->SetPipelineState(pass.pso);
    pass.list->SetGraphicsRootSignature(pass.root);
    ID3D12DescriptorHeap* heaps[] = {pass.srv_heap};
    pass.list->SetDescriptorHeaps(1, heaps);
    pass.list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

ID3D12RootSignature* CreateSteveRootSignature(ID3D12Device* device, LogFn log) {
    SteveRootDesc d = MakeSteveRootDesc();
    D3D12_ROOT_SIGNATURE_DESC rsd = SteveRootSignatureDesc(d);
    ID3DBlob* blob = nullptr;
    ID3DBlob* err = nullptr;
    ID3D12RootSignature* root = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)) || !blob) {
        if (log) log("steve: root signature failed: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
    } else if (FAILED(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&root)))) {
        if (log) log("steve: root signature creation failed");
    }
    Rel(blob);
    Rel(err);
    return root;
}

ID3D12PipelineState* CreatePipeline(ID3D12Device* device, const PsoOptions& options, LogFn log) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = MakePsoDesc(options);
    ID3D12PipelineState* pso = nullptr;
    const HRESULT hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        if (log) log("steve: pipeline state failed (0x%08X)", static_cast<unsigned>(hr));
        return nullptr;
    }
    return pso;
}

} // namespace erov::d3d12
