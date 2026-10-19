#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>

#include "cs_renderer_d3d12.hpp"
#include "cs_animation_tracks.hpp"
#include "png_wic.hpp"

namespace erov {
namespace fp = eldenring::fp;
namespace {

// Decoded-PNG cache. Switching weapons used to decode three 2048x2048 maps on
// the render thread, which caused a visible hitch. Preload once (at overlay
// init) and reuse the RGBA buffers for every later switch.
struct CachedImage {
    std::vector<uint8_t> rgba;
    unsigned width{0};
    unsigned height{0};
};
std::mutex g_decode_mutex;
std::unordered_map<std::string, CachedImage> g_decode_cache;

bool DecodePngCached(const std::string& path, const uint8_t*& rgba, unsigned& width, unsigned& height) {
    if (path.empty()) return false;
    std::lock_guard<std::mutex> lock(g_decode_mutex);
    auto it = g_decode_cache.find(path);
    if (it == g_decode_cache.end()) {
        CachedImage image;
        if (!DecodePngFile(path, image.rgba, image.width, image.height)) return false;
        it = g_decode_cache.emplace(path, std::move(image)).first;
    }
    rgba = it->second.rgba.data();
    width = it->second.width;
    height = it->second.height;
    return rgba != nullptr && width > 0 && height > 0;
}

constexpr char kCsShader[] = R"hlsl(
cbuffer Root : register(b0) {
    row_major float4x4 view_proj;
    row_major float4x4 world;
    float4 light_dir;
    float4 tint;
    float is_arm;
    float3 padding;
};

Texture2D knife_tex       : register(t0);
Texture2D glove_tex       : register(t1);
Texture2D knife_norm_tex  : register(t2);
Texture2D glove_norm_tex  : register(t3);
Texture2D knife_pbr_tex   : register(t4);
Texture2D glove_pbr_tex   : register(t5);
Texture2D bare_tex        : register(t6);
Texture2D bare_norm_tex   : register(t7);
Texture2D bare_pbr_tex    : register(t8);
Texture2D sleeve_tex      : register(t9);
Texture2D sleeve_norm_tex : register(t10);
Texture2D sleeve_pbr_tex  : register(t11);
SamplerState g_sampler    : register(s0);

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

// Industry-standard Cotangent Frame to compute Tangent-Binormal-Normal (TBN)
// from screen derivatives without requiring vertex tangents
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

// Cook-Torrance Microfacet Distribution: Trowbridge-Reitz GGX
float DistributionGGX(float3 N, float3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (kPi * denom * denom + 1e-7);
}

// Geometric Occlusion / Masking: Smith Schlick-GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx2 * ggx1;
}

// Fresnel-Schlick approximation
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float4 PSMain(VSOut i) : SV_Target {
    float2 uv = i.uv;

    // 1. Albedo & Base Color
    // Genuine CS2 textures: RGB holds diffuse color. Alpha is paint/wear mask (NOT transparency).
    // is_arm: 0 = weapon, 1 = glove, 2 = bare arm skin, 3 = agent sleeve fabric.
    float4 albedo_tex = (is_arm > 2.5) ? sleeve_tex.Sample(g_sampler, uv)
                     : (is_arm > 1.5) ? bare_tex.Sample(g_sampler, uv)
                     : (is_arm > 0.5) ? glove_tex.Sample(g_sampler, uv)
                     : knife_tex.Sample(g_sampler, uv);
    if (is_arm > 0.5 && is_arm < 1.5) {
        albedo_tex.rgb = saturate(0.30 + (albedo_tex.rgb - 0.30) * 2.2) * 1.30;
    } else if (is_arm > 2.5 && is_arm < 3.5) {
        // Fabric is mid-grey in the CS2 source; a gentle contrast keeps the
        // weave readable without turning the sleeve into a flat grey tube.
        albedo_tex.rgb = saturate(0.22 + (albedo_tex.rgb - 0.22) * 1.35) * 1.05;
    }
    albedo_tex.rgb *= tint.rgb;

    // 2. Normal Map aligned with DirectX tangent space
    float4 norm_sample = (is_arm > 2.5) ? sleeve_norm_tex.Sample(g_sampler, uv)
                       : (is_arm > 1.5) ? bare_norm_tex.Sample(g_sampler, uv)
                       : (is_arm > 0.5) ? glove_norm_tex.Sample(g_sampler, uv)
                       : knife_norm_tex.Sample(g_sampler, uv);
    float3 mapN;
    mapN.x = norm_sample.x * 2.0 - 1.0;
    mapN.y = norm_sample.y * 2.0 - 1.0;
    if (is_arm > 0.5 && is_arm < 1.5) {
        mapN.xy *= 1.6;
    } else if (is_arm > 2.5 && is_arm < 3.5) {
        mapN.xy *= 1.25;
    }
    mapN.z = sqrt(saturate(1.0 - dot(mapN.xy, mapN.xy)));

    float3 N_geom = normalize(i.normal);
    float3 N = PerturbNormal(N_geom, i.wpos, uv, mapN);

    // 3. PBR Maps (ORM: R=AO, G=Roughness, B=Metallic)
    float4 pbr = (is_arm > 2.5) ? sleeve_pbr_tex.Sample(g_sampler, uv)
               : (is_arm > 1.5) ? bare_pbr_tex.Sample(g_sampler, uv)
               : (is_arm > 0.5) ? glove_pbr_tex.Sample(g_sampler, uv)
               : knife_pbr_tex.Sample(g_sampler, uv);
    float ao = pbr.r;
    float roughness = clamp(pbr.g, 0.05, 0.98);
    float metallic = clamp(pbr.b, 0.0, 1.0);

    // In camera space, view position is origin (0, 0, 0)
    float3 V = normalize(-i.wpos);
    float3 L = normalize(light_dir.xyz);
    float3 H = normalize(V + L);

    // Dielectric baseline F0 = 0.04, metallic interpolates to albedo
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, albedo_tex.rgb, metallic);

