#include "overlay_internal.hpp"

namespace erov {
namespace detail {

#if ENABLE_CS2_VIEWMODEL
// The authentic CS2 concrete bullet-hole decal, extracted from the user's own VPK by
// tools/cs2/impact_decals.py and deployed next to the CS textures. Slot 44 is free.
void CreateCsDecalTexture(UINT srv_inc) {
    g_decal_ready = false;
    g_decal_rgba.clear();
    g_decal_w = g_decal_h = 0;
    const std::string& game_dir = eldenring::diagnostics::GetGameDir();
    std::vector<std::string> candidates;
    if (!game_dir.empty()) {
        candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "cs" /
                              "bullet_hole_concrete.png").string());
        candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bullet_hole_concrete.png").string());
    }
    candidates.push_back("mods\\mc_adapter\\textures\\cs\\bullet_hole_concrete.png");
    candidates.push_back("mods\\mc_adapter\\bullet_hole_concrete.png");
    std::string used;
    for (const auto& p : candidates) {
        if (!std::filesystem::exists(p)) continue;
        if (DecodePngFile(p, g_decal_rgba, g_decal_w, g_decal_h)) {
            used = p;
            break;
        }
    }
    if (g_decal_rgba.empty() || g_decal_w == 0 || g_decal_h == 0) {
        Logf("overlay: CS decal: no bullet_hole_concrete.png, procedural fallback");
        return;
    }
    D3D12_HEAP_PROPERTIES hd{};
    hd.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = g_decal_w;
    td.Height = g_decal_h;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    if (FAILED(g_s.device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST,
                                                   nullptr, IID_PPV_ARGS(&g_decal_tex)))) {
        Logf("overlay: CS decal: texture creation failed");
        return;
    }
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    g_s.device->GetCopyableFootprints(&td, 0, 1, 0, &g_decal_footprint, &rows, &row_bytes, &total);
    (void)row_bytes;
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
    if (FAILED(g_s.device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&g_decal_upload))) ||
        FAILED(g_decal_upload->Map(0, &none, &mapped))) {
        SafeRelease(g_decal_tex);
        SafeRelease(g_decal_upload);
        Logf("overlay: CS decal: upload buffer failed");
        return;
    }
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped) + g_decal_footprint.Offset +
                        static_cast<size_t>(y) * g_decal_footprint.Footprint.RowPitch,
                    g_decal_rgba.data() + static_cast<size_t>(y) * g_decal_w * 4,
                    static_cast<size_t>(g_decal_w) * 4);
    }
    g_decal_upload->Unmap(0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    cpu.ptr = static_cast<SIZE_T>(srv_handle(g_s.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr, srv_inc, 44));
    g_decal_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
    g_decal_gpu.ptr = srv_handle(g_decal_gpu.ptr, srv_inc, 44);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    g_s.device->CreateShaderResourceView(g_decal_tex, &sd, cpu);
    g_decal_pending = true;
    g_decal_ready = true;
    Logf("overlay: CS decal: %ux%u from %s (slot 44)", g_decal_w, g_decal_h, used.c_str());
}
#endif

