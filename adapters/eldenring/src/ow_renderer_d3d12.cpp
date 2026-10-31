#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ow_renderer_d3d12.hpp"
#include "png_wic.hpp"

namespace erow {
namespace fp = eldenring::fp;
namespace {

constexpr char kOwShader[] = R"hlsl(
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 light_dir;
    float4 tint;
};

Texture2D albedo_tex : register(t0);
Texture2D normal_tex : register(t1);
Texture2D pbr_tex    : register(t2);
SamplerState g_sampler : register(s0);

struct VSIn {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
};

struct VSOut {
    float4 pos    : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD0;
    float3 wpos   : TEXCOORD1;
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

// Cotangent frame from screen derivatives: no vertex tangents required.
float3 PerturbNormal(float3 N, float3 p, float2 uv, float3 mapN) {
    float3 dp1 = ddx(p);
    float3 dp2 = ddy(p);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, N);
    float3 dp1perp = cross(N, dp1);
    float3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float det = max(dot(T, T), dot(B, B));
    if (det < 1e-6) return N;
    float invmax = rsqrt(det);
    float3x3 TBN = float3x3(T * invmax, B * invmax, N);
    float3 res = mul(mapN, TBN);
    float len2 = dot(res, res);
    return (len2 > 1e-6) ? (res * rsqrt(len2)) : N;
}

static const float kPi = 3.14159265358979323846;

float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (kPi * denom * denom + 1e-7);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float3 AcesFilm(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(VSOut i) : SV_Target {
    float4 albedo = albedo_tex.Sample(g_sampler, i.uv);
    albedo.rgb *= tint.rgb;

    float4 norm_sample = normal_tex.Sample(g_sampler, i.uv);
    float3 mapN;
    mapN.x = norm_sample.x * 2.0 - 1.0;
    // DirectX normal maps store green pointing down; the tangent frame is right-handed.
    mapN.y = 1.0 - norm_sample.y * 2.0;
    mapN.z = sqrt(saturate(1.0 - dot(mapN.xy, mapN.xy)));

    float3 N = PerturbNormal(normalize(i.normal), i.wpos, i.uv, mapN);

    float4 pbr = pbr_tex.Sample(g_sampler, i.uv);
    float ao = pbr.r;
    float roughness = clamp(pbr.g, 0.05, 0.98);
    float metallic = clamp(pbr.b, 0.0, 1.0);

    float3 V = normalize(-i.wpos);
    float3 L = normalize(light_dir.xyz);
    float3 H = normalize(V + L);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), albedo.rgb, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    float3 specular = clamp(numerator / denominator, 0.0, 4.0);

    float3 kS = F;
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);

    float3 light_radiance = float3(1.8, 1.8, 1.75);
    float direct_ao = lerp(1.0, ao, 0.75);
    float3 Lo_key = (kD * albedo.rgb / kPi + specular) * light_radiance * NdotL * direct_ao;

    float NdotV = max(dot(N, V), 0.0);
    float3 fill_radiance = float3(0.65, 0.65, 0.70);
    float3 Lo_fill = (kD * albedo.rgb / kPi) * fill_radiance * NdotV * ao;
    float3 Lo = Lo_key + Lo_fill;

    // Same IBL-style ambient as the CS2 viewmodel so the gauntlet keeps the
    // metallic studio look the original textures were authored for.
    float3 R = reflect(-V, N);
    float3 sky_color = float3(0.35, 0.38, 0.45);
    float3 ground_color = float3(0.18, 0.18, 0.20);
    float3 ambient_irradiance = lerp(ground_color, sky_color, N.y * 0.5 + 0.5);
    float3 env_spec = lerp(ground_color, sky_color, R.y * 0.5 + 0.5);
    float3 env_F = F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) *
                            pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 5.0);
    float3 env_reflection = env_spec * env_F * (1.0 - roughness * 0.55) * 1.35;
    float3 ambient = (kD * albedo.rgb * ambient_irradiance + env_reflection) * ao;
    float3 color = Lo + ambient + albedo.rgb * 0.08;

    float rim = pow(1.0 - saturate(dot(N, V)), 3.0) * 0.22;
    color += rim * float3(0.55, 0.62, 0.72);

    float sky_occlusion = lerp(0.78, 1.0, saturate(N.y * 0.5 + 0.5));
    color *= sky_occlusion;
    color = AcesFilm(color * 1.15);
    return float4(color, 1.0);
}
)hlsl";