    // Cook-Torrance BRDF evaluation
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    float3 specular = clamp(numerator / denominator, 0.0, 4.0);

    float3 kS = F;
    float3 kD = (float3(1.0, 1.0, 1.0) - kS) * (1.0 - metallic * 0.75); // Preserve albedo details on metallic weapons
    float NdotL = max(dot(N, L), 0.0);

    // Key light + Viewmodel camera fill light
    float3 light_radiance = float3(1.8, 1.8, 1.75);
    float3 Lo_key = (kD * albedo_tex.rgb / kPi + specular) * light_radiance * NdotL;

    float NdotV = max(dot(N, V), 0.0);
    float3 fill_radiance = float3(0.65, 0.65, 0.70);
    float3 Lo_fill = (kD * albedo_tex.rgb / kPi) * fill_radiance * NdotV;

    float3 Lo = Lo_key + Lo_fill;

    // 4. Ambient & Specular Environment Reflection (IBL simulation)
    float3 R = reflect(-V, N);
    float3 sky_color = float3(0.35, 0.38, 0.45);
    float3 ground_color = float3(0.18, 0.18, 0.20);
    float3 ambient_irradiance = lerp(ground_color, sky_color, N.y * 0.5 + 0.5);

    // Specular environment reflection with roughness dissipation
    float3 env_spec = lerp(ground_color, sky_color, R.y * 0.5 + 0.5);
    float3 env_F = F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(clamp(1.0 - max(dot(N, V), 0.0), 0.0, 1.0), 5.0);
    float3 env_reflection = env_spec * env_F * (1.0 - roughness * 0.45);

    // Direct albedo baseline to guarantee weapon skin/texture visibility under all angles
    float3 ambient = (kD * albedo_tex.rgb * ambient_irradiance + env_reflection) * ao;
    float3 color = Lo + ambient + albedo_tex.rgb * 0.25;

    // Tone mapping & saturation
    color = color / (color + 1.0);
    return float4(saturate(color), 1.0);
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

    // Root signature: 44 root constants (b0) + 1 descriptor table (t0..t8: 9 textures) + 1 static sampler (s0)
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 12; // t0..t5 knife/glove maps, t6..t8 bare-arm maps, t9..t11 sleeve maps
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

    if (log_) log_("cs_render: D3D12 Cook-Torrance PBR pipeline initialized successfully");
    return true;
}

