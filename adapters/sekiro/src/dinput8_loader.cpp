#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <unknwn.h>
#include <dinput.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <cfloat>
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <mutex>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <psapi.h>
#include <wincodec.h>

#include "sekiro_native.hpp"
#include "mc/hud_atlas.hpp"
#include "mc/item_model.hpp"
#include "sekiro_hud_atlas.hpp"
#include "sekiro_adapter.hpp"
#include "sekiro_live.hpp"
#include "sekiro_enemies.hpp"
#include "sekiro_host_write.hpp"
#include "sekiro_input_filter.hpp"
#include "sekiro_model.hpp"
#include "sekiro_steve.hpp"
#include "mc/hud_layout.hpp"
#include "d3d11_rig/rig_renderer.hpp"
#include "d3d11_rig/depth_capture.hpp"
#include "mc/hud.hpp"
#include "mc/session.hpp"

// Declare external plugin entry lifecycle functions
extern "C" {
void SekiroMod_Initialize(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera);
void SekiroMod_Shutdown();
void SekiroMod_SetSteveMode(bool active);
bool SekiroMod_IsSteveModeActive();
void SekiroMod_Tick(float delta_time, const mc::InputSnapshot* input);
void SekiroMod_UpdatePointers(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera);
bool SekiroMod_RegisterEntity(uint64_t entity_id, sekiro::native::ChrIns* entity);
void SekiroMod_UnregisterEntity(uint64_t entity_id);
size_t SekiroMod_DrainEnemyHealthWrites(uint64_t* ids, float* healths, size_t max);
bool SekiroMod_IsNativeInputSuppressed();
mc::Session* SekiroMod_GetSession();
const mc::adapter::SekiroAdapter* SekiroMod_GetAdapter();
}

// Forward declare ImGui Win32 handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace {

void Log(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    FILE* f = nullptr;
    if (fopen_s(&f, "mc_adapter.log", "a") == 0 && f) {
        fprintf(f, "[mc_adapter] %s\n", buf);
        fclose(f);
    }
}

// Real system dinput8.dll handle & function pointer
HMODULE g_system_dinput8 = nullptr;
using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
DirectInput8Create_t g_system_DirectInput8Create = nullptr;

// D3D11 Hook typedefs
using Present_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers_t = HRESULT(WINAPI*)(IDXGISwapChain*, UINT, UINT, UINT, DXGI_FORMAT, UINT);

Present_t g_original_present = nullptr;
ResizeBuffers_t g_original_resize_buffers = nullptr;

std::atomic<bool> g_mod_initialized{false};
std::atomic<bool> g_imgui_initialized{false};
std::chrono::steady_clock::time_point g_last_frame_time;

ID3D11Device* g_d3d_device = nullptr;
ID3D11DeviceContext* g_d3d_context = nullptr;
ID3D11ShaderResourceView* g_hud_srv = nullptr;
std::vector<BYTE> g_atlas_rgba; // the atlas the HUD was built from, kept so held items can be extruded from it
UINT g_atlas_w = 0, g_atlas_h = 0;
mc::ItemId g_held_item_applied = mc::ItemId::None;
bool g_held_item_dirty = true;
ID3D11SamplerState* g_point_sampler = nullptr;
bool g_show_debug_panel = false;
HWND g_game_hwnd = nullptr;
WNDPROC g_original_wndproc = nullptr;

template <typename T>
struct ComGuard {
    T* p{nullptr};
    ComGuard() = default;
    ComGuard(const ComGuard&) = delete;
    ComGuard& operator=(const ComGuard&) = delete;
    ~ComGuard() { if (p) p->Release(); }
    T** put() { return &p; }
    T* operator->() const { return p; }
};

// Decodes a PNG (from a file when `path` is set, otherwise from memory) into tightly packed RGBA8.
bool DecodePngToRgba(const wchar_t* path, const BYTE* data, size_t size, std::vector<BYTE>& out, UINT& width, UINT& height) {
    ComGuard<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put())))) {
        return false;
    }
    ComGuard<IWICBitmapDecoder> decoder;
    ComGuard<IWICStream> stream;
    if (path) {
        if (FAILED(factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put()))) {
            return false;
        }
    } else if (FAILED(factory->CreateStream(stream.put())) ||
               FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size))) ||
               FAILED(factory->CreateDecoderFromStream(stream.p, nullptr, WICDecodeMetadataCacheOnDemand, decoder.put()))) {
        return false;
    }
    ComGuard<IWICBitmapFrameDecode> frame;
    ComGuard<IWICFormatConverter> converter;
    if (FAILED(decoder->GetFrame(0, frame.put())) || FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(frame.p, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0f,
                                     WICBitmapPaletteTypeCustom)) ||
        FAILED(converter->GetSize(&width, &height)) || width == 0 || height == 0) {
        return false;
    }
    out.resize(static_cast<size_t>(width) * height * 4);
    return SUCCEEDED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(out.size()), out.data()));
}

ID3D11ShaderResourceView* CreateSrvFromRgba(ID3D11Device* device, const std::vector<BYTE>& pixels, UINT width, UINT height) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels.data();
    sub.SysMemPitch = width * 4;

    ComGuard<ID3D11Texture2D> tex;
    if (FAILED(device->CreateTexture2D(&desc, &sub, tex.put()))) return nullptr;
    ID3D11ShaderResourceView* srv = nullptr;
    if (FAILED(device->CreateShaderResourceView(tex.p, nullptr, &srv))) return nullptr;
    return srv;
}

// Real Minecraft sprites are extracted locally from the player's own client.jar and are never
// committed to the repo; the embedded atlas is only a placeholder fallback.
constexpr const wchar_t* kExternalHudAtlasPath = L"mods\\mc_adapter\\mc_hud_atlas.png";

ID3D11ShaderResourceView* CreateHudTextureSRV(ID3D11Device* device) {
    if (!device) return nullptr;

    std::vector<BYTE> pixels;
    UINT width = 0, height = 0;
    if (DecodePngToRgba(kExternalHudAtlasPath, nullptr, 0, pixels, width, height)) {
        Log("HUD atlas source: external file (real Minecraft sprites) %ux%u", width, height);
    } else if (DecodePngToRgba(nullptr, sekiro::hud::kHudAtlasPngData, sekiro::hud::kHudAtlasPngSize, pixels, width, height)) {
        Log("HUD atlas source: EMBEDDED PLACEHOLDER (no mods\\mc_adapter\\mc_hud_atlas.png found) %ux%u", width, height);
    } else {
        Log("HUD atlas: failed to decode any atlas");
        return nullptr;
    }
    g_atlas_rgba = pixels;
    g_atlas_w = width;
    g_atlas_h = height;
    g_held_item_dirty = true;
    return CreateSrvFromRgba(device, pixels, width, height);
}

std::atomic<bool> g_screenshot_requested{false};

