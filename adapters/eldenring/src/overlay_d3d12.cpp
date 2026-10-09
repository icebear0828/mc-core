#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>

#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_dx12.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>

#include <mutex>
#include <vector>

#include "eldenring_fp.hpp"
#include "eldenring_hudtex.hpp"
#include "eldenring_particles.hpp"
#include "eldenring_blocks.hpp"
#include "mc/hud_layout.hpp"
#include "mc/inventory_layout.hpp"
#include "mc/hud.hpp"
#include "overlay_d3d12.hpp"
#include "steve_renderer_d3d12.hpp"

namespace erov {
namespace {

constexpr UINT kMaxFrames = 8;
constexpr UINT kVtblPresent = 8;
constexpr UINT kVtblResizeBuffers = 13;
constexpr UINT kVtblExecuteCommandLists = 10;
constexpr UINT kVtblDeviceCreateDsv = 21;

HudProvider g_provider = nullptr;
LogFn g_log = nullptr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
PresentFn g_present_orig = nullptr;
ResizeFn g_resize_orig = nullptr;
ExecuteFn g_execute_orig = nullptr;
using CreateDsvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
CreateDsvFn g_create_dsv_orig = nullptr;

SteveConfig g_steve_cfg;
// The HUD atlas, enlarged 4x with nearest neighbour (ImGui's DX12 backend samples linearly), in heap slot 3.
std::vector<uint8_t> g_atlas_rgba;
unsigned g_atlas_w = 0, g_atlas_h = 0;
constexpr unsigned kAtlasScale = 4;
ID3D12Resource* g_atlas_tex = nullptr;
ID3D12Resource* g_atlas_upload = nullptr;
D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_atlas_footprint{};
bool g_atlas_pending = false;
D3D12_GPU_DESCRIPTOR_HANDLE g_atlas_gpu{};
bool g_atlas_ready = false;
void CreateAtlasTexture(UINT srv_inc); // defined with the HUD drawing below
// Combat particles: events queued by the loader, simulated and drawn here.
struct FxEvent {
    FxKind kind;
    float pos[3];
    int count;
};
std::mutex g_fx_mutex;
std::vector<FxEvent> g_fx_events;
eldenring::fx::ParticleSystem g_particles;
eldenring::fx::Rng g_fx_rng{20251009u};
mc::rig::Mat4 g_fx_vp{};
float g_fx_fov = 0.8378f;
bool g_fx_valid = false;
eldenring::fp::HandAnimator g_hand;
eldenring::fp::SwayFilter g_sway;
float g_walk_dist = 0.f, g_walk_bob = 0.f;
D3D12_GPU_DESCRIPTOR_HANDLE g_item_table_gpu{}; // slots 4 and 5
bool g_fp_built = false;
std::atomic<int> g_trace_left{0}; // F12: frames of per-frame figure/camera positions still to write to the log
std::vector<uint8_t> g_skin_rgba; // decoded by the loader; applied when the renderer is created
unsigned g_skin_w = 0, g_skin_h = 0;
SteveRenderer g_steve;
mc::SteveAnimator g_anim;
eldenring::render::SteveMotion g_motion;
float g_death_seconds = 0.f; // how long the figure has been dying (0 = alive)
std::mutex g_depth_mutex;
ID3D12Resource* g_depth_res = nullptr; // AddRef'd scene depth in use (R32G8X24_TYPELESS), guarded by g_depth_mutex
std::vector<ID3D12Resource*> g_depth_candidates; // AddRef'd, oldest first, guarded by g_depth_mutex
UINT g_depth_w = 0, g_depth_h = 0;
std::atomic<bool> g_depth_dirty{false};
D3D12_CPU_DESCRIPTOR_HANDLE g_depth_cpu{};
D3D12_GPU_DESCRIPTOR_HANDLE g_depth_gpu{};
std::mutex g_block_mutex;
mc::rig::RigMesh g_block_mesh;
std::atomic<bool> g_blocks_dirty{false};
D3D12_CPU_DESCRIPTOR_HANDLE g_depth_copy_cpu{}; // slot 6: the same depth view again, in front of the atlas (held item table)
D3D12_GPU_DESCRIPTOR_HANDLE g_held_table_gpu{};

// The DIRECT queue the game submits on. Written by any thread inside ExecuteCommandLists.
std::atomic<ID3D12CommandQueue*> g_queue{nullptr};

struct Frame {
    ID3D12CommandAllocator* alloc{nullptr};
    ID3D12Resource* back{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fence_value{0};
};

struct State {
    bool ready{false};
    bool failed{false};
    IDXGISwapChain3* swap{nullptr};
    ID3D12Device* device{nullptr};
    ID3D12CommandQueue* queue{nullptr};
    ID3D12DescriptorHeap* rtv_heap{nullptr};
    ID3D12DescriptorHeap* srv_heap{nullptr};
    ID3D12GraphicsCommandList* list{nullptr};
    ID3D12Fence* fence{nullptr};
    HANDLE fence_event{nullptr};
    UINT64 fence_counter{0};
    UINT buffers{0};
    UINT width{0}, height{0};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    Frame frames[kMaxFrames];
    bool imgui_ready{false};
    std::chrono::steady_clock::time_point last_frame{};
};
State g_s;

template <typename T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

void Logf(const char* msg) {
    if (g_log) g_log("%s", msg);
}

void WaitForGpu() {
    if (!g_s.queue || !g_s.fence || !g_s.fence_event) return;
    const UINT64 v = ++g_s.fence_counter;
    if (SUCCEEDED(g_s.queue->Signal(g_s.fence, v)) && g_s.fence->GetCompletedValue() < v) {
        g_s.fence->SetEventOnCompletion(v, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 2000);
    }
}

void Teardown() {
    if (g_s.ready) WaitForGpu();
    g_steve.release();
    if (g_s.imgui_ready) {
        ImGui_ImplDX12_Shutdown();
        ImGui::DestroyContext();
        g_s.imgui_ready = false;
    }
    for (Frame& f : g_s.frames) {
        SafeRelease(f.alloc);
        SafeRelease(f.back);
        f.fence_value = 0;
    }
    SafeRelease(g_s.list);
    SafeRelease(g_s.rtv_heap);
    SafeRelease(g_s.srv_heap);
    SafeRelease(g_atlas_tex);
    SafeRelease(g_atlas_upload);
    g_atlas_ready = false;
    SafeRelease(g_s.fence);
    if (g_s.fence_event) {
        CloseHandle(g_s.fence_event);
        g_s.fence_event = nullptr;
    }
    SafeRelease(g_s.device);
    SafeRelease(g_s.swap);
    g_s.queue = nullptr; // not owned
    g_s.ready = false;
}

// Looks for the queue pointer inside the swap chain object, to confirm the queue we learned from ExecuteCommandLists
// is the one DXGI presents with (the layout is dxgi.dll internal: only used for the log, never for the decision).
int FindQueueOffsetInSwapChain(IDXGISwapChain* sc, ID3D12CommandQueue* q) {
    const auto* words = reinterpret_cast<const uintptr_t*>(sc);
    for (int i = 0; i < 0x100; ++i) {
        uintptr_t v = 0;
        __try {
            v = words[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
        if (v == reinterpret_cast<uintptr_t>(q)) return i * static_cast<int>(sizeof(uintptr_t));
    }
    return -1;
}

bool Init(IDXGISwapChain* sc, ID3D12CommandQueue* queue) {
    IDXGISwapChain3* sc3 = nullptr;
    if (FAILED(sc->QueryInterface(IID_PPV_ARGS(&sc3)))) {
        Logf("overlay: swap chain is not IDXGISwapChain3");
        return false;
    }
    g_s.swap = sc3;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(sc3->GetDesc(&desc))) return false;
    g_s.buffers = std::min<UINT>(desc.BufferCount, kMaxFrames);
    g_s.width = desc.BufferDesc.Width;
    g_s.height = desc.BufferDesc.Height;
    g_s.format = desc.BufferDesc.Format;
    if (FAILED(sc3->GetDevice(IID_PPV_ARGS(&g_s.device)))) return false;
    // The queue must belong to the same device as the swap chain.
    ID3D12Device* qdev = nullptr;
    if (FAILED(queue->GetDevice(IID_PPV_ARGS(&qdev))) || qdev != g_s.device) {
        SafeRelease(qdev);
        Logf("overlay: captured queue belongs to another device, waiting for another one");
        SafeRelease(g_s.device);
        SafeRelease(g_s.swap);
        return false;
    }
    SafeRelease(qdev);
    g_s.queue = queue;

    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtv_desc.NumDescriptors = g_s.buffers;
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};
    srv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_desc.NumDescriptors = 8; // 0: ImGui font, 1: scene depth, 2: Steve skin (1 and 2 form one table), 3: HUD atlas, 4: (unused depth), 5: atlas again (4 and 5 form the table for first-person items), 6: scene depth copy, 7: atlas again (6 and 7 form the table for the third-person held item)
    srv_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(g_s.device->CreateDescriptorHeap(&rtv_desc, IID_PPV_ARGS(&g_s.rtv_heap))) ||
        FAILED(g_s.device->CreateDescriptorHeap(&srv_desc, IID_PPV_ARGS(&g_s.srv_heap)))) {
        Logf("overlay: descriptor heaps failed");
        return false;
    }
    const UINT inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    D3D12_CPU_DESCRIPTOR_HANDLE h = g_s.rtv_heap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < g_s.buffers; ++i) {
        Frame& f = g_s.frames[i];
        if (FAILED(g_s.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.alloc))) ||
            FAILED(sc3->GetBuffer(i, IID_PPV_ARGS(&f.back)))) {
            Logf("overlay: per-frame resources failed");
            return false;
        }
        f.rtv = h;
        D3D12_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = g_s.format;
        rd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
        g_s.device->CreateRenderTargetView(f.back, &rd, f.rtv);
        h.ptr += inc;
    }
    if (FAILED(g_s.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_s.frames[0].alloc, nullptr,
                                             IID_PPV_ARGS(&g_s.list)))) {
        Logf("overlay: command list failed");
        return false;
    }
    g_s.list->Close();
    if (FAILED(g_s.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_s.fence)))) return false;
    g_s.fence_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!g_s.fence_event) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplDX12_Init(g_s.device, static_cast<int>(g_s.buffers), g_s.format, g_s.srv_heap,
                             g_s.srv_heap->GetCPUDescriptorHandleForHeapStart(), g_s.srv_heap->GetGPUDescriptorHandleForHeapStart())) {
        ImGui::DestroyContext();
        Logf("overlay: ImGui_ImplDX12_Init failed");
        return false;
    }
    g_s.imgui_ready = true;
    {
        const UINT srv_inc = g_s.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        g_depth_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        g_depth_cpu.ptr += srv_inc;
        g_depth_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_depth_gpu.ptr += srv_inc;
        g_steve.setDepthView(g_s.device, nullptr, g_depth_cpu);
        if (!g_steve.init(g_s.device, g_s.format, g_log)) {
            Logf("overlay: Steve renderer unavailable");
        } else {
            D3D12_CPU_DESCRIPTOR_HANDLE skin_cpu = g_depth_cpu;
            skin_cpu.ptr += srv_inc;
            // The skin the loader decoded from mods\mc_adapter\steve.png, or the flat grey-brown that was used before the skin existed.
            std::vector<uint8_t> flat;
            const std::vector<uint8_t>* pixels = &g_skin_rgba;
            unsigned sw = g_skin_w, sh = g_skin_h;
            if (pixels->empty() || sw == 0 || sh == 0) {
                sw = sh = 64;
                flat.resize(static_cast<size_t>(sw) * sh * 4);
                for (size_t k = 0; k < flat.size(); k += 4) {
                    flat[k] = 199;
                    flat[k + 1] = 153;
                    flat[k + 2] = 117;
                    flat[k + 3] = 255;
                }
                pixels = &flat;
                Logf("overlay: Steve skin: none given, flat colour");
            } else {
                char skin_msg[96];
                snprintf(skin_msg, sizeof(skin_msg), "overlay: Steve skin: %ux%u from file", sw, sh);
                Logf(skin_msg);
            }
            if (!g_steve.setSkin(g_s.device, pixels->data(), sw, sh, skin_cpu)) Logf("overlay: Steve skin upload failed");
        }
        g_depth_dirty.store(true); // bind the depth captured so far
        CreateAtlasTexture(srv_inc);
        {
            // The first-person arm and the sprites of the default hotbar items.
            std::vector<SteveRenderer::FpItemCell> cells;
            const float aw = static_cast<float>(g_atlas_w), ah = static_cast<float>(g_atlas_h);
            for (mc::ItemId id : mc::paletteItems()) {
                if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(id)) {
                    cells.push_back({static_cast<uint16_t>(id), static_cast<int>(std::lround(uv->u0 * aw)), static_cast<int>(std::lround(uv->v0 * ah)),
                                     static_cast<int>(std::lround((uv->u1 - uv->u0) * aw)), static_cast<int>(std::lround((uv->v1 - uv->v0) * ah))});
                }
            }
            g_fp_built = g_steve.initFirstPerson(g_s.device, g_atlas_rgba.empty() ? nullptr : g_atlas_rgba.data(), g_atlas_w, g_atlas_h, cells);
            Logf(g_fp_built ? "overlay: first-person view model ready" : "overlay: first-person view model failed");
            Logf(g_steve.initHeldItems(g_s.device, g_atlas_rgba.empty() ? nullptr : g_atlas_rgba.data(), g_atlas_w, g_atlas_h, cells)
                     ? "overlay: third-person held items ready"
                     : "overlay: third-person held items failed");
        }
    }
    g_s.last_frame = std::chrono::steady_clock::now();
    g_s.ready = true;
    char msg[256];
    snprintf(msg, sizeof(msg), "overlay: ready (buffers=%u, %ux%u, format=%d, queue=%p, queue found in swap chain at +0x%X)", g_s.buffers,
             g_s.width, g_s.height, static_cast<int>(g_s.format), static_cast<void*>(queue),
             static_cast<unsigned>(std::max(0, FindQueueOffsetInSwapChain(sc, queue))));
    Logf(msg);
    return true;
}

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
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srv_inc) * 3;
    g_atlas_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
    g_atlas_gpu.ptr += static_cast<UINT64>(srv_inc) * 3;
    D3D12_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sd.Texture2D.MipLevels = 1;
    g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, cpu);
    // Held items sample the atlas through their own table: slot 4 (unused depth) + slot 5 (the atlas again).
    {
        D3D12_CPU_DESCRIPTOR_HANDLE c4 = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        c4.ptr += static_cast<SIZE_T>(srv_inc) * 4;
        D3D12_CPU_DESCRIPTOR_HANDLE c5 = c4;
        c5.ptr += srv_inc;
        g_steve.setDepthView(g_s.device, nullptr, c4);
        g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, c5);
        g_item_table_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_item_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 4;
        // The third-person held item: slot 6 follows the scene depth (see RenderFrame), slot 7 is the atlas.
        g_depth_copy_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
        g_depth_copy_cpu.ptr += static_cast<SIZE_T>(srv_inc) * 6;
        D3D12_CPU_DESCRIPTOR_HANDLE c7 = g_depth_copy_cpu;
        c7.ptr += srv_inc;
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy_cpu);
        g_s.device->CreateShaderResourceView(g_atlas_tex, &sd, c7);
        g_held_table_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
        g_held_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 6;
    }
    g_atlas_pending = true;
    g_atlas_ready = true;
    char msg[96];
    snprintf(msg, sizeof(msg), "overlay: HUD atlas: %ux%u from file, uploaded as %ux%u", g_atlas_w, g_atlas_h, w, h);
    Logf(msg);
}