void CsRenderer::release() {
    Rel(pso_);
    Rel(root_);
    Rel(vb_);
    Rel(ib_);
    knife_tex_.release();
    glove_tex_.release();
    knife_normal_tex_.release();
    glove_normal_tex_.release();
    knife_pbr_tex_.release();
    glove_pbr_tex_.release();
    bare_tex_.release();
    bare_normal_tex_.release();
    bare_pbr_tex_.release();
    sleeve_tex_.release();
    sleeve_normal_tex_.release();
    sleeve_pbr_tex_.release();
    {
        std::lock_guard<std::mutex> lock(clip_durations_mutex_);
        clip_durations_.clear();
    }
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

bool CsRenderer::setKnifeNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return knife_normal_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setGloveNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return glove_normal_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setKnifePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return knife_pbr_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setGlovePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return glove_pbr_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setBareArmTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return bare_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setBareArmNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return bare_normal_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setBareArmPbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return bare_pbr_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setSleeveTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return sleeve_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setSleeveNormalTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return sleeve_normal_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::setSleevePbrTexture(ID3D12Device* device, const uint8_t* rgba, unsigned width, unsigned height, D3D12_CPU_DESCRIPTOR_HANDLE slot) {
    return sleeve_pbr_tex_.create(device, rgba, width, height, slot);
}

bool CsRenderer::loadTexturesFromFiles(ID3D12Device* device,
                                      const std::string& knife_png,
                                      const std::string& glove_png,
                                      D3D12_CPU_DESCRIPTOR_HANDLE knife_slot,
                                      D3D12_CPU_DESCRIPTOR_HANDLE glove_slot,
                                      const std::string& knife_norm_png,
                                      const std::string& glove_norm_png,
                                      const std::string& knife_pbr_png,
                                      const std::string& glove_pbr_png,
                                      const std::string& bare_png,
                                      const std::string& bare_norm_png,
                                      const std::string& bare_pbr_png,
                                      const std::string& sleeve_png,
                                      const std::string& sleeve_norm_png,
                                      const std::string& sleeve_pbr_png) {
    if (!device) return false;

    const UINT srv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE slot0 = knife_slot;
    D3D12_CPU_DESCRIPTOR_HANDLE slot1 = (glove_slot.ptr != 0) ? glove_slot : D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + srv_inc};
    D3D12_CPU_DESCRIPTOR_HANDLE slot2 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 2 * srv_inc}; // knife_norm
    D3D12_CPU_DESCRIPTOR_HANDLE slot3 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 3 * srv_inc}; // glove_norm
    D3D12_CPU_DESCRIPTOR_HANDLE slot4 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 4 * srv_inc}; // knife_pbr
    D3D12_CPU_DESCRIPTOR_HANDLE slot5 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 5 * srv_inc}; // glove_pbr
    D3D12_CPU_DESCRIPTOR_HANDLE slot6 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 6 * srv_inc}; // bare albedo
    D3D12_CPU_DESCRIPTOR_HANDLE slot7 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 7 * srv_inc}; // bare normal
    D3D12_CPU_DESCRIPTOR_HANDLE slot8 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 8 * srv_inc}; // bare pbr
    D3D12_CPU_DESCRIPTOR_HANDLE slot9 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 9 * srv_inc}; // sleeve albedo
    D3D12_CPU_DESCRIPTOR_HANDLE slot10 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 10 * srv_inc}; // sleeve normal
    D3D12_CPU_DESCRIPTOR_HANDLE slot11 = D3D12_CPU_DESCRIPTOR_HANDLE{slot0.ptr + 11 * srv_inc}; // sleeve pbr

    // 1. Knife Base Color (Albedo)
    const uint8_t* knife_rgba = nullptr;
    unsigned kw = 0, kh = 0;
    if (DecodePngCached(knife_png, knife_rgba, kw, kh)) {
        setKnifeTexture(device, knife_rgba, kw, kh, slot0);
        if (log_) log_("cs_render: loaded knife texture %s (%ux%u)", knife_png.c_str(), kw, kh);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4, 210);
        for (size_t p = 3; p < fallback.size(); p += 4) fallback[p] = 255;
        setKnifeTexture(device, fallback.data(), 16, 16, slot0);
        if (log_) log_("cs_render: knife texture not found at %s, using fallback", knife_png.c_str());
    }

    // 2. Glove Base Color (Albedo)
    const uint8_t* glove_rgba = nullptr;
    unsigned gw = 0, gh = 0;
    if (DecodePngCached(glove_png, glove_rgba, gw, gh)) {
        setGloveTexture(device, glove_rgba, gw, gh, slot1);
        if (log_) log_("cs_render: loaded glove texture %s (%ux%u)", glove_png.c_str(), gw, gh);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4, 45);
        for (size_t p = 3; p < fallback.size(); p += 4) fallback[p] = 255;
        setGloveTexture(device, fallback.data(), 16, 16, slot1);
        if (log_) log_("cs_render: glove texture not found at %s, using fallback", glove_png.c_str());
    }

    std::filesystem::path kp(knife_png);
    std::filesystem::path gp(glove_png);

    // 2.1 Bare-arm skin (forearm / upper-arm sleeve). Material 1 of the CS2
    // glove model, not the glove material.
    const uint8_t* bare_rgba = nullptr;
    unsigned bw = 0, bh = 0;
    if (DecodePngCached(bare_png, bare_rgba, bw, bh)) {
        setBareArmTexture(device, bare_rgba, bw, bh, slot6);
        if (log_) log_("cs_render: loaded bare-arm texture %s (%ux%u)", bare_png.c_str(), bw, bh);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4);
        for (size_t p = 0; p < fallback.size(); p += 4) {
            fallback[p] = 178; fallback[p + 1] = 145; fallback[p + 2] = 124; fallback[p + 3] = 255;
        }
        setBareArmTexture(device, fallback.data(), 16, 16, slot6);
        if (log_) log_("cs_render: bare-arm texture not found, using skin fallback");
    }

    const uint8_t* bare_norm_rgba = nullptr;
    unsigned bnw = 0, bnh = 0;
    if (DecodePngCached(bare_norm_png, bare_norm_rgba, bnw, bnh)) {
        setBareArmNormalTexture(device, bare_norm_rgba, bnw, bnh, slot7);
        if (log_) log_("cs_render: loaded bare-arm normal %s (%ux%u)", bare_norm_png.c_str(), bnw, bnh);
    } else {
        std::vector<uint8_t> flat_norm(16 * 16 * 4);
        for (size_t p = 0; p < flat_norm.size(); p += 4) {
            flat_norm[p] = 128; flat_norm[p + 1] = 128; flat_norm[p + 2] = 255; flat_norm[p + 3] = 255;
        }
        setBareArmNormalTexture(device, flat_norm.data(), 16, 16, slot7);
    }

    const uint8_t* bare_pbr_rgba = nullptr;
    unsigned bpw = 0, bph = 0;
    if (DecodePngCached(bare_pbr_png, bare_pbr_rgba, bpw, bph)) {
        setBareArmPbrTexture(device, bare_pbr_rgba, bpw, bph, slot8);
        if (log_) log_("cs_render: loaded bare-arm ORM %s (%ux%u)", bare_pbr_png.c_str(), bpw, bph);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4);
        for (size_t p = 0; p < fallback.size(); p += 4) {
            fallback[p] = 255; fallback[p + 1] = 180; fallback[p + 2] = 0; fallback[p + 3] = 255;
        }
        setBareArmPbrTexture(device, fallback.data(), 16, 16, slot8);
    }

    // 2.2 Agent uniform sleeve (fabric). The sleeve is a distinct material
    // from the bare-arm skin; binding it here keeps the upper arms from
    // looking like a pale mannequin.
    const uint8_t* sleeve_rgba = nullptr;
    unsigned sw = 0, sh = 0;
    if (DecodePngCached(sleeve_png, sleeve_rgba, sw, sh)) {
        setSleeveTexture(device, sleeve_rgba, sw, sh, slot9);
        if (log_) log_("cs_render: loaded sleeve texture %s (%ux%u)", sleeve_png.c_str(), sw, sh);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4);
        for (size_t p = 0; p < fallback.size(); p += 4) {
            fallback[p] = 76; fallback[p + 1] = 82; fallback[p + 2] = 76; fallback[p + 3] = 255;
        }
        setSleeveTexture(device, fallback.data(), 16, 16, slot9);
        if (log_) log_("cs_render: sleeve texture not found, using fabric fallback");
    }

    const uint8_t* sleeve_norm_rgba = nullptr;
    unsigned snw = 0, snh = 0;
    if (DecodePngCached(sleeve_norm_png, sleeve_norm_rgba, snw, snh)) {
        setSleeveNormalTexture(device, sleeve_norm_rgba, snw, snh, slot10);
        if (log_) log_("cs_render: loaded sleeve normal %s (%ux%u)", sleeve_norm_png.c_str(), snw, snh);
    } else {
        std::vector<uint8_t> flat_norm(16 * 16 * 4);
        for (size_t p = 0; p < flat_norm.size(); p += 4) {
            flat_norm[p] = 128; flat_norm[p + 1] = 128; flat_norm[p + 2] = 255; flat_norm[p + 3] = 255;
        }
        setSleeveNormalTexture(device, flat_norm.data(), 16, 16, slot10);
    }

    const uint8_t* sleeve_pbr_rgba = nullptr;
    unsigned spw = 0, sph = 0;
    if (DecodePngCached(sleeve_pbr_png, sleeve_pbr_rgba, spw, sph)) {
        setSleevePbrTexture(device, sleeve_pbr_rgba, spw, sph, slot11);
        if (log_) log_("cs_render: loaded sleeve ORM %s (%ux%u)", sleeve_pbr_png.c_str(), spw, sph);
    } else {
        std::vector<uint8_t> fallback(16 * 16 * 4);
        for (size_t p = 0; p < fallback.size(); p += 4) {
            fallback[p] = 255; fallback[p + 1] = 200; fallback[p + 2] = 0; fallback[p + 3] = 255;
        }
        setSleevePbrTexture(device, fallback.data(), 16, 16, slot11);
    }

    auto findExistingFile = [](const std::filesystem::path& base_p, const std::string& explicit_override,
                               const std::string& subfolder, const std::string& filename) -> std::string {
        if (!explicit_override.empty() && std::filesystem::exists(explicit_override)) {
            return explicit_override;
        }
        std::vector<std::filesystem::path> tries;
        if (!base_p.empty()) {
            std::filesystem::path parent = base_p.parent_path();
            tries.push_back(parent / filename);
            tries.push_back(parent / "textures" / subfolder / filename);
            tries.push_back(parent / subfolder / filename);
            if (parent.has_parent_path()) {
                tries.push_back(parent.parent_path() / "textures" / subfolder / filename);
            }
        }
        tries.push_back(std::filesystem::path("mods") / "mc_adapter" / "textures" / subfolder / filename);
        tries.push_back(std::filesystem::path("mods") / "mc_adapter" / filename);
        tries.push_back(filename);
        for (const auto& t : tries) {
            if (std::filesystem::exists(t)) return t.string();
        }
        return "";
    };

    // 3. Knife Normal Map
    bool knife_norm_loaded = false;
    std::string kn_path = findExistingFile(kp, knife_norm_png, "knife_butterfly", "default_normal_tga_4fc2d3dd.png");
    if (!kn_path.empty()) {
        const uint8_t* kn_rgba = nullptr;
        unsigned kn_w = 0, kn_h = 0;
        if (DecodePngCached(kn_path, kn_rgba, kn_w, kn_h)) {
            setKnifeNormalTexture(device, kn_rgba, kn_w, kn_h, slot2);
            knife_norm_loaded = true;
            if (log_) log_("cs_render: loaded knife normal map %s (%ux%u)", kn_path.c_str(), kn_w, kn_h);
        }
    }
    if (!knife_norm_loaded) {
        std::vector<uint8_t> flat_norm(16 * 16 * 4, 128);
        for (size_t p = 0; p < flat_norm.size(); p += 4) {
            flat_norm[p] = 128;     // X = 0
            flat_norm[p + 1] = 128; // Y = 0
            flat_norm[p + 2] = 255; // Z = 1
            flat_norm[p + 3] = 255;
        }
        setKnifeNormalTexture(device, flat_norm.data(), 16, 16, slot2);
    }

    // 4. Glove Normal Map
    bool glove_norm_loaded = false;
    std::string gn_path = findExistingFile(gp, glove_norm_png, "glove_hardknuckle", "glove_hardknuckle_normal_tga_a8df881b.png");
    if (!gn_path.empty()) {
        const uint8_t* gn_rgba = nullptr;
        unsigned gn_w = 0, gn_h = 0;
        if (DecodePngCached(gn_path, gn_rgba, gn_w, gn_h)) {
            setGloveNormalTexture(device, gn_rgba, gn_w, gn_h, slot3);
            glove_norm_loaded = true;
            if (log_) log_("cs_render: loaded glove normal map %s (%ux%u)", gn_path.c_str(), gn_w, gn_h);
        }
    }
    if (!glove_norm_loaded) {
        std::vector<uint8_t> flat_norm(16 * 16 * 4, 128);
        for (size_t p = 0; p < flat_norm.size(); p += 4) {
            flat_norm[p] = 128;
            flat_norm[p + 1] = 128;
            flat_norm[p + 2] = 255;
            flat_norm[p + 3] = 255;
        }
        setGloveNormalTexture(device, flat_norm.data(), 16, 16, slot3);
    }

    // 5. Knife PBR Map (ORM: R=AO, G=Roughness, B=Metallic)
    bool knife_pbr_loaded = false;
    if (!knife_pbr_png.empty() && std::filesystem::exists(knife_pbr_png)) {
        const uint8_t* pbr_rgba = nullptr;
        unsigned pw = 0, ph = 0;
        if (DecodePngCached(knife_pbr_png, pbr_rgba, pw, ph)) {
            setKnifePbrTexture(device, pbr_rgba, pw, ph, slot4);
            knife_pbr_loaded = true;
            if (log_) log_("cs_render: loaded knife PBR map %s (%ux%u)", knife_pbr_png.c_str(), pw, ph);
        }
    }
    if (!knife_pbr_loaded) {
        std::string kr_path = findExistingFile(kp, "", "knife_butterfly", "knife_butterfly_rough_psd_80f06397.png");
        std::string kao_path = findExistingFile(kp, "", "knife_butterfly", "knife_butterfly_ao_psd_496afc91.png");
        const uint8_t* r_rgba = nullptr;
        const uint8_t* ao_rgba = nullptr;
        unsigned rw = 0, rh = 0, aow = 0, aoh = 0;
        bool has_r = DecodePngCached(kr_path, r_rgba, rw, rh);
        bool has_ao = DecodePngCached(kao_path, ao_rgba, aow, aoh);
        if (has_r || has_ao) {
            std::vector<uint8_t> orm;
            unsigned orm_w = 0, orm_h = 0;
            if (eldenring::cs::packOrmTexture(has_ao ? ao_rgba : nullptr, aow, aoh,
                                              has_r ? r_rgba : nullptr, rw, rh,
                                              216 /* metallic=0.85 */, orm, orm_w, orm_h)) {
                setKnifePbrTexture(device, orm.data(), orm_w, orm_h, slot4);
                knife_pbr_loaded = true;
                if (log_) log_("cs_render: packed knife ORM PBR map (%ux%u) from AO/Roughness assets", orm_w, orm_h);
            }
        }
    }
    if (!knife_pbr_loaded) {
        // Polished blade fallback: AO=255, Roughness=38 (0.15), Metallic=216 (0.85)
        std::vector<uint8_t> knife_pbr(16 * 16 * 4);
        for (size_t p = 0; p < knife_pbr.size(); p += 4) {
            knife_pbr[p] = 255;     // AO = 1.0
            knife_pbr[p + 1] = 38;  // Roughness = 0.15 (polished steel)
            knife_pbr[p + 2] = 216; // Metallic = 0.85
            knife_pbr[p + 3] = 255;
        }
        setKnifePbrTexture(device, knife_pbr.data(), 16, 16, slot4);
    }

    // 6. Glove PBR Map (ORM)
    bool glove_pbr_loaded = false;
    if (!glove_pbr_png.empty() && std::filesystem::exists(glove_pbr_png)) {
        const uint8_t* pbr_rgba = nullptr;
        unsigned pw = 0, ph = 0;
        if (DecodePngCached(glove_pbr_png, pbr_rgba, pw, ph)) {
            setGlovePbrTexture(device, pbr_rgba, pw, ph, slot5);
            glove_pbr_loaded = true;
            if (log_) log_("cs_render: loaded glove PBR map %s (%ux%u)", glove_pbr_png.c_str(), pw, ph);
        }
    }
    if (!glove_pbr_loaded) {
        std::string gao_path = findExistingFile(gp, "", "glove_hardknuckle", "glove_hardknuckle_ao_psd_5be17a0d.png");
        const uint8_t* ao_rgba = nullptr;
        unsigned aow = 0, aoh = 0;
        if (DecodePngCached(gao_path, ao_rgba, aow, aoh)) {
            std::vector<uint8_t> orm;
            unsigned orm_w = 0, orm_h = 0;
            if (eldenring::cs::packOrmTexture(ao_rgba, aow, aoh, nullptr, 0, 0,
                                              12 /* metallic=0.05 */, orm, orm_w, orm_h)) {
                setGlovePbrTexture(device, orm.data(), orm_w, orm_h, slot5);
                glove_pbr_loaded = true;
                if (log_) log_("cs_render: packed glove ORM PBR map (%ux%u) from AO asset", orm_w, orm_h);
            }
        }
    }
    if (!glove_pbr_loaded) {
        // Tactical glove fallback: AO=255, Roughness=216 (0.85), Metallic=12 (0.05)
        std::vector<uint8_t> glove_pbr(16 * 16 * 4);
        for (size_t p = 0; p < glove_pbr.size(); p += 4) {
            glove_pbr[p] = 255;     // AO = 1.0
            glove_pbr[p + 1] = 216; // Roughness = 0.85 (tactical fabric)
            glove_pbr[p + 2] = 12;  // Metallic = 0.05
            glove_pbr[p + 3] = 255;
        }
        setGlovePbrTexture(device, glove_pbr.data(), 16, 16, slot5);
    }

    return true;
}


