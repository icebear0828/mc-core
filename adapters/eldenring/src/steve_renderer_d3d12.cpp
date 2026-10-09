#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "steve_renderer_d3d12.hpp"

namespace erov {
namespace {

constexpr char kShader[] = R"hlsl(
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 scene; // x: depth * view z constant, y: relative bias, z: absolute bias (metres), w: mode (0 off, 1 occlude, 2 debug colours)
    float4 dims;  // x, y: depth texture size, z, w: back buffer size
};
Texture2D<float2> scene_depth : register(t0);
RWByteAddressBuffer stats : register(u0); // [0] sum of K * 1e6, [4] pixel count, [8] max, [12] min (K = depth * view z)

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; float3 wpos : TEXCOORD1; };

VSOut VSMain(VSIn i) {
    VSOut o;
    float4 w = mul(float4(i.pos, 1.0), world);
    o.pos = mul(w, view_proj);
    o.uv = i.uv;
    o.wpos = w.xyz;
    return o;
}

struct FullOut { float4 pos : SV_POSITION; };

FullOut VSFull(uint id : SV_VertexID) {
    FullOut o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

// Debug: the scene depth as grey (scene.x is the gain, sqrt spreads the small reverse-Z values). Exactly zero (cleared,
// or nothing drawn there) is dark blue.
float4 PSDepthView(FullOut i) : SV_Target {
    int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
    float gd = scene_depth.Load(int3(texel, 0)).r;
    if (gd <= 0.0) return float4(0.0, 0.0, 0.25, 0.9);
    float g = sqrt(saturate(gd * scene.x));
    return float4(g, g, g, 0.9);
}

float4 PSMain(VSOut i) : SV_Target {
    if (scene.w > 0.5) {
        // Reverse-Z game depth: nearer = larger, 0 = far. SV_POSITION.w is 1 / view-space z in a pixel shader.
        int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
        float gd = scene_depth.Load(int3(texel, 0)).r;
        float steve_z = rcp(i.pos.w);
        if (scene.w > 2.5) {
            // Measurement: the constant K = depth * view z over the figure's pixels (the native character is drawn
            // in the same place, so the scene depth here is its own distance).
            if (gd > 0.0) {
                uint q = (uint)(gd * steve_z * 1000000.0);
                uint old;
                stats.InterlockedAdd(0, q, old);
                stats.InterlockedAdd(4, 1, old);
                stats.InterlockedMax(8, q, old);
                stats.InterlockedMin(12, q, old);
            }
            return float4(1.0, 1.0, 0.0, 1.0);
        }
        if (scene.w > 1.5) {
            // Calibration view: the scene's distance at this pixel (scene.x / depth) over the figure's own distance.
            if (gd <= 0.0) return float4(1.0, 0.0, 1.0, 1.0);                 // magenta: no depth here (far plane or bad read)
            float r = (scene.x / gd) / steve_z;
            if (r < 0.5) return float4(1.0, 0.0, 0.0, 1.0);                   // red: the scene is much nearer
            if (r < 0.92) return float4(1.0, 0.55, 0.0, 1.0);                 // orange: the scene is somewhat nearer
            if (r < 1.08) return float4(1.0, 1.0, 0.0, 1.0);                  // yellow: same distance (the constant is right)
            if (r < 2.0) return float4(0.0, 0.8, 0.2, 1.0);                   // green: the scene is farther
            return float4(0.0, 0.4, 1.0, 1.0);                                // blue: far behind
        }
        if (gd > 0.0 && scene.x / gd < steve_z * (1.0 - scene.y) - scene.z) discard;
    }
    // Minecraft's fixed face shading (top 1.0, north/south 0.8, east/west 0.6), from the face normal.
    float3 n = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
    float3 a = abs(n);
    float face = a.y > 0.7 ? 1.0 : (a.z > a.x ? 0.82 : 0.68);
    float3 rgb = float3(0.78, 0.60, 0.46) * face;
    return float4(rgb, 1.0);
}
)hlsl";