// Recorded at the start of the frame's command list, before anything samples the atlas.
void RecordAtlasUpload(ID3D12GraphicsCommandList* list) {
    if (!g_atlas_pending || !g_atlas_tex || !g_atlas_upload) return;
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

// Clip-space position of a world point (row vector times the matrix); false when it is behind the camera.
bool ProjectToScreen(const mc::rig::Mat4& m, const float p[3], float w, float h, float fov_y, ImVec2& out, float& px_per_m) {
    const auto& a = m.m;
    const float cx = p[0] * a[0] + p[1] * a[4] + p[2] * a[8] + a[12];
    const float cy = p[0] * a[1] + p[1] * a[5] + p[2] * a[9] + a[13];
    const float cw = p[0] * a[3] + p[1] * a[7] + p[2] * a[11] + a[15];
    if (!(cw > 0.05f)) return false;
    out = {(cx / cw * 0.5f + 0.5f) * w, (0.5f - cy / cw * 0.5f) * h};
    px_per_m = (h * 0.5f) / (std::tan(fov_y * 0.5f) * cw);
    return true;
}

void DrawFx(ImDrawList* dl, ImTextureID atlas, float w, float h) {
    if (!g_fx_valid || !g_atlas_ready) return;
    for (const eldenring::fx::Particle& p : g_particles.alive()) {
        ImVec2 s;
        float ppm = 0.f;
        if (!ProjectToScreen(g_fx_vp, p.pos, w, h, g_fx_fov, s, ppm)) continue;
        const float half = std::max(2.f, p.size * ppm * 0.5f);
        const float fade = eldenring::fx::ParticleSystem::fade(p);
        const mc::hud::HudUV* uv = nullptr;
        ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(255 * std::min(1.f, fade * 1.5f)));
        switch (p.kind) {
            case eldenring::fx::Kind::Crit: uv = &mc::hud::kUV_PARTICLE_CRIT; break;
            case eldenring::fx::Kind::Damage: uv = &mc::hud::kUV_PARTICLE_DAMAGE; break;
            case eldenring::fx::Kind::Sweep: {
                static const mc::hud::HudUV* const frames[8] = {&mc::hud::kUV_PARTICLE_SWEEP_0, &mc::hud::kUV_PARTICLE_SWEEP_1, &mc::hud::kUV_PARTICLE_SWEEP_2,
                                                                  &mc::hud::kUV_PARTICLE_SWEEP_3, &mc::hud::kUV_PARTICLE_SWEEP_4, &mc::hud::kUV_PARTICLE_SWEEP_5,
                                                                  &mc::hud::kUV_PARTICLE_SWEEP_6, &mc::hud::kUV_PARTICLE_SWEEP_7};
                uv = frames[std::clamp(p.frame, 0, 7)];
                col = IM_COL32(255, 255, 255, 255);
                break;
            }
        }
        dl->AddImage(atlas, {s.x - half, s.y - half}, {s.x + half, s.y + half}, {uv->u0, uv->v0}, {uv->u1, uv->v1}, col);
    }
}