bool CsRenderer::loadModel(ID3D12Device* device, const std::string& path) {
    if (!model_.loadFromFile(path)) {
        if (log_) log_("cs_render: failed to load CSVM file from %s", path.c_str());
        return false;
    }
    if (log_) log_("cs_render: CSVM parsed from %s: %u verts, %u indices, %zu submeshes, %zu clips",
                   path.c_str(), model_.header_.total_vertices, model_.header_.total_indices,
                   model_.submeshes.size(), model_.anim_clips.size());

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
        } else if (n.find("sleeve") != std::string::npos) {
            submesh_types_[i] = SubmeshType::Sleeve;
        } else if (n.find("arm_upper") != std::string::npos || n.find("arm_lower") != std::string::npos) {
            submesh_types_[i] = SubmeshType::BareArm;
        } else if (n.find("arm") != std::string::npos || n.find("glove") != std::string::npos ||
                   n.find("hand") != std::string::npos) {
            submesh_types_[i] = SubmeshType::Arms;
        } else {
            submesh_types_[i] = SubmeshType::Base;
        }
    }

    animator_.setModel(&model_);
    {
        // Publish clip durations for the input thread. The render thread owns
        // model_; the dispatcher must never walk anim_clips concurrently.
        std::lock_guard<std::mutex> lock(clip_durations_mutex_);
        clip_durations_.clear();
        for (const auto& entry : model_.anim_clips) {
            const auto& clip = entry.second;
            if (clip.fps > 0.f && clip.frame_count > 1) {
                clip_durations_[entry.first] =
                    static_cast<float>(clip.frame_count - 1) / clip.fps;
            }
        }
    }
    if (log_) log_("cs_render: CSVM model loaded: %u vertices, %u indices, %zu submeshes, %zu anim clips",
                   model_.header_.total_vertices, model_.header_.total_indices, model_.submeshes.size(), model_.anim_clips.size());
    return true;
}

