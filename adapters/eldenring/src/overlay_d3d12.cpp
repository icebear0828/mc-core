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
#include <filesystem>

#include <mutex>
#include <vector>

#include "eldenring_bow.hpp"
#include "eldenring_fp.hpp"
#include "eldenring_hudtex.hpp"
#include "eldenring_depth.hpp"
#include "depth_probe_d3d12.hpp"
#include "eldenring_pixel.hpp"
#include "eldenring_particles.hpp"
#include "eldenring_blockicon.hpp"
#include "eldenring_heart_anim.hpp"
#include "eldenring_blocks.hpp"
#include "mc/hud_layout.hpp"
#include "mc/inventory_layout.hpp"
#include "mc/hud.hpp"
#include "overlay_d3d12.hpp"
#include "png_wic.hpp"
#include "steve_renderer_d3d12.hpp"
#if ENABLE_CS2_VIEWMODEL
#include "cs_renderer_d3d12.hpp"
#include "cs_impact_decals.hpp"
#endif
#if ENABLE_OW_VIEWMODEL
#include "ow_renderer_d3d12.hpp"
#endif
#include "eldenring_runtime_context.hpp"
#include "eldenring_inventory_preview.hpp"
#include "eldenring_diagnostics.hpp"

namespace erov {
namespace {

struct PreviewState {
    bool active{false};
    mc::HudRect rect{};
    float mouse_x{0.f};
    float mouse_y{0.f};
    float scale{1.f};
    uint16_t held_item{0};
    uint16_t offhand_item{0};
};
static PreviewState g_inv_preview{};
static D3D12_CPU_DESCRIPTOR_HANDLE g_current_rtv{};

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
#if ENABLE_OW_VIEWMODEL
OwConfig g_ow_cfg;
#endif
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
#if ENABLE_CS2_VIEWMODEL
// Authentic CS2 bullet-hole decal (mods/mc_adapter/textures/cs/bullet_hole_concrete.png), heap slot 44.
std::vector<uint8_t> g_decal_rgba;
unsigned g_decal_w = 0, g_decal_h = 0;
ID3D12Resource* g_decal_tex = nullptr;
ID3D12Resource* g_decal_upload = nullptr;
D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_decal_footprint{};
bool g_decal_pending = false;
D3D12_GPU_DESCRIPTOR_HANDLE g_decal_gpu{};
bool g_decal_ready = false;
void CreateCsDecalTexture(UINT srv_inc);
#endif
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
// CS hitscan tracers: world-space segments queued by CombatService::fireCsShot.
struct TracerEvent {
    float from[3];
    float to[3];
    uint64_t start_ms;
    float life_s;
};
std::mutex g_tracer_mutex;
std::vector<TracerEvent> g_tracer_events;
#if ENABLE_CS2_VIEWMODEL
// CS bullet holes on static geometry. The ring is bounded and refreshes hits within 3 cm.
std::mutex g_decal_mutex;
eldenring::cs::ImpactDecalRing g_decal_ring;
#endif
eldenring::fp::HandAnimator g_hand;
eldenring::fp::SwayFilter g_sway;
float g_walk_dist = 0.f, g_walk_bob = 0.f;
D3D12_GPU_DESCRIPTOR_HANDLE g_item_table_gpu{}; // slots 4 and 5
bool g_fp_built = false;
std::atomic<int> g_trace_left{0}; // F12: frames of per-frame figure/camera positions still to write to the log
std::vector<uint8_t> g_skin_rgba; // decoded by the loader; applied when the renderer is created
unsigned g_skin_w = 0, g_skin_h = 0;
SteveRenderer g_steve;
#if ENABLE_CS2_VIEWMODEL
CsRenderer g_cs;
// CS arsenal runtime state: texture slots and the weapon currently loaded.
D3D12_CPU_DESCRIPTOR_HANDLE g_cs_weapon_tex_cpu{};
D3D12_CPU_DESCRIPTOR_HANDLE g_cs_glove_tex_cpu{};
D3D12_CPU_DESCRIPTOR_HANDLE g_cs_knife_table_cpu{};
D3D12_CPU_DESCRIPTOR_HANDLE g_cs_ak_table_cpu{};
D3D12_GPU_DESCRIPTOR_HANDLE g_cs_knife_table_gpu{};
D3D12_GPU_DESCRIPTOR_HANDLE g_cs_ak_table_gpu{};
std::string g_cs_game_dir;
std::string g_cs_knife_model_path;
uint8_t g_cs_active_slot = 0;
uint8_t g_cs_gun_table_slot = 0; // weapon slot whose maps are in g_cs_ak_table (0 = none)
#endif

#if ENABLE_OW_VIEWMODEL
// Overwatch (Doomfist) first-person gauntlet. It owns a renderer, a descriptor
// heap and a config of its own; nothing here touches the CS2 tables.
erow::OwRenderer g_ow;
ID3D12DescriptorHeap* g_ow_srv_heap = nullptr; // 3 SRVs: albedo, normal, ORM
D3D12_GPU_DESCRIPTOR_HANDLE g_ow_table_gpu{};
bool g_ow_initialized = false;
bool g_ow_model_loaded = false;
bool g_ow_load_failed = false;
#endif
mc::SteveAnimator g_anim;
eldenring::render::SteveMotion g_motion;
eldenring::render::HeadTracker g_head;
std::mutex g_mob_mutex;
std::vector<eldenring::mobs::MobSnapshot> g_mob_snapshot; // the list the loader gave last
std::mutex g_arrow_mutex;
std::vector<eldenring::summon::ArrowDraw> g_arrow_snapshot;
eldenring::mobs::MobRegistry g_mob_registry;              // render thread only
mc::PlayerDeathTracker g_death_tracker;
mc::PlayerSleepTracker g_sleep_tracker;
std::mutex g_depth_mutex;
ID3D12Resource* g_depth_res = nullptr; // AddRef'd scene depth in use (R32G8X24_TYPELESS), guarded by g_depth_mutex
std::vector<ID3D12Resource*> g_depth_candidates; // AddRef'd, oldest first, guarded by g_depth_mutex
UINT g_depth_w = 0, g_depth_h = 0;
std::atomic<bool> g_depth_dirty{false};
D3D12_CPU_DESCRIPTOR_HANDLE g_depth_cpu{};
D3D12_GPU_DESCRIPTOR_HANDLE g_depth_gpu{};
std::mutex g_shot_mutex;
std::string g_shot_path;
std::atomic<bool> g_shot_requested{false};
std::mutex g_block_mutex;
mc::rig::RigMesh g_block_mesh;
std::mutex g_crack_mutex;
mc::rig::RigMesh g_crack_mesh;
std::atomic<bool> g_crack_dirty{false};
std::atomic<bool> g_blocks_dirty{false};
D3D12_CPU_DESCRIPTOR_HANDLE g_depth_copy2_cpu{}; // slot 8: default mob depth copy
D3D12_GPU_DESCRIPTOR_HANDLE g_mob_table_gpu{};   // default mob table; ptr 0 = no mob skin
std::map<std::string, D3D12_GPU_DESCRIPTOR_HANDLE> g_mob_tables_gpu;
std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> g_mob_depth_copy_cpus;
mc::model::EntityModel g_mob_model;              // default model file
bool g_mob_model_given = false;
bool g_mob_model_gpu = false;                    // the model's meshes are on the GPU
std::vector<uint8_t> g_mob_rgba;                 // decoded by the loader; applied when the renderer is created
unsigned g_mob_w = 0, g_mob_h = 0;
struct MobSkinEntry {
    std::vector<uint8_t> rgba;
    unsigned w{0}, h{0};
};
std::map<std::string, mc::model::EntityModel> g_mob_models;
std::map<std::string, MobSkinEntry> g_mob_skins;
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

template <typename... Args>
void Logf(const char* fmt, Args... args) {
    if (g_log) g_log(fmt, args...);
}
void Logf(const char* msg) {
    if (g_log) g_log("%s", msg);
}

// Elden Ring can lose its DirectInput devices when it stops being the
// foreground window. Skipping the CS viewmodel while unfocused avoids drawing
// into a swap chain the game is throttling, and matches the hotkey gate.
bool GameIsForeground() {
    DWORD pid = 0;
    HWND fg = GetForegroundWindow();
    if (fg != nullptr) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

#if ENABLE_CS2_VIEWMODEL
std::string ResolveCsAssetPath(const std::string& path) {
    if (path.empty()) return path;
    const std::filesystem::path p(path);
    if (p.is_absolute() || g_cs_game_dir.empty()) return path;
    return (std::filesystem::path(g_cs_game_dir) / p).string();
}

// Loads the weapon requested by the hotkey dispatcher. The knife is the
// fallback: if a gun asset is missing we keep rendering the current weapon.
void UpdateCsWeaponSlot(uint8_t want_slot) {
    if (want_slot == g_cs_active_slot) return;
    Logf("overlay: CS weapon switch requested -> %u", static_cast<unsigned>(want_slot));

    bool loaded = false;
    if (want_slot == 0) {
        if (!g_cs_knife_model_path.empty() && std::filesystem::exists(g_cs_knife_model_path) &&
            g_cs.loadModel(g_s.device, g_cs_knife_model_path)) {
            loaded = true;
        }
    } else {
        const char* id = (want_slot == 1) ? "weapon_ak47"
                       : (want_slot == 2) ? "weapon_awp"
                       : "weapon_m4a1_silencer";
        if (const auto* found = eldenring::cs::CSWeaponRegistry::instance().findWeapon(id)) {
            auto def = *found;
            def.model_csvm_path = ResolveCsAssetPath(def.model_csvm_path);
            def.albedo_tex_path = ResolveCsAssetPath(def.albedo_tex_path);
            def.normal_tex_path = ResolveCsAssetPath(def.normal_tex_path);
            def.pbr_tex_path = ResolveCsAssetPath(def.pbr_tex_path);
            if (std::filesystem::exists(def.model_csvm_path) && g_cs.loadWeapon(g_s.device, def)) {
                // The gun table is shared by every gun: swap in this weapon's
                // maps unless they are already there. The decoded PNGs stay
                // cached, so a switch only pays the GPU upload.
                if (g_cs_gun_table_slot != want_slot) {
                    const bool any_real = g_cs.loadWeaponTextures(g_s.device, def, g_cs_ak_table_cpu);
                    g_cs_gun_table_slot = want_slot;
                    if (!any_real) {
                        Logf("overlay: CS weapon %u has no texture maps, using fallbacks",
                             static_cast<unsigned>(want_slot));
                    }
                }
                loaded = true;
            }
        }
    }

    if (loaded) {
        g_cs_active_slot = want_slot;
        Logf("overlay: CS weapon slot -> %u", static_cast<unsigned>(want_slot));
    } else {
        // Keep the current weapon and undo the request so we do not retry
        // a missing asset every frame.
        eldenring::RuntimeContext::Instance().mode().cs_weapon_slot.store(g_cs_active_slot);
        Logf("overlay: CS weapon slot %u unavailable, keeping %u",
             static_cast<unsigned>(want_slot), static_cast<unsigned>(g_cs_active_slot));
    }
}
#endif

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
#if ENABLE_CS2_VIEWMODEL
    g_cs.release();
#endif
#if ENABLE_OW_VIEWMODEL
    g_ow.release();
    SafeRelease(g_ow_srv_heap);
    g_ow_table_gpu = {};
    g_ow_initialized = false;
    g_ow_model_loaded = false;
    g_ow_load_failed = false;
#endif
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
#if ENABLE_CS2_VIEWMODEL
    SafeRelease(g_decal_tex);
    SafeRelease(g_decal_upload);
    g_decal_ready = false;
#endif
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
    srv_desc.NumDescriptors = 64; // 0: ImGui, 1-2: Steve, 3: HUD, 4-5: FP items, 6-7: Held, 8-9: Creeper, 10-11: Zombie, 12-13: Skeleton, 14-15: extra, 16-27: CS2 knife table, 32-43: CS2 gun table
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
            g_depth_copy2_cpu = {}; // a new heap: nothing of the old one is valid
            g_mob_table_gpu = {};
            g_mob_tables_gpu.clear();
            g_mob_depth_copy_cpus.clear();
            g_mob_model_gpu = false;

            // Upload all registered mob models to GPU
            for (const auto& [name, model] : g_mob_models) {
                if (g_steve.setMobModel(g_s.device, name, model)) {
                    Logf("overlay: mob model '%s' ready (%zu bones)", name.c_str(), model.bones.size());
                }
            }
            if (g_mob_model_given) {
                g_mob_model_gpu = g_steve.setMobModel(g_s.device, g_mob_model);
                Logf(g_mob_model_gpu ? "overlay: default mob model ready (Bedrock geometry, one mesh per bone)" : "overlay: default mob model upload failed, the mobs use the Steve rig");
            }
            g_mob_registry.setModel(g_mob_model_gpu ? &g_mob_model : nullptr);

            // Mobs list to allocate descriptor tables for
            const std::vector<std::string> known_mobs = {"creeper", "zombie", "skeleton"};
            size_t slot_idx = 8;
            for (const auto& mob : known_mobs) {
                if (slot_idx + 1 >= 16) break;
                D3D12_CPU_DESCRIPTOR_HANDLE depth_slot = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
                depth_slot.ptr += static_cast<SIZE_T>(srv_inc) * slot_idx;
                D3D12_CPU_DESCRIPTOR_HANDLE skin_slot = depth_slot;
                skin_slot.ptr += srv_inc;

                g_steve.setDepthView(g_s.device, nullptr, depth_slot);
                g_mob_depth_copy_cpus.push_back(depth_slot);

                // Find skin for this mob
                const uint8_t* skin_data = nullptr;
                unsigned mob_sw = 0, mob_sh = 0;
                auto it = g_mob_skins.find(mob);
                if (it != g_mob_skins.end() && !it->second.rgba.empty()) {
                    skin_data = it->second.rgba.data();
                    mob_sw = it->second.w;
                    mob_sh = it->second.h;
                } else if (!g_mob_rgba.empty() && g_mob_w != 0 && g_mob_h != 0) {
                    skin_data = g_mob_rgba.data();
                    mob_sw = g_mob_w;
                    mob_sh = g_mob_h;
                }

                if (skin_data && mob_sw != 0 && mob_sh != 0) {
                    if (g_steve.setMobSkin(g_s.device, mob, skin_data, mob_sw, mob_sh, skin_slot)) {
                        D3D12_GPU_DESCRIPTOR_HANDLE gpu_table = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
                        gpu_table.ptr += static_cast<UINT64>(srv_inc) * slot_idx;
                        g_mob_tables_gpu[mob] = gpu_table;
                        if (g_mob_table_gpu.ptr == 0) {
                            g_mob_table_gpu = gpu_table;
                            g_depth_copy2_cpu = depth_slot;
                        }
                        Logf("overlay: mob skin '%s' ready (%ux%u at slot %zu)", mob.c_str(), mob_sw, mob_sh, slot_idx);
                    }
                }
                slot_idx += 2;
            }
        }
        g_depth_dirty.store(true); // bind the depth captured so far
        CreateAtlasTexture(srv_inc);
#if ENABLE_CS2_VIEWMODEL
        CreateCsDecalTexture(srv_inc);
#endif
#if ENABLE_CS2_VIEWMODEL
        // A build without MC gameplay must never start in MC mode, even if a
        // config file or a missing CS asset would otherwise fall back to it.
        // A combined build keeps the configured startup mode (mc_er_mode.txt).
#if !ENABLE_MC_MODE
        eldenring::RuntimeContext::Instance().mode().mc_mode.store(false);
#endif
        if (!g_cs.init(g_s.device, g_s.format, g_log)) {
            Logf("overlay: CS viewmodel renderer unavailable");
        } else {
            D3D12_CPU_DESCRIPTOR_HANDLE cs_table_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
            cs_table_cpu.ptr += static_cast<SIZE_T>(srv_inc) * 16;
            D3D12_CPU_DESCRIPTOR_HANDLE glove_cpu = cs_table_cpu;
            glove_cpu.ptr += srv_inc;
            D3D12_CPU_DESCRIPTOR_HANDLE ak_table_cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
            ak_table_cpu.ptr += static_cast<SIZE_T>(srv_inc) * 32;
            g_cs_weapon_tex_cpu = cs_table_cpu;
            g_cs_glove_tex_cpu = glove_cpu;
            g_cs_knife_table_cpu = cs_table_cpu;
            g_cs_ak_table_cpu = ak_table_cpu;
            g_cs_gun_table_slot = 0;

            D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_base = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
            g_cs_knife_table_gpu = srv_gpu_base;
            g_cs_knife_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 16;
            g_cs_ak_table_gpu = srv_gpu_base;
            g_cs_ak_table_gpu.ptr += static_cast<UINT64>(srv_inc) * 32;

            const std::string& game_dir = eldenring::diagnostics::GetGameDir();
            g_cs_game_dir = game_dir;

            std::vector<std::string> model_candidates;
            if (!game_dir.empty()) {
                model_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "cs_butterfly_viewmodel.bin").string());
                model_candidates.push_back((std::filesystem::path(game_dir) / "cs_butterfly_viewmodel.bin").string());
            }
            model_candidates.push_back("mods\\mc_adapter\\cs_butterfly_viewmodel.bin");
            model_candidates.push_back("cs_butterfly_viewmodel.bin");

