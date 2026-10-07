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

#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <psapi.h>
#include <wincodec.h>

#include "sekiro_native.hpp"
#include "sekiro_hud_atlas.hpp"
#include "sekiro_adapter.hpp"
#include "sekiro_live.hpp"
#include "sekiro_steve.hpp"
#include "steve_renderer.hpp"
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

SelfMemoryReader g_memory;
std::unique_ptr<sekiro::live::LiveBinder> g_binder;
std::atomic<bool> g_binder_ready{false};
std::atomic<bool> g_stop{false};
sekiro::native::ChrIns g_player_mirror;
sekiro::native::ChrCam g_camera_mirror;
sekiro::live::LiveMirror g_live_mirror;
bool g_in_world = false;
sekiro::live::LiveSample g_last_sample{};
std::unique_ptr<sekiro::render::SteveRenderer> g_steve_renderer;
bool g_steve_renderer_tried = false;
bool g_logged_fov = false;
bool g_warned_model_hide = false;
sekiro::live::SampleStatus g_last_sample_status = sekiro::live::SampleStatus::NotBound;

const char* LinkStateText() {
    if (!g_binder_ready.load()) return "SEARCHING for game structures (waiting for code to unpack)";
    return g_in_world ? "LINKED: player + camera bound" : "BOUND: waiting for a loaded world";
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
            g_binder = std::move(binder);
            g_binder_ready.store(true);
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
        g_live_mirror.update(sample, dt, g_player_mirror, g_camera_mirror);
        g_last_sample = sample;
        if (!g_in_world) {
            g_in_world = true;
            SekiroMod_UpdatePointers(&g_player_mirror, &g_camera_mirror);
            Log("Entered world: player (%.2f, %.2f, %.2f) m", sample.player_pos.X, sample.player_pos.Y, sample.player_pos.Z);
            if (!g_warned_model_hide) {
                g_warned_model_hide = true;
                Log("WARNING: hiding the native Wolf model is NOT wired to game memory yet (offsets unknown); "
                    "only the mirror flag changes.");
            }
        }
        return true;
    }

    if (g_in_world) {
        g_in_world = false;
        g_live_mirror.reset();
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

void RenderMinecraftHUD(float screen_w, float screen_h, bool is_steve_mode) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // Ensure HUD Texture Atlas SRV is created
    if (!g_hud_srv && g_d3d_device) {
        g_hud_srv = CreateHudTextureSRV(g_d3d_device);
        if (g_hud_srv) {
            Log("Successfully created D3D11 ShaderResourceView for Minecraft HUD Atlas!");
        }
    }

    // 1. Crosshair in screen center
    if (is_steve_mode) {
        float cx = screen_w * 0.5f;
        float cy = screen_h * 0.5f;
        float ch_sz = 16.0f;
        if (g_hud_srv) {
            draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                ImVec2(cx - ch_sz * 0.5f, cy - ch_sz * 0.5f),
                ImVec2(cx + ch_sz * 0.5f, cy + ch_sz * 0.5f),
                ImVec2(sekiro::hud::kUV_CROSSHAIR.u0, sekiro::hud::kUV_CROSSHAIR.v0),
                ImVec2(sekiro::hud::kUV_CROSSHAIR.u1, sekiro::hud::kUV_CROSSHAIR.v1));
        } else {
            ImU32 ch_col = IM_COL32(255, 255, 255, 220);
            draw->AddRectFilled(ImVec2(cx - 8, cy - 1), ImVec2(cx + 8, cy + 1), ch_col);
            draw->AddRectFilled(ImVec2(cx - 1, cy - 8), ImVec2(cx + 1, cy + 8), ch_col);
        }
    }

    // 2. Hotbar at bottom center
    if (is_steve_mode) {
        float slot_size = 46.0f;
        float bar_w = slot_size * 9.0f;
        float start_x = (screen_w - bar_w) * 0.5f;
        float start_y = screen_h - 70.0f;

        mc::HudEngine* hud = GetHud();
        int active_slot = hud ? hud->getSelectedSlot() : g_selected_slot;

        for (int i = 0; i < 9; ++i) {
            float sx = start_x + i * slot_size;
            float sy = start_y;
            bool selected = (i == active_slot);

            if (g_hud_srv) {
                // Official Minecraft Slot Texture
                draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                    ImVec2(sx, sy), ImVec2(sx + slot_size, sy + slot_size),
                    ImVec2(sekiro::hud::kUV_HOTBAR_SLOT.u0, sekiro::hud::kUV_HOTBAR_SLOT.v0),
                    ImVec2(sekiro::hud::kUV_HOTBAR_SLOT.u1, sekiro::hud::kUV_HOTBAR_SLOT.v1));

                // Official Minecraft 16x16 Item Texture Icon
                const auto& item_uv = sekiro::hud::kUV_ITEMS[i];
                float pad = 7.0f;
                draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                    ImVec2(sx + pad, sy + pad), ImVec2(sx + slot_size - pad, sy + slot_size - pad),
                    ImVec2(item_uv.u0, item_uv.v0), ImVec2(item_uv.u1, item_uv.v1));

                // If selected, render official Minecraft cursor frame
                if (selected) {
                    float ext = 4.0f;
                    draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                        ImVec2(sx - ext, sy - ext), ImVec2(sx + slot_size + ext, sy + slot_size + ext),
                        ImVec2(sekiro::hud::kUV_HOTBAR_CURSOR.u0, sekiro::hud::kUV_HOTBAR_CURSOR.v0),
                        ImVec2(sekiro::hud::kUV_HOTBAR_CURSOR.u1, sekiro::hud::kUV_HOTBAR_CURSOR.v1));
                }
            } else {
                draw->AddRectFilled(ImVec2(sx, sy), ImVec2(sx + slot_size, sy + slot_size), IM_COL32(40, 40, 40, 220));
            }

            // Key number
            char key_str[4];
            snprintf(key_str, sizeof(key_str), "%d", i + 1);
            draw->AddText(ImVec2(sx + 3.0f, sy + 2.0f), IM_COL32(220, 220, 220, 220), key_str);
        }

        // Active item label above hotbar
        const char* item_name = hud ? hud->getSelectedItemDisplayName() : kHotbarItems[active_slot];
        char active_info[128];
        if (item_name && item_name[0] != '\0') {
            snprintf(active_info, sizeof(active_info), "[%d] %s", active_slot + 1, item_name);
        } else {
            snprintf(active_info, sizeof(active_info), "[%d]", active_slot + 1);
        }
        float info_w = ImGui::CalcTextSize(active_info).x;
        draw->AddText(ImVec2((screen_w - info_w) * 0.5f + 1.0f, start_y - 42.0f + 1.0f), IM_COL32(0, 0, 0, 220), active_info);
        draw->AddText(ImVec2((screen_w - info_w) * 0.5f, start_y - 42.0f), IM_COL32(255, 255, 255, 255), active_info);

        // 10 Red Hearts
        mc::HeartContainers hearts = hud ? hud->computeHearts() : mc::HeartContainers{10, false, 0};
        float heart_start_x = start_x;
        float heart_start_y = start_y - 24.0f;
        int drawn_hearts = 0;
        for (int h = 0; h < hearts.full_hearts && drawn_hearts < 10; ++h, ++drawn_hearts) {
            float hx = heart_start_x + drawn_hearts * 18.0f;
            if (g_hud_srv) {
                draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                    ImVec2(hx, heart_start_y), ImVec2(hx + 18.0f, heart_start_y + 18.0f),
                    ImVec2(sekiro::hud::kUV_HEART_FULL.u0, sekiro::hud::kUV_HEART_FULL.v0),
                    ImVec2(sekiro::hud::kUV_HEART_FULL.u1, sekiro::hud::kUV_HEART_FULL.v1));
            }
        }

        // 10 Food drumsticks (Hunger)
        mc::HungerContainers hunger = hud ? hud->computeHunger() : mc::HungerContainers{10, false, 0};
        float food_start_x = start_x + bar_w - 180.0f;
        int drawn_food = 0;
        for (int fd = 0; fd < hunger.full_drumsticks && drawn_food < 10; ++fd, ++drawn_food) {
            float fx = food_start_x + drawn_food * 18.0f;
            if (g_hud_srv) {
                draw->AddImage(reinterpret_cast<ImTextureID>(g_hud_srv),
                    ImVec2(fx, heart_start_y), ImVec2(fx + 18.0f, heart_start_y + 18.0f),
                    ImVec2(sekiro::hud::kUV_HUNGER_FULL.u0, sekiro::hud::kUV_HUNGER_FULL.v0),
                    ImVec2(sekiro::hud::kUV_HUNGER_FULL.u1, sekiro::hud::kUV_HUNGER_FULL.v1));
            }
        }
    }
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
    if (g_d3d_context) {
        g_d3d_context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    return g_original_resize_buffers(pSwapChain, buffer_count, width, height, new_format, swap_chain_flags);
}

