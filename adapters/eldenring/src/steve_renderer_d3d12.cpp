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

float4 PSMain(VSOut i) : SV_Target {
    if (scene.w > 0.5) {
        // Reverse-Z game depth: nearer = larger, 0 = far. SV_POSITION.w is 1 / view-space z in a pixel shader.
        int2 texel = int2(i.pos.xy / dims.zw * dims.xy);
        float gd = scene_depth.Load(int3(texel, 0)).r;
        float steve_z = rcp(i.pos.w);
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

    // Root signature: 40 root constants (view_proj, world, scene, dims) + a table with the depth SRV.
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 40;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd{};
    rsd.NumParameters = 2;
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
    if (FAILED(hr)) {
        if (log) log("steve: pipeline state failed (0x%08X)", static_cast<unsigned>(hr));
        release();
        return false;
    }
    return true;
}

void SteveRenderer::release() {
    Rel(pso_);
    Rel(root_);
    Rel(vertices_);
    Rel(indices_);
}

void SteveRenderer::setDepthView(ID3D12Device* device, ID3D12Resource* depth, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(depth, &sd, slot); // a null resource makes a valid null descriptor
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
}

} // namespace erov