// Writes the frame about to be presented (including our overlay) to a PNG next to the game, so UI
// problems can be inspected without a screen capture tool.
void SaveFramePng(IDXGISwapChain* swap_chain, const wchar_t* out_path) {
    ComGuard<ID3D11Texture2D> back;
    if (FAILED(swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(back.put())))) {
        Log("Screenshot: GetBuffer failed");
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    back->GetDesc(&desc);
    if (desc.SampleDesc.Count != 1) {
        Log("Screenshot: multisampled back buffer is not supported");
        return;
    }
    D3D11_TEXTURE2D_DESC staging_desc = desc;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.BindFlags = 0;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    staging_desc.MiscFlags = 0;
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    ComGuard<ID3D11Texture2D> staging;
    if (FAILED(g_d3d_device->CreateTexture2D(&staging_desc, nullptr, staging.put()))) {
        Log("Screenshot: staging texture creation failed");
        return;
    }
    g_d3d_context->CopyResource(staging.p, back.p);

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(g_d3d_context->Map(staging.p, 0, D3D11_MAP_READ, 0, &mapped))) {
        Log("Screenshot: Map failed");
        return;
    }
    const UINT w = desc.Width, h = desc.Height;
    std::vector<BYTE> rgba(static_cast<size_t>(w) * h * 4);
    bool supported = true;
    for (UINT y = 0; y < h && supported; ++y) {
        const BYTE* src = static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch;
        BYTE* dst = rgba.data() + static_cast<size_t>(y) * w * 4;
        switch (desc.Format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            std::copy(src, src + static_cast<size_t>(w) * 4, dst);
            break;
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            for (UINT x = 0; x < w; ++x) {
                dst[x * 4 + 0] = src[x * 4 + 2];
                dst[x * 4 + 1] = src[x * 4 + 1];
                dst[x * 4 + 2] = src[x * 4 + 0];
                dst[x * 4 + 3] = src[x * 4 + 3];
            }
            break;
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            for (UINT x = 0; x < w; ++x) {
                const uint32_t v = *reinterpret_cast<const uint32_t*>(src + static_cast<size_t>(x) * 4);
                dst[x * 4 + 0] = static_cast<BYTE>(((v >> 0) & 0x3FF) >> 2);
                dst[x * 4 + 1] = static_cast<BYTE>(((v >> 10) & 0x3FF) >> 2);
                dst[x * 4 + 2] = static_cast<BYTE>(((v >> 20) & 0x3FF) >> 2);
                dst[x * 4 + 3] = 255;
            }
            break;
        default:
            supported = false;
            break;
        }
    }
    g_d3d_context->Unmap(staging.p, 0);
    if (!supported) {
        Log("Screenshot: unsupported back buffer format %d", static_cast<int>(desc.Format));
        return;
    }

    ComGuard<IWICImagingFactory> factory;
    ComGuard<IWICStream> file;
    ComGuard<IWICBitmapEncoder> encoder;
    ComGuard<IWICBitmapFrameEncode> frame;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.put()))) ||
        FAILED(factory->CreateStream(file.put())) || FAILED(file->InitializeFromFilename(out_path, GENERIC_WRITE)) ||
        FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put())) ||
        FAILED(encoder->Initialize(file.p, WICBitmapEncoderNoCache)) || FAILED(encoder->CreateNewFrame(frame.put(), nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(w, h))) {
        Log("Screenshot: PNG encoder setup failed");
        return;
    }
    // The PNG encoder natively takes BGRA. SetPixelFormat silently rewrites the GUID to the closest
    // supported format without converting our data, so write BGRA and verify what we got back.
    for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
        std::swap(rgba[i], rgba[i + 2]);
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format)) || !IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA)) {
        Log("Screenshot: PNG encoder does not accept 32bppBGRA; aborting instead of writing swapped colours");
        return;
    }
    if (FAILED(frame->WritePixels(h, w * 4, static_cast<UINT>(rgba.size()), rgba.data())) || FAILED(frame->Commit()) ||
        FAILED(encoder->Commit())) {
        Log("Screenshot: PNG write failed");
        return;
    }
    Log("Screenshot saved (%ux%u, DXGI format %d)", w, h, static_cast<int>(desc.Format));
}

int g_frame_log_count = 0;
int g_selected_slot = 0; // 0..8

mc::HudEngine* GetHud() {
    mc::Session* session = SekiroMod_GetSession();
    return session ? &session->hud() : nullptr;
}

const char* kHotbarItems[9] = {
    "Diamond Sword",
    "Diamond Pickaxe",
    "Dirt Block",
    "Stone Block",
    "TNT Block",
    "Golden Apple",
    "Bow",
    "Elytra",
    "Totem of Undying"
};

// ---------------------------------------------------------------------------------------------
// Live game link. The real game memory is only ever *read* through sekiro::live (validated); the
// adapter works on these mirror structures, never on a cast of game memory. There is deliberately no
// fallback object: when the link is not established the mod reports it and does not tick.
// ---------------------------------------------------------------------------------------------
class SelfMemoryReader : public sekiro::live::IMemoryReader {
public:
    bool read(uintptr_t address, void* out, size_t size) const override {
        SIZE_T got = 0;
        return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
    }
};

// Writes into our own process through the OS so an unwritable page fails instead of faulting.
class SelfMemoryWriter : public sekiro::live::IMemoryWriter {
public:
    bool write(uintptr_t address, const void* data, size_t size) override {
        SIZE_T written = 0;
        return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), data, size, &written) && written == size;
    }
};

SelfMemoryReader g_memory;
SelfMemoryWriter g_memory_writer;
std::unique_ptr<sekiro::live::ModelHider> g_model_hider;
std::mutex g_hider_mutex;                   // the Present thread and the guard thread both drive the hider
std::atomic<bool> g_wolf_hide_wanted{false};
sekiro::live::HideStatus g_last_hide_status = sekiro::live::HideStatus::Idle;
std::unique_ptr<sekiro::live::LiveBinder> g_binder;
std::atomic<bool> g_binder_ready{false};
std::atomic<bool> g_stop{false};
sekiro::native::ChrIns g_player_mirror;
sekiro::native::ChrCam g_camera_mirror;
sekiro::live::LiveMirror g_live_mirror;
sekiro::live::CameraStabilizer g_camera_stabilizer;
sekiro::live::EnemyTracker g_enemy_tracker;
std::unique_ptr<sekiro::live::HostHealthWriter> g_health_writer;
size_t g_logged_enemy_count = static_cast<size_t>(-1);
bool g_in_world = false;
sekiro::live::LiveSample g_last_sample{};
std::unique_ptr<mc::d3d11::RigRenderer> g_steve_renderer;
mc::d3d11::DepthCapture g_depth_capture;
std::atomic<bool> g_depth_dump_requested{false};
bool g_steve_renderer_tried = false;
bool g_logged_fov = false;
sekiro::live::SampleStatus g_last_sample_status = sekiro::live::SampleStatus::NotBound;

const char* LinkStateText() {
    if (!g_binder_ready.load()) return "SEARCHING for game structures (waiting for code to unpack)";
    return g_in_world ? "LINKED: player + camera bound" : "BOUND: waiting for a loaded world";
}

// The game can make the Wolf visible again between two of our Present calls (warps, cutscenes, outfit changes), and
// each such reset would show the Wolf for a frame. This thread re-applies the hide every ~2 ms while Steve mode
// owns the model, so a reset is undone before the next frame is rendered. update() is idempotent and only writes
// when the masks are not already zero.
DWORD WINAPI WolfGuardThread(LPVOID) {
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
    while (!g_stop.load()) {
        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -20000LL; // 2 ms, relative
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, 5);
        } else {
            Sleep(2);
        }
        if (!g_wolf_hide_wanted.load()) continue;
        std::lock_guard<std::mutex> lock(g_hider_mutex);
        if (g_model_hider) g_model_hider->update(true);
    }
    if (timer) CloseHandle(timer);
    return 0;
}

// Runs on the loader thread: the Steam wrapper decrypts code lazily, so keep scanning until the
// signatures show up. Refuses to bind on ambiguous matches instead of guessing.
void BindLiveGameState() {
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), GetModuleHandleA(nullptr), &mi, sizeof(mi))) {
        Log("BindLiveGameState: GetModuleInformation failed (%lu)", GetLastError());
        return;
    }
    Log("Sekiro Base: %p, Size: 0x%llX", mi.lpBaseOfDll, static_cast<unsigned long long>(mi.SizeOfImage));
    auto binder = std::make_unique<sekiro::live::LiveBinder>(
        g_memory, reinterpret_cast<uintptr_t>(mi.lpBaseOfDll), static_cast<size_t>(mi.SizeOfImage));

    sekiro::live::BindStatus last = sekiro::live::BindStatus::Bound;
    for (int attempt = 0; !g_stop.load(); ++attempt) {
        const sekiro::live::BindStatus st = binder->scan();
        if (st == sekiro::live::BindStatus::Bound) {
            Log("Bound game structures after %d scan(s): WorldChrMan global RVA 0x%X, %zu camera candidate(s)",
                attempt + 1, binder->worldChrManGlobalRva(), binder->cameraCandidateRvas().size());
            g_model_hider = std::make_unique<sekiro::live::ModelHider>(
                g_memory, g_memory_writer, binder->imageBase(), binder->imageSize(), binder->worldChrManGlobalRva());
            g_health_writer = std::make_unique<sekiro::live::HostHealthWriter>(g_memory, g_memory_writer, binder->imageBase(), binder->imageSize());
            g_binder = std::move(binder);
            g_binder_ready.store(true);
            CreateThread(nullptr, 0, WolfGuardThread, nullptr, 0, nullptr);
            return;
        }
        if (st != last || attempt % 30 == 0) {
            Log("Scan %d: %s", attempt + 1,
                st == sekiro::live::BindStatus::Ambiguous
                    ? "AMBIGUOUS WorldChrMan signature (refusing to guess)"
                    : "signatures not found yet (code may still be packed)");
            last = st;
        }
        Sleep(1000);
    }
}

// Enemies are only tracked (and so hittable) while Steve mode is on. Everything is dropped otherwise.
void ReleaseEnemies() {
    for (const mc::EntityId id : g_enemy_tracker.clear()) SekiroMod_UnregisterEntity(static_cast<uint64_t>(id));
    g_logged_enemy_count = static_cast<size_t>(-1);
}