// The inventory screen: Minecraft's 3-row container (item palette on top, the player's slots below), the stack on the cursor,
// a tooltip and our own mouse pointer.
void DrawInventory(const HudState& hud, float w, float h) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImTextureID atlas = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_atlas_gpu.ptr));
    dl->AddRectFilled({0.f, 0.f}, {w, h}, IM_COL32(16, 16, 16, 150)); // Minecraft dims the world behind a screen
    const mc::InventoryLayout lay(w, h);
    const mc::HudRect p = lay.panel();
    const float gs = static_cast<float>(lay.scale());
    auto image = [&](float x, float y, float rw, float rh, const mc::hud::HudUV& uv) {
        dl->AddImage(atlas, {x, y}, {x + rw, y + rh}, {uv.u0, uv.v0}, {uv.u1, uv.v1});
    };
    if (g_atlas_ready) {
        image(p.x, p.y, 176.f * gs, 71.f * gs, mc::hud::kUV_CONTAINER_TOP);
        image(p.x, p.y + 71.f * gs, 176.f * gs, 96.f * gs, mc::hud::kUV_CONTAINER_BOTTOM);
    } else {
        dl->AddRectFilled({p.x, p.y}, {p.x + p.w, p.y + p.h}, IM_COL32(198, 198, 198, 255));
    }
    const float fs = 8.f * gs;
    const ImU32 label = IM_COL32(64, 64, 64, 255);
    dl->AddText(ImGui::GetFont(), fs, {lay.titleItems().x, lay.titleItems().y}, label, "Items");
    dl->AddText(ImGui::GetFont(), fs, {lay.titleInventory().x, lay.titleInventory().y}, label, "Inventory");

    auto stackCount = [&](const mc::HudRect& r, unsigned n) {
        if (n <= 1) return;
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%u", n);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
        const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
        dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
        dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
    };
    auto icon = [&](const mc::HudRect& r, mc::ItemId item) {
        if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(item)) image(r.x, r.y, r.w, r.h, *uv);
    };
    const auto& palette = mc::paletteItems();
    for (size_t i = 0; i < palette.size() && i < mc::InventoryLayout::kPaletteSlots; ++i) icon(lay.paletteSlot(static_cast<int>(i)), palette[i]);
    for (int i = 0; i < 36; ++i) {
        const mc::ItemId item = static_cast<mc::ItemId>(hud.inv_item[i]);
        if (item == mc::ItemId::None) continue;
        icon(lay.invSlot(i), item);
        stackCount(lay.invSlot(i), hud.inv_count[i]);
    }
    // hover: the vanilla slot highlight and the item name
    const mc::SlotRef hover = lay.hitTest(hud.mouse_x, hud.mouse_y);
    mc::ItemId hovered = mc::ItemId::None;
    if (hover.kind == mc::SlotRef::Kind::Palette && static_cast<size_t>(hover.index) < palette.size()) {
        hovered = palette[static_cast<size_t>(hover.index)];
    } else if (hover.kind == mc::SlotRef::Kind::Inventory) {
        hovered = static_cast<mc::ItemId>(hud.inv_item[hover.index]);
    }
    if (hover.kind == mc::SlotRef::Kind::Palette || hover.kind == mc::SlotRef::Kind::Inventory) {
        const mc::HudRect r = hover.kind == mc::SlotRef::Kind::Palette ? lay.paletteSlot(hover.index) : lay.invSlot(hover.index);
        dl->AddRectFilled({r.x, r.y}, {r.x + r.w, r.y + r.h}, IM_COL32(255, 255, 255, 128));
    }
    // the stack on the cursor is drawn centred on the pointer, above everything
    const mc::ItemId held = static_cast<mc::ItemId>(hud.cursor_item);
    if (held != mc::ItemId::None) {
        const mc::HudRect r{hud.mouse_x - 8.f * gs, hud.mouse_y - 8.f * gs, 16.f * gs, 16.f * gs};
        icon(r, held);
        stackCount(r, hud.cursor_count);
    } else if (hovered != mc::ItemId::None) {
        const char* name = mc::HudEngine::getItemDisplayName(hovered);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, name);
        const float pad = 3.f * gs;
        const float tx = std::min(hud.mouse_x + 12.f * gs, w - sz.x - 2.f * pad), ty = hud.mouse_y - 12.f * gs;
        dl->AddRectFilled({tx - pad, ty - pad}, {tx + sz.x + pad, ty + sz.y + pad}, IM_COL32(16, 0, 16, 230));
        dl->AddRect({tx - pad, ty - pad}, {tx + sz.x + pad, ty + sz.y + pad}, IM_COL32(80, 0, 160, 255), 0.f, 0, gs);
        dl->AddText(ImGui::GetFont(), fs, {tx, ty}, IM_COL32(255, 255, 255, 255), name);
    }
    // the pointer (the game's own cursor is not shown while the mouse is captured)
    const float u = std::max(1.f, gs);
    const ImVec2 m{hud.mouse_x, hud.mouse_y};
    dl->AddTriangleFilled(m, {m.x, m.y + 11.f * u}, {m.x + 7.f * u, m.y + 8.f * u}, IM_COL32(255, 255, 255, 255));
    dl->AddTriangle(m, {m.x, m.y + 11.f * u}, {m.x + 7.f * u, m.y + 8.f * u}, IM_COL32(0, 0, 0, 255), 1.f);
}