template <typename T>
void Rel(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

bool CompileShader(const char* entry, const char* profile, ID3DBlob** out, OwRenderer::LogFn log) {
    ID3DBlob* errors = nullptr;
    const HRESULT hr = D3DCompile(kOwShader, sizeof(kOwShader) - 1, "ow_viewmodel.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out, &errors);
    if (FAILED(hr)) {
        if (log) log("ow_render: shader %s failed: %s", entry, errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        Rel(errors);
        return false;
    }
    Rel(errors);
    return true;
}

ID3D12Resource* UploadBuffer(ID3D12Device* device, const void* data, size_t size) {
    if (!device || !data || size == 0) return nullptr;
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

// Persistent upload buffer for the bone palette: mapped once at init and
// rewritten every frame, so the draw path never allocates.
ID3D12Resource* MappedUploadBuffer(ID3D12Device* device, size_t size, void** mapped_out) {
    if (!device || size == 0 || mapped_out == nullptr) return nullptr;
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
    D3D12_RANGE none{0, 0};
    if (FAILED(res->Map(0, &none, mapped_out))) {
        res->Release();
        return nullptr;
    }
    std::memset(*mapped_out, 0, size);
    return res;
}

bool ReadFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    if (size <= 0) return false;
    f.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(f);
}

// CPU skinning: blend up to four palette matrices per vertex and write the
// 40-byte CSVM vertex the unchanged v1 shader consumes. The palette already
// maps bind space into viewmodel space, so the shader's `world` still applies
// the viewmodel bob/scale exactly as it did for the rigid asset.
void SkinVertices(const eldenring::ow::OwModel& model, const std::vector<fp::M4>& palette,
                  eldenring::ow::OwVertex* out) {
    for (size_t i = 0; i < model.vertices.size(); ++i) {
        const eldenring::ow::OwVertex& v = model.vertices[i];
        float p[3] = {0.f, 0.f, 0.f};
        float n[3] = {0.f, 0.f, 0.f};
        uint32_t weight_sum = 0;
        for (int j = 0; j < 4; ++j) {
            const uint32_t weight = v.weights[j];
            if (weight == 0) continue;
            const uint32_t bone = v.joints[j];
            if (bone >= palette.size()) continue;
            const fp::M4& m = palette[bone];
            const float w = static_cast<float>(weight) / 255.0f;
            p[0] += w * (m.m[0] * v.pos[0] + m.m[1] * v.pos[1] + m.m[2] * v.pos[2] + m.m[3]);
            p[1] += w * (m.m[4] * v.pos[0] + m.m[5] * v.pos[1] + m.m[6] * v.pos[2] + m.m[7]);
            p[2] += w * (m.m[8] * v.pos[0] + m.m[9] * v.pos[1] + m.m[10] * v.pos[2] + m.m[11]);
            n[0] += w * (m.m[0] * v.normal[0] + m.m[1] * v.normal[1] + m.m[2] * v.normal[2]);
            n[1] += w * (m.m[4] * v.normal[0] + m.m[5] * v.normal[1] + m.m[6] * v.normal[2]);
            n[2] += w * (m.m[8] * v.normal[0] + m.m[9] * v.normal[1] + m.m[10] * v.normal[2]);
            weight_sum += weight;
        }
        eldenring::ow::OwVertex& dst = out[i];
        if (weight_sum == 0) {
            for (int k = 0; k < 3; ++k) {
                p[k] = v.pos[k];
                n[k] = v.normal[k];
            }
        }
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 1e-6f && std::isfinite(len)) {
            n[0] /= len; n[1] /= len; n[2] /= len;
        } else {
            n[0] = 0.f; n[1] = 0.f; n[2] = 1.f;
        }
        for (int k = 0; k < 3; ++k) {
            dst.pos[k] = p[k];
            dst.normal[k] = n[k];
        }
        dst.uv[0] = v.uv[0];
        dst.uv[1] = v.uv[1];
        std::memset(dst.joints, 0, sizeof(dst.joints));
        std::memset(dst.weights, 0, sizeof(dst.weights));
    }
}

} // namespace

void OwRenderer::TextureGpu::release() {
    Rel(tex);
    Rel(upload);
    pending = false;
}

bool OwRenderer::TextureGpu::create(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height,
                                    D3D12_CPU_DESCRIPTOR_HANDLE slot, bool srgb) {
    release();
    if (!device || !rgba || width == 0 || height == 0 || slot.ptr == 0) return false;

    // Albedo is sRGB-encoded; normal/ORM maps are linear data.
    const DXGI_FORMAT format = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;

    D3D12_HEAP_PROPERTIES hd{};
    hd.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = width;
    td.Height = height;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = format;
    td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&tex)))) {
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
    if (FAILED(device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&upload))) ||
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
    sd.Format = td.Format;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(tex, &sd, slot);
    pending = true;
    return true;
}