template <typename T>
void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool Compile(const char* entry, const char* profile, ID3DBlob** out, SteveRenderer::LogFn log) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, "steve.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        if (log) log("steve: shader %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
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

bool SteveRenderer::init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log) {
    release();
    // Geometry: 12 parts, vertices already in the game's axes and metres, feet at the origin.
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        const mc::rig::RigMesh mesh = mc::rig::buildPartMesh(static_cast<mc::StevePart>(p), eldenring::render::kBasis);
        base_vertex_[p] = static_cast<int>(vertices.size());
        first_index_[p] = static_cast<unsigned>(indices.size());
        index_count_[p] = static_cast<unsigned>(mesh.indices.size());
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    }
    vertices_ = UploadBuffer(device, vertices.data(), vertices.size() * sizeof(mc::rig::RigVertex));
    indices_ = UploadBuffer(device, indices.data(), indices.size() * sizeof(uint16_t));
    if (!vertices_ || !indices_) {
        if (log) log("steve: vertex/index buffers failed");
        release();
        return false;
    }
    vbv_ = {vertices_->GetGPUVirtualAddress(), static_cast<UINT>(vertices.size() * sizeof(mc::rig::RigVertex)),
            static_cast<UINT>(sizeof(mc::rig::RigVertex))};
    ibv_ = {indices_->GetGPUVirtualAddress(), static_cast<UINT>(indices.size() * sizeof(uint16_t)), DXGI_FORMAT_R16_UINT};

    // Statistics buffer for the calibration mode: default heap (UAV), an upload buffer with the initial values, readback.
    {
        const uint32_t init_values[4] = {0, 0, 0, 0xFFFFFFFFu};
        stats_init_ = UploadBuffer(device, init_values, sizeof(init_values));
        D3D12_HEAP_PROPERTIES hd{};
        hd.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_HEAP_PROPERTIES hr_{};
        hr_.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = 256;
        rd.Height = 1;
        rd.DepthOrArraySize = 1;
        rd.MipLevels = 1;
        rd.SampleDesc.Count = 1;
        rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        D3D12_RESOURCE_DESC ud = rd;
        ud.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&stats_));
        device->CreateCommittedResource(&hr_, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_PPV_ARGS(&stats_readback_));
    }

    // Root signature: 40 root constants (view_proj, world, scene, dims) + a table with the depth SRV.
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[3]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 40;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
    params[2].Descriptor.ShaderRegister = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 3;
    rsd.pParameters = params;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rs_blob = nullptr;
    ID3DBlob* rs_err = nullptr;
    if (FAILED(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rs_blob, &rs_err)) ||
        FAILED(device->CreateRootSignature(0, rs_blob->GetBufferPointer(), rs_blob->GetBufferSize(), IID_PPV_ARGS(&root_)))) {
        if (log) log("steve: root signature failed: %s", rs_err ? static_cast<const char*>(rs_err->GetBufferPointer()) : "?");
        Rel(rs_blob);
        Rel(rs_err);
        release();
        return false;
    }
    Rel(rs_blob);
    Rel(rs_err);

    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    if (!Compile("VSMain", "vs_5_0", &vs, log) || !Compile("PSMain", "ps_5_0", &ps, log)) {
        Rel(vs);
        Rel(ps);
        release();
        return false;
    }
    const D3D12_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    ID3DBlob* vs_full = nullptr;
    ID3DBlob* ps_full = nullptr;
    const bool full_ok = Compile("VSFull", "vs_5_0", &vs_full, log) && Compile("PSDepthView", "ps_5_0", &ps_full, log);
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {layout, 2};
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.SampleMask = UINT_MAX;
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 12 boxes: no need to trust the mesh winding
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = FALSE; // occlusion is done in the pixel shader
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = rtv_format;
    pd.SampleDesc.Count = 1;
    const HRESULT hr = device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso_));
    Rel(vs);
    Rel(ps);
    if (SUCCEEDED(hr) && full_ok) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC fd = pd;
        fd.VS = {vs_full->GetBufferPointer(), vs_full->GetBufferSize()};
        fd.PS = {ps_full->GetBufferPointer(), ps_full->GetBufferSize()};
        fd.InputLayout = {nullptr, 0};
        fd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        fd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        fd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        fd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        fd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        fd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        fd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        if (FAILED(device->CreateGraphicsPipelineState(&fd, IID_PPV_ARGS(&pso_depthview_))) && log) log("steve: depth view pipeline failed");
    }
    Rel(vs_full);
    Rel(ps_full);
    if (FAILED(hr)) {
        if (log) log("steve: pipeline state failed (0x%08X)", static_cast<unsigned>(hr));
        release();
        return false;
    }
    return true;
}