std::vector<sekiro::live::LiveEnemy> g_last_enemy_list; // what the last frame read, for diagnostics

void SyncEnemies(float dt) {
    if (!g_binder_ready.load() || !g_in_world || !SekiroMod_IsSteveModeActive()) {
        ReleaseEnemies();
        return;
    }
    std::vector<sekiro::live::LiveEnemy>& list = g_last_enemy_list;
    g_binder->enumerateEnemies(list);
    const auto changes = g_enemy_tracker.update(list, dt);
    for (const mc::EntityId id : changes.removed) SekiroMod_UnregisterEntity(static_cast<uint64_t>(id));
    for (const mc::EntityId id : changes.added) {
        SekiroMod_RegisterEntity(static_cast<uint64_t>(id), g_enemy_tracker.mirror(id));
    }
    if (g_enemy_tracker.count() != g_logged_enemy_count) {
        g_logged_enemy_count = g_enemy_tracker.count();
        Log("Tracking %zu hostile enemies (%zu loaded characters read)", g_logged_enemy_count, list.size());
    }
}

// Why did that click (not) hit anything? Logged for the first clicks of a run: where the ray started and went,
// what it found, and every loaded character within 12 m with the reason it is or is not a target.
void LogAttackDiagnostics() {
    static int logged = 0;
    if (logged >= 40) return;
    const mc::adapter::SekiroAdapter* adapter = SekiroMod_GetAdapter();
    if (!adapter) return;
    ++logged;
    const auto& ray = adapter->lastRay();
    if (!ray.valid) {
        Log("Attack click #%d: the Session did not cast a ray", logged);
        return;
    }
    const mc::Vec3 d = (ray.end - ray.start).normalized();
    Log("Attack click #%d: ray from (%.0f,%.0f,%.0f) cm dir (%.2f,%.2f,%.2f) -> %s%s entity=%llu at (%.0f,%.0f,%.0f); %zu tracked",
        logged, ray.start.x, ray.start.y, ray.start.z, d.x, d.y, d.z, ray.result.has_hit ? "HIT" : "no hit",
        ray.result.is_block ? " (block)" : "", static_cast<unsigned long long>(ray.result.hit_entity), ray.result.point.x,
        ray.result.point.y, ray.result.point.z, g_enemy_tracker.count());
    for (const auto& e : g_last_enemy_list) {
        const float dx = e.position.X - g_last_sample.player_pos.X, dz = e.position.Z - g_last_sample.player_pos.Z;
        const float dist = std::sqrt(dx * dx + dz * dz), dy = e.position.Y - g_last_sample.player_pos.Y;
        if (dist > 12.0f) continue;
        Log("    near: slot %u id %u team %u dist %.1f m dy %.1f hp %s%.0f/%.0f %s%s", e.slot, e.char_id, e.team, dist, dy,
            e.hp_valid ? "" : "(unknown) ", e.hp, e.max_hp, e.hostile ? "hostile" : "not-hostile", e.dead ? " DEAD" : "");
    }
}

// Health the core took from enemies this frame, written into the game (lower-only, class-checked).
void ApplyEnemyHealthWrites() {
    if (!g_health_writer) return;
    uint64_t ids[16];
    float healths[16];
    const size_t n = SekiroMod_DrainEnemyHealthWrites(ids, healths, 16);
    for (size_t i = 0; i < n; ++i) {
        const uintptr_t handle = g_enemy_tracker.handleOf(static_cast<mc::EntityId>(ids[i]));
        if (handle == 0) continue; // the enemy left the world between the hit and now
        const auto r = g_health_writer->lowerEnemyHealth(handle, healths[i]);
        static int logged = 0;
        if (logged < 40 && r != sekiro::live::HostHealthWriter::Result::Unchanged) {
            ++logged;
            Log("Enemy hit: wrote hp %.0f -> %s", healths[i],
                r == sekiro::live::HostHealthWriter::Result::Written ? "written"
                : r == sekiro::live::HostHealthWriter::Result::Rejected ? "REJECTED (validation failed, nothing written)"
                : "WRITE FAILED");
        }
    }
}

// Called every frame from Present. Returns true only when player and camera were read and validated.
bool SyncLiveGameState(float dt) {
    using sekiro::live::SampleStatus;
    SampleStatus st = SampleStatus::NotBound;
    sekiro::live::LiveSample sample{};
    if (g_binder_ready.load()) {
        st = g_binder->sample(sample);
    }

    if (st != g_last_sample_status) {
        const char* name = st == SampleStatus::Ok ? "Ok" : st == SampleStatus::NotInWorld ? "NotInWorld"
                         : st == SampleStatus::Invalid ? "Invalid (validation failed)" : "NotBound";
        Log("Live sample status -> %s", name);
        g_last_sample_status = st;
    }

    if (st == SampleStatus::Ok) {
        g_camera_stabilizer.apply(sample);
        g_live_mirror.update(sample, dt, g_player_mirror, g_camera_mirror);
        g_last_sample = sample;
        if (g_model_hider) {
            std::lock_guard<std::mutex> hider_lock(g_hider_mutex);
            g_wolf_hide_wanted.store(g_player_mirror.bModelHidden);
            const sekiro::live::HideStatus hs = g_model_hider->update(g_player_mirror.bModelHidden);
            static unsigned logged_resets = 0, logged_changes = 0;
            if (g_model_hider->resetCount() != logged_resets && logged_resets < 60) {
                logged_resets = g_model_hider->resetCount();
                Log("Wolf model: the game made it visible again behind our back (reset #%u); hidden again", logged_resets);
            } else {
                logged_resets = g_model_hider->resetCount();
            }
            if (g_model_hider->objectChangeCount() != logged_changes) {
                logged_changes = g_model_hider->objectChangeCount();
                Log("Wolf model: the game replaced the Wolf's model object (warp/cutscene #%u); hidden again", logged_changes);
            }
            {
                // While the Wolf is being hidden, say what both hide paths see once a second for the first 8 s.
                static DWORD last_describe = 0;
                static int described = 0;
                if (g_player_mirror.bModelHidden) {
                    const DWORD now = GetTickCount();
                    if (hs != g_last_hide_status) described = 0;
                    if (described < 8 && now - last_describe >= 1000) {
                        last_describe = now;
                        ++described;
                        Log("Wolf model state: %s", g_model_hider->describe().c_str());
                    }
                }
            }
            if (hs != g_last_hide_status) {
                const char* name = hs == sekiro::live::HideStatus::Hidden ? "Hidden (Wolf draw mask = 0)"
                                 : hs == sekiro::live::HideStatus::Idle ? "Idle (Wolf visible)"
                                 : hs == sekiro::live::HideStatus::NotInWorld ? "NotInWorld"
                                 : hs == sekiro::live::HideStatus::Rejected ? "REJECTED (entity or mask failed validation; nothing written)"
                                 : "WriteFailed";
                Log("Wolf model: %s", name);
                g_last_hide_status = hs;
            }
        }
        if (!g_in_world) {
            g_in_world = true;
            SekiroMod_UpdatePointers(&g_player_mirror, &g_camera_mirror);
            Log("Entered world: player (%.2f, %.2f, %.2f) m", sample.player_pos.X, sample.player_pos.Y, sample.player_pos.Z);
        }
        return true;
    }

    if (g_in_world) {
        g_in_world = false;
        g_wolf_hide_wanted.store(false);
        if (g_model_hider) {
            std::lock_guard<std::mutex> hider_lock(g_hider_mutex);
            g_model_hider->forgetObject();
        }
        g_last_hide_status = sekiro::live::HideStatus::Idle;
        g_live_mirror.reset();
        g_camera_stabilizer.reset();
        ReleaseEnemies();
        SekiroMod_UpdatePointers(nullptr, nullptr);
        Log("Left world / link lost");
    }
    return false;
}

LRESULT CALLBACK DetourWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (g_imgui_initialized.load()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam);
    }
    return CallWindowProcA(g_original_wndproc, hwnd, msg, wparam, lparam);
}

