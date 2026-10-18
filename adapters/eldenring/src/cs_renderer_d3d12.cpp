#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "cs_renderer_d3d12.hpp"
#include "cs_animation_tracks.hpp"
#include "png_wic.hpp"

namespace erov {
namespace fp = eldenring::fp;
namespace {

constexpr char kCsShader[] = R"hlsl(
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 light_dir;
    float4 tint;
    float is_arm;
    float3 padding;
};

Texture2D knife_tex : register(t0);
Texture2D glove_tex : register(t1);
SamplerState g_sampler : register(s0);

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
    float diff = max(dot(n, l), 0.0) * 0.65 + 0.35;

    float3 v = normalize(-i.wpos);
    float3 h = normalize(l + v);
    float spec = pow(max(dot(n, h), 0.0), 32.0);

    float4 tex_col = (is_arm > 0.5) ? glove_tex.Sample(g_sampler, i.uv) : knife_tex.Sample(g_sampler, i.uv);
    if (tex_col.a < 0.05) {
        tex_col = tint;
    }

    float metallic = (is_arm > 0.5) ? 0.05 : 0.65;
    float3 rgb = tex_col.rgb * diff + spec * metallic;
    return float4(saturate(rgb), 1.0);
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

    // Root signature: 44 root constants (b0) + 1 descriptor table (t0..t1: knife & glove) + 1 static sampler (s0)
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 2; // t0: knife_tex, t1: glove_tex
    range.BaseShaderRegister = 0;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 44;
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
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
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
    knife_tex_.release();
    glove_tex_.release();
}

bool CsRenderer::TextureGpu::create(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    release();
    if (!device || !rgba || width == 0 || height == 0 || slot.ptr == 0) return false;

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

    if (FAILED(device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)))) {
        return false;
    }

    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    device->GetCopyableFootprints(&td, 0, 1, 0, &footprint, &rows, &row_bytes, &total);

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
    if (FAILED(device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload))) ||
        FAILED(upload->Map(0, &none, &mapped))) {
        release();
        return false;
    }

    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + static_cast<size_t>(y) * footprint.Footprint.RowPitch,
                    rgba + static_cast<size_t>(y) * width * 4, static_cast<size_t>(width) * 4);
    }
    upload->Unmap(0, nullptr);

    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex, &sd, slot);
    pending = true;
    return true;
}

void CsRenderer::TextureGpu::recordUpload(ID3D12GraphicsCommandList* list) {
    if (!pending || !tex || !upload) return;
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = tex;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = tex;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    list->ResourceBarrier(1, &b);
    pending = false;
}

bool CsRenderer::setKnifeTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return knife_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setGloveTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return glove_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::loadTexturesFromFiles(ID3D12Device* device,
                                      const std::string& knife_png,
                                      const std::string& glove_png,
                                      D3D12_CPU_DESCRIPTOR_HANDLE knife_slot,
                                      D3D12_CPU_DESCRIPTOR_HANDLE glove_slot) {
    std::vector<uint8_t> knife_rgba;
    unsigned kw = 0, kh = 0;
    if (!knife_png.empty() && DecodePngFile(knife_png, knife_rgba, kw, kh)) {
        if (setKnifeTexture(device, knife_rgba.data(), kw, kh, knife_slot)) {
            if (log_) log_("cs_render: loaded knife texture %s (%ux%u)", knife_png.c_str(), kw, kh);
        }
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4, 210);
        setKnifeTexture(device, fallback.data(), 16, 16, knife_slot);
        if (log_) log_("cs_render: knife texture not found at %s, using fallback", knife_png.c_str());
    }

    std::vector<uint8_t> glove_rgba;
    unsigned gw = 0, gh = 0;
    if (!glove_png.empty() && DecodePngFile(glove_png, glove_rgba, gw, gh)) {
        if (setGloveTexture(device, glove_rgba.data(), gw, gh, glove_slot)) {
            if (log_) log_("cs_render: loaded glove texture %s (%ux%u)", glove_png.c_str(), gw, gh);
        }
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4, 45);
        for (size_t p = 3; p < fallback.size(); p += 4) fallback[p] = 255;
        setGloveTexture(device, fallback.data(), 16, 16, glove_slot);
        if (log_) log_("cs_render: glove texture not found at %s, using fallback", glove_png.c_str());
    }
    return true;
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

    // Identify submesh roles
    submesh_types_.clear();
    submesh_types_.resize(model_.submeshes.size(), SubmeshType::Base);

    for (size_t i = 0; i < model_.submeshes.size(); ++i) {
        std::string n = model_.submeshes[i].bone_name;
        for (char& c : n) c = static_cast<char>(tolower(c));
        if (n.find("blade") != std::string::npos) {
            submesh_types_[i] = SubmeshType::Blade;
        } else if (n.find("front") != std::string::npos || n.find("handle0") != std::string::npos || n.find("handle1") != std::string::npos) {
            submesh_types_[i] = SubmeshType::HandleFront;
        } else if (n.find("rear") != std::string::npos || n.find("handle2") != std::string::npos) {
            submesh_types_[i] = SubmeshType::HandleRear;
        } else if (n.find("lock") != std::string::npos || n.find("latch") != std::string::npos) {
            submesh_types_[i] = SubmeshType::Lock;
        } else if (n.find("arm") != std::string::npos || n.find("glove") != std::string::npos) {
            submesh_types_[i] = SubmeshType::Arms;
        } else {
            submesh_types_[i] = SubmeshType::Base;
        }
    }

    if (log_) log_("cs_render: CSVM model loaded: %u vertices, %u indices, %zu submeshes",
                   model_.header_.total_vertices, model_.header_.total_indices, model_.submeshes.size());
    return true;
}