            for (const auto& p : model_candidates) {
                if (std::filesystem::exists(p) && g_cs.loadModel(g_s.device, p)) {
                    char cs_msg[256];
                    snprintf(cs_msg, sizeof(cs_msg), "overlay: CS viewmodel model loaded successfully from %s", p.c_str());
                    Logf(cs_msg);
                    g_cs_knife_model_path = p;
                    // Auto-start CS only when no other mode is active. A
                    // combined build keeps whatever mc_er_mode.txt selected
                    // (mc by default), so deploying OW or MC never silently
                    // switches the player into CS.
                    auto& cs_mode_ref = eldenring::RuntimeContext::Instance().mode();
                    if (!cs_mode_ref.ow_mode.load() && !cs_mode_ref.mc_mode.load()) {
                        cs_mode_ref.cs_mode.store(true);
                        cs_mode_ref.first_person.store(true);
                    }
                    break;
                }
            }

            std::vector<std::string> knife_tex_candidates;
            if (!game_dir.empty()) {
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly.png").string());
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "knife_butterfly" / "knife_butterfly_color_psd_5a5a48d7.png").string());
                knife_tex_candidates.push_back((std::filesystem::path(game_dir) / "knife_butterfly.png").string());
            }
            knife_tex_candidates.push_back("mods\\mc_adapter\\knife_butterfly.png");
            knife_tex_candidates.push_back("mods\\mc_adapter\\textures\\knife_butterfly\\knife_butterfly_color_psd_5a5a48d7.png");
            knife_tex_candidates.push_back("knife_butterfly.png");

            // The packed ORM map is produced by tools/cs2/texture_packer.py:
            // R=AO, G=roughness, B=metallic. The raw CS2 `*_rough_psd` map is
            // not an ORM map, so it must never be used as a fallback here.
            std::vector<std::string> knife_norm_candidates;
            std::vector<std::string> knife_pbr_candidates;
            if (!game_dir.empty()) {
                knife_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly_normal.png").string());
                knife_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "knife_butterfly" / "default_normal_tga_4fc2d3dd.png").string());
                knife_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "knife_butterfly_pbr.png").string());
            }
            knife_norm_candidates.push_back("mods\\mc_adapter\\knife_butterfly_normal.png");
            knife_norm_candidates.push_back("mods\\mc_adapter\\textures\\knife_butterfly\\default_normal_tga_4fc2d3dd.png");
            knife_norm_candidates.push_back("knife_butterfly_normal.png");
            knife_pbr_candidates.push_back("mods\\mc_adapter\\knife_butterfly_pbr.png");
            knife_pbr_candidates.push_back("knife_butterfly_pbr.png");

            std::vector<std::string> glove_tex_candidates;
            if (!game_dir.empty()) {
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle.png").string());
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "glove_hardknuckle_black_color_psd_8f00aa00.png").string());
                glove_tex_candidates.push_back((std::filesystem::path(game_dir) / "glove_hardknuckle.png").string());
            }
            glove_tex_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle.png");
            glove_tex_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\glove_hardknuckle_black_color_psd_8f00aa00.png");
            glove_tex_candidates.push_back("glove_hardknuckle.png");

            std::vector<std::string> glove_norm_candidates;
            std::vector<std::string> glove_pbr_candidates;
            if (!game_dir.empty()) {
                glove_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle_normal.png").string());
                glove_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "glove_hardknuckle_normal_tga_a8df881b.png").string());
                glove_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "glove_hardknuckle_pbr.png").string());
            }
            glove_norm_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle_normal.png");
            glove_norm_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\glove_hardknuckle_normal_tga_a8df881b.png");
            glove_norm_candidates.push_back("glove_hardknuckle_normal.png");
            glove_pbr_candidates.push_back("mods\\mc_adapter\\glove_hardknuckle_pbr.png");
            glove_pbr_candidates.push_back("glove_hardknuckle_pbr.png");

            std::string knife_png;
            for (const auto& p : knife_tex_candidates) {
                if (std::filesystem::exists(p)) { knife_png = p; break; }
            }
            std::string glove_png;
            for (const auto& p : glove_tex_candidates) {
                if (std::filesystem::exists(p)) { glove_png = p; break; }
            }

            std::vector<std::string> bare_tex_candidates;
            if (!game_dir.empty()) {
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm.png").string());
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "bare_arm_133_color_psd_5b76e0cf.png").string());
                bare_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_133_color_psd_5b76e0cf.png").string());
            }
            bare_tex_candidates.push_back("mods\\mc_adapter\\bare_arm.png");
            bare_tex_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\bare_arm_133_color_psd_5b76e0cf.png");
            bare_tex_candidates.push_back("bare_arm_133_color_psd_5b76e0cf.png");

            std::vector<std::string> bare_norm_candidates;
            if (!game_dir.empty()) {
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_normal.png").string());
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "glove_hardknuckle" / "bare_arm_normal_tga_c9fefc2d.png").string());
                bare_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_normal_tga_c9fefc2d.png").string());
            }
            bare_norm_candidates.push_back("mods\\mc_adapter\\bare_arm_normal.png");
            bare_norm_candidates.push_back("mods\\mc_adapter\\textures\\glove_hardknuckle\\bare_arm_normal_tga_c9fefc2d.png");
            bare_norm_candidates.push_back("bare_arm_normal_tga_c9fefc2d.png");

            std::vector<std::string> bare_pbr_candidates;
            if (!game_dir.empty()) {
                bare_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "bare_arm_pbr.png").string());
            }
            bare_pbr_candidates.push_back("mods\\mc_adapter\\bare_arm_pbr.png");
            bare_pbr_candidates.push_back("bare_arm_pbr.png");

            std::string bare_png;
            for (const auto& p : bare_tex_candidates) {
                if (std::filesystem::exists(p)) { bare_png = p; break; }
            }
            std::string bare_norm_png;
            for (const auto& p : bare_norm_candidates) {
                if (std::filesystem::exists(p)) { bare_norm_png = p; break; }
            }
            std::string bare_pbr_png;
            for (const auto& p : bare_pbr_candidates) {
                if (std::filesystem::exists(p)) { bare_pbr_png = p; break; }
            }

            std::string knife_norm_png;
            for (const auto& p : knife_norm_candidates) {
                if (std::filesystem::exists(p)) { knife_norm_png = p; break; }
            }
            std::string knife_pbr_png;
            for (const auto& p : knife_pbr_candidates) {
                if (std::filesystem::exists(p)) { knife_pbr_png = p; break; }
            }
            std::string glove_norm_png;
            for (const auto& p : glove_norm_candidates) {
                if (std::filesystem::exists(p)) { glove_norm_png = p; break; }
            }
            std::string glove_pbr_png;
            for (const auto& p : glove_pbr_candidates) {
                if (std::filesystem::exists(p)) { glove_pbr_png = p; break; }
            }

            std::vector<std::string> sleeve_tex_candidates;
            if (!game_dir.empty()) {
                sleeve_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_color.png").string());
                sleeve_tex_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_color.png").string());
            }
            sleeve_tex_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_color.png");
            sleeve_tex_candidates.push_back("sleeve_fbi_color.png");

            std::vector<std::string> sleeve_norm_candidates;
            if (!game_dir.empty()) {
                sleeve_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_normal.png").string());
                sleeve_norm_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_normal.png").string());
            }
            sleeve_norm_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_normal.png");
            sleeve_norm_candidates.push_back("sleeve_fbi_normal.png");

            std::vector<std::string> sleeve_pbr_candidates;
            if (!game_dir.empty()) {
                sleeve_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "textures" / "sleeves" / "sleeve_fbi_pbr.png").string());
                sleeve_pbr_candidates.push_back((std::filesystem::path(game_dir) / "mods" / "mc_adapter" / "sleeve_fbi_pbr.png").string());
            }
            sleeve_pbr_candidates.push_back("mods\\mc_adapter\\textures\\sleeves\\sleeve_fbi_pbr.png");
            sleeve_pbr_candidates.push_back("sleeve_fbi_pbr.png");

            auto firstExisting = [](const std::vector<std::string>& candidates) {
                for (const auto& p : candidates) {
                    if (std::filesystem::exists(p)) return p;
                }
                return std::string();
            };
            const std::string sleeve_png = firstExisting(sleeve_tex_candidates);
            const std::string sleeve_norm_png = firstExisting(sleeve_norm_candidates);
            const std::string sleeve_pbr_png = firstExisting(sleeve_pbr_candidates);

            g_cs.loadTexturesFromFiles(g_s.device, knife_png, glove_png, cs_table_cpu, glove_cpu,
                                       knife_norm_png, glove_norm_png, knife_pbr_png, glove_pbr_png,
                                       bare_png, bare_norm_png, bare_pbr_png,
                                       sleeve_png, sleeve_norm_png, sleeve_pbr_png);
            // The knife/glove/bare/sleeve maps are uploaded and never re-read;
            // only gun maps are worth keeping decoded for later switches.
            erov::ReleaseCsDecodeCache();

            // Load the AK-47 weapon maps into the second table once. Switching
            // then only swaps the table handle: no PNG decode, no 48 MB upload.
            if (const auto* ak = eldenring::cs::CSWeaponRegistry::instance().findWeapon("weapon_ak47")) {
                auto ak_def = *ak;
                ak_def.albedo_tex_path = ResolveCsAssetPath(ak_def.albedo_tex_path);
                ak_def.normal_tex_path = ResolveCsAssetPath(ak_def.normal_tex_path);
                ak_def.pbr_tex_path = ResolveCsAssetPath(ak_def.pbr_tex_path);
                g_cs.loadWeaponTextures(g_s.device, ak_def, ak_table_cpu);
                g_cs_gun_table_slot = 1;
            }
            // Glove + bare-arm + sleeve descriptors are shared by both weapon tables.
            const UINT shared_slots[] = {1, 3, 5, 6, 7, 8, 9, 10, 11};
            for (UINT shared : shared_slots) {
                D3D12_CPU_DESCRIPTOR_HANDLE src{cs_table_cpu.ptr + static_cast<SIZE_T>(srv_inc) * shared};
                D3D12_CPU_DESCRIPTOR_HANDLE dst{ak_table_cpu.ptr + static_cast<SIZE_T>(srv_inc) * shared};
                g_s.device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
        }