void DrawHud(const HudState& hud, float w, float h) {
    if (hud.inv_open) {
        DrawInventory(hud, w, h);
        return;
    }
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const float scale = std::max(0.5f, h / 1080.f);
    const float cell = 20.f * scale;
    const float gap = 2.f * scale;
    const float slot = 44.f * scale;
    const float hotbar_w = 9.f * slot;
    const float x0 = (w - hotbar_w) * 0.5f;
    const float y_bar = h - slot - 8.f * scale;
    const mc::HudLayout layout(w, h);
    const ImTextureID atlas = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_atlas_gpu.ptr));
    auto sprite = [&](const mc::HudRect& r, const mc::hud::HudUV& uv) {
        dl->AddImage(atlas, {r.x, r.y}, {r.x + r.w, r.y + r.h}, {uv.u0, uv.v0}, {uv.u1, uv.v1});
    };
    const float ratio_hp = hud.max_hp > 0 ? std::clamp(static_cast<float>(hud.hp) / static_cast<float>(hud.max_hp), 0.f, 1.f) : 0.f;
    const int hp_halves = static_cast<int>(std::ceil(ratio_hp * 20.f));
    float text_x = x0, text_y = y_bar - cell - 6.f * scale - 18.f * scale;
    if (g_atlas_ready) {
        // Real Minecraft sprites at vanilla positions (mc::HudLayout), the Minecraft GUI scale of this resolution.
        sprite(layout.hotbar(), mc::hud::kUV_HOTBAR);
        for (int i = 0; i < 9; ++i) {
            if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(static_cast<mc::ItemId>(hud.hotbar[i]))) sprite(layout.item(i), *uv);
        }
        sprite(layout.selection(std::clamp(hud.selected_slot, 0, 8)), mc::hud::kUV_HOTBAR_SELECTION);
        for (int i = 0; i < 10; ++i) {
            sprite(layout.heart(i), mc::hud::kUV_HEART_CONTAINER);
            if (hp_halves >= 2 * (i + 1)) {
                sprite(layout.heart(i), mc::hud::kUV_HEART_FULL);
            } else if (hp_halves == 2 * i + 1) {
                sprite(layout.heart(i), mc::hud::kUV_HEART_HALF);
            }
        }
        // stack sizes (Minecraft: bottom right of the icon, white with a dark shadow, only above 1)
        for (int i = 0; i < 9; ++i) {
            if (hud.hotbar_count[i] <= 1 || hud.hotbar[i] == 0) continue;
            char cnt[8];
            snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(hud.hotbar_count[i]));
            const float gs = static_cast<float>(layout.scale());
            const float fs = layout.textSize();
            const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
            const mc::HudRect r = layout.item(i);
            const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
            dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
            dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
        }
        // absorption (golden apple, totem): golden hearts in a row above the health, 2 Minecraft HP per heart
        {
            const int full = static_cast<int>(std::floor(hud.absorption_mc / 2.f));
            const bool half = hud.absorption_mc - 2.f * static_cast<float>(full) >= 1.f;
            for (int i = 0; i < std::min(10, full + (half ? 1 : 0)); ++i) {
                mc::HudRect r = layout.heart(i);
                r.y -= 10.f * static_cast<float>(layout.scale());
                sprite(r, i < full ? mc::hud::kUV_HEART_ABSORB_FULL : mc::hud::kUV_HEART_ABSORB_HALF);
            }
        }
        text_x = layout.heart(0).x;
        text_y = layout.heart(0).y - 28.f * scale;
    }
    // hotbar
    for (int i = 0; i < 9 && !g_atlas_ready; ++i) {
        const float x = x0 + static_cast<float>(i) * slot;
        dl->AddRectFilled({x, y_bar}, {x + slot, y_bar + slot}, IM_COL32(0, 0, 0, 120));
        const bool sel = i == hud.selected_slot;
        dl->AddRect({x, y_bar}, {x + slot, y_bar + slot}, sel ? IM_COL32(255, 255, 255, 255) : IM_COL32(110, 110, 110, 255),
                    0.f, 0, (sel ? 3.f : 1.5f) * scale);
    }
    // hearts: 10 hearts = 20 half hearts, from the real HP
    const float ratio = hud.max_hp > 0 ? std::clamp(static_cast<float>(hud.hp) / static_cast<float>(hud.max_hp), 0.f, 1.f) : 0.f;
    const int halves = static_cast<int>(std::ceil(ratio * 20.f));
    const float y_hearts = y_bar - cell - 6.f * scale;
    for (int i = 0; i < 10 && !g_atlas_ready; ++i) {
        const float x = x0 + static_cast<float>(i) * (cell + gap);
        dl->AddRectFilled({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(40, 0, 0, 200));
        const int fill = std::clamp(halves - i * 2, 0, 2);
        if (fill >= 1) {
            const float fw = fill == 2 ? cell : cell * 0.5f;
            dl->AddRectFilled({x, y_hearts}, {x + fw, y_hearts + cell}, IM_COL32(205, 30, 30, 255));
        }
        dl->AddRect({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
    }
    // crosshair
    const float cx = w * 0.5f, cy = h * 0.5f, arm = 10.f * scale;
    dl->AddLine({cx - arm, cy}, {cx + arm, cy}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    dl->AddLine({cx, cy - arm}, {cx, cy + arm}, IM_COL32(255, 255, 255, 200), 2.f * scale);
    // hit marker: four diagonal ticks around the crosshair (Minecraft style); orange and longer for a critical hit
    if (hud.hit > 0.f) {
        const float a = std::clamp(hud.hit, 0.f, 1.f);
        const float tick_gap = (hud.hit_crit ? 7.f : 5.f) * scale, len = (hud.hit_crit ? 12.f : 8.f) * scale;
        const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
        const float s = 0.70710678f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                dl->AddLine({cx + sx * tick_gap * s, cy + sy * tick_gap * s}, {cx + sx * (tick_gap + len) * s, cy + sy * (tick_gap + len) * s}, col,
                            2.5f * scale);
            }
        }
    }
    // kill marker: a red X, bigger and longer-lived
    if (hud.kill > 0.f) {
        const float a = std::clamp(hud.kill, 0.f, 1.f);
        const float tick_gap = 9.f * scale, len = 16.f * scale;
        const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
        const float s = 0.70710678f;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                dl->AddLine({cx + sx * tick_gap * s, cy + sy * tick_gap * s}, {cx + sx * (tick_gap + len) * s, cy + sy * (tick_gap + len) * s}, col,
                            3.5f * scale);
            }
        }
    }
    DrawFx(dl, atlas, w, h);
    // eating: a thin bar under the crosshair that fills in 1.6 s
    if (hud.eating > 0.f) {
        const float bw = 60.f * scale, bh = 5.f * scale, bx = cx - bw * 0.5f, by = cy + 22.f * scale;
        dl->AddRectFilled({bx, by}, {bx + bw, by + bh}, IM_COL32(0, 0, 0, 160));
        dl->AddRectFilled({bx, by}, {bx + bw * std::clamp(hud.eating, 0.f, 1.f), by + bh}, IM_COL32(240, 240, 240, 230));
    }
    // totem of undying: the icon swells in the middle of the screen and fades out
    if (hud.totem > 0.f && g_atlas_ready) {
        const float t = std::clamp(hud.totem, 0.f, 1.f);
        const float size = (150.f + 150.f * (1.f - t)) * scale;
        const float a = std::min(1.f, t * 1.6f);
        const mc::hud::HudUV& uv = mc::hud::kUV_ITEM_TOTEM_OF_UNDYING;
        dl->AddImage(atlas, {cx - size * 0.5f, cy - size * 0.5f}, {cx + size * 0.5f, cy + size * 0.5f}, {uv.u0, uv.v0}, {uv.u1, uv.v1},
                     IM_COL32(255, 255, 255, static_cast<int>(255 * a)));
    }
    if (hud.slot_probe >= 0) {
        char pb[48];
        snprintf(pb, sizeof(pb), "PART SLOT %d HIDDEN", hud.slot_probe);
        dl->AddText(ImGui::GetFont(), 36.f * scale, {w * 0.5f - 200.f * scale, 60.f * scale}, IM_COL32(255, 230, 0, 255), pb);
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "MC %d/%d", hud.hp, hud.max_hp);
    dl->AddText({text_x, text_y}, IM_COL32(255, 255, 255, 220), buf);
}