void InitImGui(IDXGISwapChain* pSwapChain) {
    if (g_imgui_initialized.load()) return;

    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(pSwapChain->GetDesc(&desc))) {
        Log("Failed to get swap chain desc for ImGui init");
        return;
    }

    g_game_hwnd = desc.OutputWindow;
    if (!g_game_hwnd) {
        g_game_hwnd = GetForegroundWindow();
    }

    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&g_d3d_device)))) {
        Log("Failed to get D3D11 device for ImGui init");
        return;
    }

    g_d3d_device->GetImmediateContext(&g_d3d_context);
    Log(g_depth_capture.install(g_d3d_device, g_d3d_context) ? "Depth capture hook installed" : "Depth capture hook FAILED");

    // Hook WndProc for input
    if (g_game_hwnd) {
        g_original_wndproc = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(g_game_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(DetourWndProc)));
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Don't create imgui.ini

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_game_hwnd);
    ImGui_ImplDX11_Init(g_d3d_device, g_d3d_context);

    g_imgui_initialized.store(true);
    Log("ImGui successfully initialized on SwapChain (HWND: %p, Device: %p)", g_game_hwnd, g_d3d_device);
}

// Swapped in for the HUD draw calls: Minecraft's sprites are meant to be sampled without filtering.
void UsePointSampler(const ImDrawList*, const ImDrawCmd*) {
    if (g_d3d_context && g_point_sampler) {
        g_d3d_context->PSSetSamplers(0, 1, &g_point_sampler);
    }
}

const sekiro::hud::HudUV* ItemIconUv(mc::ItemId item) {
    switch (item) {
        case mc::ItemId::DiamondSword: return &sekiro::hud::kUV_ITEM_DIAMOND_SWORD;
        case mc::ItemId::DiamondPickaxe: return &sekiro::hud::kUV_ITEM_DIAMOND_PICKAXE;
        case mc::ItemId::BlockDirt: return &sekiro::hud::kUV_ITEM_DIRT;
        case mc::ItemId::BlockStone: return &sekiro::hud::kUV_ITEM_STONE;
        case mc::ItemId::BlockTnt: return &sekiro::hud::kUV_ITEM_TNT;
        case mc::ItemId::GoldenApple: return &sekiro::hud::kUV_ITEM_GOLDEN_APPLE;
        case mc::ItemId::Bow: return &sekiro::hud::kUV_ITEM_BOW;
        case mc::ItemId::Elytra: return &sekiro::hud::kUV_ITEM_ELYTRA;
        case mc::ItemId::TotemOfUndying: return &sekiro::hud::kUV_ITEM_TOTEM_OF_UNDYING;
        default: return nullptr;
    }
}

void Blit(ImDrawList* draw, const sekiro::hud::HudUV& uv, const mc::HudRect& r, ImU32 tint = IM_COL32_WHITE) {
    draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv), ImVec2(r.x, r.y), ImVec2(r.x + r.w, r.y + r.h),
                   ImVec2(uv.u0, uv.v0), ImVec2(uv.u1, uv.v1), tint);
}

// Minecraft-style drop shadow: the same text one GUI pixel down-right in dark grey.
void ShadowText(ImDrawList* draw, float size, ImVec2 pos, ImU32 color, float shadow_offset, const char* text) {
    const ImU32 shadow = IM_COL32(62, 62, 62, (color >> IM_COL32_A_SHIFT) & 0xFF);
    draw->AddText(ImGui::GetFont(), size, ImVec2(pos.x + shadow_offset, pos.y + shadow_offset), shadow, text);
    draw->AddText(ImGui::GetFont(), size, pos, color, text);
}

// Hurt flash (red screen edges, stronger for bigger hits) and the hit marker around the crosshair.
void DrawFeedbackOverlay(ImDrawList* draw, const mc::HudLayout& layout, float screen_w, float screen_h, float gui_scale) {
    const mc::Session* session = SekiroMod_GetSession();
    if (!session) return;
    const mc::Session::Feedback& fb = session->feedback();

    if (fb.hurt_flash > 0.f) {
        const float strength = std::min(1.f, 0.35f + fb.hurt_amount * 4.f); // a scratch is faint, a big hit is not
        const float a = fb.hurt_flash * strength * 0.75f;
        const ImU32 edge = IM_COL32(200, 0, 0, static_cast<int>(a * 255.f));
        const ImU32 clear = IM_COL32(200, 0, 0, 0);
        const float t = std::min(screen_w, screen_h) * 0.22f; // thickness of the vignette
        draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(screen_w, t), edge, edge, clear, clear);
        draw->AddRectFilledMultiColor(ImVec2(0, screen_h - t), ImVec2(screen_w, screen_h), clear, clear, edge, edge);
        draw->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(t, screen_h), edge, clear, clear, edge);
        draw->AddRectFilledMultiColor(ImVec2(screen_w - t, 0), ImVec2(screen_w, screen_h), clear, edge, edge, clear);
    }
    if (fb.hit_marker > 0.f) {
        const mc::HudRect c = layout.crosshair();
        const float cx = c.x + c.w * 0.5f, cy = c.y + c.h * 0.5f;
        const float inner = 4.f * gui_scale, outer = 9.f * gui_scale;
        const ImU32 col = IM_COL32(255, 255, 255, static_cast<int>(fb.hit_marker * 255.f));
        for (const float sx : {-1.f, 1.f}) {
            for (const float sy : {-1.f, 1.f}) {
                draw->AddLine(ImVec2(cx + sx * inner, cy + sy * inner), ImVec2(cx + sx * outer, cy + sy * outer), col, std::max(1.f, gui_scale * 0.75f));
            }
        }
    }
}

void RenderMinecraftHUD(float screen_w, float screen_h, bool is_steve_mode, float dt) {
    if (!g_hud_srv && g_d3d_device) {
        g_hud_srv = CreateHudTextureSRV(g_d3d_device);
        if (g_hud_srv) {
            Log("Successfully created D3D11 ShaderResourceView for Minecraft HUD Atlas!");
        }
    }
    if (!g_point_sampler && g_d3d_device) {
        D3D11_SAMPLER_DESC sd{};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        g_d3d_device->CreateSamplerState(&sd, &g_point_sampler);
    }

    mc::HudEngine* hud = GetHud();
    if (!is_steve_mode || !g_hud_srv || !hud) return;

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    const mc::HudLayout layout(screen_w, screen_h);
    const float s = static_cast<float>(layout.scale());

    draw->AddCallback(UsePointSampler, nullptr);

    Blit(draw, sekiro::hud::kUV_CROSSHAIR, layout.crosshair());
    DrawFeedbackOverlay(draw, layout, screen_w, screen_h, s);
    Blit(draw, sekiro::hud::kUV_HOTBAR, layout.hotbar());

    const int active = hud->getSelectedSlot();
    Blit(draw, sekiro::hud::kUV_HOTBAR_SELECTION, layout.selection(active));

    for (int i = 0; i < mc::HudEngine::kHotbarSlotCount; ++i) {
        const mc::HudSlot& slot = hud->getSlot(i);
        const sekiro::hud::HudUV* uv = ItemIconUv(slot.item);
        if (!uv) continue;
        const mc::HudRect icon = layout.item(i);
        Blit(draw, *uv, icon);
        if (slot.count > 1) {
            char count[16];
            snprintf(count, sizeof(count), "%u", slot.count);
            const float size = layout.textSize();
            const float width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, count).x;
            ShadowText(draw, size, ImVec2(icon.x + 17.0f * s - width, icon.y + 9.0f * s - size * 0.5f), IM_COL32_WHITE, s, count);
        }
    }

    // Health: every container is drawn, then the filled part on top
    const mc::HeartContainers hearts = hud->computeHearts();
    for (int i = 0; i < 10; ++i) {
        const mc::HudRect r = layout.heart(i);
        Blit(draw, sekiro::hud::kUV_HEART_CONTAINER, r);
        if (i < hearts.full_hearts) Blit(draw, sekiro::hud::kUV_HEART_FULL, r);
        else if (i == hearts.full_hearts && hearts.has_half_heart) Blit(draw, sekiro::hud::kUV_HEART_HALF, r);
    }
    // Hunger grows from the right edge towards the centre
    const mc::HungerContainers hunger = hud->computeHunger();
    for (int i = 0; i < 10; ++i) {
        const mc::HudRect r = layout.food(i);
        Blit(draw, sekiro::hud::kUV_HUNGER_CONTAINER, r);
        if (i < hunger.full_drumsticks) Blit(draw, sekiro::hud::kUV_HUNGER_FULL, r);
        else if (i == hunger.full_drumsticks && hunger.has_half_drumstick) Blit(draw, sekiro::hud::kUV_HUNGER_HALF, r);
    }

    // Selected-item name: shown for two seconds after the slot or item changes, fading over the last half second
    static int last_slot = -1;
    static mc::ItemId last_item = mc::ItemId::None;
    static float label_time = 0.0f;
    const mc::ItemId item = hud->getSelectedItem();
    if (active != last_slot || item != last_item) {
        last_slot = active;
        last_item = item;
        label_time = 2.0f;
    }
    label_time = std::max(0.0f, label_time - dt);
    const char* name = hud->getSelectedItemDisplayName();
    if (label_time > 0.0f && name && name[0] != '\0') {
        const float alpha = std::min(1.0f, label_time / 0.5f);
        const float size = layout.textSize();
        const float width = ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, name).x;
        ShadowText(draw, size, ImVec2(std::floor((screen_w - width) * 0.5f), layout.itemNameBaselineY()),
                   IM_COL32(255, 255, 255, static_cast<int>(alpha * 255.0f)), s, name);
    }

    draw->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