void SteveRenderer::release() {
    Rel(stats_);
    Rel(stats_init_);
    Rel(stats_readback_);
    Rel(pso_);
    Rel(pso_depthview_);
    Rel(root_);
    Rel(vertices_);
    Rel(indices_);
}

bool SteveRenderer::readStats(uint32_t out[4]) {
    if (!stats_readback_) return false;
    void* mapped = nullptr;
    D3D12_RANGE range{0, 16};
    if (FAILED(stats_readback_->Map(0, &range, &mapped))) return false;
    std::memcpy(out, mapped, 16);
    D3D12_RANGE none{0, 0};
    stats_readback_->Unmap(0, &none);
    return true;
}

void SteveRenderer::setDepthView(ID3D12Device* device, ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depth, &sd, slot); // a null resource makes a valid null descriptor
}

void SteveRenderer::drawDepthView(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                                  D3D12_GPU_DESCRIPTOR_HANDLE depth_table, unsigned width, unsigned height, float gain, float depth_w,
                                  float depth_h) {
    if (!pso_depthview_) return;
    D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->SetPipelineState(pso_depthview_);
    list->SetGraphicsRootSignature(root_);
    ID3D12DescriptorHeap* heaps[] = {srv_heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    const float extra[8] = {gain, 0.f, 0.f, 0.f, depth_w, depth_h, static_cast<float>(width), static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    list->DrawInstanced(3, 1, 0, 0);
}

void SteveRenderer::draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap, D3D12_GPU_DESCRIPTOR_HANDLE depth_table,
                         unsigned width, unsigned height, const mc::rig::Mat4& view_proj,
                         const eldenring::render::PartMatrices& parts, const SteveParams& params) {
    if (!ready()) return;
    D3D12_VIEWPORT vp{0.f, 0.f, static_cast<float>(width), static_cast<float>(height), 0.f, 1.f};
    D3D12_RECT sc{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
    list->RSSetViewports(1, &vp);
    list->RSSetScissorRects(1, &sc);
    list->SetPipelineState(pso_);
    list->SetGraphicsRootSignature(root_);
    ID3D12DescriptorHeap* heaps[] = {srv_heap};
    list->SetDescriptorHeaps(1, heaps);
    list->SetGraphicsRootDescriptorTable(1, depth_table);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);
    const bool measure = params.mode > 2.5f && stats_ && stats_init_ && stats_readback_;
    if (measure) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(stats_, 0, stats_init_, 0, 16);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1, &b);
        list->SetGraphicsRootUnorderedAccessView(2, stats_->GetGPUVirtualAddress());
    }
    list->SetGraphicsRoot32BitConstants(0, 16, view_proj.m.data(), 0);
    const float extra[8] = {params.depth_const,
                            params.rel_bias,
                            params.abs_bias,
                            params.mode,
                            params.depth_w,
                            params.depth_h,
                            static_cast<float>(width),
                            static_cast<float>(height)};
    list->SetGraphicsRoot32BitConstants(0, 8, extra, 32);
    for (size_t p = 0; p < static_cast<size_t>(mc::StevePart::Count); ++p) {
        list->SetGraphicsRoot32BitConstants(0, 16, parts[p].m.data(), 16);
        list->DrawIndexedInstanced(index_count_[p], 1, first_index_[p], base_vertex_[p], 0);
    }
    if (measure) {
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = stats_;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &b);
        list->CopyBufferRegion(stats_readback_, 0, stats_, 0, 16);
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        list->ResourceBarrier(1, &b);
    }
}

} // namespace erov
