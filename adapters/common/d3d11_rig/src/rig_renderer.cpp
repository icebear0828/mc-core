#include "d3d11_rig/rig_renderer.hpp"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace mc::d3d11 {

namespace {

using Microsoft::WRL::ComPtr;

constexpr char kShaderSource[] = R"hlsl(
cbuffer Frame : register(b0) { row_major float4x4 view_proj; };
cbuffer Part  : register(b1) { row_major float4x4 world; };
cbuffer Scene : register(b0) {
    float4 scene; // x depth*z constant, y relative bias, z metre bias, w occlusion enabled
    float4 probe; // xy: uv of the scene around Steve, z: ambient matching enabled
};
Texture2D skin : register(t0);
Texture2D<float2> scene_depth : register(t1);
Texture2D scene_color : register(t2);
SamplerState point_clamp : register(s0);
SamplerState linear_clamp : register(s1);

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
        float gd = scene_depth.Load(int3(i.pos.xy, 0)).r;
        float steve_z = rcp(i.pos.w);
        if (gd > 0.0 && scene.x / gd < steve_z * (1.0 - scene.y) - scene.z) discard;
    }
    float4 c = skin.Sample(point_clamp, i.uv);
    clip(c.a - 0.5);          // overlay layers (hat, jacket, sleeves, pants) are cut out, not blended
    float3 rgb = c.rgb;

    if (probe.z > 0.5) {
        // Minecraft's fixed face shading (top 1.0, north/south 0.8, east/west 0.6), from the face normal.
        float3 n = normalize(cross(ddx(i.wpos), ddy(i.wpos)));
        float3 a = abs(n);
        float face = a.y > 0.7 ? 1.0 : (a.z > a.x ? 0.82 : 0.68);

        // Light the figure like the world around it: average colour of the frame near Steve (mip chain of
        // the frame copy), taken ahead of him so he does not light himself.
        float3 amb = 0;
        [unroll] for (int k = 0; k < 5; ++k) {
            float2 o = float2((k - 2) * 0.05, ((k % 2) * 2 - 1) * 0.04);
            amb += scene_color.SampleLevel(linear_clamp, saturate(probe.xy + o), 6.0).rgb;
        }
        amb *= 0.2;
        float luma = dot(amb, float3(0.299, 0.587, 0.114));
        float exposure = clamp(0.30 + 2.6 * luma, 0.30, 1.0);   // dark places darken him, bright ones leave him alone
        float3 tint = amb / max(luma, 1e-3);
        tint = lerp(float3(1, 1, 1), clamp(tint, 0.5, 1.6), 0.45);   // pick up the scene's colour cast

        float g = dot(rgb, float3(0.299, 0.587, 0.114));
        rgb = lerp(float3(g, g, g), rgb, 0.82);                // the game is desaturated; pure MC colours are not
        rgb = rgb * face * exposure * tint;
    }
    return float4(rgb, 1.0);
}
)hlsl";

void logLine(const char* what) {
    FILE* f = nullptr;
    if (fopen_s(&f, "mc_adapter.log", "a") == 0 && f) {
        fprintf(f, "[mc_adapter] RigRenderer: %s\n", what);
        fclose(f);
    }
}

bool compile(const char* entry, const char* profile, ComPtr<ID3DBlob>& out) {
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(kShaderSource, sizeof(kShaderSource) - 1, "steve.hlsl", nullptr, nullptr, entry, profile,
                                  D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, out.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(hr)) {
        std::string msg = std::string("shader compile failed (") + entry + "): " +
                          (errors ? static_cast<const char*>(errors->GetBufferPointer()) : "no message");
        logLine(msg.c_str());
        return false;
    }
    return true;
}

// Everything the draw touches, so the game's next frame sees the state it left.
struct StateBackup {
    D3D11_VIEWPORT viewport{};
    UINT viewport_count{1};
    ComPtr<ID3D11RasterizerState> raster;
    ComPtr<ID3D11BlendState> blend;
    FLOAT blend_factor[4]{};
    UINT sample_mask{0};
    ComPtr<ID3D11DepthStencilState> depth_state;
    UINT stencil_ref{0};
    ComPtr<ID3D11RenderTargetView> rtv;
    ComPtr<ID3D11DepthStencilView> dsv;
    ComPtr<ID3D11InputLayout> layout;
    D3D11_PRIMITIVE_TOPOLOGY topology{};
    ComPtr<ID3D11Buffer> vertex_buffer;
    UINT vertex_stride{0}, vertex_offset{0};
    ComPtr<ID3D11Buffer> index_buffer;
    DXGI_FORMAT index_format{};
    UINT index_offset{0};
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11GeometryShader> gs;
    ComPtr<ID3D11HullShader> hs;
    ComPtr<ID3D11DomainShader> ds;
    ComPtr<ID3D11Buffer> vs_cb[2];
    ComPtr<ID3D11ShaderResourceView> ps_srv;
    ComPtr<ID3D11ShaderResourceView> ps_srv1;
    ComPtr<ID3D11ShaderResourceView> ps_srv2;
    ComPtr<ID3D11SamplerState> ps_sampler1;
    ComPtr<ID3D11Buffer> ps_cb0;
    ComPtr<ID3D11SamplerState> ps_sampler;