HRESULT WINAPI DetourResizeBuffers(
    IDXGISwapChain* pSwapChain,
    UINT buffer_count,
    UINT width,
    UINT height,
    DXGI_FORMAT new_format,
    UINT swap_chain_flags
) {
    Log("DetourResizeBuffers called (Width: %u, Height: %u)", width, height);
    if (g_hud_srv) {
        g_hud_srv->Release();
        g_hud_srv = nullptr;
    }
    g_depth_capture.reset();
    if (g_d3d_context) {
        g_d3d_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    return g_original_resize_buffers(pSwapChain, buffer_count, width, height, new_format, swap_chain_flags);
}

// Creates the renderer on first use and loads the real skin when it was extracted locally.
void EnsureSteveRenderer() {
    if (g_steve_renderer_tried || !g_d3d_device) return;
    g_steve_renderer_tried = true;
    auto renderer = std::make_unique<mc::d3d11::RigRenderer>();
    mc::d3d11::RigRenderer::PartMeshes meshes;
    for (size_t i = 0; i < meshes.size(); ++i) meshes[i] = sekiro::render::buildPartMesh(static_cast<mc::StevePart>(i));
    renderer->setDepthConvention(sekiro::render::kSekiroDepth);
    constexpr float kShadowRadiusCm = 50.0f; // Minecraft's entity shadow radius is half a block
    if (!renderer->init(g_d3d_device, meshes)) {
        Log("Steve renderer init failed; the 3D rig will not be drawn");
        return;
    }
    std::vector<BYTE> skin;
    UINT w = 0, h = 0;
    if (DecodePngToRgba(L"mods\\mc_adapter\\steve.png", nullptr, 0, skin, w, h) && w == sekiro::render::kSkinSize && h == sekiro::render::kSkinSize) {
        renderer->setSkin(g_d3d_device, skin, w, h);
        Log("Steve skin: external file (real Minecraft skin) %ux%u", w, h);
    } else {
        Log("Steve skin: mods\\mc_adapter\\steve.png missing or not 64x64; using a neutral grey skin");
    }
    renderer->setGroundShadow(mc::rig::buildGroundShadowMesh(sekiro::render::kSekiroBasis, kShadowRadiusCm));
    g_steve_renderer = std::move(renderer);
}

// The HUD atlas cell holding an item's flat sprite (nullptr when the item has none).
const mc::hud::HudUV* HeldItemUv(mc::ItemId item) {
    switch (item) {
        case mc::ItemId::DiamondSword: return &mc::hud::kUV_ITEM_DIAMOND_SWORD;
        case mc::ItemId::DiamondPickaxe: return &mc::hud::kUV_ITEM_DIAMOND_PICKAXE;
        case mc::ItemId::Bow: return &mc::hud::kUV_ITEM_BOW;
        case mc::ItemId::GoldenApple: return &mc::hud::kUV_ITEM_GOLDEN_APPLE;
        case mc::ItemId::TotemOfUndying: return &mc::hud::kUV_ITEM_TOTEM_OF_UNDYING;
        default: return nullptr;
    }
}

// Keeps the renderer's held item in step with the selected hotbar slot.
void SyncHeldItem() {
    if (!g_steve_renderer) return;
    mc::HudEngine* hud = GetHud();
    const mc::ItemId item = hud ? hud->getSelectedItem() : mc::ItemId::None;
    if (item == g_held_item_applied && !g_held_item_dirty) return;
    g_held_item_applied = item;
    g_held_item_dirty = false;

    const mc::hud::HudUV* uv = HeldItemUv(item);
    if (!uv || !mc::rig::isHeldAsFlatSprite(item) || g_atlas_rgba.empty() || !g_hud_srv) {
        g_steve_renderer->setHeldItem({}, nullptr);
        return;
    }
    mc::rig::ItemSprite sprite;
    sprite.rgba = g_atlas_rgba.data();
    sprite.atlas_w = static_cast<int>(g_atlas_w);
    sprite.atlas_h = static_cast<int>(g_atlas_h);
    sprite.x = static_cast<int>(std::lround(uv->u0 * static_cast<float>(g_atlas_w)));
    sprite.y = static_cast<int>(std::lround(uv->v0 * static_cast<float>(g_atlas_h)));
    sprite.w = static_cast<int>(std::lround((uv->u1 - uv->u0) * static_cast<float>(g_atlas_w)));
    sprite.h = static_cast<int>(std::lround((uv->v1 - uv->v0) * static_cast<float>(g_atlas_h)));
    const mc::rig::RigMesh mesh = mc::rig::buildHeldItemMesh(sprite, mc::rig::heldItemStyle(item), sekiro::render::kSekiroBasis);
    const bool ok = g_steve_renderer->setHeldItem(mesh, g_hud_srv);
    Log("Held item %d: %zu vertices from atlas cell (%d,%d) %dx%d (%s)", static_cast<int>(item), mesh.vertices.size(), sprite.x,
        sprite.y, sprite.w, sprite.h, ok ? "ok" : "FAILED");
}

// Draws the real 3D rig into the frame, positioned from the live camera matrix and player position.
void DrawSteveRig(ID3D11RenderTargetView* target, float screen_w, float screen_h) {
    if (!g_in_world || !SekiroMod_IsSteveModeActive()) return;
    EnsureSteveRenderer();
    if (!g_steve_renderer || !g_steve_renderer->ready()) return;
    const mc::adapter::SekiroAdapter* adapter = SekiroMod_GetAdapter();
    if (!adapter || !adapter->isSteveSpawned()) return;

    constexpr float kFallbackFov = 1.0f;
    const float fov = g_last_sample.cam_fov_y > 0.0f ? g_last_sample.cam_fov_y : kFallbackFov;
    if (!g_logged_fov) {
        g_logged_fov = true;
        Log("Steve rig camera: vertical FOV %.4f rad (%s), aspect %.3f", fov,
            g_last_sample.cam_fov_y > 0.0f ? "read from game" : "FALLBACK, game value not trusted", screen_w / screen_h);
    }
    const sekiro::render::Mat4 view_proj = sekiro::render::viewProjection(g_last_sample, fov, screen_w / screen_h);

    const auto& root = adapter->getSteveRoot();
    std::array<sekiro::render::Mat4, static_cast<size_t>(mc::StevePart::Count)> world;
    for (size_t i = 0; i < world.size(); ++i) {
        const auto part = static_cast<mc::StevePart>(i);
        const sekiro::native::SekiroVisualMeshComponent* visual = adapter->getStevePartVisual(part);
        const sekiro::native::FQuat rot = visual ? visual->RotationQuat : sekiro::native::FQuat{};
        world[i] = sekiro::render::partMatrix(part, rot, root.position, root.yaw);
    }
    {
        // Light him like the world he stands in: sample the frame around his chest.
        const sekiro::native::FVector3 chest{root.position.X, root.position.Y + 1.0f, root.position.Z};
        const auto clip = sekiro::render::transform(view_proj, chest);
        if (clip[3] > 0.0f) {
            g_steve_renderer->setAmbientProbe(clip[0] / clip[3] * 0.5f + 0.5f, 1.0f - (clip[1] / clip[3] * 0.5f + 0.5f));
        }
    }
    g_steve_renderer->setGroundShadowWorld(sekiro::render::translation(root.position));
    if (const mc::Session* session = SekiroMod_GetSession()) g_steve_renderer->setHurtTint(session->feedback().hurt_flash);
    SyncHeldItem();
    g_steve_renderer->setSceneDepth(g_depth_capture.sceneDepth(static_cast<unsigned>(screen_w), static_cast<unsigned>(screen_h)));
    g_steve_renderer->draw(g_d3d_context, target, static_cast<UINT>(screen_w), static_cast<UINT>(screen_h), view_proj, world);
}

// Per-frame trace for remote debugging of motion: arm by creating mc_cmd_trace.txt, it records the next
// 360 frames to mc_trace.csv (position, velocity, facing, camera and what Steve was drawn with).
void TraceFrame(float dt) {
    static bool armed = false;
    static std::vector<std::array<float, 17>> rows;
    static unsigned poll = 0;
    if (!armed && (++poll % 10u) == 0u && GetFileAttributesW(L"mc_cmd_trace.txt") != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(L"mc_cmd_trace.txt");
        armed = true;
        rows.clear();
    }
    if (!armed) return;
    const mc::adapter::SekiroAdapter* adapter = SekiroMod_GetAdapter();
    const mc::Session* session = SekiroMod_GetSession();
    if (!adapter || !session) return;
    const auto& root = adapter->getSteveRoot();
    const auto& smp = g_last_sample;
    rows.push_back({dt * 1000.0f, smp.player_pos.X, smp.player_pos.Y, smp.player_pos.Z, g_player_mirror.Velocity.X,
                    g_player_mirror.Velocity.Z, smp.cam_pos.X, smp.cam_pos.Z, smp.cam_forward.X, smp.cam_forward.Z,
                    smp.facing_valid ? 1.0f : 0.0f, smp.facing_x, smp.facing_z, root.position.X, root.position.Z, root.yaw,
                    session->lastAnimInput().forward_speed});
    if (rows.size() >= 360) {
        armed = false;
        FILE* f = nullptr;
        if (fopen_s(&f, "mc_trace.csv", "w") == 0 && f) {
            fprintf(f, "dt_ms,px,py,pz,vx,vz,camx,camz,camfx,camfz,facing_ok,fx,fz,rootx,rootz,rootyaw,anim_fwd\n");
            for (const auto& r : rows) {
                for (size_t i = 0; i < r.size(); ++i) fprintf(f, "%s%.5f", i ? "," : "", r[i]);
                fprintf(f, "\n");
            }
            fclose(f);
        }
        Log("Trace written: %zu frames", rows.size());
        rows.clear();
    }
}

// Our own key/button edges. GetAsyncKeyState's "pressed since the last call" bit is shared with the game (it
// imports the same function) so the game could take a click before we saw it; levels cannot be taken.
enum KeySlot : size_t { kKeyF6, kKeyF7, kKeyF8, kKeyLeft, kKeyRight, kKeySpace, kKeyDigit0, kKeyCount = kKeyDigit0 + 9 };
sekiro::input::EdgeSet<kKeyCount> g_key_edges;
bool KeyRising(size_t slot, int vk) {
    return g_key_edges.rising(slot, (GetAsyncKeyState(vk) & 0x8000) != 0);
}

HRESULT WINAPI DetourPresent(IDXGISwapChain* pSwapChain, UINT sync_interval, UINT flags) {
    if (!g_mod_initialized.load()) {
        Log("Initializing SekiroMod (no player bound yet; waiting for live game link)...");
        SekiroMod_Initialize(nullptr, nullptr);
        g_last_frame_time = std::chrono::steady_clock::now();
        g_mod_initialized.store(true);
        Log("SekiroMod initialized successfully.");
    }

    if (!g_imgui_initialized.load()) {
        InitImGui(pSwapChain);
    }

    // F6 Hotkey toggle
    if (KeyRising(kKeyF6, VK_F6)) {
        bool current_active = SekiroMod_IsSteveModeActive();
        bool new_active = !current_active;
        SekiroMod_SetSteveMode(new_active);
        MessageBeep(MB_ICONASTERISK);
        Log(">>> Hotkey [F6] triggered! Steve Mode toggled to: %s", new_active ? "TRUE (ACTIVE)" : "FALSE (STANDBY)");
    }

    if (KeyRising(kKeyF7, VK_F7)) {
        g_screenshot_requested.store(true);
    }
    // Remote debugging: an external tool asks for a frame by creating mc_cmd_screenshot.txt in the game
    // folder (it cannot press F7 for us). Polled every few frames so the cost is negligible.
    static unsigned poll_counter = 0;
    if ((++poll_counter % 6u) == 0u && GetFileAttributesW(L"mc_cmd_screenshot.txt") != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(L"mc_cmd_screenshot.txt");
        g_screenshot_requested.store(true);
    }
    if ((poll_counter % 6u) == 3u && GetFileAttributesW(L"mc_cmd_depth.txt") != INVALID_FILE_ATTRIBUTES) {
        DeleteFileW(L"mc_cmd_depth.txt");
        g_depth_dump_requested.store(true);
    }
    if (KeyRising(kKeyF8, VK_F8)) {
        g_show_debug_panel = !g_show_debug_panel;
    }

    // Gather this frame's input events for the mc-core Session
    mc::InputSnapshot input_snapshot{};
    mc::HudEngine* global_hud = GetHud();
    for (int k = 0; k < 9; ++k) {
        if (KeyRising(kKeyDigit0 + static_cast<size_t>(k), '1' + k)) {
            input_snapshot.hotbar_select = k;
            g_selected_slot = k;
            Log("Selected hotbar slot: %d (%s)", k + 1, global_hud ? mc::HudEngine::getItemDisplayName(global_hud->getSlot(k).item) : kHotbarItems[k]);
        }
    }
    if (g_imgui_initialized.load() && ImGui::GetIO().MouseWheel != 0.0f) {
        input_snapshot.scroll = ImGui::GetIO().MouseWheel > 0.0f ? -1 : 1;
    }
    input_snapshot.attack_pressed = KeyRising(kKeyLeft, VK_LBUTTON);
    input_snapshot.attack_held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    input_snapshot.use_pressed = KeyRising(kKeyRight, VK_RBUTTON);
    input_snapshot.glide_toggle = KeyRising(kKeySpace, VK_SPACE);

    // Compute delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_frame_time).count();
    g_last_frame_time = now;
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 1.0f / 60.0f;
    }

    // Tick mc-core engine only with a validated live link; otherwise stay idle and say so in the HUD
    if (SyncLiveGameState(dt)) {
        SyncEnemies(dt);
        SekiroMod_Tick(dt, &input_snapshot);
        if (input_snapshot.attack_pressed && SekiroMod_IsSteveModeActive()) LogAttackDiagnostics();
        ApplyEnemyHealthWrites();
        TraceFrame(dt);
    }

    // Get screen dimensions from SwapChain Desc
    DXGI_SWAP_CHAIN_DESC sc_desc{};
    float screen_w = 1920.0f;
    float screen_h = 1080.0f;
    if (SUCCEEDED(pSwapChain->GetDesc(&sc_desc)) && sc_desc.BufferDesc.Width > 0 && sc_desc.BufferDesc.Height > 0) {
        screen_w = static_cast<float>(sc_desc.BufferDesc.Width);
        screen_h = static_cast<float>(sc_desc.BufferDesc.Height);
    }

    // Render ImGui On-Screen Overlay
    if (g_imgui_initialized.load() && g_d3d_device && g_d3d_context) {
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(screen_w, screen_h);

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        const bool is_active = SekiroMod_IsSteveModeActive();
        global_hud = GetHud();

        // 1. Status Panel Window
        const bool show_panel = g_show_debug_panel || !g_in_world;
        if (show_panel) {
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(480, 132), ImGuiCond_Always);
        ImGuiWindowFlags flags_win = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        if (is_active) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 1.0f, 0.8f, 1.0f));
            ImGui::Begin("Minecraft Core Mod (mc-core)", nullptr, flags_win);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "[ACTIVE] Minecraft Steve Mode Engaged!");
            ImGui::Separator();
            const char* cur_name = global_hud ? global_hud->getSelectedItemDisplayName() : kHotbarItems[g_selected_slot];
            int cur_slot = global_hud ? global_hud->getSelectedSlot() : g_selected_slot;
            ImGui::Text("Item: [%d] %s  |  Attack Swing: LMB", cur_slot + 1, cur_name);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[F6] Exit  |  [1-9] Slot  |  [F7] Screenshot  |  [F8] Hide this panel");
            ImGui::TextColored(g_in_world ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Link: %s", LinkStateText());
            ImGui::End();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::Begin("Minecraft Core Mod (mc-core)", nullptr, flags_win);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[STANDBY] mc-core Adapter Ready");
            ImGui::Separator();
            ImGui::Text("Target: Sekiro: Shadows Die Twice (DirectX 11)");
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), ">>> Press [F6] to ACTIVATE Minecraft Steve Mode <<<");
            ImGui::TextColored(g_in_world ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f) : ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Link: %s", LinkStateText());
            ImGui::End();
            ImGui::PopStyleColor();
        }

        }

        // 2. Render the Minecraft HUD (crosshair, hotbar, hearts, hunger)
        RenderMinecraftHUD(screen_w, screen_h, is_active, dt);

        ImGui::Render();

        // Dynamic per-frame RTV creation to render directly onto the active back buffer
        ID3D11Texture2D* pBackBuffer = nullptr;
        HRESULT hr_buf = pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&pBackBuffer));
        if (SUCCEEDED(hr_buf) && pBackBuffer) {
            ID3D11RenderTargetView* rtv = nullptr;
            HRESULT hr_rtv = g_d3d_device->CreateRenderTargetView(pBackBuffer, nullptr, &rtv);
            pBackBuffer->Release();

            if (SUCCEEDED(hr_rtv) && rtv) {
                // Save old render targets
                ID3D11RenderTargetView* old_rtv = nullptr;
                ID3D11DepthStencilView* old_dsv = nullptr;
                g_d3d_context->OMGetRenderTargets(1, &old_rtv, &old_dsv);

                // Set viewport
                D3D11_VIEWPORT vp{};
                vp.Width = screen_w;
                vp.Height = screen_h;
                vp.MinDepth = 0.0f;
                vp.MaxDepth = 1.0f;
                g_d3d_context->RSSetViewports(1, &vp);

                // The 3D rig goes under the 2D HUD
                DrawSteveRig(rtv, screen_w, screen_h);

                // Bind RTV and draw ImGui
                g_d3d_context->OMSetRenderTargets(1, &rtv, nullptr);
                ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

                // Restore old render targets
                g_d3d_context->OMSetRenderTargets(1, &old_rtv, old_dsv);
                if (old_rtv) old_rtv->Release();
                if (old_dsv) old_dsv->Release();

                rtv->Release();

                if (g_frame_log_count < 5) {
                    g_frame_log_count++;
                    Log("Frame %d: Rendered successfully to back buffer (%.0fx%.0f)", g_frame_log_count, screen_w, screen_h);
                }
            } else if (g_frame_log_count < 5) {
                Log("Failed to create RTV (HR: 0x%08X)", (unsigned)hr_rtv);
            }
        } else if (g_frame_log_count < 5) {
            Log("Failed to get SwapChain back buffer (HR: 0x%08X)", (unsigned)hr_buf);
        }
    }

    if (g_depth_dump_requested.exchange(false) && g_d3d_device && g_d3d_context) {
        g_depth_capture.dump(g_d3d_device, g_d3d_context, Log);
        // Calibration: project points on the player's vertical axis and read the game's depth there. With a
        // reverse-Z projection depth*z is constant when the far plane is at infinity.
        if (g_in_world) {
            DXGI_SWAP_CHAIN_DESC scd{};
            pSwapChain->GetDesc(&scd);
            const float w = static_cast<float>(scd.BufferDesc.Width), h = static_cast<float>(scd.BufferDesc.Height);
            const float fov = g_last_sample.cam_fov_y > 0.0f ? g_last_sample.cam_fov_y : 1.0f;
            const auto vp = sekiro::render::viewProjection(g_last_sample, fov, w / h);
            const auto view = sekiro::render::viewFromCamera(g_last_sample);
            std::vector<mc::d3d11::DepthCapture::Probe> probes;
            for (int i = 0; i < 12; ++i) {
                sekiro::native::FVector3 p = g_last_sample.player_pos;
                p.Y += 0.15f + 0.14f * static_cast<float>(i);
                const auto clip = sekiro::render::transform(vp, p);
                const float vz = sekiro::render::transformPoint(view, p).Z;
                if (clip[3] <= 0.0f) continue;
                const float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
                probes.push_back({static_cast<unsigned>(std::max(0.0f, (nx * 0.5f + 0.5f) * w)),
                                  static_cast<unsigned>(std::max(0.0f, (1.0f - (ny * 0.5f + 0.5f)) * h)), vz});
            }
            g_depth_capture.probe(g_d3d_device, g_d3d_context, probes.data(), probes.size(), Log);
        }
        g_depth_capture.armTrace(g_d3d_device, Log);
    }
    if (g_screenshot_requested.exchange(false) && g_d3d_device && g_d3d_context) {
        SaveFramePng(pSwapChain, L"mc_screenshot.png");
    }

    return g_original_present(pSwapChain, sync_interval, flags);
}