void OwRenderer::TextureGpu::recordUpload(ID3D12GraphicsCommandList* list) {
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

bool OwRenderer::init(ID3D12Device* device, DXGI_FORMAT rtv_format, LogFn log) {
    release();
    log_ = log;
    device_ = device;
    if (!device) return false;

    // Root signature: 40 root constants (b0) + one table of three SRVs + a static sampler.
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 3;
    range.BaseShaderRegister = 0;
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
        if (log_) log_("ow_render: root signature failed");
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
    // The UE gauntlet mesh is not guaranteed to use the same winding as the
    // host; draw both faces and let the depth test pick the nearest surface.
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
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
        if (log_) log_("ow_render: PSO creation failed");
        release();
        return false;
    }

    if (log_) log_("ow_render: D3D12 PBR pipeline initialized");
    return true;
}

bool OwRenderer::ensureDepth(ID3D12Device* device, unsigned width, unsigned height) {
    if (depth_tex_ && depth_w_ == width && depth_h_ == height) return true;
    if (!device || width == 0 || height == 0) return false;

    Rel(depth_tex_);
    if (!dsv_heap_) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        hd.NumDescriptors = 1;
        if (FAILED(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&dsv_heap_)))) return false;
    }

    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = width;
    rd.Height = height;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_D32_FLOAT;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 1.0f;
    if (FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                                               IID_PPV_ARGS(&depth_tex_)))) {
        return false;
    }
    device->CreateDepthStencilView(depth_tex_, nullptr, dsv_heap_->GetCPUDescriptorHandleForHeapStart());
    dsv_cpu_ = dsv_heap_->GetCPUDescriptorHandleForHeapStart();
    depth_w_ = width;
    depth_h_ = height;
    return true;
}

void OwRenderer::release() {
    Rel(pso_);
    Rel(root_);
    Rel(vb_);
    Rel(ib_);
    if (skinned_vb_) {
        skinned_vb_->Unmap(0, nullptr);
        Rel(skinned_vb_);
    }
    skinned_vb_mapped_ = nullptr;
    skinned_slot_stride_ = 0;
    skinned_slot_ = 0;
    Rel(depth_tex_);
    Rel(dsv_heap_);
    dsv_cpu_ = {};
    depth_w_ = depth_h_ = 0;
    device_ = nullptr;
    albedo_tex_.release();
    normal_tex_.release();
    pbr_tex_.release();
    animator_.setModel(nullptr);
    model_ = eldenring::ow::OwModel{};
    model_loaded_ = false;
}