void CreateAtlasTexture(UINT srv_inc) {
    g_atlas_ready = false;
    if (g_atlas_rgba.empty() || g_atlas_w == 0 || g_atlas_h == 0) {
        Logf("overlay: HUD atlas: none given, plain rectangles");
        return;
    }
    const std::vector<uint8_t> big = eldenring::render::upscaleNearest(g_atlas_rgba.data(), g_atlas_w, g_atlas_h, kAtlasScale);
    const unsigned w = g_atlas_w * kAtlasScale, h = g_atlas_h * kAtlasScale;
    D3D12_HEAP_PROPERTIES hd{};
    hd.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td{};
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = w;
    td.Height = h;
    td.DepthOrArraySize = 1;
    td.MipLevels = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    if (FAILED(g_s.device->CreateCommittedResource(&hd, D3D12_HEAP_FLAG_NONE, &td, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                   IID_PPV_ARGS(&g_atlas_tex)))) {
        Logf("overlay: HUD atlas: texture creation failed");
        return;
    }
    UINT rows = 0;
    UINT64 row_bytes = 0, total = 0;
    g_s.device->GetCopyableFootprints(&td, 0, 1, 0, &g_atlas_footprint, &rows, &row_bytes, &total);
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
    if (FAILED(g_s.device->CreateCommittedResource(&hu, D3D12_HEAP_FLAG_NONE, &ud, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                   IID_PPV_ARGS(&g_atlas_upload))) ||
        FAILED(g_atlas_upload->Map(0, &none, &mapped))) {
        SafeRelease(g_atlas_tex);
        SafeRelease(g_atlas_upload);
        Logf("overlay: HUD atlas: upload buffer failed");
        return;
    }
    for (UINT y = 0; y < rows; ++y) {
        std::memcpy(static_cast<uint8_t*>(mapped) + g_atlas_footprint.Offset + static_cast<size_t>(y) * g_atlas_footprint.Footprint.RowPitch,
                    big.data() + static_cast<size_t>(y) * w * 4, static_cast<size_t>(w) * 4);
    }
    g_atlas_upload->Unmap(0, nullptr);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    cpu.ptr = static_cast<SIZE_T>(srv_handle(g_s.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr, srv_inc, 3));
    g_atlas_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
    g_atlas_gpu.ptr = srv_handle(g_atlas_gpu.ptr, srv_inc, 3);
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, cpu);
    // Held items sample the atlas through their own table: slot 4 (unused depth) + slot 5 (the atlas again).
    {
        D3D12_CPU_DESCRIPTOR_HANDLE c4{};
        c4.ptr = static_cast<SIZE_T>(srv_handle(g_s.srv_heap->GetCPUDescriptorHandleForHeapStart().ptr, srv_inc, 4));
        D3D12_CPU_DESCRIPTOR_HANDLE c5{};
        c5.ptr = static_cast<SIZE_T>(srv_handle(c4.ptr, srv_inc, 1));
        g_steve.setDepthView(g_s.device, nullptr, c4);
        g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, c5);
        g_item_table_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_item_table_gpu.ptr = srv_handle(g_item_table_gpu.ptr, srv_inc, 4);
        // The third-person held item: slot 6 follows the scene depth (see RenderFrame), slot 7 is the atlas.
        g_depth_copy_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        g_depth_copy_cpu.ptr = static_cast<SIZE_T>(srv_handle(g_depth_copy_cpu.ptr, srv_inc, 6));
        D3D12_CPU_DESCRIPTOR_HANDLE c7{};
        c7.ptr = static_cast<SIZE_T>(srv_handle(g_depth_copy_cpu.ptr, srv_inc, 1));
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy_cpu);
        g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, c7);
        g_held_table_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_held_table_gpu.ptr = srv_handle(g_held_table_gpu.ptr, srv_inc, 6);
    }
    g_atlas_pending = true;
    g_atlas_ready = true;
    char msg[96];
    snprintf(msg, sizeof(msg), "overlay: HUD atlas: %ux%u from file, uploaded as %ux%u", g_atlas_w, g_atlas_h, w, h);
    Logf(msg);
}

// Recorded at the start of the frame's command list, before anything samples the atlas.
void RecordAtlasUpload(ID3D12GraphicsCommandList* list) {
    if (g_atlas_pending && g_atlas_tex && g_atlas_upload) {
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = g_atlas_tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = g_atlas_upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = g_atlas_footprint;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_atlas_tex;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &b);
        g_atlas_pending = false;
    }
#if ENABLE_CS2_VIEWMODEL
    if (g_decal_pending && g_decal_tex && g_decal_upload) {
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = g_decal_tex;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = g_decal_upload;
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint = g_decal_footprint;
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = g_decal_tex;
        b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        b.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &b);
        g_decal_pending = false;
    }
#endif
}

} // namespace detail
} // namespace erov