bool QuerySwapChainPresentAndResize(void** out_present, void** out_resize) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "SekiroMcDummyWindow";
    RegisterClassA(&wc);

    HWND hwnd = CreateWindowA(
        wc.lpszClassName, "Dummy", WS_OVERLAPPEDWINDOW,
        0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!hwnd) {
        Log("Failed to create dummy window (Error: %lu)", GetLastError());
        UnregisterClassA(wc.lpszClassName, wc.hInstance);
        return false;
    }

    D3D_FEATURE_LEVEL feature_level;
    const D3D_FEATURE_LEVEL feature_levels[] = { D3D_FEATURE_LEVEL_11_0 };

    DXGI_SWAP_CHAIN_DESC scd{};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* dummy_swap_chain = nullptr;
    ID3D11Device* dummy_device = nullptr;
    ID3D11DeviceContext* dummy_context = nullptr;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        feature_levels, 1, D3D11_SDK_VERSION, &scd,
        &dummy_swap_chain, &dummy_device, &feature_level, &dummy_context
    );

    if (FAILED(hr)) {
        Log("Hardware device creation failed (HR: 0x%08X), falling back to WARP...", (unsigned)hr);
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
            feature_levels, 1, D3D11_SDK_VERSION, &scd,
            &dummy_swap_chain, &dummy_device, &feature_level, &dummy_context
        );
    }

    bool success = false;
    if (SUCCEEDED(hr) && dummy_swap_chain) {
        void** vtable = *reinterpret_cast<void***>(dummy_swap_chain);
        *out_present = vtable[8];        // IDXGISwapChain::Present
        *out_resize = vtable[13];       // IDXGISwapChain::ResizeBuffers
        success = true;

        dummy_swap_chain->Release();
        dummy_device->Release();
        dummy_context->Release();
    } else {
        Log("D3D11CreateDeviceAndSwapChain failed completely (HR: 0x%08X)", (unsigned)hr);
    }

    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return success;
}