bool OwRenderer::loadModel(ID3D12Device* device, const std::string& path) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(path, bytes)) {
        if (log_) log_("ow_render: cannot read %s", path.c_str());
        return false;
    }
    eldenring::ow::OwModel parsed;
    if (!eldenring::ow::parseOwModel(bytes.data(), bytes.size(), parsed)) {
        if (log_) log_("ow_render: invalid or unsupported CSVM %s", path.c_str());
        return false;
    }

    Rel(vb_);
    Rel(ib_);
    if (skinned_vb_) {
        skinned_vb_->Unmap(0, nullptr);
        Rel(skinned_vb_);
    }
    skinned_vb_mapped_ = nullptr;
    skinned_slot_stride_ = 0;
    skinned_slot_ = 0;

    const size_t ib_size = parsed.indices.size() * sizeof(uint32_t);
    ib_ = UploadBuffer(device, parsed.indices.data(), ib_size);
    if (!ib_) {
        if (log_) log_("ow_render: index upload failed");
        return false;
    }

    const UINT vertex_stride = static_cast<UINT>(sizeof(eldenring::ow::OwVertex));
    const size_t per_slot = parsed.vertices.size() * vertex_stride;
    if (parsed.skinned()) {
        // CPU skinning writes this ring every frame; keep slots 256-byte
        // aligned so each one is a valid vertex-buffer base address.
        const UINT slot_stride = static_cast<UINT>((per_slot + 255u) & ~static_cast<size_t>(255u));
        skinned_vb_ = MappedUploadBuffer(device, static_cast<size_t>(slot_stride) * kSkinnedSlots,
                                         &skinned_vb_mapped_);
        if (!skinned_vb_) {
            Rel(ib_);
            if (log_) log_("ow_render: skinned vertex buffer creation failed");
            return false;
        }
        skinned_slot_stride_ = slot_stride;
        vbv_ = {skinned_vb_->GetGPUVirtualAddress(), static_cast<UINT>(per_slot), vertex_stride};
    } else {
        vb_ = UploadBuffer(device, parsed.vertices.data(), per_slot);
        if (!vb_) {
            Rel(ib_);
            if (log_) log_("ow_render: vertex upload failed");
            return false;
        }
        vbv_ = {vb_->GetGPUVirtualAddress(), static_cast<UINT>(per_slot), vertex_stride};
    }
    ibv_ = {ib_->GetGPUVirtualAddress(), static_cast<UINT>(ib_size), DXGI_FORMAT_R32_UINT};
    {
        std::lock_guard<std::mutex> lock(model_mutex_);
        model_ = std::move(parsed);
    }
    animator_.setModel(&model_);
    model_loaded_ = true;
    if (log_) {
        log_("ow_render: gauntlet loaded from %s (%zu verts, %zu indices, %zu submeshes, %zu bones, %zu clips)",
             path.c_str(), model_.vertices.size(), model_.indices.size(), model_.submeshes.size(),
             model_.bones.size(), model_.clips.size());
    }
    return true;
}

float OwRenderer::clipDuration(const char* name) const {
    std::lock_guard<std::mutex> lock(model_mutex_);
    return model_.clipDuration(name);
}