#endif
#if ENABLE_OW_VIEWMODEL
        {
            // The gauntlet owns a three-slot SRV heap. It never shares a slot
            // with the CS2 knife/gun tables, so either subsystem can be built
            // without the other.
            D3D12_DESCRIPTOR_HEAP_DESC ow_heap{};
            ow_heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            ow_heap.NumDescriptors = 3;
            ow_heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (SUCCEEDED(g_s.device->CreateDescriptorHeap(&ow_heap, IID_PPV_ARGS(&g_ow_srv_heap))) &&
                g_ow.init(g_s.device, g_s.format, g_log)) {
                g_ow_table_gpu = g_ow_srv_heap->GetGPUDescriptorHandleForHeapStart();
                g_ow_initialized = true;
                Logf("overlay: OW gauntlet renderer ready (dedicated 3-slot SRV heap)");
            } else {
                Logf("overlay: OW gauntlet renderer unavailable");
            }
        }
#endif
        {
            // The first-person arm and the sprites of the default hotbar items.
            std::vector<SteveRenderer::FpItemCell> cells;
            const float aw = static_cast<float>(g_atlas_w), ah = static_cast<float>(g_atlas_h);
            std::vector<mc::ItemId> sprite_items = mc::paletteItems();
            for (mc::ItemId id : {mc::ItemId::BowPulling0, mc::ItemId::BowPulling1, mc::ItemId::BowPulling2}) sprite_items.push_back(id); // the drawn bow, never in a slot
            for (mc::ItemId id : sprite_items) {
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
    if (g_steve_cfg.depth_probe) probe::Install(g_s.list, g_log);
    return true;
}

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
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = g_s.srv_heap->GetCPUDescriptorHandleForHeapStart();
    cpu.ptr += static_cast<SIZE_T>(srv_inc) * 44;
    g_decal_gpu = g_s.srv_heap->GetGPUDescriptorHandleForHeapStart();
    g_decal_gpu.ptr += static_cast<UINT64>(srv_inc) * 44;
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

#if ENABLE_CS2_VIEWMODEL
// The muzzle flash is drawn in screen space at the gun's muzzle track: the viewmodel has its own projection,
// so projecting the track with the viewmodel matrix is the only position that stays on the barrel.
void DrawCsMuzzleFlash(ImDrawList* dl, float w, float h) {
    auto& mode = eldenring::RuntimeContext::Instance().mode();
    if (!mode.cs_mode.load() || mode.cs_weapon_slot.load() == 0) return;
    if (mode.cs_knife_state.load() != 2) return;
    const float t = mode.cs_knife_anim_time.load();
    if (t > 0.07f) return;

    float x = 0.f, y = 0.f;
    if (!g_cs.muzzleScreen(static_cast<unsigned>(w), static_cast<unsigned>(h), x, y)) return;
    const float fade = std::max(0.f, 1.f - t / 0.07f);
    const float r = 16.f * (0.55f + 0.45f * fade);
    dl->AddCircleFilled({x, y}, r, IM_COL32(255, 200, 110, static_cast<int>(190.f * fade)));
    dl->AddCircleFilled({x, y}, r * 0.45f, IM_COL32(255, 255, 235, static_cast<int>(235.f * fade)));
    for (int i = 0; i < 4; ++i) {
        const float a = 0.785398f * static_cast<float>(i) + 0.392699f;
        dl->AddLine({x, y}, {x + std::cos(a) * r * 1.7f, y + std::sin(a) * r * 1.7f},
                    IM_COL32(255, 210, 140, static_cast<int>(110.f * fade)), 2.f);
    }
}
#endif

// Tracers are 2D lines projected from world space: cheap, readable and independent of the scene depth buffer.
void DrawTracers(ImDrawList* dl, float w, float h) {
    if (!g_fx_valid) return;
    const uint64_t now = GetTickCount64();
    std::vector<TracerEvent> live;
    {
        std::lock_guard<std::mutex> g(g_tracer_mutex);
        for (auto it = g_tracer_events.begin(); it != g_tracer_events.end();) {
            const float age = static_cast<float>(now - it->start_ms) / 1000.f;
            if (age >= it->life_s) {
                it = g_tracer_events.erase(it);
            } else {
                live.push_back(*it);
                ++it;
            }
        }
    }
    for (const TracerEvent& t : live) {
        const float age = static_cast<float>(now - t.start_ms) / 1000.f;
        const float travel = std::max(1e-4f, t.life_s * 0.7f);
        const float u = std::clamp(age / travel, 0.f, 1.f);
        const float fade = age < travel
            ? 1.f
            : std::max(0.f, 1.f - (age - travel) / std::max(1e-4f, t.life_s - travel));
        if (fade <= 0.f) continue;

        float d[3] = {t.to[0] - t.from[0], t.to[1] - t.from[1], t.to[2] - t.from[2]};
        const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (!(dist > 1e-3f)) continue;
        for (int i = 0; i < 3; ++i) d[i] /= dist;

        // A short streak flies from the muzzle to the impact, not a line across the whole path. The tail is
        // long enough to read at a distance, and the core is drawn over a soft glow.
        const float head = u * dist;
        const float tail = std::max(0.f, head - std::min(10.f, dist));
        float hp[3], tp[3];
        for (int i = 0; i < 3; ++i) {
            hp[i] = t.from[i] + d[i] * head;
            tp[i] = t.from[i] + d[i] * tail;
        }
        ImVec2 a, b;
        float ppm = 0.f;
        if (!ProjectToScreen(g_fx_vp, tp, w, h, g_fx_fov, a, ppm)) continue;
        if (!ProjectToScreen(g_fx_vp, hp, w, h, g_fx_fov, b, ppm)) continue;
        dl->AddLine(a, b, IM_COL32(255, 196, 96, static_cast<int>(70.f * fade)), 6.f);
        dl->AddLine(a, b, IM_COL32(255, 238, 180, static_cast<int>(235.f * fade)), 2.5f);
        dl->AddCircleFilled(b, 2.5f, IM_COL32(255, 250, 225, static_cast<int>(220.f * fade)), 8);
    }
}

#if ENABLE_CS2_VIEWMODEL
// Bullet holes on floors and walls. The quad lies in the surface plane (built from the Havok normal), so a
// floor hit reads as a flat mark rather than a screen-facing sprite. The last fifth of the lifetime fades.
void DrawDecals(ImDrawList* dl, float w, float h) {
    if (!g_fx_valid) return;
    const uint64_t now = GetTickCount64();
    std::vector<eldenring::cs::ImpactDecal> live;
    {
        std::lock_guard<std::mutex> g(g_decal_mutex);
        g_decal_ring.expire(now);
        live.reserve(g_decal_ring.size());
        for (size_t i = 0; i < g_decal_ring.size(); ++i) live.push_back(g_decal_ring.at(i));
    }
    for (const eldenring::cs::ImpactDecal& d : live) {
        const float age = static_cast<float>(now - d.start_ms) / 1000.f;
        const float fade = eldenring::cs::decalFade(age, d.life_s);
        if (fade <= 0.f) continue;

        float tangent[3], bitangent[3];
        eldenring::cs::decalTangentBasis(d.normal, tangent, bitangent);
        const float half = 0.08f; // 16 cm across; the authentic texture's hole fills most of it
        const float lift = 0.015f;
        const float center[3] = {d.pos[0] + d.normal[0] * lift, d.pos[1] + d.normal[1] * lift,
                                 d.pos[2] + d.normal[2] * lift};
        ImVec2 q[4];
        float ppm = 0.f;
        bool visible = true;
        for (int i = 0; i < 4 && visible; ++i) {
            const float sx = (i == 0 || i == 3) ? -1.f : 1.f;
            const float sy = (i < 2) ? -1.f : 1.f;
            const float p[3] = {
                center[0] + tangent[0] * half * sx + bitangent[0] * half * sy,
                center[1] + tangent[1] * half * sx + bitangent[1] * half * sy,
                center[2] + tangent[2] * half * sx + bitangent[2] * half * sy,
            };
            visible = ProjectToScreen(g_fx_vp, p, w, h, g_fx_fov, q[i], ppm);
        }
        if (!visible) continue;

        const ImVec2 c{(q[0].x + q[1].x + q[2].x + q[3].x) * 0.25f,
                       (q[0].y + q[1].y + q[2].y + q[3].y) * 0.25f};
        const float span = std::max(std::fabs(q[0].x - q[2].x), std::fabs(q[0].y - q[2].y));
        if (g_decal_ready) {
            // Authentic CS2 concrete bullet-hole decal, extracted from the user's own VPK.
            const ImTextureID tex = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_decal_gpu.ptr));
            const ImU32 col = IM_COL32(225, 225, 225, static_cast<int>(255.f * fade));
            if (span < 4.f) {
                // Too far / too grazing to resolve the quad: draw the texture at a minimum readable size.
                const float s = std::max(4.f, span * 1.5f);
                dl->AddImage(tex, ImVec2{c.x - s, c.y - s}, ImVec2{c.x + s, c.y + s},
                             ImVec2{0.f, 0.f}, ImVec2{1.f, 1.f}, col);
            } else {
                dl->AddImageQuad(tex, q[0], q[1], q[2], q[3], ImVec2{0.f, 0.f}, ImVec2{1.f, 0.f},
                                 ImVec2{1.f, 1.f}, ImVec2{0.f, 1.f}, col);
            }
        } else if (span < 4.f) {
            // Procedural fallback when the extracted CS2 texture is unavailable.
            dl->AddCircleFilled(c, 4.f, IM_COL32(190, 178, 155, static_cast<int>(85.f * fade)), 8);
            dl->AddCircleFilled(c, 2.5f, IM_COL32(12, 10, 9, static_cast<int>(235.f * fade)), 8);
        } else {
            const auto shrink = [&](float s, ImVec2 out[4]) {
                for (int i = 0; i < 4; ++i) out[i] = ImVec2{c.x + (q[i].x - c.x) * s, c.y + (q[i].y - c.y) * s};
            };
            // Dust ring, scorch, then the dark hole.
            dl->AddQuadFilled(q[0], q[1], q[2], q[3], IM_COL32(190, 178, 155, static_cast<int>(80.f * fade)));
            ImVec2 scorch[4];
            shrink(0.62f, scorch);
            dl->AddQuadFilled(scorch[0], scorch[1], scorch[2], scorch[3],
                              IM_COL32(48, 40, 32, static_cast<int>(150.f * fade)));
            ImVec2 hole[4];
            shrink(0.30f, hole);
            dl->AddQuadFilled(hole[0], hole[1], hole[2], hole[3],
                              IM_COL32(12, 10, 9, static_cast<int>(230.f * fade)));
            for (int i = 0; i < 4; ++i) {
                const ImVec2 e{c.x + (q[i].x - c.x) * 0.9f, c.y + (q[i].y - c.y) * 0.9f};
                dl->AddLine(c, e, IM_COL32(32, 27, 22, static_cast<int>(95.f * fade)), 1.f);
            }
        }
    }
}
#endif