void RenderFrame(IDXGISwapChain* sc) {
    HudState hud;
    SteveState steve;
    if (!g_provider || !g_provider(hud, steve) || !hud.show || !hud.mc_mode) return;

    if (g_depth_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_cpu);
        if (g_depth_copy_cpu.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy_cpu);
    }

    const UINT idx = g_s.swap->GetCurrentBackBufferIndex();
    if (idx >= g_s.buffers) return;
    Frame& f = g_s.frames[idx];
    if (f.fence_value != 0 && g_s.fence->GetCompletedValue() < f.fence_value) {
        g_s.fence->SetEventOnCompletion(f.fence_value, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 1000);
    }
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::max(1.f / 1000.f, std::chrono::duration<float>(now - g_s.last_frame).count());
    g_s.last_frame = now;

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = {static_cast<float>(g_s.width), static_cast<float>(g_s.height)};
    io.DeltaTime = dt;
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();
    {
        // Combat particles: take the queued events, advance them, remember this frame's camera for the projection.
        g_fx_valid = steve.cam_valid;
        if (steve.cam_valid) {
            g_fx_vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / static_cast<float>(g_s.height));
            g_fx_fov = steve.fov_y;
        }
        std::vector<FxEvent> events;
        {
            std::lock_guard<std::mutex> g(g_fx_mutex);
            events.swap(g_fx_events);
        }
        for (const FxEvent& e : events) {
            switch (e.kind) {
                case FxKind::Crit: g_particles.spawnCrit(e.pos, g_fx_rng); break;
                case FxKind::Damage: g_particles.spawnDamage(e.pos, e.count, g_fx_rng); break;
                case FxKind::Sweep: g_particles.spawnSweep(e.pos); break;
            }
        }
        g_particles.update(dt);
    }
    DrawHud(hud, io.DisplaySize.x, io.DisplaySize.y);
    ImGui::Render();

    f.alloc->Reset();
    g_s.list->Reset(f.alloc, nullptr);
    RecordAtlasUpload(g_s.list);
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = f.back;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    ID3D12DescriptorHeap* heaps[] = {g_s.srv_heap};
    g_s.list->SetDescriptorHeaps(1, heaps);
    if (g_steve_cfg.depthview_gain > 0.f && g_depth_res != nullptr) {
        g_steve.drawDepthView(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, g_steve_cfg.depthview_gain,
                              static_cast<float>(g_depth_w), static_cast<float>(g_depth_h));
    }
    bool blocks_drawn = false;
    if (g_blocks_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_block_mutex);
        g_steve.setBlocks(g_block_mesh);
    }
    if (steve.cam_valid && g_steve.ready() && g_atlas_ready && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        SteveParams bp;
        bp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.f : 0.f);
        bp.depth_const = g_steve_cfg.depth_const;
        bp.rel_bias = g_steve_cfg.rel_bias;
        bp.abs_bias = g_steve_cfg.abs_bias;
        bp.depth_w = static_cast<float>(g_depth_w);
        bp.depth_h = static_cast<float>(g_depth_h);
        blocks_drawn = g_steve.drawBlocks(g_s.device, g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, bp, f.rtv, idx);
        if (blocks_drawn) g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    }
    if (steve.draw && g_steve.ready()) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        mc::SteveAnimInput anim_in = g_motion.update(dt, steve.feet, steve.yaw);
        anim_in.swing_progress = steve.swing;
        g_anim.update(dt, anim_in);
        if (g_trace_left.load() > 0) {
            g_trace_left.fetch_sub(1);
            char tr[220];
            snprintf(tr, sizeof(tr), "trace: dt=%.4f feet=%.4f %.4f %.4f cam=%.4f %.4f %.4f yaw=%.3f fwd=%.3f strafe=%.3f", dt, steve.feet[0],
                     steve.feet[1], steve.feet[2], steve.cam.position.x, steve.cam.position.y, steve.cam.position.z, steve.yaw,
                     anim_in.forward_speed, anim_in.strafe_speed);
            Logf(tr);
        }
        auto parts = eldenring::render::posedMatrices(g_anim.getTransforms(), {steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw);
        g_death_seconds = steve.dead ? g_death_seconds + dt : 0.f;
        if (g_death_seconds > 0.f) {
            const mc::rig::Mat4 fall = eldenring::render::deathFallMatrix({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw,
                                                                         eldenring::render::deathFlipFraction(g_death_seconds));
            for (auto& m : parts) m = m * fall;
        }
        SteveParams sp;
        sp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.debug == 2 ? 3.f : (g_steve_cfg.debug != 0 ? 2.f : (g_steve_cfg.occlusion ? 1.f : 0.f)));
        sp.depth_const = g_steve_cfg.depth_const;
        sp.rel_bias = g_steve_cfg.rel_bias;
        sp.abs_bias = g_steve_cfg.abs_bias;
        sp.depth_w = static_cast<float>(g_depth_w);
        sp.depth_h = static_cast<float>(g_depth_h);
        sp.tint[0] = 1.f; // Minecraft's hurt flash: red over the lit skin for the 10 ticks after a hit
        sp.tint[1] = sp.tint[2] = 0.f;
        sp.tint[3] = 0.4f * std::min(1.f, steve.hurt * 4.f);
        sp.keep_depth = blocks_drawn; // the blocks left their depth in the figure's buffer: the figure sorts against them
        sp.held_item = steve.held_item;
        sp.held_table = g_atlas_ready ? g_held_table_gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};
        if (g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
            g_steve.draw(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, parts, sp, f.rtv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr); // ImGui draws without a depth view
        }
        if (sp.mode > 2.5f) {
            static auto last_log = std::chrono::steady_clock::now();
            const auto t = std::chrono::steady_clock::now();
            uint32_t st[64];
            if (t - last_log > std::chrono::seconds(2) && g_steve.readStats(st)) {
                last_log = t;
                uint64_t total = 0;
                for (uint32_t c : st) total += c;
                if (g_log && total > 0) {
                    int order[64];
                    for (int i = 0; i < 64; ++i) order[i] = i;
                    std::sort(order, order + 64, [&](int x, int y) { return st[x] > st[y]; });
                    char line[400];
                    int n = snprintf(line, sizeof(line), "steve: K=depth*z histogram, %llu px; top bins:", static_cast<unsigned long long>(total));
                    for (int i = 0; i < 5; ++i) {
                        const double centre = 0.0005 * std::pow(2.0, (order[i] + 0.5) / 8.0);
                        n += snprintf(line + n, sizeof(line) - static_cast<size_t>(n), " [K~%.5f: %.0f%%]", centre,
                                      100.0 * st[order[i]] / static_cast<double>(total));
                    }
                    g_log("%s (assumed %.5f)", line, g_steve_cfg.depth_const);
                }
            }
        }
    }
    if (steve.first_person && steve.cam_valid && g_steve.ready() && g_fp_built && g_steve.firstPersonReady() &&
        g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        namespace fp = eldenring::fp;
        // The hand: lowered/raised by the hotbar and the attack cooldown, swaying behind the view, bobbing with the steps.
        const mc::ItemId wanted = static_cast<mc::ItemId>(steve.held_item);
        g_hand.tick(dt, wanted, std::clamp(steve.cooldown, 0.f, 1.f));
        const float pitch_deg = -std::asin(std::clamp(steve.cam.forward.y, -1.f, 1.f)) * 180.f / fp::kPi; // positive = looking down
        const float yaw_deg = std::atan2(steve.cam.forward.x, steve.cam.forward.z) * 180.f / fp::kPi;
        float d_pitch = 0.f, d_yaw = 0.f;
        g_sway.update(dt, pitch_deg, yaw_deg, d_pitch, d_yaw);
        g_walk_dist += steve.speed_mps * dt * 0.6f;
        const float bob_target = steve.on_ground ? std::min(0.1f, steve.speed_mps / 20.f) : 0.f;
        g_walk_bob += (bob_target - g_walk_bob) * (1.f - std::pow(0.6f, dt * 20.f));
        const fp::M4 base = fp::mul(fp::walkBob(g_walk_dist, g_walk_bob), fp::handSway(d_pitch, d_yaw));
        const float equipped = g_hand.equipped();
        const fp::M4 item_pose = steve.eating > 0.f ? fp::eatPose(steve.eating, equipped) : fp::itemPose(steve.swing, equipped);
        const bool held_block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(g_hand.shownItem())) != mc::BlockId::Air;
        const fp::M4 item_world = fp::mul(fp::mul(base, item_pose), held_block ? fp::blockDisplay() : fp::itemDisplay());
        const fp::M4 arm_world = fp::mul(base, fp::bareArmPose(steve.swing, equipped));
        const mc::ItemId shown = g_hand.shownItem();
        const mc::rig::Mat4 proj = mc::rig::perspectiveLH(70.f * fp::kDeg, static_cast<float>(g_s.width) / static_cast<float>(g_s.height), 0.05f, 20.f);
        D3D12_GPU_DESCRIPTOR_HANDLE skin_table = g_depth_gpu; // slots 1 and 2: depth, skin
        g_steve.drawFirstPerson(g_s.list, g_s.srv_heap, skin_table, g_item_table_gpu, g_s.width, g_s.height, proj, fp::toHost(arm_world),
                                fp::toHost(item_world), static_cast<uint16_t>(shown), f.rtv);
        g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr); // ImGui draws without a depth view
    }
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_s.list);
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->Close();
    ID3D12CommandList* lists[] = {g_s.list};
    g_execute_orig(g_s.queue, 1, lists);
    f.fence_value = ++g_s.fence_counter;
    g_s.queue->Signal(g_s.fence, f.fence_value);
    (void)sc;
}