bool OwRenderer::loadTextures(ID3D12Device* device, const std::string& albedo_png, const std::string& normal_png,
                              const std::string& pbr_png, D3D12_CPU_DESCRIPTOR_HANDLE first_slot) {
    if (!device || first_slot.ptr == 0) return false;
    const UINT inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    auto solid = [](std::vector<uint8_t>& out, uint8_t r, uint8_t g, uint8_t b) {
        out.resize(4 * 4 * 4);
        for (size_t i = 0; i < out.size(); i += 4) {
            out[i] = r;
            out[i + 1] = g;
            out[i + 2] = b;
            out[i + 3] = 255;
        }
    };

    std::vector<uint8_t> rgba;
    unsigned w = 0, h = 0;
    if (albedo_png.empty() || !erov::DecodePngFile(albedo_png, rgba, w, h)) {
        if (log_) log_("ow_render: albedo %s missing, using flat grey", albedo_png.c_str());
        solid(rgba, 45, 45, 45);
        w = h = 4;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE slot = first_slot;
    if (!albedo_tex_.create(device, rgba.data(), w, h, slot, /*srgb=*/true)) {
        if (log_) log_("ow_render: albedo upload failed");
        return false;
    }
    slot.ptr += inc;

    // A missing normal/ORM map must still bind a real descriptor: sampling an
    // unwritten slot is invalid in D3D12. Fall back to a flat map.
    if (!normal_png.empty() && erov::DecodePngFile(normal_png, rgba, w, h)) {
        normal_tex_.create(device, rgba.data(), w, h, slot);
    } else {
        solid(rgba, 128, 128, 255); // mapN = (0, 0, 1)
        normal_tex_.create(device, rgba.data(), 4, 4, slot);
    }
    slot.ptr += inc;
    if (!pbr_png.empty() && erov::DecodePngFile(pbr_png, rgba, w, h)) {
        pbr_tex_.create(device, rgba.data(), w, h, slot);
    } else {
        solid(rgba, 255, 200, 0); // AO = 1, roughness = 0.78, metallic = 0
        pbr_tex_.create(device, rgba.data(), 4, 4, slot);
    }
    return true;
}

void OwRenderer::draw(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* srv_heap,
                      D3D12_GPU_DESCRIPTOR_HANDLE tex_table, unsigned width, unsigned height,
                      const mc::rig::Mat4& projection, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                      D3D12_CPU_DESCRIPTOR_HANDLE dsv) {
    if (!ready() || !list) return;

    const bool dedicated_depth = ensureDepth(device_, width, height);
    if (dedicated_depth) dsv = dsv_cpu_;

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

    albedo_tex_.recordUpload(list);
    normal_tex_.recordUpload(list);
    pbr_tex_.recordUpload(list);

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

    list->SetGraphicsRoot32BitConstants(0, 16, projection.m.data(), 0);

    const std::vector<fp::M4>& palette = animator_.palette();
    const std::vector<fp::M4>* active_palette = &palette;
    if (animator_.currentState() == 2 && cfg_.shoot_motion < 0.999f) {
        // The authored hand cannon clip throws the gauntlet 0.35 m towards the
        // camera and 0.34 m up. Blend each bone back towards the idle pose so
        // the recoil reads as a kick instead of the whole arm floating.
        const auto idle_it = model_.clips.find("idle");
        if (idle_it != model_.clips.end()) {
            effective_palette_ = palette;
            const auto& idle_tracks = idle_it->second.tracks;
            for (size_t b = 0; b < effective_palette_.size(); ++b) {
                if (b < idle_tracks.size() && !idle_tracks[b].empty()) {
                    effective_palette_[b] =
                        eldenring::ow::owLerpMatrix(idle_tracks[b][0], effective_palette_[b], cfg_.shoot_motion);
                }
            }
            active_palette = &effective_palette_;
        }
    }
    const bool skinned = model_.skinned() && skinned_vb_mapped_ != nullptr && !active_palette->empty();
    if (skinned) {
        const uint32_t slot = skinned_slot_;
        skinned_slot_ = (skinned_slot_ + 1u) % kSkinnedSlots;
        auto* dst = reinterpret_cast<eldenring::ow::OwVertex*>(
            static_cast<uint8_t*>(skinned_vb_mapped_) + static_cast<size_t>(slot) * skinned_slot_stride_);
        SkinVertices(model_, *active_palette, dst);
        vbv_ = {skinned_vb_->GetGPUVirtualAddress() + static_cast<UINT64>(slot) * skinned_slot_stride_,
                static_cast<UINT>(model_.vertices.size() * sizeof(eldenring::ow::OwVertex)),
                static_cast<UINT>(sizeof(eldenring::ow::OwVertex))};
    }

    fp::M4 world = animator_.pose().base;
    if (cfg_.scale != 1.0f) world = fp::mul(world, fp::scale(cfg_.scale));
    const mc::rig::Mat4 host_world = fp::toHost(world);
    list->SetGraphicsRoot32BitConstants(0, 16, host_world.m.data(), 16);

    const float light[4] = {0.35f, 0.85f, -0.40f, 0.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, light, 32);
    const float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, tint, 36);

    for (const eldenring::ow::OwSubmesh& sub : model_.submeshes) {
        if (sub.index_count == 0) continue;
        list->DrawIndexedInstanced(sub.index_count, 1, sub.first_index, sub.base_vertex, 0);
    }
}

} // namespace erow