void DrawFx(ImDrawList* dl, ImTextureID atlas, float w, float h) {
    if (!g_fx_valid || !g_atlas_ready) return;
    for (const eldenring::fx::Particle& p : g_particles.alive()) {
        ImVec2 s;
        float ppm = 0.f;
        if (!ProjectToScreen(g_fx_vp, p.pos, w, h, g_fx_fov, s, ppm)) continue;
        const float half = std::max(2.f, p.size * ppm * 0.5f);
        const float fade = eldenring::fx::ParticleSystem::fade(p);
        if (p.kind == eldenring::fx::Kind::Spark) {
            dl->AddCircleFilled(s, std::max(1.f, half), IM_COL32(255, 220, 140, static_cast<int>(255 * fade)), 6);
            continue;
        }
        if (p.kind == eldenring::fx::Kind::Dust) {
            dl->AddCircleFilled(s, std::max(1.5f, half), IM_COL32(175, 165, 148, static_cast<int>(65 * fade)), 8);
            dl->AddCircleFilled(s, std::max(1.f, half * 0.55f), IM_COL32(205, 195, 178, static_cast<int>(55 * fade)), 8);
            continue;
        }
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

// An item's icon in a slot: blocks are drawn as the cube Minecraft shows (three textured faces, see eldenring_blockicon.hpp), everything
// else as its flat sprite.
void DrawItemIcon(ImDrawList* dl, ImTextureID atlas, const mc::HudRect& r, mc::ItemId item) {
    const mc::BlockId block = eldenring::blocks::blockForItem(item);
    if (block != mc::BlockId::Air) {
        const eldenring::blocks::IconCube icon = eldenring::blocks::blockIcon(block);
        for (int i = 0; i < icon.count; ++i) {
            const eldenring::blocks::IconFace& f = icon.faces[i];
            const ImVec2 q[4] = {{r.x + f.p[0][0] * r.w, r.y + f.p[0][1] * r.h}, {r.x + f.p[1][0] * r.w, r.y + f.p[1][1] * r.h},
                                 {r.x + f.p[2][0] * r.w, r.y + f.p[2][1] * r.h}, {r.x + f.p[3][0] * r.w, r.y + f.p[3][1] * r.h}};
            const int c = static_cast<int>(255.f * f.shade);
            dl->AddImageQuad(atlas, q[0], q[1], q[2], q[3], {f.uv.u0, f.uv.v0}, {f.uv.u1, f.uv.v0}, {f.uv.u1, f.uv.v1}, {f.uv.u0, f.uv.v1}, IM_COL32(c, c, c, 255));
        }
        return;
    }
    if (const mc::hud::HudUV* uv = eldenring::render::uvForItem(item)) dl->AddImage(atlas, {r.x, r.y}, {r.x + r.w, r.y + r.h}, {uv->u0, uv->v0}, {uv->u1, uv->v1});
}

// The inventory screen: Minecraft's 3-row container (item palette on top, the player's slots below), the stack on the cursor,
// a tooltip and our own mouse pointer.
void DrawInventory(const HudState& hud, float w, float h) {
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    const ImTextureID atlas = reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(g_atlas_gpu.ptr));
    dl->AddRectFilled({0.f, 0.f}, {w, h}, IM_COL32(16, 16, 16, 150)); // Minecraft dims the world behind a screen
    const mc::InventoryLayout lay(w, h, hud.creative);
    const mc::HudRect p = lay.panel();
    const float gs = static_cast<float>(lay.scale());
    const float fs = 8.f * gs;
    const ImU32 label = IM_COL32(64, 64, 64, 255);

    auto drawSlotBox = [&](const mc::HudRect& r) {
        dl->AddLine({r.x, r.y}, {r.x + r.w, r.y}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({r.x, r.y}, {r.x, r.y + r.h}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({r.x, r.y + r.h}, {r.x + r.w, r.y + r.h}, IM_COL32(255, 255, 255, 200), gs);
        dl->AddLine({r.x + r.w, r.y}, {r.x + r.w, r.y + r.h}, IM_COL32(255, 255, 255, 200), gs);
        dl->AddRectFilled({r.x + gs, r.y + gs}, {r.x + r.w - gs, r.y + r.h - gs}, IM_COL32(139, 139, 139, 255));
    };

    auto stackCount = [&](const mc::HudRect& r, unsigned n) {
        if (n <= 1) return;
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%u", n);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
        const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
        dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
        dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
    };
    auto icon = [&](const mc::HudRect& r, mc::ItemId item) { DrawItemIcon(dl, atlas, r, item); };

    // 1. Draw Container Base Panel (176 x 166)
    dl->AddRectFilled({p.x, p.y}, {p.x + p.w, p.y + p.h}, IM_COL32(198, 198, 198, 255));
    dl->AddLine({p.x, p.y}, {p.x + p.w, p.y}, IM_COL32(255, 255, 255, 255), 2.f * gs);
    dl->AddLine({p.x, p.y}, {p.x, p.y + p.h}, IM_COL32(255, 255, 255, 255), 2.f * gs);
    dl->AddLine({p.x, p.y + p.h}, {p.x + p.w, p.y + p.h}, IM_COL32(85, 85, 85, 255), 2.f * gs);
    dl->AddLine({p.x + p.w, p.y}, {p.x + p.w, p.y + p.h}, IM_COL32(85, 85, 85, 255), 2.f * gs);

    dl->AddText(ImGui::GetFont(), fs, {p.x, p.y - 10.f * gs}, IM_COL32(255, 255, 255, 230), hud.creative ? "Creative Mode" : "Survival Mode");
    if (!hud.creative && hud.armor_points > 0) {
        char arm_label[48];
        const float red = std::min(80.f, static_cast<float>(hud.armor_points) * 4.0f);
        snprintf(arm_label, sizeof(arm_label), "Armor: %d (-%.0f%%)", hud.armor_points, red);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, arm_label);
        const ImVec2 pos{p.x + p.w - sz.x - 2.f * gs, p.y - 10.f * gs};
        dl->AddText(ImGui::GetFont(), fs, {pos.x + 1.f, pos.y + 1.f}, IM_COL32(0, 0, 0, 200), arm_label);
        dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(85, 255, 255, 255), arm_label);
    }

    const auto& palette = mc::paletteItems();

    if (hud.creative) {
        // Creative mode palette on top
        dl->AddText(ImGui::GetFont(), fs, {lay.titleItems().x, lay.titleItems().y}, label, "Items");
        for (int i = 0; i < mc::InventoryLayout::kPaletteSlots; ++i) {
            drawSlotBox(lay.paletteSlot(i));
        }
        for (size_t i = 0; i < palette.size() && i < mc::InventoryLayout::kPaletteSlots; ++i) {
            icon(lay.paletteSlot(static_cast<int>(i)), palette[i]);
        }
    } else {
        // Vanilla Minecraft Survival layout:
        // Central 3D Player Preview Window (26, 8, 51, 72)
        const mc::HudRect prev = lay.playerPreview();
        dl->AddRectFilled({prev.x, prev.y}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(16, 16, 16, 255));
        dl->AddLine({prev.x, prev.y}, {prev.x + prev.w, prev.y}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({prev.x, prev.y}, {prev.x, prev.y + prev.h}, IM_COL32(55, 55, 55, 255), gs);
        dl->AddLine({prev.x, prev.y + prev.h}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(255, 255, 255, 180), gs);
        dl->AddLine({prev.x + prev.w, prev.y}, {prev.x + prev.w, prev.y + prev.h}, IM_COL32(255, 255, 255, 180), gs);

        // Queue real 3D Steve character rendering inside the preview window
        g_inv_preview.active = true;
        g_inv_preview.rect = prev;
        g_inv_preview.mouse_x = hud.mouse_x;
        g_inv_preview.mouse_y = hud.mouse_y;
        g_inv_preview.scale = gs;
        const int sel = std::clamp(hud.selected_slot, 0, 8);
        g_inv_preview.held_item = static_cast<uint16_t>(hud.hotbar[sel]);
        g_inv_preview.offhand_item = static_cast<uint16_t>(hud.offhand_item);

        dl->AddCallback([](const ImDrawList*, const ImDrawCmd*) {
            if (!g_inv_preview.active || !g_steve.ready() || g_current_rtv.ptr == 0) return;
            const float cx = g_inv_preview.rect.x + g_inv_preview.rect.w * 0.5f;
            const float cy = g_inv_preview.rect.y + g_inv_preview.rect.h * 0.5f;
            float look_yaw = 0.f, look_pitch = 0.f, body_yaw = 0.f;
            eldenring::preview::computePreviewTracking(g_inv_preview.mouse_x, g_inv_preview.mouse_y, cx, cy,
                                                       g_inv_preview.scale, look_yaw, look_pitch, body_yaw);
            const auto parts = eldenring::preview::computePreviewPartMatrices(look_yaw, look_pitch, body_yaw);
            const mc::rig::Mat4 vp = eldenring::preview::computePreviewViewProjection(g_inv_preview.rect.w / g_inv_preview.rect.h);

            const D3D12_VIEWPORT pvp{g_inv_preview.rect.x, g_inv_preview.rect.y, g_inv_preview.rect.w, g_inv_preview.rect.h, 0.f, 1.f};
            const D3D12_RECT psc{static_cast<LONG>(g_inv_preview.rect.x), static_cast<LONG>(g_inv_preview.rect.y),
                                static_cast<LONG>(g_inv_preview.rect.x + g_inv_preview.rect.w),
                                static_cast<LONG>(g_inv_preview.rect.y + g_inv_preview.rect.h)};

            SteveParams sp;
            sp.mode = 0.f; // 0: no scene occlusion
            sp.depth_const = 0.0501f;
            sp.rel_bias = 0.f;
            sp.abs_bias = 0.f;
            sp.tint[0] = sp.tint[1] = sp.tint[2] = sp.tint[3] = 0.f;
            sp.keep_depth = false;
            sp.held_item = g_inv_preview.held_item;
            sp.offhand_item = g_inv_preview.offhand_item;
            sp.shield_blocking = false;
            sp.block_offhand = false;
            sp.held_table = g_atlas_ready ? g_held_table_gpu : D3D12_GPU_DESCRIPTOR_HANDLE{};

            g_steve.drawInventoryPreview(g_s.list, g_s.srv_heap, g_depth_gpu, pvp, psc, vp, parts, sp, g_current_rtv);
        }, nullptr);
        dl->AddCallback(ImDrawCallback_ResetRenderState, nullptr);

        // 2x2 Crafting grid and result
        dl->AddText(ImGui::GetFont(), fs, {lay.titleCrafting().x, lay.titleCrafting().y}, label, "Crafting");
        for (int i = 0; i < 4; ++i) drawSlotBox(lay.craftSlot(i));
        drawSlotBox(lay.craftResultSlot());
        // Crafting arrow
        dl->AddLine({p.x + 135.f * gs, p.y + 37.f * gs}, {p.x + 148.f * gs, p.y + 37.f * gs}, IM_COL32(139, 139, 139, 255), 2.f * gs);
        dl->AddTriangleFilled({p.x + 148.f * gs, p.y + 33.f * gs}, {p.x + 148.f * gs, p.y + 41.f * gs}, {p.x + 152.f * gs, p.y + 37.f * gs}, IM_COL32(139, 139, 139, 255));

        // 2. 4 Armor slots on the left (x = 8, y = 8, 26, 44, 62)
        for (int i = 0; i < 4; ++i) {
            const mc::HudRect arm = lay.armorSlot(i);
            drawSlotBox(arm);
            const mc::HudRect arm_item{arm.x + 1.f * gs, arm.y + 1.f * gs, 16.f * gs, 16.f * gs};
            if (hud.armor_item[i] == 0) {
                if (g_atlas_ready) {
                    if (const mc::hud::HudUV* wuv = eldenring::render::uvForEmptyArmorSlot(static_cast<mc::ArmorSlot>(i))) {
                        dl->AddImage(atlas, {arm_item.x, arm_item.y}, {arm_item.x + arm_item.w, arm_item.y + arm_item.h},
                                     {wuv->u0, wuv->v0}, {wuv->u1, wuv->v1}, IM_COL32(255, 255, 255, 90));
                    }
                }
            } else {
                icon(arm_item, static_cast<mc::ItemId>(hud.armor_item[i]));
                stackCount(arm_item, hud.armor_count[i]);
            }
        }

        // 3. Offhand slot in inventory (x = 77, y = 62)
        const mc::HudRect off = lay.offhandSlot();
        drawSlotBox(off);
        const mc::HudRect off_item{off.x + 1.f * gs, off.y + 1.f * gs, 16.f * gs, 16.f * gs};
        if (hud.offhand_item == 0) {
            if (g_atlas_ready) {
                const mc::hud::HudUV& suv = mc::hud::kUV_ITEM_SHIELD;
                dl->AddImage(atlas, {off_item.x, off_item.y}, {off_item.x + off_item.w, off_item.y + off_item.h},
                             {suv.u0, suv.v0}, {suv.u1, suv.v1}, IM_COL32(255, 255, 255, 90));
            }
        } else {
            icon(off_item, static_cast<mc::ItemId>(hud.offhand_item));
            stackCount(off_item, hud.offhand_count);
        }
    }

    dl->AddText(ImGui::GetFont(), fs, {lay.titleInventory().x, lay.titleInventory().y}, label, "Inventory");

    // 4. Main Inventory (3x9) and Hotbar (1x9)
    for (int i = 0; i < 36; ++i) {
        const mc::HudRect r = lay.invSlot(i);
        drawSlotBox(r);
        const mc::ItemId item = static_cast<mc::ItemId>(hud.inv_item[i]);
        if (item != mc::ItemId::None) {
            const mc::HudRect item_r{r.x + 1.f * gs, r.y + 1.f * gs, 16.f * gs, 16.f * gs};
            icon(item_r, item);
            stackCount(item_r, hud.inv_count[i]);
        }
    }

    // 5. Hover: slot highlight and tooltip
    const mc::SlotRef hover = lay.hitTest(hud.mouse_x, hud.mouse_y);
    mc::ItemId hovered = mc::ItemId::None;
    mc::HudRect hover_r{};
    bool show_hover = false;
    if (hover.kind == mc::SlotRef::Kind::Palette && static_cast<size_t>(hover.index) < palette.size()) {
        hovered = palette[static_cast<size_t>(hover.index)];
        hover_r = lay.paletteSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Inventory && hover.index >= 0 && hover.index < 36) {
        hovered = static_cast<mc::ItemId>(hud.inv_item[hover.index]);
        hover_r = lay.invSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Offhand) {
        hovered = static_cast<mc::ItemId>(hud.offhand_item);
        hover_r = lay.offhandSlot();
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Armor && hover.index >= 0 && hover.index < 4) {
        hovered = static_cast<mc::ItemId>(hud.armor_item[hover.index]);
        hover_r = lay.armorSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::Crafting && hover.index >= 0 && hover.index < 4) {
        hover_r = lay.craftSlot(hover.index);
        show_hover = true;
    } else if (hover.kind == mc::SlotRef::Kind::CraftResult) {
        hover_r = lay.craftResultSlot();
        show_hover = true;
    }
    if (show_hover) {
        dl->AddRectFilled({hover_r.x, hover_r.y}, {hover_r.x + hover_r.w, hover_r.y + hover_r.h}, IM_COL32(255, 255, 255, 128));
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

eldenring::live::HeartAnimator g_hearts;

void DrawHud(const HudState& hud, float w, float h) {
    if (hud.ow_mode) {
        // Overwatch HUD: crosshair + ability legend. The MC hearts/hotbar must
        // not leak into OW mode (the CS branch returns for the same reason).
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float cx = w * 0.5f, cy = h * 0.5f;
        const float s = std::max(0.5f, h / 1080.f);
        const ImU32 cyan = IM_COL32(120, 255, 220, 235);
        const ImU32 shadow = IM_COL32(0, 0, 0, 190);
        dl->AddCircleFilled({cx, cy}, 1.8f * s, shadow);
        dl->AddCircleFilled({cx, cy}, 1.2f * s, cyan);
        const float gap = 7.f * s, len = 9.f * s, th = 2.f * s;
        auto tick = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine({x0, y0}, {x1, y1}, shadow, th + 2.f * s);
            dl->AddLine({x0, y0}, {x1, y1}, cyan, th);
        };
        tick(cx - gap - len, cy, cx - gap, cy);
        tick(cx + gap, cy, cx + gap + len, cy);
        tick(cx, cy - gap - len, cx, cy - gap);
        tick(cx, cy + gap, cx, cy + gap + len);

        // Hit/kill feedback from the DamageQueue: the same diagonal ticks the
        // CS branch uses. Without this an OW punch deals damage but the screen
        // gives no confirmation.
        if (hud.hit > 0.f) {
            const float a = std::clamp(hud.hit, 0.f, 1.f);
            const float hit_gap = (hud.hit_crit ? 8.f : 6.f) * s, hit_len = (hud.hit_crit ? 14.f : 10.f) * s;
            const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a))
                                           : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
            const float d = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * hit_gap * d, cy + sy * hit_gap * d},
                                {cx + sx * (hit_gap + hit_len) * d, cy + sy * (hit_gap + hit_len) * d},
                                col, 3.f * s);
                }
            }
        }
        if (hud.kill > 0.f) {
            const float a = std::clamp(hud.kill, 0.f, 1.f);
            const float kill_gap = 11.f * s, kill_len = 18.f * s;
            const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
            const float d = 0.70710678f;
            dl->AddLine({cx - kill_gap * d, cy - kill_gap * d}, {cx + kill_len * d, cy + kill_len * d}, col, 3.f * s);
            dl->AddLine({cx + kill_gap * d, cy - kill_gap * d}, {cx - kill_len * d, cy + kill_len * d}, col, 3.f * s);
        }

        const char* legend[4] = {"[LMB] Hand Cannon", "[RMB] Rocket Punch", "[Q] Seismic Slam", "[E] Rising Uppercut"};
        const float fs = 15.f * s;
        for (int i = 0; i < 4; ++i) {
            const float px = 24.f * s;
            const float py = h - (110.f - i * 24.f) * s;
            dl->AddText(ImGui::GetFont(), fs, {px + s, py + s}, IM_COL32(0, 0, 0, 200), legend[i]);
            dl->AddText(ImGui::GetFont(), fs, {px, py}, cyan, legend[i]);
        }
        return;
    }