void STDMETHODCALLTYPE CreateDsvDetour(ID3D12Device* d, ID3D12Resource* res, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                       D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    g_create_dsv_orig(d, res, desc, handle);
    if (res == nullptr) return;
    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd.Format != DXGI_FORMAT_R32G8X24_TYPELESS || rd.Width < 1024 ||
        rd.DepthOrArraySize != 1) {
        return;
    }
    {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        if (g_depth_res == res) return;
        bool known = false;
        for (ID3D12Resource* c : g_depth_candidates) known = known || c == res;
        if (!known) {
            res->AddRef();
            g_depth_candidates.push_back(res);
            if (g_depth_candidates.size() > 8) {
                g_depth_candidates.front()->Release();
                g_depth_candidates.erase(g_depth_candidates.begin());
            }
        }
        res->AddRef();
        if (g_depth_res) g_depth_res->Release();
        g_depth_res = res;
        g_depth_w = static_cast<UINT>(rd.Width);
        g_depth_h = rd.Height;
    }
    g_depth_dirty.store(true);
    static int logged = 0;
    if (logged++ < 20 && g_log) {
        g_log("overlay: scene depth captured: %p %llux%u format=%d samples=%u flags=0x%X dsv_format=%d", static_cast<void*>(res),
              static_cast<unsigned long long>(rd.Width), rd.Height, static_cast<int>(rd.Format), rd.SampleDesc.Count,
              static_cast<unsigned>(rd.Flags), desc ? static_cast<int>(desc->Format) : -1);
    }
}