    void save(ID3D11DeviceContext* c) {
        c->RSGetViewports(&viewport_count, &viewport);
        c->RSGetState(raster.GetAddressOf());
        c->OMGetBlendState(blend.GetAddressOf(), blend_factor, &sample_mask);
        c->OMGetDepthStencilState(depth_state.GetAddressOf(), &stencil_ref);
        c->OMGetRenderTargets(1, rtv.GetAddressOf(), dsv.GetAddressOf());
        c->IAGetInputLayout(layout.GetAddressOf());
        c->IAGetPrimitiveTopology(&topology);
        c->IAGetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &vertex_stride, &vertex_offset);
        c->IAGetIndexBuffer(index_buffer.GetAddressOf(), &index_format, &index_offset);
        c->VSGetShader(vs.GetAddressOf(), nullptr, nullptr);
        c->PSGetShader(ps.GetAddressOf(), nullptr, nullptr);
        c->GSGetShader(gs.GetAddressOf(), nullptr, nullptr);
        c->HSGetShader(hs.GetAddressOf(), nullptr, nullptr);
        c->DSGetShader(ds.GetAddressOf(), nullptr, nullptr);
        ID3D11Buffer* cbs[2] = {};
        c->VSGetConstantBuffers(0, 2, cbs);
        vs_cb[0].Attach(cbs[0]);
        vs_cb[1].Attach(cbs[1]);
        c->PSGetShaderResources(0, 1, ps_srv.GetAddressOf());
        c->PSGetShaderResources(1, 1, ps_srv1.GetAddressOf());
        c->PSGetShaderResources(2, 1, ps_srv2.GetAddressOf());
        c->PSGetSamplers(1, 1, ps_sampler1.GetAddressOf());
        c->PSGetConstantBuffers(0, 1, ps_cb0.GetAddressOf());
        c->PSGetSamplers(0, 1, ps_sampler.GetAddressOf());
    }

    void restore(ID3D11DeviceContext* c) {
        c->RSSetViewports(viewport_count, &viewport);
        c->RSSetState(raster.Get());
        c->OMSetBlendState(blend.Get(), blend_factor, sample_mask);
        c->OMSetDepthStencilState(depth_state.Get(), stencil_ref);
        ID3D11RenderTargetView* rtvs[1] = {rtv.Get()};
        c->OMSetRenderTargets(1, rtvs, dsv.Get());
        c->IASetInputLayout(layout.Get());
        c->IASetPrimitiveTopology(topology);
        ID3D11Buffer* vb = vertex_buffer.Get();
        c->IASetVertexBuffers(0, 1, &vb, &vertex_stride, &vertex_offset);
        c->IASetIndexBuffer(index_buffer.Get(), index_format, index_offset);
        c->VSSetShader(vs.Get(), nullptr, 0);
        c->PSSetShader(ps.Get(), nullptr, 0);
        c->GSSetShader(gs.Get(), nullptr, 0);
        c->HSSetShader(hs.Get(), nullptr, 0);
        c->DSSetShader(ds.Get(), nullptr, 0);
        ID3D11Buffer* cbs[2] = {vs_cb[0].Get(), vs_cb[1].Get()};
        c->VSSetConstantBuffers(0, 2, cbs);
        ID3D11ShaderResourceView* srv = ps_srv.Get();
        c->PSSetShaderResources(0, 1, &srv);
        ID3D11ShaderResourceView* srv1 = ps_srv1.Get();
        c->PSSetShaderResources(1, 1, &srv1);
        ID3D11ShaderResourceView* srv2 = ps_srv2.Get();
        c->PSSetShaderResources(2, 1, &srv2);
        ID3D11SamplerState* sampler1 = ps_sampler1.Get();
        c->PSSetSamplers(1, 1, &sampler1);
        ID3D11Buffer* cb0 = ps_cb0.Get();
        c->PSSetConstantBuffers(0, 1, &cb0);
        ID3D11SamplerState* sampler = ps_sampler.Get();
        c->PSSetSamplers(0, 1, &sampler);
    }
};

} // namespace

