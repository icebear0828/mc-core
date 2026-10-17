#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cs_renderer_d3d12.hpp"

namespace erov {
namespace fp = eldenring::fp;
namespace {

constexpr char kCsShader[] = R"hlsl(
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 light_dir;
    float4 tint;
};

Texture2D<float4> knife_tex : register(t0);
SamplerState knife_sampler : register(s0);

struct VSIn {
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOut {
    float4 pos : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float3 wpos : TEXCOORD1;
};

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 w = mul(float4(i.pos, 1.0), world);
    o.pos = mul(w, view_proj);
    o.normal = mul(float4(i.normal, 0.0), world).xyz;
    o.uv = i.uv;
    o.wpos = w.xyz;
    return o;
}

float4 PSMain(VSOut i) : SV_Target {
    float3 n = normalize(i.normal);
    float3 l = normalize(light_dir.xyz);
    float diff = max(dot(n, l), 0.0) * 0.55 + 0.45;
    float4 tex = knife_tex.Sample(knife_sampler, i.uv);
    if (tex.a < 0.05 || (tex.r < 0.02 && tex.g < 0.02 && tex.b < 0.02)) {
        tex = float4(0.85, 0.88, 0.92, 1.0); // Polished stainless steel fallback
    }
    return float4(tex.rgb * diff, 1.0);
}
)hlsl";

template <typename T>
void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool CompileShader(const char* entry, const char* profile, ID3DBlob** out, CsRenderer::LogFn log) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kCsShader, sizeof(kCsShader) - 1, "cs_viewmodel.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        if (log) log("cs_render: shader %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        Rel(errors);
        return false;
    }
    Rel(errors);
    return true;
}

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

} // namespace

bool CsRenderer::init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log) {
    release();
    log_ = log;

    // Root signature: 40 root constants (b0) + 1 descriptor table (t0) + 1 static sampler (s0)
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 40;
    params[0].Constants.ShaderRegister = 0;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderRegister = 0;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
    rsd.pParameters = params;
    rsd.NumStaticSamplers = 1;
    rsd.pStaticSamplers = &sampler;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ID3DBlob* rs_blob = nullptr;
    ID3DBlob* rs_err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err)) ||
        FAILED(device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_)))) {
        if (log_) log_("cs_render: root signature failed");
        Rel(rs_blob);
        Rel(rs_err);
        return false;
    }
    Rel(rs_blob);
    Rel(rs_err);

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!CompileShader("VSMain", "vs_5_0", &vs, log_) || !CompileShader("PSMain", "ps_5_0", &ps, log_)) {
        Rel(vs);
        Rel(ps);
        release();
        return false;
    }

    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {layout, 3};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = rtv_format;
    pd.SampleDesc.Count = 1;

    const HRESULT hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_));
    Rel(vs);
    Rel(ps);
    if (FAILED(hr)) {
        if (log_) log_("cs_render: PSO creation failed");
        release();
        return false;
    }

    if (log_) log_("cs_render: D3D12 pipeline initialized successfully");
    return true;
}

void CsRenderer::release() {
    Rel(pso_);
    Rel(root_);
    Rel(vb_);
    Rel(ib_);
    Rel(tex_);
    Rel(upload_);
}

bool CsRenderer::loadModel(ID3D12Device* device, const std::string& path) {
    if (!model_.loadFromFile(path)) {
        if (log_) log_("cs_render: failed to load CSVM file from %s", path.c_str());
        return false;
    }

    Rel(vb_);
    Rel(ib_);

    const size_t vb_size = model_.vertices.size() * sizeof(eldenring::cs::CSVMVertex);
    const size_t ib_size = model_.indices.size() * sizeof(uint32_t);

    vb_ = UploadBuffer(device, model_.vertices.data(), vb_size);
    ib_ = UploadBuffer(device, model_.indices.data(), ib_size);
    if (!vb_ || !ib_) {
        Rel(vb_);
        Rel(ib_);
        if (log_) log_("cs_render: failed to create vertex/index upload buffers");
        return false;
    }

    vbv_ = {vb_->GetGPUVirtualAddress(), static_cast<UINT>(vb_size), static_cast<UINT>(sizeof(eldenring::cs::CSVMVertex))};
    ibv_ = {ib_->GetGPUVirtualAddress(), static_cast<UINT>(ib_size), DXGI_FORMAT_R32_UINT};

    // Identify key bone submesh slots
    blade_idx_ = -1;
    handle_front_idx_ = -1;
    handle_rear_idx_ = -1;
    lock_idx_ = -1;
    arms_idx_ = -1;

    for (size_t i = 0; i < model_.submeshes.size(); ++i) {
        std::string n = model_.submeshes[i].bone_name;
        for (char& c : n) c = static_cast<char>(tolower(c));
        if (n.find("blade") != std::string::npos) blade_idx_ = static_cast<int>(i);
        else if (n.find("front") != std::string::npos || n.find("handle0") != std::string::npos || n.find("handle1") != std::string::npos) handle_front_idx_ = static_cast<int>(i);
        else if (n.find("rear") != std::string::npos || n.find("handle2") != std::string::npos) handle_rear_idx_ = static_cast<int>(i);
        else if (n.find("lock") != std::string::npos || n.find("latch") != std::string::npos) lock_idx_ = static_cast<int>(i);
        else if (n.find("arm") != std::string::npos || n.find("glove") != std::string::npos) arms_idx_ = static_cast<int>(i);
    }

    if (log_) log_("cs_render: CSVM model loaded: %u vertices, %u indices, %zu submeshes",
                   model_.header_.total_vertices, model_.header_.total_indices, model_.submeshes.size());
    return true;
}