bool CsRenderer::loadWeapon(ID3D12Device* device, const eldenring::cs::CSWeaponDefinition& def) {
    active_weapon_def_ = def;
    has_active_weapon_ = true;
    bool ok = true;
    if (!def.model_csvm_path.empty()) {
        ok = loadModel(device, def.model_csvm_path);
    }
    if (log_) log_("cs_render: loaded weapon '%s' (%s, slot %d) -> %s", def.id.c_str(), def.display_name.c_str(),
                   static_cast<int>(def.slot), ok ? "ok" : "FAILED");
    return ok;
}

bool CsRenderer::loadWeaponTextures(ID3D12Device* device,
                                    const eldenring::cs::CSWeaponDefinition& def,
                                    D3D12_CPU_DESCRIPTOR_HANDLE weapon_slot) {
    if (!device || weapon_slot.ptr == 0) return false;

    const UINT srv_inc = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE normal_slot{weapon_slot.ptr + 2 * srv_inc};
    D3D12_CPU_DESCRIPTOR_HANDLE pbr_slot{weapon_slot.ptr + 4 * srv_inc};

    bool any_loaded = false;
    const uint8_t* rgba = nullptr;
    unsigned width = 0, height = 0;

    if (DecodePngCached(def.albedo_tex_path, rgba, width, height)) {
        setKnifeTexture(device, rgba, width, height, weapon_slot);
        any_loaded = true;
        if (log_) log_("cs_render: weapon albedo %s (%ux%u)", def.albedo_tex_path.c_str(), width, height);
    }
    if (DecodePngCached(def.normal_tex_path, rgba, width, height)) {
        setKnifeNormalTexture(device, rgba, width, height, normal_slot);
        any_loaded = true;
        if (log_) log_("cs_render: weapon normal %s (%ux%u)", def.normal_tex_path.c_str(), width, height);
    }
    if (DecodePngCached(def.pbr_tex_path, rgba, width, height)) {
        setKnifePbrTexture(device, rgba, width, height, pbr_slot);
        any_loaded = true;
        if (log_) log_("cs_render: weapon ORM %s (%ux%u)", def.pbr_tex_path.c_str(), width, height);
    }
    return any_loaded;
}