#if ENABLE_CS2_VIEWMODEL
    if (hud.cs_mode) {
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        const float cx = w * 0.5f, cy = h * 0.5f;
        const ImU32 green = IM_COL32(0, 255, 120, 240);
        const ImU32 shadow = IM_COL32(0, 0, 0, 200);
        const float gap = 5.f, len = 9.f, th = 2.f;

        // Center dot
        dl->AddRectFilled({cx - 1.f, cy - 1.f}, {cx + 2.f, cy + 2.f}, shadow);
        dl->AddRectFilled({cx, cy}, {cx + 1.f, cy + 1.f}, green);

        auto line = [&](float x0, float y0, float x1, float y1) {
            dl->AddLine({x0, y0}, {x1, y1}, shadow, th + 2.f);
            dl->AddLine({x0, y0}, {x1, y1}, green, th);
        };
        line(cx - gap - len, cy, cx - gap, cy);
        line(cx + gap, cy, cx + gap + len, cy);
        line(cx, cy - gap - len, cx, cy - gap);
        line(cx, cy + gap, cx, cy + gap + len);

        // Hit/kill feedback from the DamageQueue: four diagonal ticks, a red X on a kill.
        const float hs = std::max(0.5f, h / 1080.f);
        if (hud.hit > 0.f) {
            const float a = std::clamp(hud.hit, 0.f, 1.f);
            const float hit_gap = (hud.hit_crit ? 7.f : 5.f) * hs, hit_len = (hud.hit_crit ? 12.f : 8.f) * hs;
            const ImU32 col = hud.hit_crit ? IM_COL32(255, 170, 0, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a));
            const float s = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * hit_gap * s, cy + sy * hit_gap * s}, {cx + sx * (hit_gap + hit_len) * s, cy + sy * (hit_gap + hit_len) * s}, col,
                                2.5f * hs);
                }
            }
        }
        if (hud.kill > 0.f) {
            const float a = std::clamp(hud.kill, 0.f, 1.f);
            const float kill_gap = 9.f * hs, kill_len = 16.f * hs;
            const ImU32 col = IM_COL32(225, 40, 40, static_cast<int>(255 * a));
            const float s = 0.70710678f;
            for (int sx = -1; sx <= 1; sx += 2) {
                for (int sy = -1; sy <= 1; sy += 2) {
                    dl->AddLine({cx + sx * kill_gap * s, cy + sy * kill_gap * s}, {cx + sx * (kill_gap + kill_len) * s, cy + sy * (kill_gap + kill_len) * s}, col,
                                3.5f * hs);
                }
            }
        }

        // Weapon Card in bottom right
        const float card_w = 270.f, card_h = 105.f;
        const float x = w - card_w - 25.f, y = h - card_h - 25.f;
        dl->AddRectFilled({x, y}, {x + card_w, y + card_h}, IM_COL32(12, 18, 26, 220), 6.f);
        dl->AddRect({x, y}, {x + card_w, y + card_h}, IM_COL32(0, 255, 140, 230), 6.f, 0, 2.0f);

        auto& r_mode = eldenring::RuntimeContext::Instance().mode();
        const uint8_t state = r_mode.cs_knife_state.load();
        const uint8_t slot = r_mode.cs_weapon_slot.load();
        const bool is_gun = slot != 0;
        const char* weapon_name = (slot == 1) ? "AK-47 (CS2)"
                                : (slot == 2) ? "AWP (CS2)"
                                : (slot == 3) ? "M4A1-S (CS2)"
                                : "BUTTERFLY KNIFE (CS2)";
        const char* status_str = (state == 1) ? "[INSPECTING...]" :
                                 (state == 2) ? (is_gun ? "[FIRING...]" : "[LIGHT ATTACK]") :
                                 (state == 3) ? (is_gun ? "[SECONDARY]" : "[HEAVY ATTACK]") :
                                 (state == 4) ? "[DEPLOYING...]" :
                                 (state == 5) ? "[RELOADING...]" :
                                 (state == 6) ? "[BACKSTAB]" : "[READY]";
        dl->AddText(ImGui::GetFont(), 19.f, {x + 14.f, y + 10.f}, IM_COL32(0, 255, 150, 255), weapon_name);
        dl->AddText(ImGui::GetFont(), 15.f, {x + 14.f, y + 36.f}, IM_COL32(255, 215, 0, 255), status_str);
        if (is_gun) {
            char ammo_str[64];
            snprintf(ammo_str, sizeof(ammo_str), "%u / %u",
                     static_cast<unsigned>(r_mode.cs_ammo.load()),
                     static_cast<unsigned>(r_mode.cs_reserve.load()));
            dl->AddText(ImGui::GetFont(), 22.f, {x + card_w - 96.f, y + 10.f}, IM_COL32(255, 255, 255, 245), ammo_str);
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 60.f}, IM_COL32(200, 210, 225, 230), "[F] Inspect | [LMB] Fire | [R] Reload");
            dl->AddText(ImGui::GetFont(), 12.f, {x + 14.f, y + 80.f}, IM_COL32(160, 175, 195, 210), "[1]AK [2]AWP [3]Knife [4]M4 | [F1] Toggle");
        } else {
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 60.f}, IM_COL32(200, 210, 225, 230), "[F] Spin Inspect  |  [LMB] Slash");
            dl->AddText(ImGui::GetFont(), 13.f, {x + 14.f, y + 80.f}, IM_COL32(160, 175, 195, 210), "[RMB] Heavy Attack  |  [F1] Mode Toggle");
        }
        return;
    }