bool CsRenderer::setTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    if (!rgba || width == 0 || height == 0) return false;
    Rel(tex_);
    Rel(upload_);

    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex_)))) {
        return false;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Format = rd.Format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex_, &srv, slot);

    return true;
}

void CsRenderer::tick(float dt, uint8_t state, float anim_time) {
    (void)dt;
    // Base viewmodel placement in first-person camera space
    // +X: right, +Y: up, -Z: forward
    fp::M4 base = fp::translate(0.24f, -0.20f, -0.42f);
    base = fp::mul(base, fp::scale(0.012f));
    base = fp::mul(base, fp::rotX(-12.f));
    base = fp::mul(base, fp::rotY(-18.f));

    if (state == 1) { // Inspect (Flower Spin)
        const float progress = std::clamp(anim_time / 2.8f, 0.f, 1.f);
        const float flare = std::sin(progress * 3.14159f * 4.0f) * 45.0f;
        const float blade_spin = anim_time * 720.0f; // 720 deg/sec spin

        blade_pose_ = fp::mul(base, fp::rotZ(blade_spin));
        handle_front_pose_ = fp::mul(base, fp::rotZ(flare));
        handle_rear_pose_ = fp::mul(base, fp::rotZ(-flare));
        lock_pose_ = handle_front_pose_;
        arms_pose_ = base;
    } else if (state == 2) { // Slash
        const float p = std::clamp(anim_time / 0.45f, 0.f, 1.f);
        const float slash_rot = -40.f + p * 80.f;
        const float forward = -std::sin(p * 3.14159f) * 0.10f;

        fp::M4 slash_base = fp::mul(base, fp::translate(0.f, 0.f, forward));
        slash_base = fp::mul(slash_base, fp::rotZ(slash_rot));
        blade_pose_ = slash_base;
        handle_front_pose_ = slash_base;
        handle_rear_pose_ = slash_base;
        lock_pose_ = slash_base;
        arms_pose_ = slash_base;
    } else if (state == 3) { // Backstab
        const float p = std::clamp(anim_time / 0.55f, 0.f, 1.f);
        const float thrust = -std::sin(p * 3.14159f) * 0.16f;

        fp::M4 stab_base = fp::mul(base, fp::translate(0.f, -0.05f, thrust));
        stab_base = fp::mul(stab_base, fp::rotX(55.f));
        blade_pose_ = stab_base;
        handle_front_pose_ = stab_base;
        handle_rear_pose_ = stab_base;
        lock_pose_ = stab_base;
        arms_pose_ = stab_base;
    } else if (state == 4) { // Deploy Flip
        const float p = std::clamp(anim_time / 0.75f, 0.f, 1.f);
        const float unfold = (1.f - p) * 160.f;

        blade_pose_ = fp::mul(base, fp::rotX(unfold));
        handle_front_pose_ = fp::mul(base, fp::rotZ((1.f - p) * 35.f));
        handle_rear_pose_ = fp::mul(base, fp::rotZ(-(1.f - p) * 35.f));
        lock_pose_ = handle_front_pose_;
        arms_pose_ = base;
    } else { // Idle
        const float bob = std::sin(anim_time * 2.5f) * 0.005f;
        fp::M4 idle_base = fp::mul(base, fp::translate(0.f, bob, 0.f));
        blade_pose_ = idle_base;
        handle_front_pose_ = idle_base;
        handle_rear_pose_ = idle_base;
        lock_pose_ = idle_base;
        arms_pose_ = idle_base;
    }
}

void CsRenderer::draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                      D3D12_GPU_DESCRIPTOR_HANDLE tex_table, unsigned width, unsigned height,
                      const mc::rig::Mat4& projection, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                      D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    if (!ready()) return;

    D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);

    list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    list->SetPipelineState(pso_);
    list->SetGraphicsRootSignature(root_);

    ID3D12DescriptorHeap* heaps[] = {srv_heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, tex_table);

    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);

    // 16 floats view_proj
    list->SetGraphicsRoot32BitConstants(0, 16, projection.m.data(), 0);

    const float light[4] = {0.45f, 0.85f, -0.45f, 0.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, light, 32);

    const float tint[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, tint, 36);

    for (size_t i = 0; i < model_.submeshes.size(); ++i) {
        const auto& sub = model_.submeshes[i];
        const int idx = static_cast<int>(i);

        fp::M4 pose = base_pose_;
        if (idx == blade_idx_) pose = blade_pose_;
        else if (idx == handle_front_idx_) pose = handle_front_pose_;
        else if (idx == handle_rear_idx_) pose = handle_rear_pose_;
        else if (idx == lock_idx_) pose = lock_pose_;
        else if (idx == arms_idx_) pose = arms_pose_;
        else pose = blade_pose_;

        const mc::rig::Mat4 world = fp::toHost(pose);
        list->SetGraphicsRoot32BitConstants(0, 16, world.m.data(), 16);

        list->DrawIndexedInstanced(sub.index_count, 1, sub.first_index, sub.base_vertex, 0);
    }
}

} // namespace erov