// Creates the renderer on first use and loads the real skin when it was extracted locally.
void EnsureSteveRenderer() {
    if (g_steve_renderer_tried || !g_d3d_device) return;
    g_steve_renderer_tried = true;
    auto renderer = std::make_unique<sekiro::render::SteveRenderer>();
    if (!renderer->init(g_d3d_device)) {
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
    g_steve_renderer = std::move(renderer);
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
    g_steve_renderer->draw(g_d3d_context, target, static_cast<UINT>(screen_w), static_cast<UINT>(screen_h), view_proj, world);
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
    if (GetAsyncKeyState(VK_F6) & 1) {
        bool current_active = SekiroMod_IsSteveModeActive();
        bool new_active = !current_active;
        SekiroMod_SetSteveMode(new_active);
        MessageBeep(MB_ICONASTERISK);
        Log(">>> Hotkey [F6] triggered! Steve Mode toggled to: %s", new_active ? "TRUE (ACTIVE)" : "FALSE (STANDBY)");
    }

    if (GetAsyncKeyState(VK_F7) & 1) {
        g_screenshot_requested.store(true);
    }

    // Gather this frame's input events for the mc-core Session
    mc::InputSnapshot input_snapshot{};
    mc::HudEngine* global_hud = GetHud();
    for (int k = 0; k < 9; ++k) {
        if (GetAsyncKeyState('1' + k) & 1) {
            input_snapshot.hotbar_select = k;
            g_selected_slot = k;
            Log("Selected hotbar slot: %d (%s)", k + 1, global_hud ? mc::HudEngine::getItemDisplayName(global_hud->getSlot(k).item) : kHotbarItems[k]);
        }
    }
    if (g_imgui_initialized.load() && ImGui::GetIO().MouseWheel != 0.0f) {
        input_snapshot.scroll = ImGui::GetIO().MouseWheel > 0.0f ? -1 : 1;
    }
    input_snapshot.attack_pressed = (GetAsyncKeyState(VK_LBUTTON) & 1) != 0;
    input_snapshot.attack_held = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    input_snapshot.use_pressed = (GetAsyncKeyState(VK_RBUTTON) & 1) != 0;
    input_snapshot.glide_toggle = (GetAsyncKeyState(VK_SPACE) & 1) != 0;

    // Compute delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_frame_time).count();
    g_last_frame_time = now;
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 1.0f / 60.0f;
    }

    // Tick mc-core engine only with a validated live link; otherwise stay idle and say so in the HUD
    if (SyncLiveGameState(dt)) {
        SekiroMod_Tick(dt, &input_snapshot);
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
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press [F6] to Exit Minecraft Mode  |  Keys [1-9] Change Slot");
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

        // 2. Render Full Minecraft Crosshair, Hotbar, 3D Steve Rig & Hand
        RenderMinecraftHUD(screen_w, screen_h, is_active);

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
        return g_system_DirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
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
        if (g_system_dinput8) {
            FreeLibrary(g_system_dinput8);
            g_system_dinput8 = nullptr;
        }
        Log("mc-core Sekiro Adapter Detached cleanly.");
        break;
    }
    return TRUE;
}