#endif
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
    if (g_atlas_ready) {
        // Real Minecraft sprites at vanilla positions (mc::HudLayout), the Minecraft GUI scale of this resolution.
        sprite(layout.hotbar(), mc::hud::kUV_HOTBAR);
        for (int i = 0; i < 9; ++i) DrawItemIcon(dl, atlas, layout.item(i), static_cast<mc::ItemId>(hud.hotbar[i]));
        sprite(layout.selection(std::clamp(hud.selected_slot, 0, 8)), mc::hud::kUV_HOTBAR_SELECTION);
        // Offhand slot (to the left of hotbar, vanilla hotbar_offhand_left sprite)
        const float gs = static_cast<float>(layout.scale());
        if (hud.offhand_item != 0) {
            sprite(layout.offhand(), mc::hud::kUV_HOTBAR_OFFHAND_LEFT);
            DrawItemIcon(dl, atlas, layout.offhandItem(), static_cast<mc::ItemId>(hud.offhand_item));
            if (hud.offhand_count > 1) {
                char cnt[8];
                snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(hud.offhand_count));
                const float fs = layout.textSize();
                const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
                const mc::HudRect r = layout.offhandItem();
                const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
                dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
                dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
            }
        }
        // The hearts as Gui.renderHearts draws them: the container blinks after a hit and the hearts just lost show white, one heart jumps
        // while Regeneration is active, and the row shakes at two hearts or less.
        const int abs_halves = static_cast<int>(std::lround(hud.absorption_mc));
        g_hearts.update(ImGui::GetIO().DeltaTime, hp_halves, abs_halves, hud.regen);
        const bool blink = g_hearts.blink();
        const int shown_before = g_hearts.displayHealth();
        for (int i = 0; !hud.creative && i < 10; ++i) { // creative mode shows no health, hunger or experience
            mc::HudRect r = layout.heart(i);
            if (g_hearts.regenIndex() == i) r.y -= 2.f * gs;
            r.y += static_cast<float>(g_hearts.shake(i)) * gs;
            sprite(r, blink ? mc::hud::kUV_HEART_CONTAINER_BLINKING : mc::hud::kUV_HEART_CONTAINER);
            if (blink) {
                if (2 * i + 1 < shown_before) sprite(r, mc::hud::kUV_HEART_FULL_BLINKING);
                else if (2 * i + 1 == shown_before) sprite(r, mc::hud::kUV_HEART_HALF_BLINKING);
            }
            if (2 * i + 1 < hp_halves) sprite(r, mc::hud::kUV_HEART_FULL);
            else if (2 * i + 1 == hp_halves) sprite(r, mc::hud::kUV_HEART_HALF);
        }
        // hunger: ten icons from the right, shaking once the saturation is used up
        if (!hud.creative) {
            static uint32_t food_rng = 7u;
            static float food_clock = 0.f;
            static int food_shake[10] = {};
            food_clock += ImGui::GetIO().DeltaTime;
            if (food_clock >= 0.05f) {
                food_clock = 0.f;
                for (int& v : food_shake) {
                    food_rng = food_rng * 1664525u + 1013904223u;
                    v = static_cast<int>((food_rng >> 16) % 3u) - 1;
                }
            }
            for (int i = 0; i < 10; ++i) {
                mc::HudRect r = layout.food(i);
                if (hud.food_shaking) r.y += static_cast<float>(food_shake[i]) * gs;
                sprite(r, mc::hud::kUV_HUNGER_CONTAINER);
                if (2 * i + 1 < hud.food) sprite(r, mc::hud::kUV_HUNGER_FULL);
                else if (2 * i + 1 == hud.food) sprite(r, mc::hud::kUV_HUNGER_HALF);
            }
        }
        // experience bar and level number
        if (!hud.creative) {
            const mc::HudRect bar = layout.xpBar();
            sprite(bar, mc::hud::kUV_XP_BAR_BACKGROUND);
            const int px = std::clamp(static_cast<int>(hud.xp_progress * 183.f), 0, 182);
            if (px > 0) {
                const mc::hud::HudUV& uv = mc::hud::kUV_XP_BAR_PROGRESS;
                const float frac = static_cast<float>(px) / 182.f;
                dl->AddImage(atlas, {bar.x, bar.y}, {bar.x + bar.w * frac, bar.y + bar.h}, {uv.u0, uv.v0}, {uv.u0 + (uv.u1 - uv.u0) * frac, uv.v1});
            }
            if (hud.xp_level > 0) {
                char lvl[8];
                snprintf(lvl, sizeof(lvl), "%d", hud.xp_level);
                const float fs = layout.textSize();
                const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, lvl);
                const float tx = layout.xpLevelCentreX() - sz.x * 0.5f, ty = layout.xpLevelTextTop();
                for (const ImVec2 o : {ImVec2{gs, 0.f}, ImVec2{-gs, 0.f}, ImVec2{0.f, gs}, ImVec2{0.f, -gs}}) {
                    dl->AddText(ImGui::GetFont(), fs, {tx + o.x, ty + o.y}, IM_COL32(0, 0, 0, 255), lvl); // the black outline
                }
                dl->AddText(ImGui::GetFont(), fs, {tx, ty}, IM_COL32(0x80, 0xFF, 0x20, 255), lvl);
            }
        }
        // stack sizes (Minecraft: bottom right of the icon, white with a dark shadow, only above 1)
        for (int i = 0; i < 9; ++i) {
            if (hud.hotbar_count[i] <= 1 || hud.hotbar[i] == 0) continue;
            char cnt[8];
            snprintf(cnt, sizeof(cnt), "%u", static_cast<unsigned>(hud.hotbar_count[i]));
            const float fs = layout.textSize();
            const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, cnt);
            const mc::HudRect r = layout.item(i);
            const ImVec2 pos{r.x + 17.f * gs - sz.x, r.y + 9.f * gs};
            dl->AddText(ImGui::GetFont(), fs, {pos.x + gs, pos.y + gs}, IM_COL32(40, 40, 40, 255), cnt);
            dl->AddText(ImGui::GetFont(), fs, pos, IM_COL32(255, 255, 255, 255), cnt);
        }
        // absorption (golden apple, totem): golden hearts in a row above the health, 2 Minecraft HP per heart
        if (!hud.creative) {
            const int full = static_cast<int>(std::floor(hud.absorption_mc / 2.f));
            const bool half = hud.absorption_mc - 2.f * static_cast<float>(full) >= 1.f;
            for (int i = 0; i < std::min(10, full + (half ? 1 : 0)); ++i) {
                mc::HudRect r = layout.heart(i);
                r.y -= 10.f * static_cast<float>(layout.scale());
                sprite(r, mc::hud::kUV_HEART_CONTAINER);
                sprite(r, i < full ? mc::hud::kUV_HEART_ABSORB_FULL : mc::hud::kUV_HEART_ABSORB_HALF);
            }
        }
        // armor bar: 10 chestplate icons in a row above the hearts (or above golden absorption hearts)
        if (!hud.creative && hud.armor_points > 0) {
            const bool has_absorb = hud.absorption_mc > 0.f;
            for (int i = 0; i < 10; ++i) {
                const mc::HudRect r = layout.armor(i, has_absorb);
                sprite(r, mc::hud::kUV_ARMOR_EMPTY);
                if (2 * i + 1 < hud.armor_points) {
                    sprite(r, mc::hud::kUV_ARMOR_FULL);
                } else if (2 * i + 1 == hud.armor_points) {
                    sprite(r, mc::hud::kUV_ARMOR_HALF);
                }
            }
        }
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
    for (int i = 0; i < 10 && !g_atlas_ready && !hud.creative; ++i) {
        const float x = x0 + static_cast<float>(i) * (cell + gap);
        dl->AddRectFilled({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(40, 0, 0, 200));
        const int fill = std::clamp(halves - i * 2, 0, 2);
        if (fill >= 1) {
            const float fw = fill == 2 ? cell : cell * 0.5f;
            dl->AddRectFilled({x, y_hearts}, {x + fw, y_hearts + cell}, IM_COL32(205, 30, 30, 255));
        }
        dl->AddRect({x, y_hearts}, {x + cell, y_hearts + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
    }
    // armor bar fallback (drawn above hearts when atlas is not present)
    if (!g_atlas_ready && !hud.creative && hud.armor_points > 0) {
        const bool has_absorb = hud.absorption_mc > 0.f;
        const float y_armor = y_hearts - (has_absorb ? 2.f : 1.f) * (cell + 4.f * scale);
        for (int i = 0; i < 10; ++i) {
            const float x = x0 + static_cast<float>(i) * (cell + gap);
            dl->AddRectFilled({x, y_armor}, {x + cell, y_armor + cell}, IM_COL32(35, 35, 35, 200));
            const int fill = std::clamp(hud.armor_points - i * 2, 0, 2);
            if (fill >= 1) {
                const float fw = fill == 2 ? cell : cell * 0.5f;
                dl->AddRectFilled({x, y_armor}, {x + fw, y_armor + cell}, IM_COL32(180, 200, 220, 255));
            }
            dl->AddRect({x, y_armor}, {x + cell, y_armor + cell}, IM_COL32(0, 0, 0, 255), 0.f, 0, 1.5f * scale);
        }
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
    if (hud.mode_toast > 0.f) { // F5: the mode name, top left, fading out
        const float a = std::clamp(hud.mode_toast, 0.f, 1.f);
        const float fs = 18.f * scale;
        const ImVec2 pos{16.f * scale, 16.f * scale};
        const char* name = hud.creative ? "Creative Mode" : "Survival Mode";
        dl->AddText(ImGui::GetFont(), fs, {pos.x + scale, pos.y + scale}, IM_COL32(0, 0, 0, static_cast<int>(200 * a)), name);
        dl->AddText(ImGui::GetFont(), fs, pos, hud.creative ? IM_COL32(255, 220, 90, static_cast<int>(255 * a)) : IM_COL32(255, 255, 255, static_cast<int>(255 * a)), name);
    }
    // armor damage reduction live toast (shows damage breakdown when hit)
    if (hud.armor_toast_timer > 0.f) {
        const float a = std::clamp(hud.armor_toast_timer / 3.0f, 0.f, 1.f);
        const float fs = 15.f * scale;
        char msg[128];
        snprintf(msg, sizeof(msg), "[Armor] Raw: %d -> Final: %d (-%.1f%%, %d pts)",
                 hud.armor_toast_raw, hud.armor_toast_final, hud.armor_toast_ratio * 100.f, hud.armor_toast_pts);
        const ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(fs, 1e9f, 0.f, msg);
        const float px = 16.f * scale, py = 42.f * scale;
        dl->AddRectFilled({px - 4.f * scale, py - 2.f * scale}, {px + sz.x + 8.f * scale, py + sz.y + 4.f * scale},
                          IM_COL32(20, 20, 20, static_cast<int>(180 * a)), 4.f * scale);
        dl->AddText(ImGui::GetFont(), fs, {px + scale, py + scale}, IM_COL32(0, 0, 0, static_cast<int>(200 * a)), msg);
        dl->AddText(ImGui::GetFont(), fs, {px, py}, IM_COL32(85, 255, 255, static_cast<int>(255 * a)), msg);
    }
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
}

void RenderFrame(IDXGISwapChain* sc) {
    HudState hud;
    SteveState steve;
#if ENABLE_CS2_VIEWMODEL
    if (!g_provider || !g_provider(hud, steve) || !hud.show || (!hud.mc_mode && !hud.cs_mode && !hud.ow_mode)) return;
#else
    if (!g_provider || !g_provider(hud, steve) || !hud.show || (!hud.mc_mode && !hud.ow_mode)) return;
#endif

    if (g_depth_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_depth_mutex);
        g_steve.setDepthView(g_s.device, g_depth_res, g_depth_cpu);
        if (g_depth_copy_cpu.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy_cpu);
        if (g_depth_copy2_cpu.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, g_depth_copy2_cpu);
        for (auto h : g_mob_depth_copy_cpus) {
            if (h.ptr != 0) g_steve.setDepthView(g_s.device, g_depth_res, h);
        }
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

    // Frame-time probe: opt in with mc_er_perflog.txt (a 5 s window makes sustained drops visible).
    static const bool perf_log = [] {
        const std::string& dir = eldenring::diagnostics::GetGameDir();
        return !dir.empty() && std::filesystem::exists(std::filesystem::path(dir) / "mc_er_perflog.txt");
    }();
    static double perf_accum = 0.0;
    static unsigned perf_frames = 0;
    if (perf_log) {
        perf_accum += dt;
        ++perf_frames;
        if (perf_accum >= 5.0) {
            Logf("perf: %.1f fps (%.2f ms/frame)", perf_frames / perf_accum, 1000.0 * perf_accum / perf_frames);
            perf_accum = 0.0;
            perf_frames = 0;
        }
    }

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
                case FxKind::Spark: g_particles.spawnSpark(e.pos, g_fx_rng); break;
                case FxKind::Dust: g_particles.spawnDust(e.pos, g_fx_rng); break;
            }
        }
        g_particles.update(dt);
#if ENABLE_CS2_VIEWMODEL
        DrawDecals(ImGui::GetBackgroundDrawList(), io.DisplaySize.x, io.DisplaySize.y);
#endif
        DrawTracers(ImGui::GetBackgroundDrawList(), io.DisplaySize.x, io.DisplaySize.y);
#if ENABLE_CS2_VIEWMODEL
        DrawCsMuzzleFlash(ImGui::GetBackgroundDrawList(), io.DisplaySize.x, io.DisplaySize.y);
#endif
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
    if (g_crack_dirty.exchange(false)) {
        std::lock_guard<std::mutex> g(g_crack_mutex);
        g_steve.setTerrainCrack(g_crack_mesh);
    }
    if (steve.cam_valid && g_steve.ready() && g_atlas_ready && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        SteveParams bp;
        bp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f); // 1.25: occluded by the scene, but not within 2.2 m of the camera
        bp.depth_const = g_steve_cfg.depth_const;
        bp.rel_bias = g_steve_cfg.rel_bias;
        bp.abs_bias = g_steve_cfg.abs_bias;
        bp.depth_w = static_cast<float>(g_depth_w);
        bp.depth_h = static_cast<float>(g_depth_h);
        blocks_drawn = g_steve.drawBlocks(g_s.device, g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, bp, f.rtv, idx);
        // The crack gets a near-zero bias: the block bias (8% + 5 cm) would force a floor decal to
        // float in front of the floor to survive the scene-depth test. It is a separate mesh, so it
        // is drawn even when there are no placed blocks.
        SteveParams cp = bp;
        cp.mode = g_depth_res == nullptr ? 0.f : 1.0f;
        cp.rel_bias = 0.0f;
        cp.abs_bias = 0.001f;
        cp.keep_depth = blocks_drawn;
        const bool crack_drawn = g_steve.drawTerrainCrack(g_s.device, g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, cp, f.rtv, idx);
        static uint64_t last_crack_log = 0;
        const uint64_t now_crack = GetTickCount64();
        if (g_log && now_crack - last_crack_log > 1000) {
            last_crack_log = now_crack;
            size_t cv = 0, ci = 0;
            {
                std::lock_guard<std::mutex> g(g_crack_mutex);
                cv = g_crack_mesh.vertices.size();
                ci = g_crack_mesh.indices.size();
            }
            g_log("crackdraw: mesh=%zu/%zu drawn=%d mode=%.2f rel=%.3f abs=%.3f", cv, ci, crack_drawn ? 1 : 0,
                  cp.mode, cp.rel_bias, cp.abs_bias);
        }
        if (blocks_drawn || crack_drawn) g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    }
    g_death_tracker.update(dt, steve.dead);
    if (g_death_tracker.justRespawned()) {
        g_anim.reset();
        g_motion.reset();
    }
    const bool should_draw_steve = steve.draw && g_death_tracker.shouldDrawModel();
    if (should_draw_steve && g_steve.ready()) {
        const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
        const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
        if (!steve.dead) {
            g_sleep_tracker.update(dt, steve.sleeping);
            const float sleep_frac = g_sleep_tracker.sleepFraction();

            mc::SteveAnimInput anim_in = g_motion.update(dt, steve.feet, steve.yaw);
            anim_in.swing_progress = steve.swing;
            anim_in.bow_charge = steve.bow_charge;
            anim_in.bow_offhand = steve.bow_offhand;
            anim_in.is_blocking = steve.blocking;
            anim_in.block_offhand = steve.block_offhand;
            anim_in.is_crouching = steve.sneaking;
            anim_in.is_sprinting = steve.sprinting;
            anim_in.on_ground = steve.on_ground;
            anim_in.is_sleeping = (sleep_frac > 0.05f);
            if (!anim_in.is_sleeping) {
                g_head.update(steve.cam.forward.x, steve.cam.forward.y, steve.cam.forward.z, steve.yaw, anim_in);
            }
            g_anim.update(dt, anim_in);
            if (g_trace_left.load() > 0) {
                g_trace_left.fetch_sub(1);
                char tr[220];
                snprintf(tr, sizeof(tr), "trace: dt=%.4f feet=%.4f %.4f %.4f cam=%.4f %.4f %.4f yaw=%.3f fwd=%.3f strafe=%.3f", dt, steve.feet[0],
                         steve.feet[1], steve.feet[2], steve.cam.position.x, steve.cam.position.y, steve.cam.position.z, steve.yaw,
                         anim_in.forward_speed, anim_in.strafe_speed);
                Logf(tr);
            }
        } else {
            g_sleep_tracker.reset();
        }
        auto parts = eldenring::render::posedMatrices(g_anim.getTransforms(), {steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw);
        const float fall_frac = g_death_tracker.fallFraction();
        if (fall_frac > 0.f) {
            const mc::rig::Mat4 fall = eldenring::render::deathFallMatrix({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw, fall_frac);
            for (auto& m : parts) m = m * fall;
        } else {
            const float sleep_frac = g_sleep_tracker.sleepFraction();
            if (sleep_frac > 0.f) {
                const mc::rig::Mat4 sleep = eldenring::render::sleepPoseMatrix({steve.feet[0], steve.feet[1], steve.feet[2]}, steve.yaw, sleep_frac);
                for (auto& m : parts) m = m * sleep;
            }
        }
        if (steve.shadow && g_steve.shadowReady() && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
            const float ground = steve.ground_valid ? steve.ground_y : steve.feet[1];
            SteveParams shp;
            shp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f); // like the blocks: not within 2.2 m of the camera
            shp.depth_const = g_steve_cfg.depth_const;
            shp.rel_bias = g_steve_cfg.rel_bias;
            shp.abs_bias = g_steve_cfg.abs_bias;
            shp.depth_w = static_cast<float>(g_depth_w);
            shp.depth_h = static_cast<float>(g_depth_h);
            shp.keep_depth = blocks_drawn;
            g_steve.drawShadows(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp,
                                {eldenring::shadow::shadowAt(steve.feet[0], steve.feet[1], steve.feet[2], ground)}, shp, f.rtv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
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
        sp.tint[3] = 0.4f * (g_death_tracker.isDying() ? 1.0f : std::min(1.f, steve.hurt * 4.f));
        sp.keep_depth = blocks_drawn; // the blocks left their depth in the figure's buffer: the figure sorts against them
        sp.held_item = static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.held_item), steve.bow_charge));
        // a bow in the offhand shows the drawn-bow sprite while it is drawn (the held item in the right hand stays what it is)
        sp.shield_blocking = steve.blocking;
        sp.block_offhand = steve.block_offhand;
        sp.offhand_item = steve.bow_offhand ? static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.offhand_item), steve.bow_charge))
                                            : steve.offhand_item;
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
    // The mobs over the summons: the same rig, one pose per summoned character. They sort against the scene (and the blocks) like Steve.
    if (steve.cam_valid && g_steve.ready() && g_steve.ensureDepth(g_s.device, g_s.width, g_s.height)) {
        std::vector<eldenring::mobs::MobSnapshot> snapshot;
        {
            std::lock_guard<std::mutex> g(g_mob_mutex);
            snapshot = g_mob_snapshot;
        }
        std::vector<eldenring::summon::ArrowDraw> arrow_snapshot;
        {
            std::lock_guard<std::mutex> g(g_arrow_mutex);
            arrow_snapshot = g_arrow_snapshot;
        }
        const auto draws = g_mob_registry.update(dt, snapshot);
        if (!draws.empty() || !arrow_snapshot.empty()) {
            const float scene_h = g_steve_cfg.scene_height > 0.f ? g_steve_cfg.scene_height : static_cast<float>(g_s.height);
            const mc::rig::Mat4 vp = mc::rig::viewProjection(steve.cam, steve.fov_y, static_cast<float>(g_s.width) / scene_h);
            bool keep = blocks_drawn || should_draw_steve; // the blocks and Steve left their depth in the figure's buffer
            if (!draws.empty()) {
                if (steve.shadow && g_steve.shadowReady()) {
                    std::vector<eldenring::shadow::Shadow> shadows;
                    for (const auto& d : draws) shadows.push_back(eldenring::shadow::shadowAt(d.feet[0], d.feet[1], d.feet[2], d.feet[1])); // a mob stands on the ground
                    SteveParams shp;
                    shp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.25f : 0.f);
                    shp.depth_const = g_steve_cfg.depth_const;
                    shp.rel_bias = g_steve_cfg.rel_bias;
                    shp.abs_bias = g_steve_cfg.abs_bias;
                    shp.depth_w = static_cast<float>(g_depth_w);
                    shp.depth_h = static_cast<float>(g_depth_h);
                    shp.keep_depth = keep;
                    g_steve.drawShadows(g_s.list, g_s.srv_heap, g_depth_gpu, g_s.width, g_s.height, vp, shadows, shp, f.rtv);
                }
                for (const auto& d : draws) {
                    SteveParams mp;
                    mp.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.f : 0.f);
                    mp.depth_const = g_steve_cfg.depth_const;
                    mp.rel_bias = g_steve_cfg.rel_bias;
                    mp.abs_bias = g_steve_cfg.abs_bias;
                    mp.depth_w = static_cast<float>(g_depth_w);
                    mp.depth_h = static_cast<float>(g_depth_h);
                    mp.keep_depth = keep;
                    mp.held_item = static_cast<uint16_t>(d.held_item);
                    mp.held_table = g_held_table_gpu;
                    mp.tint[0] = 1.f; // the same red flash as Steve's hurt: 10 ticks, strongest at the start; a dying mob stays red
                    mp.tint[1] = mp.tint[2] = 0.f;
                    mp.tint[3] = 0.4f * std::min(1.f, d.hurt * 4.f);
                    if (d.white_flash > 0.f) {
                        mp.tint[0] = mp.tint[1] = mp.tint[2] = 1.f;
                        mp.tint[3] = 0.7f * d.white_flash;
                    }
                    D3D12_GPU_DESCRIPTOR_HANDLE table = g_mob_table_gpu.ptr != 0 ? g_mob_table_gpu : g_depth_gpu;
                    if (!d.model_type.empty()) {
                        auto it = g_mob_tables_gpu.find(d.model_type);
                        if (it != g_mob_tables_gpu.end() && it->second.ptr != 0) {
                            table = it->second;
                        }
                    }
                    if (d.generic) g_steve.drawMobModel(g_s.list, g_s.srv_heap, table, g_s.width, g_s.height, vp, d.bones, mp, f.rtv, d.model_type);
                    else g_steve.draw(g_s.list, g_s.srv_heap, table, g_s.width, g_s.height, vp, d.parts, mp, f.rtv);
                    keep = true; // the next one sorts against this one
                }
            }
            if (!arrow_snapshot.empty() && g_held_table_gpu.ptr != 0) {
                std::vector<mc::rig::Mat4> arrow_matrices;
                arrow_matrices.reserve(arrow_snapshot.size());
                for (const auto& a : arrow_snapshot) {
                    arrow_matrices.push_back(eldenring::summon::arrowWorldMatrix(a));
                }
                SteveParams arrow_p;
                arrow_p.mode = g_depth_res == nullptr ? 0.f : (g_steve_cfg.occlusion ? 1.f : 0.f);
                arrow_p.depth_const = g_steve_cfg.depth_const;
                arrow_p.rel_bias = g_steve_cfg.rel_bias;
                arrow_p.abs_bias = g_steve_cfg.abs_bias;
                arrow_p.depth_w = static_cast<float>(g_depth_w);
                arrow_p.depth_h = static_cast<float>(g_depth_h);
                arrow_p.keep_depth = keep;
                g_steve.drawArrows(g_s.list, g_s.srv_heap, g_held_table_gpu, g_s.width, g_s.height, vp, arrow_matrices, arrow_p, f.rtv);
            }
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
    } else {
        g_mob_registry.clear();
    }
    [[maybe_unused]] auto& r_mode = eldenring::RuntimeContext::Instance().mode();