void STDMETHODCALLTYPE ExecuteDetour(ID3D12CommandQueue* q, UINT n, ID3D12CommandList* const* lists) {
    if (g_queue.load(std::memory_order_relaxed) == nullptr) {
        const D3D12_COMMAND_QUEUE_DESC d = q->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) g_queue.store(q);
    }
    g_execute_orig(q, n, lists);
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (!g_s.failed) {
        ID3D12CommandQueue* q = g_queue.load();
        if (!g_s.ready && q != nullptr) {
            if (!Init(sc, q)) {
                // Init may fail only because the first captured queue is the wrong device; give up after it released everything.
                Teardown();
                static int attempts = 0;
                if (++attempts >= 20) {
                    g_s.failed = true;
                    Logf("overlay: giving up after repeated init failures");
                }
            }
        }
        if (g_s.ready) {
            __try {
                RenderFrame(sc);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                g_s.failed = true;
                Logf("overlay: exception while drawing, overlay disabled");
            }
        }
    }
    return g_present_orig(sc, sync, flags);
}

HRESULT STDMETHODCALLTYPE ResizeDetour(IDXGISwapChain* sc, UINT count, UINT w, UINT h, DXGI_FORMAT fmt, UINT flags) {
    if (g_s.ready) {
        Teardown(); // rebuilt on the next Present
        Logf("overlay: resize, resources released");
    }
    return g_resize_orig(sc, count, w, h, fmt, flags);
}