bool RigRenderer::init(ID3D11Device* device, const PartMeshes& meshes) {
    ready_ = false;
    device_ = device;

    ComPtr<ID3DBlob> vs_blob, ps_blob;
    if (!compile("VSMain", "vs_5_0", vs_blob) || !compile("PSMain", "ps_5_0", ps_blob)) return false;
    if (FAILED(device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, vs_.GetAddressOf())) ||
        FAILED(device->CreatePixelShader(ps_blob->GetBufferPointer(), ps_blob->GetBufferSize(), nullptr, ps_.GetAddressOf()))) {
        logLine("CreateShader failed");
        return false;
    }

    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(device->CreateInputLayout(elements, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), layout_.GetAddressOf()))) {
        logLine("CreateInputLayout failed");
        return false;
    }

    // All 12 meshes back to back: 24 vertices / 36 indices each (indices are relative to the part).
    std::vector<mc::rig::RigVertex> vertices;
    std::vector<uint16_t> indices;
    for (size_t i = 0; i < meshes.size(); ++i) {
        const mc::rig::RigMesh& mesh = meshes[i];
        vertices_per_part_ = static_cast<UINT>(mesh.vertices.size());
        indices_per_part_ = static_cast<UINT>(mesh.indices.size());
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    }

    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(mc::rig::RigVertex));
    vb.Usage = D3D11_USAGE_IMMUTABLE;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vd{vertices.data(), 0, 0};
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth = static_cast<UINT>(indices.size() * sizeof(uint16_t));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id{indices.data(), 0, 0};
    if (FAILED(device->CreateBuffer(&vb, &vd, vertices_.GetAddressOf())) || FAILED(device->CreateBuffer(&ib, &id, indices_.GetAddressOf()))) {
        logLine("CreateBuffer (geometry) failed");
        return false;
    }

    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(float) * 16;
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cb, nullptr, frame_cb_.GetAddressOf())) || FAILED(device->CreateBuffer(&cb, nullptr, part_cb_.GetAddressOf()))) {
        logLine("CreateBuffer (constants) failed");
        return false;
    }
    cb.ByteWidth = sizeof(float) * 8;
    if (FAILED(device->CreateBuffer(&cb, nullptr, scene_cb_.GetAddressOf()))) {
        logLine("CreateBuffer (constants) failed");
        return false;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE; // winding is outward-consistent, but the mirrored basis makes culling a trap
    rd.DepthClipEnable = TRUE;
    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    D3D11_DEPTH_STENCIL_DESC dd{};
    dd.DepthEnable = TRUE;
    dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    dd.DepthFunc = D3D11_COMPARISON_LESS;
    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT; // keep the skin pixelated like Minecraft
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC ls{};
    ls.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    ls.AddressU = ls.AddressV = ls.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    ls.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&ls, linear_sampler_.GetAddressOf()))) {
        logLine("CreateSamplerState (linear) failed");
        return false;
    }
    if (FAILED(device->CreateRasterizerState(&rd, raster_.GetAddressOf())) || FAILED(device->CreateBlendState(&bd, blend_.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&dd, depth_state_.GetAddressOf())) || FAILED(device->CreateSamplerState(&sd, sampler_.GetAddressOf()))) {
        logLine("CreateState failed");
        return false;
    }

    // Neutral skin until the real one is supplied
    std::vector<uint8_t> grey(static_cast<size_t>(mc::rig::kSkinSize) * mc::rig::kSkinSize * 4, 190);
    for (size_t i = 3; i < grey.size(); i += 4) grey[i] = 255;
    if (!setSkin(device, grey, mc::rig::kSkinSize, mc::rig::kSkinSize)) return false;

    ready_ = true;
    return true;
}