DWORD WINAPI LoaderThread(LPVOID) {
    Log("LoaderThread started. Waiting 1500ms for Sekiro process initialization...");
    Sleep(1500);

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        Log("MinHook initialization failed (Code: %d)", static_cast<int>(status));
        return 1;
    }

    void* present_addr = nullptr;
    void* resize_addr = nullptr;
    if (QuerySwapChainPresentAndResize(&present_addr, &resize_addr) && present_addr) {
        Log("Found Present at %p, ResizeBuffers at %p", present_addr, resize_addr);
        if (MH_CreateHook(present_addr, reinterpret_cast<void*>(&DetourPresent), reinterpret_cast<void**>(&g_original_present)) == MH_OK) {
            MH_EnableHook(present_addr);
            Log("Successfully hooked IDXGISwapChain::Present!");
        } else {
            Log("Failed to create hook for Present");
        }

        if (resize_addr && MH_CreateHook(resize_addr, reinterpret_cast<void*>(&DetourResizeBuffers), reinterpret_cast<void**>(&g_original_resize_buffers)) == MH_OK) {
            MH_EnableHook(resize_addr);
            Log("Successfully hooked IDXGISwapChain::ResizeBuffers!");
        }
    } else {
        Log("Failed to locate Present address");
    }

    BindLiveGameState();
    return 0;
}

} // namespace

// =========================================================================
// DirectInput mouse hooks: keep the native character from attacking/guarding while a Minecraft action owns
// the mouse buttons. The game's own DirectInput mouse device is found when it is created (we are its
// dinput8.dll), and its GetDeviceState / GetDeviceData results are filtered on the way to the game. Only
// the two buttons are cleared; camera movement and every key stay untouched.
// =========================================================================

namespace {

// IDirectInputDevice8 vtable: IUnknown 0-2, GetCapabilities 3, EnumObjects 4, GetProperty 5, SetProperty 6,
// Acquire 7, Unacquire 8, GetDeviceState 9, GetDeviceData 10. IDirectInput8: CreateDevice is 3.
using CreateDevice_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);
using GetDeviceState_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);
using GetDeviceData_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, void*, LPDWORD, DWORD);

CreateDevice_t g_original_create_device = nullptr;
GetDeviceState_t g_original_get_device_state = nullptr;
GetDeviceData_t g_original_get_device_data = nullptr;
void* g_create_device_target = nullptr;
void* g_device_state_target = nullptr;
void* g_device_data_target = nullptr;
std::mutex g_mouse_mutex;
std::vector<void*> g_mouse_devices; // identity only: never dereferenced; the game recreates devices, addresses repeat
std::vector<void*> g_keyboard_devices;
std::atomic<unsigned> g_keyboard_calls{0}, g_suppressed_keys{0};
std::atomic<unsigned> g_mouse_state_calls{0}, g_mouse_data_calls{0}, g_suppressed_clicks{0};