#if ENABLE_CS2_VIEWMODEL
    if (r_mode.cs_mode.load() && g_cs.ready()) {
        // A weapon switch releases the old VB/IB and textures; wait for the
        // GPU to finish the previous frame before replacing them.
        if (r_mode.cs_weapon_slot.load() != g_cs_active_slot) {
            WaitForGpu();
        }
        UpdateCsWeaponSlot(r_mode.cs_weapon_slot.load());
    }
    const bool cs_mode_on = r_mode.cs_mode.load();
    const bool cs_active = cs_mode_on && g_cs.ready() && GameIsForeground();
#else
    const bool cs_mode_on = false;
    const bool cs_active = false;
#endif
    const bool mc_mode_on = eldenring::RuntimeContext::Instance().mode().mc_mode.load(std::memory_order_relaxed);
    const bool ow_mode_on = eldenring::RuntimeContext::Instance().mode().ow_mode.load(std::memory_order_relaxed);
    // Do not fall back to the MC first-person model while CS mode or OW mode owns the view,
    // or while MC mode is off.
    const bool mc_fp_active = !cs_active && !cs_mode_on && !ow_mode_on && mc_mode_on && steve.first_person && !steve.dead && steve.cam_valid && g_steve.ready() && g_fp_built && g_steve.firstPersonReady() &&
        g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);

#if ENABLE_OW_VIEWMODEL
    const bool ow_active = ow_mode_on && steve.first_person && !steve.dead && GameIsForeground() && g_ow_initialized;
    if (ow_active) {
        if (!g_ow_model_loaded && !g_ow_load_failed) {
            const std::string& ow_game_dir = eldenring::diagnostics::GetGameDir();
            std::vector<std::string> ow_model_candidates;
            if (!ow_game_dir.empty()) {
                ow_model_candidates.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet.bin").string());
            }
            ow_model_candidates.push_back("mods\\mc_adapter\\ow_gauntlet.bin");
            ow_model_candidates.push_back("ow_gauntlet.bin");
            for (const auto& p : ow_model_candidates) {
                if (std::filesystem::exists(p) && g_ow.loadModel(g_s.device, p)) {
                    g_ow_model_loaded = true;
                    Logf("overlay: OW gauntlet loaded from %s", p.c_str());
                    break;
                }
            }
            if (!g_ow_model_loaded) {
                g_ow_load_failed = true;
                Logf("overlay: OW gauntlet model not found");
            } else {
                auto firstExisting = [](const std::vector<std::string>& candidates) {
                    for (const auto& p : candidates) {
                        if (std::filesystem::exists(p)) return p;
                    }
                    return std::string();
                };
                std::vector<std::string> albedo, normal, pbr;
                if (!ow_game_dir.empty()) {
                    albedo.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet.png").string());
                    normal.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet_normal.png").string());
                    pbr.push_back((std::filesystem::path(ow_game_dir) / "mods" / "mc_adapter" / "ow_gauntlet_pbr.png").string());
                }
                albedo.push_back("mods\\mc_adapter\\ow_gauntlet.png");
                normal.push_back("mods\\mc_adapter\\ow_gauntlet_normal.png");
                pbr.push_back("mods\\mc_adapter\\ow_gauntlet_pbr.png");
                if (g_ow.loadTextures(g_s.device, firstExisting(albedo), firstExisting(normal), firstExisting(pbr),
                                      g_ow_srv_heap->GetCPUDescriptorHandleForHeapStart())) {
                    Logf("overlay: OW gauntlet textures loaded");
                } else {
                    Logf("overlay: OW gauntlet textures missing");
                }
            }
        }
        if (g_ow_model_loaded) {
            g_ow.setConfig(g_ow_cfg);
            const mc::rig::Mat4 proj = g_ow.computeProjection(g_s.width, g_s.height);
            g_ow.tick(dt, r_mode.ow_state.load(), r_mode.ow_anim_time.load(), r_mode.ow_action_variant.load());
            const D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_steve.ready() ? g_steve.dsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
            g_ow.draw(g_s.list, g_ow_srv_heap, g_ow_table_gpu, g_s.width, g_s.height, proj, f.rtv, dsv);
            g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
        }
    } else
#endif
#if ENABLE_CS2_VIEWMODEL
    if (cs_active && !steve.dead) {
        const mc::rig::Mat4 proj = g_cs.computeViewmodelProjection(g_s.width, g_s.height);
        // The knife and the shared gun table have their own descriptors;
        // switching is just a descriptor-handle swap.
        D3D12_GPU_DESCRIPTOR_HANDLE cs_tex_table =
            (r_mode.cs_weapon_slot.load() == 0) ? g_cs_knife_table_gpu : g_cs_ak_table_gpu;
        if (g_steve.ready()) {
            g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);
        }
        const D3D12_CPU_DESCRIPTOR_HANDLE dsv = g_steve.ready() ? g_steve.dsvHandle() : D3D12_CPU_DESCRIPTOR_HANDLE{};
        g_cs.tick(dt, r_mode.cs_knife_state.load(), r_mode.cs_knife_anim_time.load(),
                  r_mode.cs_action_variant.load(), r_mode.cs_action_hit.load());
        g_cs.draw(g_s.list, g_s.srv_heap, cs_tex_table, g_s.width, g_s.height, proj, f.rtv, dsv);
        g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr);
    } else