bool CsRenderer::preloadTexture(const std::string& path) {
    const uint8_t* rgba = nullptr;
    unsigned width = 0, height = 0;
    const bool ok = DecodePngCached(path, rgba, width, height);
    if (ok && log_) log_("cs_render: preloaded texture %s (%ux%u)", path.c_str(), width, height);
    return ok;
}

float CsRenderer::clipDuration(const char* name) const {
    if (name == nullptr) return 0.f;
    std::lock_guard<std::mutex> lock(clip_durations_mutex_);
    const auto it = clip_durations_.find(name);
    return (it != clip_durations_.end()) ? it->second : 0.f;
}

void CsRenderer::tick(float dt, uint8_t state, float anim_time, uint8_t variant, bool hit) {
    animator_.tick(dt, state, anim_time, variant, hit);
}

bool CsRenderer::muzzleScreen(unsigned width, unsigned height, float& out_x, float& out_y) const {
    const eldenring::fp::M4* track = animator_.currentPose().findTrack("muzzle");
    if (track == nullptr) return false;

    // The muzzle track is a viewmodel-space transform; toHost gives the row-vector world matrix the
    // renderer uses. Its translation is the muzzle position, then the viewmodel projection maps it.
    const mc::rig::Mat4 world = eldenring::fp::toHost(*track);
    const mc::rig::Mat4 proj = computeViewmodelProjection(width, height);
    const float p[4] = {world.m[12], world.m[13], world.m[14], 1.f};
    float clip[4];
    for (int j = 0; j < 4; ++j) {
        clip[j] = p[0] * proj.m[j] + p[1] * proj.m[4 + j] + p[2] * proj.m[8 + j] + p[3] * proj.m[12 + j];
    }
    if (!(clip[3] > 0.05f)) return false;
    out_x = (clip[0] / clip[3] * 0.5f + 0.5f) * static_cast<float>(width);
    out_y = (0.5f - clip[1] / clip[3] * 0.5f) * static_cast<float>(height);
    return true;
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
    knife_normal_tex_.recordUpload(list);
    glove_normal_tex_.recordUpload(list);
    knife_pbr_tex_.recordUpload(list);
    glove_pbr_tex_.recordUpload(list);
    bare_tex_.recordUpload(list);
    bare_normal_tex_.recordUpload(list);
    bare_pbr_tex_.recordUpload(list);
    sleeve_tex_.recordUpload(list);
    sleeve_normal_tex_.recordUpload(list);
    sleeve_pbr_tex_.recordUpload(list);

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

    // Decoupled Viewmodel Projection: Source 2 official 68 deg FOV
    const mc::rig::Mat4 final_proj = vm_config_.use_game_projection ? projection : computeViewmodelProjection(width, height);
    list->SetGraphicsRoot32BitConstants(0, 16, final_proj.m.data(), 0);

    const float light[4] = {0.35f, 0.85f, -0.40f, 0.0f};
    list->SetGraphicsRoot32BitConstants(0, 4, light, 32);

    for (size_t i = 0; i < model_.submeshes.size(); ++i) {
        const auto& sub = model_.submeshes[i];
        const auto type = (i < submesh_types_.size()) ? submesh_types_[i] : SubmeshType::Base;

        fp::M4 pose = animator_.basePose();
        float tint[4] = {0.25f, 0.25f, 0.28f, 1.0f};
        const float is_arm = (type == SubmeshType::Sleeve) ? 3.0f
                           : (type == SubmeshType::BareArm) ? 2.0f
                           : (type == SubmeshType::Arms) ? 1.0f : 0.0f;

        // CSVM v3: a submesh follows the animation track named after its bone.
        // Knife arms_glove maps to the legacy "arms" track; gun arms_L/arms_R
        // map to their own hand tracks so each hand follows its clip bone.
        std::string track_name = sub.bone_name;
        if (track_name == "arms_glove") track_name = "arms";
        else if (track_name == "arms_L") track_name = "hand_L";
        else if (track_name == "arms_R") track_name = "hand_R";

        if (const fp::M4* tracked = animator_.currentPose().findTrack(track_name)) {
            pose = *tracked;
        } else {
            switch (type) {
                case SubmeshType::Blade:
                    pose = animator_.bladePose();
                    break;
                case SubmeshType::HandleFront:
                    pose = animator_.handleFrontPose();
                    break;
                case SubmeshType::HandleRear:
                    pose = animator_.handleRearPose();
                    break;
                case SubmeshType::Lock:
                    pose = animator_.lockPose();
                    break;
                case SubmeshType::Arms:
                    pose = animator_.armsPose();
                    break;
                case SubmeshType::Sleeve:
                    pose = animator_.armsPose();
                    break;
                case SubmeshType::Base:
                default:
                    pose = animator_.basePose();
                    break;
            }
        }

        const bool has_tex = (type == SubmeshType::Sleeve) ? (sleeve_tex_.tex != nullptr)
                           : (type == SubmeshType::BareArm) ? (bare_tex_.tex != nullptr)
                           : (type == SubmeshType::Arms) ? (glove_tex_.tex != nullptr)
                           : (knife_tex_.tex != nullptr);
        eldenring::cs::getEffectiveSubmeshTint(type, has_tex, tint);

        const mc::rig::Mat4 world = fp::toHost(pose);
        list->SetGraphicsRoot32BitConstants(0, 16, world.m.data(), 16);
        list->SetGraphicsRoot32BitConstants(0, 4, tint, 36);
        const float arm_param[4] = {is_arm, 0.0f, 0.0f, 0.0f};
        list->SetGraphicsRoot32BitConstants(0, 4, arm_param, 40);

        list->DrawIndexedInstanced(sub.index_count, 1, sub.first_index, sub.base_vertex, 0);
    }
}


} // namespace erov