// GUID_SysMouse {6F1D2B60-D5A0-11CF-BFC7-444553540000}
constexpr GUID kGuidSysMouse = {0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
// GUID_SysKeyboard {6F1D2B61-D5A0-11CF-BFC7-444553540000}
constexpr GUID kGuidSysKeyboard = {0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};

void RememberDevice(std::vector<void*>& list, void* device) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    if (std::find(list.begin(), list.end(), device) != list.end()) return;
    if (list.size() >= 64) list.erase(list.begin()); // the game recreates devices; keep the newest
    list.push_back(device);
}

bool IsKeyboardDevice(void* device) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    return std::find(g_keyboard_devices.begin(), g_keyboard_devices.end(), device) != g_keyboard_devices.end();
}

bool IsMouseDevice(void* device) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    return std::find(g_mouse_devices.begin(), g_mouse_devices.end(), device) != g_mouse_devices.end();
}

void NoteSuppressed(bool had_click) {
    if (had_click && g_suppressed_clicks.fetch_add(1) == 0) Log("DirectInput: suppressed a native mouse click (first time)");
}

HRESULT STDMETHODCALLTYPE DetourGetDeviceState(void* self, DWORD cb, LPVOID data) {
    const HRESULT hr = g_original_get_device_state(self, cb, data);
    if (SUCCEEDED(hr) && data && cb == 256 && IsKeyboardDevice(self)) {
        if (g_keyboard_calls.fetch_add(1) == 0) Log("DirectInput: the game polls its keyboard with GetDeviceState");
        if (SekiroMod_IsNativeInputSuppressed()) {
            const auto* k = static_cast<const uint8_t*>(data);
            const bool pressed = k[sekiro::input::kDikR] != 0;
            sekiro::input::suppressKeyboardKeys(data, cb);
            if (pressed && g_suppressed_keys.fetch_add(1) == 0) Log("DirectInput: suppressed a native key press (first time)");
        }
        return hr;
    }
    if (SUCCEEDED(hr) && data && IsMouseDevice(self)) {
        if (g_mouse_state_calls.fetch_add(1) == 0) Log("DirectInput: the game polls its mouse with GetDeviceState (size %lu)", cb);
        if (SekiroMod_IsNativeInputSuppressed()) {
            const auto* b = static_cast<const uint8_t*>(data);
            const bool click = (cb == 16 || cb == 20) && (b[sekiro::input::kMouseButton0Offset] || b[sekiro::input::kMouseButton1Offset]);
            sekiro::input::suppressMouseButtons(data, cb);
            NoteSuppressed(click);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE DetourGetDeviceData(void* self, DWORD cb, void* elements, LPDWORD in_out, DWORD flags) {
    const HRESULT hr = g_original_get_device_data(self, cb, elements, in_out, flags);
    if (SUCCEEDED(hr) && elements && in_out && IsKeyboardDevice(self)) {
        if (SekiroMod_IsNativeInputSuppressed()) sekiro::input::suppressBufferedKeys(elements, *in_out, cb);
        return hr;
    }
    if (SUCCEEDED(hr) && elements && in_out && IsMouseDevice(self)) {
        if (g_mouse_data_calls.fetch_add(1) == 0) Log("DirectInput: the game reads its mouse with GetDeviceData (element size %lu)", cb);
        if (SekiroMod_IsNativeInputSuppressed()) {
            sekiro::input::suppressBufferedMouseButtons(elements, *in_out, cb);
            NoteSuppressed(false);
        }
    }
    return hr;
}

void HookMouseDeviceMethods(void* device) {
    if (g_device_state_target) return;
    void** vtable = *reinterpret_cast<void***>(device);
    g_device_state_target = vtable[9];
    g_device_data_target = vtable[10];
    if (MH_CreateHook(g_device_state_target, reinterpret_cast<void*>(&DetourGetDeviceState),
                      reinterpret_cast<void**>(&g_original_get_device_state)) != MH_OK ||
        MH_EnableHook(g_device_state_target) != MH_OK) {
        Log("DirectInput: could not hook GetDeviceState");
        g_device_state_target = nullptr;
        return;
    }
    if (MH_CreateHook(g_device_data_target, reinterpret_cast<void*>(&DetourGetDeviceData),
                      reinterpret_cast<void**>(&g_original_get_device_data)) != MH_OK ||
        MH_EnableHook(g_device_data_target) != MH_OK) {
        Log("DirectInput: could not hook GetDeviceData (buffered mouse input will not be filtered)");
        g_device_data_target = nullptr;
    }
}

HRESULT STDMETHODCALLTYPE DetourCreateDevice(void* self, REFGUID guid, void** out, LPUNKNOWN outer) {
    const HRESULT hr = g_original_create_device(self, guid, out, outer);
    if (SUCCEEDED(hr) && out && *out && IsEqualGUID(guid, kGuidSysMouse)) {
        RememberDevice(g_mouse_devices, *out);
        static int logged_mice = 0;
        if (logged_mice++ < 2) Log("DirectInput: the game created its system mouse device (%p)", *out);
        HookMouseDeviceMethods(*out);
    }
    if (SUCCEEDED(hr) && out && *out && IsEqualGUID(guid, kGuidSysKeyboard)) {
        RememberDevice(g_keyboard_devices, *out);
        static int logged_keyboards = 0;
        if (logged_keyboards++ < 2) Log("DirectInput: the game created its system keyboard device (%p)", *out);
        HookMouseDeviceMethods(*out); // same IDirectInputDevice8 methods; installs them once
    }
    return hr;
}

// Called once with the IDirectInput8 the game just got from us.
void HookDirectInputInterface(void* directinput) {
    if (!directinput || g_create_device_target) return;
    const MH_STATUS init = MH_Initialize();
    if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
        Log("DirectInput: MinHook unavailable (%d); mouse suppression off", static_cast<int>(init));
        return;
    }
    void** vtable = *reinterpret_cast<void***>(directinput);
    g_create_device_target = vtable[3];
    if (MH_CreateHook(g_create_device_target, reinterpret_cast<void*>(&DetourCreateDevice),
                      reinterpret_cast<void**>(&g_original_create_device)) != MH_OK ||
        MH_EnableHook(g_create_device_target) != MH_OK) {
        Log("DirectInput: could not hook CreateDevice; mouse suppression off");
        g_create_device_target = nullptr;
    }
}

} // namespace

// =========================================================================
// DirectInput8 Proxy Export
// =========================================================================

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst,
    DWORD dwVersion,
    REFIID riidltf,
    LPVOID* ppvOut,
    LPUNKNOWN punkOuter
) {
    if (!g_system_dinput8) {
        char sys_dir[MAX_PATH]{};
        UINT len = GetSystemDirectoryA(sys_dir, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            std::string dinput_path = std::string(sys_dir) + "\\dinput8.dll";
            g_system_dinput8 = LoadLibraryA(dinput_path.c_str());
            if (g_system_dinput8) {
                g_system_DirectInput8Create = reinterpret_cast<DirectInput8Create_t>(
                    GetProcAddress(g_system_dinput8, "DirectInput8Create")
                );
                Log("Loaded system dinput8.dll and resolved DirectInput8Create.");
            }
        }
    }

    if (g_system_DirectInput8Create) {
        const HRESULT hr = g_system_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
        if (SUCCEEDED(hr) && ppvOut && *ppvOut) HookDirectInputInterface(*ppvOut);
        return hr;
    }

    Log("DirectInput8Create: Failed to forward call to system dinput8.dll");
    return E_FAIL;
}

// =========================================================================
// DLL Lifecycle
// =========================================================================

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    (void)lpReserved;
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        Log("==================================================");
        Log(" mc-core Sekiro Adapter v0.1.0 Loaded");
        Log("==================================================");
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        g_stop.store(true);
        if (g_model_hider) {
            g_model_hider->restore();
        }
        if (g_original_present) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
        if (g_mod_initialized.load()) {
            SekiroMod_Shutdown();
        }
        if (g_imgui_initialized.load()) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
        }
        if (g_hud_srv) {
            g_hud_srv->Release();
            g_hud_srv = nullptr;
        }
        if (g_point_sampler) {
            g_point_sampler->Release();
            g_point_sampler = nullptr;
        }
        if (g_system_dinput8) {
            FreeLibrary(g_system_dinput8);
            g_system_dinput8 = nullptr;
        }
        Log("mc-core Sekiro Adapter Detached cleanly.");
        break;
    }
    return TRUE;
}