// Copies the frame so far (before Steve is drawn) into a mip-mapped texture the shader can average.
bool RigRenderer::captureFrame(ID3D11DeviceContext* context, ID3D11RenderTargetView* target) {
    ComPtr<ID3D11Resource> res;
    target->GetResource(res.GetAddressOf());
    ComPtr<ID3D11Texture2D> src;
    if (!res || FAILED(res.As(&src))) return false;
    D3D11_TEXTURE2D_DESC sd{};
    src->GetDesc(&sd);
    D3D11_RENDER_TARGET_VIEW_DESC rd{};
    target->GetDesc(&rd);
    if (sd.SampleDesc.Count != 1 || rd.Format == DXGI_FORMAT_UNKNOWN) return false;
    if (!frame_tex_ || frame_w_ != sd.Width || frame_h_ != sd.Height || frame_fmt_ != rd.Format) {
        frame_srv_.Reset();
        frame_tex_.Reset();
        D3D11_TEXTURE2D_DESC td{};
        td.Width = sd.Width;
        td.Height = sd.Height;
        td.MipLevels = 0; // full chain
        td.ArraySize = 1;
        td.Format = rd.Format;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
        if (FAILED(device_->CreateTexture2D(&td, nullptr, frame_tex_.GetAddressOf())) ||
            FAILED(device_->CreateShaderResourceView(frame_tex_.Get(), nullptr, frame_srv_.GetAddressOf()))) {
            logLine("frame copy texture creation failed; ambient matching off");
            frame_tex_.Reset();
            frame_srv_.Reset();
            return false;
        }
        frame_w_ = sd.Width;
        frame_h_ = sd.Height;
        frame_fmt_ = rd.Format;
    }
    context->CopySubresourceRegion(frame_tex_.Get(), 0, 0, 0, 0, src.Get(), 0, nullptr);
    context->GenerateMips(frame_srv_.Get());
    return true;
}

void RigRenderer::setSceneDepth(ID3D11Texture2D* texture) {
    if (texture == scene_depth_tex_.Get()) return;
    scene_depth_tex_ = texture;
    scene_depth_srv_.Reset();
    if (!texture || !device_) return;
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    // Only the layout measured so far (Sekiro) is understood: 32-bit float depth + 8-bit stencil, SRV-capable.
    if (td.Format != DXGI_FORMAT_R32G8X24_TYPELESS || !(td.BindFlags & D3D11_BIND_SHADER_RESOURCE) || td.SampleDesc.Count != 1) {
        logLine("scene depth texture has an unsupported layout; occlusion off");
        return;
    }
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sv.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(texture, &sv, scene_depth_srv_.GetAddressOf()))) {
        logLine("scene depth SRV creation failed; occlusion off");
        scene_depth_srv_.Reset();
    }
}

bool RigRenderer::setHeldItem(const mc::rig::RigMesh& mesh, ID3D11ShaderResourceView* sprite_sheet) {
    item_vertices_.Reset();
    item_indices_.Reset();
    item_sheet_.Reset();
    item_index_count_ = 0;
    if (!device_ || mesh.vertices.empty() || mesh.indices.empty() || !sprite_sheet || mesh.vertices.size() > 65535) return mesh.vertices.empty();

    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = static_cast<UINT>(mesh.vertices.size() * sizeof(mc::rig::RigVertex));
    vb.Usage = D3D11_USAGE_IMMUTABLE;
    vb.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA vd{mesh.vertices.data(), 0, 0};
    D3D11_BUFFER_DESC ib{};
    ib.ByteWidth = static_cast<UINT>(mesh.indices.size() * sizeof(uint16_t));
    ib.Usage = D3D11_USAGE_IMMUTABLE;
    ib.BindFlags = D3D11_BIND_INDEX_BUFFER;
    D3D11_SUBRESOURCE_DATA id{mesh.indices.data(), 0, 0};
    if (FAILED(device_->CreateBuffer(&vb, &vd, item_vertices_.GetAddressOf())) || FAILED(device_->CreateBuffer(&ib, &id, item_indices_.GetAddressOf()))) {
        logLine("held item buffer creation failed");
        item_vertices_.Reset();
        item_indices_.Reset();
        return false;
    }
    item_sheet_ = sprite_sheet;
    item_index_count_ = static_cast<UINT>(mesh.indices.size());
    return true;
}