// Throw-away device, queue and swap chain, only to read the vtable function addresses.
bool ResolveTargets(void*& present, void*& resize, void*& execute, void*& create_dsv) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"mc_er_dummy";
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"mc", WS_OVERLAPPED, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    bool ok = false;
    ID3D12Device* dev = nullptr;
    ID3D12CommandQueue* q = nullptr;
    IDXGIFactory4* fac = nullptr;
    IDXGISwapChain1* sc1 = nullptr;
    if (hwnd && SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&dev)))) {
        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (SUCCEEDED(dev->CreateCommandQueue(&qd, IID_PPV_ARGS(&q))) && SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&fac)))) {
            DXGI_SWAP_CHAIN_DESC1 sd{};
            sd.Width = 8;
            sd.Height = 8;
            sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sd.SampleDesc.Count = 1;
            sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sd.BufferCount = 2;
            sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(fac->CreateSwapChainForHwnd(q, hwnd, &sd, nullptr, nullptr, &sc1))) {
                void** sv = *reinterpret_cast<void***>(sc1);
                void** qv = *reinterpret_cast<void***>(q);
                present = sv[kVtblPresent];
                resize = sv[kVtblResizeBuffers];
                execute = qv[kVtblExecuteCommandLists];
                create_dsv = (*reinterpret_cast<void***>(dev))[kVtblDeviceCreateDsv];
                ok = true;
            }
        }
    }
    SafeRelease(sc1);
    SafeRelease(fac);
    SafeRelease(q);
    SafeRelease(dev);
    if (hwnd) DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return ok;
}

} // namespace

void SetSteveConfig(const SteveConfig& cfg) { g_steve_cfg = cfg; }

void RequestFrameTrace(int frames) { g_trace_left.store(frames); }

void SpawnFx(FxKind kind, const float world_pos[3], int count) {
    FxEvent e{kind, {world_pos[0], world_pos[1], world_pos[2]}, count};
    std::lock_guard<std::mutex> g(g_fx_mutex);
    if (g_fx_events.size() < 64) g_fx_events.push_back(e);
}

void SetHudAtlas(const uint8_t* rgba, unsigned width, unsigned height) {
    g_atlas_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_atlas_w = width;
    g_atlas_h = height;
}

void SetSteveSkin(const uint8_t* rgba, unsigned width, unsigned height) {
    g_skin_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_skin_w = width;
    g_skin_h = height;
}

void SetBlockMesh(const mc::rig::RigMesh& mesh) {
    std::lock_guard<std::mutex> g(g_block_mutex);
    g_block_mesh = mesh;
    g_blocks_dirty.store(true);
}

void ScreenSize(float& width, float& height) {
    width = static_cast<float>(g_s.width);
    height = static_cast<float>(g_s.height);
}

void CycleDepthCandidate() {
    std::lock_guard<std::mutex> g(g_depth_mutex);
    if (g_depth_candidates.empty()) return;
    size_t cur = 0;
    for (size_t i = 0; i < g_depth_candidates.size(); ++i) {
        if (g_depth_candidates[i] == g_depth_res) cur = i;
    }
    ID3D12Resource* next = g_depth_candidates[(cur + 1) % g_depth_candidates.size()];
    next->AddRef();
    if (g_depth_res) g_depth_res->Release();
    g_depth_res = next;
    const D3D12_RESOURCE_DESC rd = next->GetDesc();
    g_depth_w = static_cast<UINT>(rd.Width);
    g_depth_h = rd.Height;
    g_depth_dirty.store(true);
    if (g_log) g_log("overlay: depth candidate %zu of %zu bound: %p %llux%u", (cur + 1) % g_depth_candidates.size() + 1,
                     g_depth_candidates.size(), static_cast<void*>(next), static_cast<unsigned long long>(rd.Width), rd.Height);
}

bool Install(HudProvider provider, LogFn log) {
    g_provider = provider;
    g_log = log;
    void *present = nullptr, *resize = nullptr, *execute = nullptr, *create_dsv = nullptr;
    if (!ResolveTargets(present, resize, execute, create_dsv)) {
        Logf("overlay: could not resolve the D3D12/DXGI vtable functions");
        return false;
    }
    if (MH_CreateHook(execute, reinterpret_cast<void*>(&ExecuteDetour), reinterpret_cast<void**>(&g_execute_orig)) != MH_OK ||
        MH_CreateHook(present, reinterpret_cast<void*>(&PresentDetour), reinterpret_cast<void**>(&g_present_orig)) != MH_OK ||
        MH_CreateHook(resize, reinterpret_cast<void*>(&ResizeDetour), reinterpret_cast<void**>(&g_resize_orig)) != MH_OK ||
        MH_CreateHook(create_dsv, reinterpret_cast<void*>(&CreateDsvDetour), reinterpret_cast<void**>(&g_create_dsv_orig)) != MH_OK) {
        Logf("overlay: MH_CreateHook failed");
        return false;
    }
    if (MH_EnableHook(execute) != MH_OK || MH_EnableHook(present) != MH_OK || MH_EnableHook(resize) != MH_OK ||
        MH_EnableHook(create_dsv) != MH_OK) {
        Logf("overlay: MH_EnableHook failed");
        return false;
    }
    char msg[200];
    snprintf(msg, sizeof(msg), "overlay: hooks installed (Present=%p ResizeBuffers=%p ExecuteCommandLists=%p CreateDepthStencilView=%p)", present,
             resize, execute, create_dsv);
    Logf(msg);
    return true;
}

} // namespace erov
