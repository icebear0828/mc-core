#pragma once

// Private shared state and cross-unit entry points for the Elden Ring D3D12 overlay.
//
// overlay_d3d12.cpp keeps only the public erov API and the high-level frame
// orchestration; the device/hook plumbing lives in overlay_hook.cpp, the 2D HUD and
// world FX in overlay_hud.cpp, the inventory screen in the same unit, the texture
// uploads in overlay_textures.cpp, and the 3D submission in overlay_scene.cpp.
//
// The globals are inline variables so every unit sees one instance without an
// extern declaration list.

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
#include "eldenring_runtime_context.hpp"
#include "eldenring_inventory_preview.hpp"
#include "eldenring_diagnostics.hpp"
#include "eldenring_overlay_frame_state.hpp"

namespace erov {
namespace detail {

struct PreviewState {
    bool active{false};
    mc::HudRect rect{};
    float mouse_x{0.f};
    float mouse_y{0.f};
    float scale{1.f};
    uint16_t held_item{0};
    uint16_t offhand_item{0};
};
inline PreviewState g_inv_preview{};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_current_rtv{};

inline constexpr UINT kMaxFrames = 8;
inline constexpr UINT kVtblPresent = 8;
inline constexpr UINT kVtblResizeBuffers = 13;
inline constexpr UINT kVtblExecuteCommandLists = 10;
inline constexpr UINT kVtblDeviceCreateDsv = 21;

inline HudProvider g_provider = nullptr;
inline LogFn g_log = nullptr;

using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
inline PresentFn g_present_orig = nullptr;
inline ResizeFn g_resize_orig = nullptr;
inline ExecuteFn g_execute_orig = nullptr;
using CreateDsvFn = void(STDMETHODCALLTYPE*)(ID3D12Device*, ID3D12Resource*, const D3D12_DEPTH_STENCIL_VIEW_DESC*, D3D12_CPU_DESCRIPTOR_HANDLE);
inline CreateDsvFn g_create_dsv_orig = nullptr;

inline SteveConfig g_steve_cfg;
// The HUD atlas, enlarged 4x with nearest neighbour (ImGui's DX12 backend samples linearly), in heap slot 3.
inline std::vector<uint8_t> g_atlas_rgba;
inline unsigned g_atlas_w = 0, g_atlas_h = 0;
inline constexpr unsigned kAtlasScale = 4;
inline ID3D12Resource* g_atlas_tex = nullptr;
inline ID3D12Resource* g_atlas_upload = nullptr;
inline D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_atlas_footprint{};
inline bool g_atlas_pending = false;
inline D3D12_GPU_DESCRIPTOR_HANDLE g_atlas_gpu{};
inline bool g_atlas_ready = false;
void CreateAtlasTexture(UINT srv_inc); // defined with the HUD drawing below
#if ENABLE_CS2_VIEWMODEL
// Authentic CS2 bullet-hole decal (mods/mc_adapter/textures/cs/bullet_hole_concrete.png), heap slot 44.
inline std::vector<uint8_t> g_decal_rgba;
inline unsigned g_decal_w = 0, g_decal_h = 0;
inline ID3D12Resource* g_decal_tex = nullptr;
inline ID3D12Resource* g_decal_upload = nullptr;
inline D3D12_PLACED_SUBRESOURCE_FOOTPRINT g_decal_footprint{};
inline bool g_decal_pending = false;
inline D3D12_GPU_DESCRIPTOR_HANDLE g_decal_gpu{};
inline bool g_decal_ready = false;
void CreateCsDecalTexture(UINT srv_inc);
#endif
// Combat particles: events queued by the loader, simulated and drawn here.
struct FxEvent {
    FxKind kind;
    float pos[3];
    int count;
};
inline std::mutex g_fx_mutex;
inline std::vector<FxEvent> g_fx_events;
inline eldenring::fx::ParticleSystem g_particles;
inline eldenring::fx::Rng g_fx_rng{20251009u};
inline mc::rig::Mat4 g_fx_vp{};
inline float g_fx_fov = 0.8378f;
inline bool g_fx_valid = false;
// CS hitscan tracers: world-space segments queued by CombatService::fireCsShot.
struct TracerEvent {
    float from[3];
    float to[3];
    uint64_t start_ms;
    float life_s;
};
inline std::mutex g_tracer_mutex;
inline std::vector<TracerEvent> g_tracer_events;
#if ENABLE_CS2_VIEWMODEL
// CS bullet holes on static geometry. The ring is bounded and refreshes hits within 3 cm.
inline std::mutex g_decal_mutex;
inline eldenring::cs::ImpactDecalRing g_decal_ring;
#endif
inline eldenring::fp::HandAnimator g_hand;
inline eldenring::fp::SwayFilter g_sway;
inline float g_walk_dist = 0.f, g_walk_bob = 0.f;
inline D3D12_GPU_DESCRIPTOR_HANDLE g_item_table_gpu{}; // slots 4 and 5
inline bool g_fp_built = false;
inline std::atomic<int> g_trace_left{0}; // F12: frames of per-frame figure/camera positions still to write to the log
inline std::vector<uint8_t> g_skin_rgba; // decoded by the loader; applied when the renderer is created
inline unsigned g_skin_w = 0, g_skin_h = 0;
inline SteveRenderer g_steve;
#if ENABLE_CS2_VIEWMODEL
inline CsRenderer g_cs;
// CS arsenal runtime state: texture slots and the weapon currently loaded.
inline D3D12_CPU_DESCRIPTOR_HANDLE g_cs_weapon_tex_cpu{};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_cs_glove_tex_cpu{};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_cs_knife_table_cpu{};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_cs_ak_table_cpu{};
inline D3D12_GPU_DESCRIPTOR_HANDLE g_cs_knife_table_gpu{};
inline D3D12_GPU_DESCRIPTOR_HANDLE g_cs_ak_table_gpu{};
inline std::string g_cs_game_dir;
inline std::string g_cs_knife_model_path;
inline uint8_t g_cs_active_slot = 0;
inline uint8_t g_cs_gun_table_slot = 0; // weapon slot whose maps are in g_cs_ak_table (0 = none)
#endif
inline mc::SteveAnimator g_anim;
inline eldenring::render::SteveMotion g_motion;
inline eldenring::render::HeadTracker g_head;
inline std::mutex g_mob_mutex;
inline std::vector<eldenring::mobs::MobSnapshot> g_mob_snapshot; // the list the loader gave last
inline std::mutex g_arrow_mutex;
inline std::vector<eldenring::summon::ArrowDraw> g_arrow_snapshot;
inline eldenring::mobs::MobRegistry g_mob_registry;              // render thread only
inline mc::PlayerDeathTracker g_death_tracker;
inline mc::PlayerSleepTracker g_sleep_tracker;
inline std::mutex g_depth_mutex;
inline ID3D12Resource* g_depth_res = nullptr; // AddRef'd scene depth in use (R32G8X24_TYPELESS), guarded by g_depth_mutex
inline std::vector<ID3D12Resource*> g_depth_candidates; // AddRef'd, oldest first, guarded by g_depth_mutex
inline UINT g_depth_w = 0, g_depth_h = 0;
inline std::atomic<bool> g_depth_dirty{false};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_depth_cpu{};
inline D3D12_GPU_DESCRIPTOR_HANDLE g_depth_gpu{};
inline std::mutex g_shot_mutex;
inline std::string g_shot_path;
inline std::atomic<bool> g_shot_requested{false};
inline std::mutex g_block_mutex;
inline mc::rig::RigMesh g_block_mesh;
inline std::mutex g_crack_mutex;
inline mc::rig::RigMesh g_crack_mesh;
inline std::atomic<bool> g_crack_dirty{false};
inline std::atomic<bool> g_blocks_dirty{false};
inline D3D12_CPU_DESCRIPTOR_HANDLE g_depth_copy2_cpu{}; // slot 8: default mob depth copy
inline D3D12_GPU_DESCRIPTOR_HANDLE g_mob_table_gpu{};   // default mob table; ptr 0 = no mob skin
inline std::map<std::string, D3D12_GPU_DESCRIPTOR_HANDLE> g_mob_tables_gpu;
inline std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> g_mob_depth_copy_cpus;
inline mc::model::EntityModel g_mob_model;              // default model file
inline bool g_mob_model_given = false;
inline bool g_mob_model_gpu = false;                    // the model's meshes are on the GPU
inline std::vector<uint8_t> g_mob_rgba;                 // decoded by the loader; applied when the renderer is created
inline unsigned g_mob_w = 0, g_mob_h = 0;
struct MobSkinEntry {
    std::vector<uint8_t> rgba;
    unsigned w{0}, h{0};
};
inline std::map<std::string, mc::model::EntityModel> g_mob_models;
inline std::map<std::string, MobSkinEntry> g_mob_skins;
inline D3D12_CPU_DESCRIPTOR_HANDLE g_depth_copy_cpu{}; // slot 6: the same depth view again, in front of the atlas (held item table)
inline D3D12_GPU_DESCRIPTOR_HANDLE g_held_table_gpu{};

// The DIRECT queue the game submits on. Written by any thread inside ExecuteCommandLists.
inline std::atomic<ID3D12CommandQueue*> g_queue{nullptr};

struct Frame {
    ID3D12CommandAllocator* alloc{nullptr};
    ID3D12Resource* back{nullptr};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
    UINT64 fence_value{0};
};

struct State {
    bool ready{false};
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
inline State g_s;

inline eldenring::live::HeartAnimator g_hearts;
inline HookStateMachine g_hook_state;

// ---- cross-unit entry points ----
void RecordAtlasUpload(ID3D12GraphicsCommandList* list);
void DrawHud(const HudState& hud, float w, float h);
void UpdateWorldFx(const SteveState& steve, float dt, float w, float h);
void WaitForGpu();
#if ENABLE_CS2_VIEWMODEL
void UpdateCsWeaponSlot(uint8_t want_slot);
#endif
void RenderFrame(IDXGISwapChain* sc);
bool InstallOverlayHooks(HudProvider provider, LogFn log);

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
inline void Logf(const char* msg) {
    if (g_log) g_log("%s", msg);
}

} // namespace detail
} // namespace erov