void CsRenderer::tick(float dt, uint8_t state, float anim_time) {
    (void)dt;
    // Base viewmodel placement in first-person camera space
    // CS2 viewmodel is already pre-posed into authentic camera space
    // Idle stance has subtle breathing bob
    const float bob = std::sin(anim_time * 2.5f) * 0.003f;
    fp::M4 base = fp::translate(0.f, bob, 0.f);

    if (state == 1) { // Authentic CS2 Butterfly Inspect Tricks
        cs_anims::SampleTrack(cs_anims::kInspectBlade, cs_anims::kInspectFrameCount, cs_anims::kInspectFps, anim_time, blade_pose_);
        cs_anims::SampleTrack(cs_anims::kInspectFront, cs_anims::kInspectFrameCount, cs_anims::kInspectFps, anim_time, handle_front_pose_);
        cs_anims::SampleTrack(cs_anims::kInspectRear, cs_anims::kInspectFrameCount, cs_anims::kInspectFps, anim_time, handle_rear_pose_);
        cs_anims::SampleTrack(cs_anims::kInspectLock, cs_anims::kInspectFrameCount, cs_anims::kInspectFps, anim_time, lock_pose_);
        cs_anims::SampleTrack(cs_anims::kInspectArms, cs_anims::kInspectFrameCount, cs_anims::kInspectFps, anim_time, arms_pose_);
        blade_pose_ = fp::mul(base, blade_pose_);
        handle_front_pose_ = fp::mul(base, handle_front_pose_);
        handle_rear_pose_ = fp::mul(base, handle_rear_pose_);
        lock_pose_ = fp::mul(base, lock_pose_);
        arms_pose_ = fp::mul(base, arms_pose_);
        base_pose_ = arms_pose_;
    } else if (state == 4) { // Authentic CS2 Butterfly Deploy Flip
        cs_anims::SampleTrack(cs_anims::kDeployBlade, cs_anims::kDeployFrameCount, cs_anims::kDeployFps, anim_time, blade_pose_);
        cs_anims::SampleTrack(cs_anims::kDeployFront, cs_anims::kDeployFrameCount, cs_anims::kDeployFps, anim_time, handle_front_pose_);
        cs_anims::SampleTrack(cs_anims::kDeployRear, cs_anims::kDeployFrameCount, cs_anims::kDeployFps, anim_time, handle_rear_pose_);
        cs_anims::SampleTrack(cs_anims::kDeployLock, cs_anims::kDeployFrameCount, cs_anims::kDeployFps, anim_time, lock_pose_);
        cs_anims::SampleTrack(cs_anims::kDeployArms, cs_anims::kDeployFrameCount, cs_anims::kDeployFps, anim_time, arms_pose_);
        blade_pose_ = fp::mul(base, blade_pose_);
        handle_front_pose_ = fp::mul(base, handle_front_pose_);
        handle_rear_pose_ = fp::mul(base, handle_rear_pose_);
        lock_pose_ = fp::mul(base, lock_pose_);
        arms_pose_ = fp::mul(base, arms_pose_);
        base_pose_ = arms_pose_;
    } else if (state == 3) { // Authentic CS2 Backstab / Heavy Attack
        cs_anims::SampleTrack(cs_anims::kBackstabBlade, cs_anims::kBackstabFrameCount, cs_anims::kBackstabFps, anim_time, blade_pose_);
        cs_anims::SampleTrack(cs_anims::kBackstabFront, cs_anims::kBackstabFrameCount, cs_anims::kBackstabFps, anim_time, handle_front_pose_);
        cs_anims::SampleTrack(cs_anims::kBackstabRear, cs_anims::kBackstabFrameCount, cs_anims::kBackstabFps, anim_time, handle_rear_pose_);
        cs_anims::SampleTrack(cs_anims::kBackstabLock, cs_anims::kBackstabFrameCount, cs_anims::kBackstabFps, anim_time, lock_pose_);
        cs_anims::SampleTrack(cs_anims::kBackstabArms, cs_anims::kBackstabFrameCount, cs_anims::kBackstabFps, anim_time, arms_pose_);
        blade_pose_ = fp::mul(base, blade_pose_);
        handle_front_pose_ = fp::mul(base, handle_front_pose_);
        handle_rear_pose_ = fp::mul(base, handle_rear_pose_);
        lock_pose_ = fp::mul(base, lock_pose_);
        arms_pose_ = fp::mul(base, arms_pose_);
        base_pose_ = arms_pose_;
    } else if (state == 2) { // Slash (Light Attack)
        const float p = std::clamp(anim_time / 0.35f, 0.f, 1.f);
        const float slash_rot = -30.f + p * 60.f;
        const float forward = -std::sin(p * 3.14159f) * 0.08f;
        fp::M4 slash_base = fp::mul(base, fp::translate(0.f, 0.f, forward));
        slash_base = fp::mul(slash_base, fp::rotZ(slash_rot));
        blade_pose_ = slash_base;
        handle_front_pose_ = slash_base;
        handle_rear_pose_ = slash_base;
        lock_pose_ = slash_base;
        base_pose_ = slash_base;
        arms_pose_ = slash_base;
    } else { // Idle
        blade_pose_ = base;
        handle_front_pose_ = base;
        handle_rear_pose_ = base;
        lock_pose_ = base;
        base_pose_ = base;
        arms_pose_ = base;
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

    if (dsv.ptr != 0) {
        list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    } else {
        list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }

    knife_tex_.recordUpload(list);
    glove_tex_.recordUpload(list);

    list->SetPipelineState(pso_);
    list->SetGraphicsRootSignature(root_);

    if (srv_heap && tex_table.ptr != 0) {
        ID3D12DescriptorHeap* heaps[] = {srv_heap};
        list->SetDescriptorHeaps(1, heaps);
        list->SetGraphicsRootDescriptorTable(1, tex_table);
    }

    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    list->IASetVertexBuffers(0, 1, &vbv_);
    list->IASetIndexBuffer(&ibv_);

    // 16 floats view_proj
    list->SetGraphicsRoot32BitConstants(0, 16, projection.m.data(), 0);

    const float light[4] = {0.35f, 0.85f, -0.40f, 0.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, light, 32);

    for (size_t i = 0; i < model_.submeshes.size(); ++i) {
        const auto& sub = model_.submeshes[i];
        const auto type = (i < submesh_types_.size()) ? submesh_types_[i] : SubmeshType::Base;

        fp::M4 pose = base_pose_;
        float tint[4] = {0.25f, 0.25f, 0.28f, 1.0f};
        const float is_arm = (type == SubmeshType::Arms) ? 1.0f : 0.0f;

        switch (type) {
            case SubmeshType::Blade:
                pose = blade_pose_;
                tint[0] = 0.95f; tint[1] = 0.95f; tint[2] = 0.98f; tint[3] = 1.0f; // Steel blade
                break;
            case SubmeshType::HandleFront:
                pose = handle_front_pose_;
                tint[0] = 0.25f; tint[1] = 0.25f; tint[2] = 0.28f; tint[3] = 1.0f; // Front handle
                break;
            case SubmeshType::HandleRear:
                pose = handle_rear_pose_;
                tint[0] = 0.25f; tint[1] = 0.25f; tint[2] = 0.28f; tint[3] = 1.0f; // Rear handle
                break;
            case SubmeshType::Lock:
                pose = lock_pose_;
                tint[0] = 0.85f; tint[1] = 0.85f; tint[2] = 0.88f; tint[3] = 1.0f; // Lock latch
                break;
            case SubmeshType::Arms:
                pose = arms_pose_;
                tint[0] = 0.20f; tint[1] = 0.22f; tint[2] = 0.24f; tint[3] = 1.0f; // Tactical glove
                break;
            case SubmeshType::Base:
            default:
                pose = base_pose_;
                tint[0] = 0.25f; tint[1] = 0.25f; tint[2] = 0.28f; tint[3] = 1.0f;
                break;
        }

        const mc::rig::Mat4 world = fp::toHost(pose);
        list->SetGraphicsRoot32BitConstants(0, 16, world.m.data(), 16);
        list->SetGraphicsRoot32BitConstants(0, 4, tint, 36);
        const float arm_param[4] = {is_arm, 0.0f, 0.0f, 0.0f};
        list->SetGraphicsRoot32BitConstants(0, 4, arm_param, 40);

        list->DrawIndexedInstanced(sub.index_count, 1, sub.first_index, sub.base_vertex, 0);
    }
}


} // namespace erov
