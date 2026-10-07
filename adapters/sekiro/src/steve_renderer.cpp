#include "steve_renderer.hpp"

#include <d3dcompiler.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace sekiro::render {

namespace {

using Microsoft::WRL::ComPtr;

constexpr char kShaderSource[] = R"hlsl(
cbuffer Frame : register(b0) { row_major float4x4 view_proj; };
cbuffer Part  : register(b1) { row_major float4x4 world; };
cbuffer Scene : register(b0) { float4 scene; }; // x depth*z constant, y relative bias, z metre bias, w enabled
Texture2D skin : register(t0);
Texture2D<float2> scene_depth : register(t1);
SamplerState point_clamp : register(s0);

struct VSIn  { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(VSIn i) {
    VSOut o;
    o.pos = mul(mul(float4(i.pos, 1.0), world), view_proj);
    o.uv = i.uv;
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
    return float4(c.rgb, 1.0);
}
)hlsl";

void logLine(const char* what) {
    FILE* f = nullptr;
    if (fopen_s(&f, "mc_adapter.log", "a") == 0 && f) {
        fprintf(f, "[mc_adapter] SteveRenderer: %s\n", what);
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
        ID3D11Buffer* cb0 = ps_cb0.Get();
        c->PSSetConstantBuffers(0, 1, &cb0);
        ID3D11SamplerState* sampler = ps_sampler.Get();
        c->PSSetSamplers(0, 1, &sampler);
    }
};

} // namespace

bool SteveRenderer::init(ID3D11Device* device) {
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
    std::vector<SteveVertex> vertices;
    std::vector<uint16_t> indices;
    for (size_t i = 0; i < static_cast<size_t>(mc::StevePart::Count); ++i) {
        const SteveMesh mesh = buildPartMesh(static_cast<mc::StevePart>(i));
        vertices_per_part_ = static_cast<UINT>(mesh.vertices.size());
        indices_per_part_ = static_cast<UINT>(mesh.indices.size());
        vertices.insert(vertices.end(), mesh.vertices.begin(), mesh.vertices.end());
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
    }

    D3D11_BUFFER_DESC vb{};
    vb.ByteWidth = static_cast<UINT>(vertices.size() * sizeof(SteveVertex));
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
    cb.ByteWidth = sizeof(float) * 4;
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
    if (FAILED(device->CreateRasterizerState(&rd, raster_.GetAddressOf())) || FAILED(device->CreateBlendState(&bd, blend_.GetAddressOf())) ||
        FAILED(device->CreateDepthStencilState(&dd, depth_state_.GetAddressOf())) || FAILED(device->CreateSamplerState(&sd, sampler_.GetAddressOf()))) {
        logLine("CreateState failed");
        return false;
    }

    // Neutral skin until the real one is supplied
    std::vector<uint8_t> grey(static_cast<size_t>(kSkinSize) * kSkinSize * 4, 190);
    for (size_t i = 3; i < grey.size(); i += 4) grey[i] = 255;
    if (!setSkin(device, grey, kSkinSize, kSkinSize)) return false;

    ready_ = true;
    return true;
}

void SteveRenderer::setSceneDepth(ID3D11Texture2D* texture) {
    if (texture == scene_depth_tex_.Get()) return;
    scene_depth_tex_ = texture;
    scene_depth_srv_.Reset();
    if (!texture || !device_) return;
    D3D11_TEXTURE2D_DESC td{};
    texture->GetDesc(&td);
    // Only the layout measured on Sekiro is understood: 32-bit float depth + 8-bit stencil, SRV-capable.
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

bool SteveRenderer::setSkin(ID3D11Device* device, const std::vector<uint8_t>& rgba, UINT width, UINT height) {
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

bool SteveRenderer::ensureDepth(UINT width, UINT height) {
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

void SteveRenderer::draw(ID3D11DeviceContext* context, ID3D11RenderTargetView* target, UINT width, UINT height,
                         const Mat4& view_projection, const std::array<Mat4, static_cast<size_t>(mc::StevePart::Count)>& part_world) {
    if (!ready_ || width == 0 || height == 0 || !ensureDepth(width, height)) return;

    auto upload = [&](ID3D11Buffer* buffer, const Mat4& m) {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return false;
        std::memcpy(mapped.pData, m.m.data(), sizeof(float) * 16);
        context->Unmap(buffer, 0);
        return true;
    };
    if (!upload(frame_cb_.Get(), view_projection)) return;
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (SUCCEEDED(context->Map(scene_cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            const float params[4] = {kSceneDepthNear, kSceneOcclusionRelativeBias, kSceneOcclusionBiasMetres,
                                     scene_depth_srv_ ? 1.0f : 0.0f};
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
    const UINT stride = sizeof(SteveVertex), offset = 0;
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
    ID3D11Buffer* scene_cb = scene_cb_.Get();
    context->PSSetConstantBuffers(0, 1, &scene_cb);
    ID3D11SamplerState* sampler = sampler_.Get();
    context->PSSetSamplers(0, 1, &sampler);

    for (size_t i = 0; i < part_world.size(); ++i) {
        if (!upload(part_cb_.Get(), part_world[i])) continue;
        context->DrawIndexed(indices_per_part_, static_cast<UINT>(i) * indices_per_part_, static_cast<INT>(i * vertices_per_part_));
    }

    backup.restore(context);
}

} // namespace sekiro::render