bool RigRenderer::setSkin(ID3D11Device* device, const std::vector<uint8_t>& rgba, UINT width, UINT height) {
    if (rgba.size() != static_cast<size_t>(width) * height * 4) return false;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{rgba.data(), width * 4, 0};
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    if (FAILED(device->CreateTexture2D(&td, &data, tex.GetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(tex.Get(), nullptr, srv.GetAddressOf()))) {
        logLine("skin texture creation failed");
        return false;
    }
    skin_ = srv;
    return true;
}

bool RigRenderer::ensureDepth(UINT width, UINT height) {
    if (depth_view_ && depth_width_ == width && depth_height_ == height) return true;
    depth_view_.Reset();
    depth_tex_.Reset();
    D3D11_TEXTURE2D_DESC td{};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (FAILED(device_->CreateTexture2D(&td, nullptr, depth_tex_.GetAddressOf())) ||
        FAILED(device_->CreateDepthStencilView(depth_tex_.Get(), nullptr, depth_view_.GetAddressOf()))) {
        logLine("depth buffer creation failed");
        return false;
    }
    depth_width_ = width;
    depth_height_ = height;
    return true;
}

void RigRenderer::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT width, UINT height,
                         const mc::rig::Mat4& view_projection, const PartMatrices& part_world) {
    if (!ready_ || width == 0 || height == 0 || !ensureDepth(width, height)) return;

    auto upload = [&](ID3D11Buffer* buffer, const mc::rig::Mat4& m) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        std::memcpy(mapped.pData, m.m.data(), sizeof(float) * 16);
        context->Unmap(buffer, 0);
        return true;
    };
    if (!upload(frame_cb_.Get(), view_projection)) return;
    const bool ambient = captureFrame(context, target);
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(scene_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const bool occlude = scene_depth_srv_ && depth_conv_.reverse_z && depth_conv_.depth_times_distance > 0.0f;
            const float params[8] = {depth_conv_.depth_times_distance, depth_conv_.relative_bias, depth_conv_.absolute_bias,
                                     occlude ? 1.0f : 0.0f, probe_u_, probe_v_, ambient ? 1.0f : 0.0f, 0.0f};
            std::memcpy(mapped.pData, params, sizeof(params));
            context->Unmap(scene_cb_.Get(), 0);
        }
    }

    StateBackup backup;
    backup.save(context);

    ID3D11RenderTargetView* rtvs[1] = {target};
    context->OMSetRenderTargets(1, rtvs, depth_view_.Get());
    context->ClearDepthStencilView(depth_view_.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    const D3D11_VIEWPORT vp{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    context->RSSetViewports(1, &vp);
    context->RSSetState(raster_.Get());
    const FLOAT no_factor[4] = {0, 0, 0, 0};
    context->OMSetBlendState(blend_.Get(), no_factor, 0xffffffff);
    context->OMSetDepthStencilState(depth_state_.Get(), 0);

    context->IASetInputLayout(layout_.Get());
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11Buffer* vb = vertices_.Get();
    const UINT stride = sizeof(mc::rig::RigVertex), offset = 0;
    context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    context->IASetIndexBuffer(indices_.Get(), DXGI_FORMAT_R16_UINT, 0);

    context->VSSetShader(vs_.Get(), nullptr, 0);
    context->PSSetShader(ps_.Get(), nullptr, 0);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    ID3D11Buffer* cbs[2] = {frame_cb_.Get(), part_cb_.Get()};
    context->VSSetConstantBuffers(0, 2, cbs);
    ID3D11ShaderResourceView* srv = skin_.Get();
    context->PSSetShaderResources(0, 1, &srv);
    ID3D11ShaderResourceView* scene_srv = scene_depth_srv_.Get();
    context->PSSetShaderResources(1, 1, &scene_srv);
    ID3D11ShaderResourceView* frame_srv = ambient ? frame_srv_.Get() : nullptr;
    context->PSSetShaderResources(2, 1, &frame_srv);
    ID3D11SamplerState* linear = linear_sampler_.Get();
    context->PSSetSamplers(1, 1, &linear);
    ID3D11Buffer* scene_cb = scene_cb_.Get();
    context->PSSetConstantBuffers(0, 1, &scene_cb);
    ID3D11SamplerState* sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    for (size_t i = 0; i < part_world.size(); ++i) {
        if (!upload(part_cb_.Get(), part_world[i])) continue;
        context->DrawIndexed(indices_per_part_, static_cast<UINT>(i) * indices_per_part_, static_cast<INT>(i * vertices_per_part_));
    }

    // The item in the right hand rides on the right arm's matrix.
    if (item_index_count_ > 0 && item_vertices_ && item_indices_ && item_sheet_) {
        if (upload(part_cb_.Get(), part_world[static_cast<size_t>(mc::StevePart::RightArm)])) {
            ID3D11Buffer* item_vb = item_vertices_.Get();
            context->IASetVertexBuffers(0, 1, &item_vb, &stride, &offset);
            context->IASetIndexBuffer(item_indices_.Get(), DXGI_FORMAT_R16_UINT, 0);
            ID3D11ShaderResourceView* sheet = item_sheet_.Get();
            context->PSSetShaderResources(0, 1, &sheet);
            context->DrawIndexed(item_index_count_, 0, 0);
        }
    }

    backup.restore(context);
}

} // namespace mc::d3d11