#endif
    if (mc_fp_active) {
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
        const bool drawing_bow = steve.bow_ticks > 0.f && g_hand.shownItem() == mc::ItemId::Bow;
        const bool main_is_shield = g_hand.shownItem() == mc::ItemId::Shield;
        const bool main_raised = steve.blocking && !steve.block_offhand; // the shield in the main hand is the one being raised
        const fp::M4 item_pose = drawing_bow ? fp::bowPose(steve.bow_ticks, equipped)
                                 : main_is_shield ? fp::shieldMainHandPose(main_raised, steve.swing, equipped)
                                 : steve.eating > 0.f ? fp::eatPose(steve.eating, equipped) : fp::itemPose(steve.swing, equipped);
        const bool held_block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(g_hand.shownItem())) != mc::BlockId::Air;
        const fp::M4 item_disp = main_is_shield ? fp::shieldFpDisplay(false, main_raised) : (held_block ? fp::blockDisplay() : fp::itemDisplay());
        const fp::M4 item_world = fp::mul(fp::mul(base, item_pose), item_disp);
        const fp::M4 arm_world = fp::mul(base, fp::bareArmPose(steve.swing, equipped));
        const mc::ItemId shown = eldenring::bow::shownBow(g_hand.shownItem(), drawing_bow ? steve.bow_charge : 0.f); // the bow with the arrow nocked while it is drawn
        const mc::rig::Mat4 proj = mc::rig::perspectiveLH(70.f * fp::kDeg, static_cast<float>(g_s.width) / static_cast<float>(g_s.height), 0.05f, 20.f);
        D3D12_GPU_DESCRIPTOR_HANDLE skin_table = g_depth_gpu; // slots 1 and 2: depth, skin
        g_steve.drawFirstPerson(g_s.list, g_s.srv_heap, skin_table, g_item_table_gpu, g_s.width, g_s.height, proj, fp::toHost(arm_world),
                                fp::toHost(item_world), static_cast<uint16_t>(shown), f.rtv);
        // Offhand item in first person (e.g. shield in offhand, torch, etc.)
        if (steve.offhand_item != 0) {
            const bool off_is_shield = static_cast<mc::ItemId>(steve.offhand_item) == mc::ItemId::Shield;
            const bool off_block = eldenring::blocks::blockForItem(static_cast<mc::ItemId>(steve.offhand_item)) != mc::BlockId::Air;
            const fp::M4 off_pose = fp::offhandItemPose(0.f); // the offhand only follows the arm transform: raised or not is the display transform
            const fp::M4 off_disp = off_is_shield ? fp::shieldFpDisplay(true, steve.blocking && steve.block_offhand)
                                                  : (off_block ? fp::blockDisplay() : fp::itemDisplay());
            const fp::M4 off_world = fp::mul(fp::mul(base, off_pose), off_disp);
            const uint16_t off_shown = steve.bow_offhand && steve.bow_ticks > 0.f
                                           ? static_cast<uint16_t>(eldenring::bow::shownBow(static_cast<mc::ItemId>(steve.offhand_item), steve.bow_charge))
                                           : steve.offhand_item;
            g_steve.drawFirstPersonItem(g_s.list, g_s.srv_heap, g_item_table_gpu, g_s.width, g_s.height, proj, fp::toHost(off_world),
                                        off_shown, f.rtv);
        }
        g_s.list->OMSetRenderTargets(1, &f.rtv, FALSE, nullptr); // ImGui draws without a depth view
    }
    if (hud.inv_open && g_steve.ready()) {
        g_steve.ensureDepth(g_s.device, g_s.width, g_s.height);
    }
    g_current_rtv = f.rtv;
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), g_s.list);
    g_inv_preview.active = false;
    g_current_rtv = {};
    // A screenshot (F4): the finished back buffer is copied into a read-back buffer in the same command list.
    ID3D12Resource* shot_buf = nullptr;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT shot_fp{};
    UINT shot_rows = 0;
    std::string shot_path;
    if (g_shot_requested.exchange(false)) {
        {
            std::lock_guard<std::mutex> g(g_shot_mutex);
            shot_path = g_shot_path;
        }
        const D3D12_RESOURCE_DESC rd = f.back->GetDesc();
        UINT64 row_bytes = 0, total = 0;
        g_s.device->GetCopyableFootprints(&rd, 0, 1, 0, &shot_fp, &shot_rows, &row_bytes, &total);
        D3D12_HEAP_PROPERTIES hp{};
        hp.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g_s.device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&shot_buf)))) {
            shot_buf = nullptr;
            Logf("screenshot: read-back buffer failed");
        } else {
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
            b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            g_s.list->ResourceBarrier(1, &b);
            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = shot_buf;
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = shot_fp;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = f.back;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = 0;
            g_s.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }
    }
    if (shot_buf == nullptr) b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_s.list->ResourceBarrier(1, &b);
    g_s.list->Close();
    ID3D12CommandList* lists[] = {g_s.list};
    g_execute_orig(g_s.queue, 1, lists);
    f.fence_value = ++g_s.fence_counter;
    g_s.queue->Signal(g_s.fence, f.fence_value);
    if (shot_buf != nullptr) {
        g_s.fence->SetEventOnCompletion(f.fence_value, g_s.fence_event);
        WaitForSingleObject(g_s.fence_event, 2000);
        const D3D12_RESOURCE_DESC rd = f.back->GetDesc();
        void* mapped = nullptr;
        D3D12_RANGE range{0, shot_fp.Footprint.RowPitch * shot_rows};
        const bool rgba = rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM || rd.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        const bool bgra = rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM || rd.Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        const bool r10 = rd.Format == DXGI_FORMAT_R10G10B10A2_UNORM; // the game's own back buffer (format 24)
        if ((rgba || bgra || r10) && SUCCEEDED(shot_buf->Map(0, &range, &mapped))) {
            const unsigned w = static_cast<unsigned>(rd.Width), h = rd.Height;
            std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
            for (unsigned y = 0; y < h; ++y) {
                const uint8_t* srow = static_cast<const uint8_t*>(mapped) + static_cast<size_t>(y) * shot_fp.Footprint.RowPitch;
                uint8_t* drow = out.data() + static_cast<size_t>(y) * w * 4;
                for (unsigned x = 0; x < w; ++x) {
                    if (r10) {
                        uint32_t packed;
                        std::memcpy(&packed, srow + x * 4, sizeof(packed));
                        eldenring::pixel::unpackR10G10B10A2(packed, drow + x * 4);
                        drow[x * 4 + 3] = 255;
                        continue;
                    }
                    drow[x * 4 + 0] = srow[x * 4 + (bgra ? 2 : 0)];
                    drow[x * 4 + 1] = srow[x * 4 + 1];
                    drow[x * 4 + 2] = srow[x * 4 + (bgra ? 0 : 2)];
                    drow[x * 4 + 3] = 255;
                }
            }
            D3D12_RANGE none{0, 0};
            shot_buf->Unmap(0, &none);
            const bool ok = EncodePngFile(shot_path, out.data(), w, h, w * 4);
            Logf(ok ? "screenshot: saved" : "screenshot: writing the PNG failed");
            if (g_log) g_log("screenshot: %ux%u -> %s", w, h, shot_path.c_str());
        } else {
            Logf("screenshot: the back buffer format is not 8-bit RGBA/BGRA or the read-back map failed");
        }
        shot_buf->Release();
    }
    (void)sc;
}

void STDMETHODCALLTYPE CreateDsvDetour(ID3D12Device* d, ID3D12Resource* res, const D3D12_DEPTH_STENCIL_VIEW_DESC* desc,
                                       D3D12_CPU_DESCRIPTOR_HANDLE handle) {
    g_create_dsv_orig(d, res, desc, handle);
    if (res == nullptr) return;
    if (g_steve_cfg.depth_probe) probe::NoteDsv(handle.ptr, res); // every depth texture, whatever its size
    const D3D12_RESOURCE_DESC rd = res->GetDesc();
    if (rd.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || rd.Format != DXGI_FORMAT_R32G8X24_TYPELESS || rd.Width < 1024 ||
        rd.DepthOrArraySize != 1) {
        return;
    }
    if (!eldenring::depth::isSceneDepth(rd.Width, rd.Height, g_s.width, g_s.height)) {
        static int ignored_logged = 0;
        if (ignored_logged++ < 10 && g_log) g_log("overlay: ignored depth %llux%u (the back buffer is %ux%u): not the scene's", static_cast<unsigned long long>(rd.Width), rd.Height, g_s.width, g_s.height);
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
    if (g_steve_cfg.depth_probe) probe::NoteExecute(n, lists);
    g_execute_orig(q, n, lists);
}

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* sc, UINT sync, UINT flags) {
    if (g_steve_cfg.depth_probe) probe::OnPresent();
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
void SetOwConfig(const OwConfig& cfg) {
#if ENABLE_OW_VIEWMODEL
    g_ow_cfg = cfg;
#else
    (void)cfg;
#endif
}

float OwClipDuration(const char* name) {
#if ENABLE_OW_VIEWMODEL
    return g_ow.clipDuration(name);
#else
    (void)name;
    return 0.f;
#endif
}

void RequestFrameTrace(int frames) { g_trace_left.store(frames); }

void SetMobModel(const mc::model::EntityModel& model) {
    SetMobModel("", model);
}

void SetMobModel(std::string_view name, const mc::model::EntityModel& model) {
    g_mob_model = model;
    g_mob_model_given = !model.bones.empty();
    if (!name.empty() && !model.bones.empty()) {
        g_mob_models[std::string(name)] = model;
        g_mob_registry.registerModel(name, &g_mob_models[std::string(name)]);
    }
}

void SetMobSkin(const uint8_t* rgba, unsigned width, unsigned height) {
    SetMobSkin("", rgba, width, height);
}

void SetMobSkin(std::string_view name, const uint8_t* rgba, unsigned width, unsigned height) {
    g_mob_rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
    g_mob_w = width;
    g_mob_h = height;
    if (!name.empty()) {
        MobSkinEntry entry;
        entry.rgba.assign(rgba, rgba + static_cast<size_t>(width) * height * 4);
        entry.w = width;
        entry.h = height;
        g_mob_skins[std::string(name)] = std::move(entry);
    }
}

void SetMobs(const std::vector<eldenring::mobs::MobSnapshot>& mobs) {
    std::lock_guard<std::mutex> g(g_mob_mutex);
    g_mob_snapshot = mobs;
}

void SetArrows(const std::vector<eldenring::summon::ArrowDraw>& arrows) {
    std::lock_guard<std::mutex> g(g_arrow_mutex);
    g_arrow_snapshot = arrows;
}

void SpawnFx(FxKind kind, const float world_pos[3], int count) {
    FxEvent e{kind, {world_pos[0], world_pos[1], world_pos[2]}, count};
    std::lock_guard<std::mutex> g(g_fx_mutex);
    if (g_fx_events.size() < 64) g_fx_events.push_back(e);
}

void SpawnTracer(const float from[3], const float to[3], float life_s) {
    TracerEvent t{{from[0], from[1], from[2]}, {to[0], to[1], to[2]}, GetTickCount64(), life_s > 0.01f ? life_s : 0.08f};
    std::lock_guard<std::mutex> g(g_tracer_mutex);
    if (g_tracer_events.size() < 64) g_tracer_events.push_back(t);
}

#if ENABLE_CS2_VIEWMODEL
void SpawnDecal(const float pos[3], const float normal[3]) {
    const uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> g(g_decal_mutex);
    g_decal_ring.addOrRefresh(pos, normal, now, eldenring::cs::kImpactDecalLifeS);
}
#endif

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

void RequestScreenshot(const char* utf8_path) {
    std::string path = utf8_path != nullptr ? utf8_path : "";
    // F4 passes a bare filename; the game's working directory is not the game
    // folder, so WIC cannot create the file. Resolve it next to the game exe.
    if (!path.empty()) {
        const std::filesystem::path p(path);
        if (p.is_relative()) {
            wchar_t exe[MAX_PATH]{};
            if (GetModuleFileNameW(nullptr, exe, MAX_PATH) > 0) {
                path = (std::filesystem::path(exe).parent_path() / p).string();
            }
        }
    }
    {
        std::lock_guard<std::mutex> g(g_shot_mutex);
        g_shot_path = path;
    }
    g_shot_requested.store(true);
}

float CsClipDuration(const char* name) {
#if ENABLE_CS2_VIEWMODEL
    return g_cs.clipDuration(name);
#else
    (void)name;
    return 0.f;
#endif
}

void SetBlockMesh(const mc::rig::RigMesh& mesh) {
    std::lock_guard<std::mutex> g(g_block_mutex);
    g_block_mesh = mesh;
    g_blocks_dirty.store(true);
}

void SetTerrainCrackMesh(const mc::rig::RigMesh& mesh) {
    std::lock_guard<std::mutex> g(g_crack_mutex);
    g_crack_mesh = mesh;
    g_crack_dirty.store(true);
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
