#define NOMINMAX
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <psapi.h>
#include <unknwn.h>
#include <dinput.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "baked_textures.hpp"
#include "sekiro_native.hpp"
#include "sekiro_adapter.hpp"
#include "mc/animator.hpp"
#include "mc/voxel_world.hpp"
#include "mc/combat.hpp"
#include "mc/ballistics.hpp"

#include <chrono>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdarg>
#include <cmath>

// Declare external plugin entry lifecycle functions
extern "C" {
void SekiroMod_Initialize(sekiro::native::ChrIns* player, sekiro::native::ChrCam* camera);
void SekiroMod_Shutdown();
void SekiroMod_SetSteveMode(bool active);
bool SekiroMod_IsSteveModeActive();
void SekiroMod_Tick(float delta_time);
}

namespace mc::adapter {
SekiroAdapter* GetGlobalSekiroAdapter();
VoxelWorld* GetGlobalVoxelWorld();
CombatEngine* GetGlobalCombatEngine();
BallisticsEngine* GetGlobalBallisticsEngine();
SteveAnimator* GetGlobalSteveAnimator();
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
HWND g_game_hwnd = nullptr;
WNDPROC g_original_wndproc = nullptr;

// Texture SRVs for pixel-perfect 16x16 Minecraft GUI
ID3D11ShaderResourceView* g_srv_diamond_sword = nullptr;
ID3D11ShaderResourceView* g_srv_diamond_pickaxe = nullptr;
ID3D11ShaderResourceView* g_srv_dirt = nullptr;
ID3D11ShaderResourceView* g_srv_stone = nullptr;
ID3D11ShaderResourceView* g_srv_tnt = nullptr;
ID3D11ShaderResourceView* g_srv_heart = nullptr;
ID3D11ShaderResourceView* g_srv_drumstick = nullptr;

// Game memory addresses
uintptr_t g_sekiro_base = 0;
uintptr_t g_world_chr_man = 0;
uintptr_t g_player_ins = 0;

// Sekiro native wrapper instances
sekiro::native::ChrIns g_native_player{};
sekiro::native::ChrCam g_native_camera{};

int g_frame_log_count = 0;
int g_selected_slot = 0; // 0..8
bool g_is_first_person = true; // true = First-Person, false = Third-Person Steve
float g_anim_timer = 0.0f;
float g_attack_anim = 0.0f; // 0..1 swing progress
float g_mining_timer = 0.0f;

ID3D11ShaderResourceView* CreatePixelTexture(ID3D11Device* device, const uint32_t* pixels, uint32_t w, uint32_t h) {
    if (!device || !pixels) return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub{};
    sub.pSysMem = pixels;
    sub.SysMemPitch = w * sizeof(uint32_t);

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(device->CreateTexture2D(&desc, &sub, &tex)) || !tex) {
        return nullptr;
    }

    ID3D11ShaderResourceView* srv = nullptr;
    device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return srv;
}

void InitTextures(ID3D11Device* device) {
    if (!device) return;
    g_srv_diamond_sword = CreatePixelTexture(device, mc::ui::k_diamond_sword_pixels, mc::ui::k_diamond_sword_w, mc::ui::k_diamond_sword_h);
    g_srv_diamond_pickaxe = CreatePixelTexture(device, mc::ui::k_diamond_pickaxe_pixels, mc::ui::k_diamond_pickaxe_w, mc::ui::k_diamond_pickaxe_h);
    g_srv_dirt = CreatePixelTexture(device, mc::ui::k_dirt_pixels, mc::ui::k_dirt_w, mc::ui::k_dirt_h);
    g_srv_stone = CreatePixelTexture(device, mc::ui::k_stone_pixels, mc::ui::k_stone_w, mc::ui::k_stone_h);
    g_srv_tnt = CreatePixelTexture(device, mc::ui::k_tnt_pixels, mc::ui::k_tnt_w, mc::ui::k_tnt_h);
    g_srv_heart = CreatePixelTexture(device, mc::ui::k_heart_pixels, mc::ui::k_heart_w, mc::ui::k_heart_h);
    g_srv_drumstick = CreatePixelTexture(device, mc::ui::k_drumstick_pixels, mc::ui::k_drumstick_w, mc::ui::k_drumstick_h);
    Log("Initialized authentic 16x16 Minecraft pixel textures in D3D11.");
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

    // Initialize point-sampled Minecraft pixel textures
    InitTextures(g_d3d_device);

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

// 3D Math & Projection structures for Steve Voxel rendering
struct Vec3F { float x, y, z; };

Vec3F RotateVertex(const Vec3F& v, float pitch, float yaw, float roll) {
    float cy = cosf(yaw), sy = sinf(yaw);
    float x1 = v.x * cy + v.z * sy;
    float z1 = -v.x * sy + v.z * cy;
    float y1 = v.y;

    float cp = cosf(pitch), sp = sinf(pitch);
    float y2 = y1 * cp - z1 * sp;
    float z2 = y1 * sp + z1 * cp;
    float x2 = x1;

    float cr = cosf(roll), sr = sinf(roll);
    return { x2 * cr - y2 * sr, x2 * sr + y2 * cr, z2 };
}

ImVec2 ProjectToScreen(const Vec3F& v, float screen_cx, float screen_cy, float scale, float cam_dist) {
    float z = v.z + cam_dist;
    if (z < 0.1f) z = 0.1f;
    float f = scale / z;
    return ImVec2(screen_cx + v.x * f, screen_cy - v.y * f);
}

ImU32 ShadeColor(ImU32 col, float factor) {
    int r = (int)(((col >> 0) & 0xFF) * factor);
    int g = (int)(((col >> 8) & 0xFF) * factor);
    int b = (int)(((col >> 16) & 0xFF) * factor);
    int a = (int)((col >> 24) & 0xFF);
    r = std::clamp(r, 0, 255);
    g = std::clamp(g, 0, 255);
    b = std::clamp(b, 0, 255);
    return IM_COL32(r, g, b, a);
}

struct ProjectedFace {
    ImVec2 pts[4];
    float avg_z;
    ImU32 color;
};

void DrawVoxelBox(
    std::vector<ProjectedFace>& out_faces,
    const Vec3F& center,
    const Vec3F& half_extents,
    float pitch, float yaw, float roll,
    ImU32 base_col,
    float screen_cx, float screen_cy, float scale, float cam_dist
) {
    float hx = half_extents.x;
    float hy = half_extents.y;
    float hz = half_extents.z;

    Vec3F local_pts[8] = {
        {-hx,  hy, -hz}, { hx,  hy, -hz}, { hx, -hy, -hz}, {-hx, -hy, -hz},
        {-hx,  hy,  hz}, { hx,  hy,  hz}, { hx, -hy,  hz}, {-hx, -hy,  hz}
    };

    Vec3F world_pts[8];
    ImVec2 screen_pts[8];
    for (int i = 0; i < 8; ++i) {
        Vec3F rotated = RotateVertex(local_pts[i], pitch, yaw, roll);
        world_pts[i] = { center.x + rotated.x, center.y + rotated.y, center.z + rotated.z };
        screen_pts[i] = ProjectToScreen(world_pts[i], screen_cx, screen_cy, scale, cam_dist);
    }

    const int face_indices[6][4] = {
        {0, 1, 2, 3}, {5, 4, 7, 6}, {4, 5, 1, 0}, {3, 2, 6, 7}, {4, 0, 3, 7}, {1, 5, 6, 2}
    };
    const float shade_factors[6] = {1.0f, 0.65f, 1.35f, 0.45f, 0.8f, 0.85f};

    for (int f = 0; f < 6; ++f) {
        float avg_z = 0.0f;
        ProjectedFace face{};
        for (int v = 0; v < 4; ++v) {
            int idx = face_indices[f][v];
            face.pts[v] = screen_pts[idx];
            avg_z += world_pts[idx].z;
        }
        face.avg_z = avg_z * 0.25f;
        face.color = ShadeColor(base_col, shade_factors[f]);
        out_faces.push_back(face);
    }
}

void RenderPixelMinecraftGUI(float screen_w, float screen_h) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    // 1. Crosshair in screen center
    float cx = screen_w * 0.5f;
    float cy = screen_h * 0.5f;
    ImU32 ch_col = IM_COL32(255, 255, 255, 220);
    ImU32 ch_bg = IM_COL32(0, 0, 0, 180);

    draw->AddRectFilled(ImVec2(cx - 9, cy - 2), ImVec2(cx + 9, cy + 2), ch_bg);
    draw->AddRectFilled(ImVec2(cx - 2, cy - 9), ImVec2(cx + 2, cy + 9), ch_bg);
    draw->AddRectFilled(ImVec2(cx - 8, cy - 1), ImVec2(cx + 8, cy + 1), ch_col);
    draw->AddRectFilled(ImVec2(cx - 1, cy - 8), ImVec2(cx + 1, cy + 8), ch_col);

    // 2. Authentic Minecraft Hotbar (9 slots)
    float slot_size = 48.0f;
    float bar_w = slot_size * 9.0f;
    float bar_h = slot_size;
    float start_x = (screen_w - bar_w) * 0.5f;
    float start_y = screen_h - 68.0f;

    // Stone-textured beveled Hotbar background
    draw->AddRectFilled(ImVec2(start_x - 4, start_y - 4), ImVec2(start_x + bar_w + 4, start_y + bar_h + 4), IM_COL32(40, 40, 40, 240), 2.0f);
    draw->AddRect(ImVec2(start_x - 4, start_y - 4), ImVec2(start_x + bar_w + 4, start_y + bar_h + 4), IM_COL32(90, 90, 90, 255), 2.0f, 0, 2.0f);

    ID3D11ShaderResourceView* item_srvs[9] = {
        g_srv_diamond_sword,
        g_srv_diamond_pickaxe,
        g_srv_dirt,
        g_srv_stone,
        g_srv_tnt,
        g_srv_heart, // Golden Apple / food
        nullptr,
        nullptr,
        nullptr
    };

    for (int i = 0; i < 9; ++i) {
        float sx = start_x + i * slot_size;
        float sy = start_y;
        bool selected = (i == g_selected_slot);

        // Beveled slot background
        ImU32 slot_bg = selected ? IM_COL32(75, 75, 75, 255) : IM_COL32(50, 50, 50, 230);
        draw->AddRectFilled(ImVec2(sx + 2, sy + 2), ImVec2(sx + slot_size - 2, sy + slot_size - 2), slot_bg);

        // Slot borders (outer dark, inner highlight)
        draw->AddLine(ImVec2(sx + 2, sy + 2), ImVec2(sx + slot_size - 2, sy + 2), IM_COL32(25, 25, 25, 255), 1.5f);
        draw->AddLine(ImVec2(sx + 2, sy + 2), ImVec2(sx + 2, sy + slot_size - 2), IM_COL32(25, 25, 25, 255), 1.5f);
        draw->AddLine(ImVec2(sx + slot_size - 2, sy + 2), ImVec2(sx + slot_size - 2, sy + slot_size - 2), IM_COL32(110, 110, 110, 255), 1.5f);
        draw->AddLine(ImVec2(sx + 2, sy + slot_size - 2), ImVec2(sx + slot_size - 2, sy + slot_size - 2), IM_COL32(110, 110, 110, 255), 1.5f);

        // Draw authentic 16x16 pixel icon
        if (item_srvs[i]) {
            draw->AddImage(
                item_srvs[i],
                ImVec2(sx + 8, sy + 8),
                ImVec2(sx + slot_size - 8, sy + slot_size - 8)
            );
        }

        // Active slot selection box (thick white bracket)
        if (selected) {
            draw->AddRect(ImVec2(sx - 2, sy - 2), ImVec2(sx + slot_size + 2, sy + slot_size + 2), IM_COL32(255, 255, 255, 255), 2.0f, 0, 3.5f);
        }

        // Slot number key
        char key_str[4];
        snprintf(key_str, sizeof(key_str), "%d", i + 1);
        draw->AddText(ImVec2(sx + 4, sy + 4), IM_COL32(180, 180, 180, 200), key_str);
    }

    // 3. 10 Authentic Pixel Hearts (Health)
    float heart_x = start_x;
    float heart_y = start_y - 30.0f;
    if (g_srv_heart) {
        for (int h = 0; h < 10; ++h) {
            draw->AddImage(
                g_srv_heart,
                ImVec2(heart_x + h * 18.0f, heart_y),
                ImVec2(heart_x + h * 18.0f + 16.0f, heart_y + 16.0f)
            );
        }
    }

    // 4. 10 Authentic Pixel Drumsticks (Hunger)
    float drum_x = start_x + bar_w - 180.0f;
    if (g_srv_drumstick) {
        for (int d = 0; d < 10; ++d) {
            draw->AddImage(
                g_srv_drumstick,
                ImVec2(drum_x + d * 18.0f, heart_y),
                ImVec2(drum_x + d * 18.0f + 16.0f, heart_y + 16.0f)
            );
        }
    }

    // 5. Perspective Indicator & Active Info
    char info_buf[128];
    snprintf(info_buf, sizeof(info_buf), "[%s View]  Slot %d: %s  |  [F5] Toggle View",
        g_is_first_person ? "First-Person" : "Third-Person Steve",
        g_selected_slot + 1,
        (g_selected_slot == 0) ? "Diamond Sword" :
        (g_selected_slot == 1) ? "Diamond Pickaxe" :
        (g_selected_slot == 2) ? "Dirt Block" :
        (g_selected_slot == 3) ? "Stone Block" :
        (g_selected_slot == 4) ? "TNT Block" : "Item"
    );
    float tw = ImGui::CalcTextSize(info_buf).x;
    draw->AddText(ImVec2((screen_w - tw) * 0.5f, start_y - 48.0f), IM_COL32(255, 255, 80, 255), info_buf);
}

void RenderFirstPersonViewmodel(float screen_w, float screen_h) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    float hand_cx = screen_w - 180.0f;
    float hand_cy = screen_h - 100.0f;

    // Authentic Minecraft swing easing: 1 - (1 - swing)^4
    float swing_ease = 1.0f - powf(1.0f - g_attack_anim, 4.0f);
    float swing_angle = swing_ease * 1.3f;
    float bob = sinf(g_anim_timer * 6.0f) * 6.0f;

    std::vector<ProjectedFace> hand_faces;
    ImU32 col_arm = IM_COL32(0, 168, 168, 255);    // Steve cyan sleeve
    ImU32 col_skin = IM_COL32(219, 176, 140, 255); // Steve skin
    ImU32 col_blade = IM_COL32(43, 219, 219, 255); // Diamond cyan
    ImU32 col_wood = IM_COL32(139, 90, 43, 255);   // Wooden hilt/tool
    ImU32 col_dirt = IM_COL32(134, 96, 67, 255);   // Dirt block
    ImU32 col_stone = IM_COL32(128, 128, 128, 255); // Stone block

    // Steve Forearm
    DrawVoxelBox(hand_faces, {0.0f, -bob, 0.0f}, {14.0f, 38.0f, 14.0f}, -0.6f + swing_angle, 0.35f, 0.15f, col_arm, hand_cx, hand_cy, 360.0f, 300.0f);
    // Steve Hand
    DrawVoxelBox(hand_faces, {0.0f, 30.0f - bob, 0.0f}, {13.0f, 10.0f, 13.0f}, -0.6f + swing_angle, 0.35f, 0.15f, col_skin, hand_cx, hand_cy, 360.0f, 300.0f);

    // Held item based on selected slot
    if (g_selected_slot == 0) {
        // Diamond Sword
        DrawVoxelBox(hand_faces, {-10.0f, 40.0f - bob, 15.0f}, {6.0f, 48.0f, 2.5f}, -0.7f + swing_angle, 0.35f, 0.15f, col_blade, hand_cx, hand_cy, 360.0f, 300.0f);
        DrawVoxelBox(hand_faces, {-10.0f, -4.0f - bob, 15.0f}, {16.0f, 4.0f, 4.0f}, -0.7f + swing_angle, 0.35f, 0.15f, col_wood, hand_cx, hand_cy, 360.0f, 300.0f);
    } else if (g_selected_slot == 1) {
        // Diamond Pickaxe
        DrawVoxelBox(hand_faces, {-10.0f, 30.0f - bob, 15.0f}, {4.0f, 40.0f, 4.0f}, -0.7f + swing_angle, 0.35f, 0.15f, col_wood, hand_cx, hand_cy, 360.0f, 300.0f);
        DrawVoxelBox(hand_faces, {-10.0f, 68.0f - bob, 15.0f}, {22.0f, 6.0f, 5.0f}, -0.7f + swing_angle, 0.35f, 0.15f, col_blade, hand_cx, hand_cy, 360.0f, 300.0f);
    } else if (g_selected_slot == 2) {
        // Dirt Block held in hand
        DrawVoxelBox(hand_faces, {-12.0f, 35.0f - bob, 15.0f}, {16.0f, 16.0f, 16.0f}, -0.5f + swing_angle, 0.4f, 0.1f, col_dirt, hand_cx, hand_cy, 360.0f, 300.0f);
    } else if (g_selected_slot == 3) {
        // Stone Block held in hand
        DrawVoxelBox(hand_faces, {-12.0f, 35.0f - bob, 15.0f}, {16.0f, 16.0f, 16.0f}, -0.5f + swing_angle, 0.4f, 0.1f, col_stone, hand_cx, hand_cy, 360.0f, 300.0f);
    }

    std::sort(hand_faces.begin(), hand_faces.end(), [](const ProjectedFace& a, const ProjectedFace& b) {
        return a.avg_z > b.avg_z;
    });

    for (const auto& f : hand_faces) {
        draw->AddConvexPolyFilled(f.pts, 4, f.color);
        draw->AddPolyline(f.pts, 4, IM_COL32(20, 20, 20, 180), ImDrawFlags_Closed, 1.2f);
    }
}

void RenderThirdPersonSteve(float screen_w, float screen_h) {
    ImDrawList* draw = ImGui::GetForegroundDrawList();

    float doll_cx = screen_w * 0.5f;
    float doll_cy = screen_h * 0.65f;

    std::vector<ProjectedFace> doll_faces;
    float leg_swing = sinf(g_anim_timer * 5.0f) * 0.55f;
    float arm_swing = sinf(g_anim_timer * 5.0f) * 0.55f;

    ImU32 col_head = IM_COL32(219, 176, 140, 255); // Steve skin
    ImU32 col_shirt = IM_COL32(0, 168, 168, 255);  // Steve cyan shirt
    ImU32 col_pants = IM_COL32(43, 53, 143, 255);  // Steve blue jeans

    // Head
    DrawVoxelBox(doll_faces, {0.0f, 65.0f, 0.0f}, {18.0f, 18.0f, 18.0f}, 0.0f, 0.0f, 0.0f, col_head, doll_cx, doll_cy, 380.0f, 250.0f);
    // Torso
    DrawVoxelBox(doll_faces, {0.0f, 26.0f, 0.0f}, {18.0f, 22.0f, 9.0f}, 0.0f, 0.0f, 0.0f, col_shirt, doll_cx, doll_cy, 380.0f, 250.0f);
    // Left Arm
    DrawVoxelBox(doll_faces, {-26.0f, 26.0f, 0.0f}, {8.0f, 22.0f, 8.0f}, arm_swing, 0.0f, 0.0f, col_shirt, doll_cx, doll_cy, 380.0f, 250.0f);
    // Right Arm
    DrawVoxelBox(doll_faces, {26.0f, 26.0f, 0.0f}, {8.0f, 22.0f, 8.0f}, -arm_swing, 0.0f, 0.0f, col_shirt, doll_cx, doll_cy, 380.0f, 250.0f);
    // Left Leg
    DrawVoxelBox(doll_faces, {-9.0f, -22.0f, 0.0f}, {8.5f, 26.0f, 8.5f}, -leg_swing, 0.0f, 0.0f, col_pants, doll_cx, doll_cy, 380.0f, 250.0f);
    // Right Leg
    DrawVoxelBox(doll_faces, {9.0f, -22.0f, 0.0f}, {8.5f, 26.0f, 8.5f}, leg_swing, 0.0f, 0.0f, col_pants, doll_cx, doll_cy, 380.0f, 250.0f);

    std::sort(doll_faces.begin(), doll_faces.end(), [](const ProjectedFace& a, const ProjectedFace& b) {
        return a.avg_z > b.avg_z;
    });

    for (const auto& f : doll_faces) {
        draw->AddConvexPolyFilled(f.pts, 4, f.color);
        draw->AddPolyline(f.pts, 4, IM_COL32(20, 20, 20, 160), ImDrawFlags_Closed, 1.2f);
    }
}

void ScanSekiroProcessMemory() {
    g_sekiro_base = reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr));
    if (!g_sekiro_base) return;

    MODULEINFO mod_info{};
    if (GetModuleInformation(GetCurrentProcess(), reinterpret_cast<HMODULE>(g_sekiro_base), &mod_info, sizeof(mod_info))) {
        Log("Sekiro Base: 0x%p, Size: 0x%08X", (void*)g_sekiro_base, mod_info.SizeOfImage);
    }

    // Initialize Sekiro Native ChrIns
    g_native_player.Handle = 1;
    g_native_player.Name = "Wolf";
    g_native_player.Health = 100.0f;
    g_native_player.MaxHealth = 100.0f;
    g_native_player.ModelAlpha = 1.0f;
    g_native_player.bModelHidden = false;
    g_native_player.bCapsulePhysicsActive = true;

    // Initialize Camera looking forward
    g_native_camera.Position = {0.0f, 100.0f, 0.0f};
    g_native_camera.Forward = {0.0f, 0.0f, 1.0f};

    SekiroMod_Initialize(&g_native_player, &g_native_camera);
    Log("Initialized mc-core Sekiro Adapter with native player & camera contexts.");
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
    return g_original_resize_buffers(pSwapChain, buffer_count, width, height, new_format, swap_chain_flags);
}

HRESULT WINAPI DetourPresent(IDXGISwapChain* pSwapChain, UINT sync_interval, UINT flags) {
    if (!g_mod_initialized.load()) {
        ScanSekiroProcessMemory();
        g_last_frame_time = std::chrono::steady_clock::now();
        g_mod_initialized.store(true);
    }

    if (!g_imgui_initialized.load()) {
        InitImGui(pSwapChain);
    }

    // F6 Hotkey toggle (Steve Mode)
    if (GetAsyncKeyState(VK_F6) & 1) {
        bool current_active = SekiroMod_IsSteveModeActive();
        bool new_active = !current_active;
        SekiroMod_SetSteveMode(new_active);
        MessageBeep(MB_ICONASTERISK);
        Log(">>> Hotkey [F6] triggered! Steve Mode: %s", new_active ? "ACTIVE (WOLF HIDDEN)" : "STANDBY (WOLF RESTORED)");
    }

    // F5 Hotkey toggle (Perspective: First-Person vs Third-Person Steve)
    if (GetAsyncKeyState(VK_F5) & 1) {
        g_is_first_person = !g_is_first_person;
        MessageBeep(MB_OK);
        Log(">>> Hotkey [F5] triggered! Perspective toggled to: %s", g_is_first_person ? "First-Person" : "Third-Person Steve");
    }

    // Hotbar 1-9 switch keys
    for (int k = 0; k < 9; ++k) {
        if (GetAsyncKeyState('1' + k) & 1) {
            g_selected_slot = k;
            Log("Selected hotbar slot: %d", k + 1);
        }
    }

    // Compute delta time
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - g_last_frame_time).count();
    g_last_frame_time = now;
    if (dt <= 0.0f || dt > 0.1f) {
        dt = 1.0f / 60.0f;
    }

    g_anim_timer += dt;
    if (g_attack_anim > 0.0f) {
        g_attack_anim = std::max(0.0f, g_attack_anim - dt * 3.5f);
    }

    const bool is_steve_mode = SekiroMod_IsSteveModeActive();

    // In-Game Interactions when Steve Mode is Active
    if (is_steve_mode) {
        auto* adapter = mc::adapter::GetGlobalSekiroAdapter();
        auto* world = mc::adapter::GetGlobalVoxelWorld();
        auto* combat = mc::adapter::GetGlobalCombatEngine();

        // Right Click: Place Voxel Block (aligned to 100cm grid)
        if (GetAsyncKeyState(VK_RBUTTON) & 1) {
            if (adapter && world) {
                mc::Vec3 cam_pos = adapter->getCameraPosition();
                mc::Vec3 cam_fwd = adapter->getCameraForward();
                mc::Vec3 ray_end = cam_pos + cam_fwd * 500.0f; // 5 meters

                mc::RaycastResult hit = adapter->raycastWorld(cam_pos, ray_end);
                if (hit.has_hit) {
                    auto target = mc::VoxelWorld::calculatePlacementTarget(hit);
                    if (target) {
                        mc::BlockId b = mc::BlockId::Stone;
                        if (g_selected_slot == 2) b = mc::BlockId::Dirt;
                        else if (g_selected_slot == 3) b = mc::BlockId::Stone;
                        else if (g_selected_slot == 4) b = mc::BlockId::Tnt;

                        if (world->placeBlock(*target, b, adapter->getPlayerPosition())) {
                            MessageBeep(MB_OK);
                            Log("Placed voxel block at Grid [%d, %d, %d]", target->x, target->y, target->z);
                        }
                    }
                }
            }
        }

        // Left Click: Attack swing or Mine Block
        if (GetAsyncKeyState(VK_LBUTTON) & 1) {
            g_attack_anim = 1.0f; // trigger swing animation
            if (adapter && world && combat) {
                mc::Vec3 cam_pos = adapter->getCameraPosition();
                mc::Vec3 cam_fwd = adapter->getCameraForward();
                mc::Vec3 ray_end = cam_pos + cam_fwd * 500.0f;

                mc::RaycastResult hit = adapter->raycastWorld(cam_pos, ray_end);
                if (hit.has_hit) {
                    if (hit.is_block) {
                        mc::GridPos gp = mc::VoxelWorld::worldToGrid(hit.point);
                        world->mineBlock(gp, (g_selected_slot == 1) ? mc::ItemId::DiamondPickaxe : mc::ItemId::DiamondSword, 0.15f);
                    } else if (hit.hit_entity_id != 0) {
                        auto intent = combat->calculateMeleeHit(0, hit.hit_entity_id, mc::ItemId::DiamondSword, 1.0f, false, true, hit.point, cam_fwd);
                        combat->executeHit(intent);
                    }
                }
            }
        }
    }

    // Tick mc-core engine
    SekiroMod_Tick(dt);

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

        // 1. Top-Left Status Panel
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(520, 115), ImGuiCond_Always);
        ImGuiWindowFlags flags_win = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

        if (is_steve_mode) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 1.0f, 0.8f, 1.0f));
            ImGui::Begin("Minecraft Core Mod (mc-core)", nullptr, flags_win);
            ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.4f, 1.0f), "[ACTIVE] Minecraft Steve Mode Engaged! (Native Wolf Hidden)");
            ImGui::Separator();
            ImGui::Text("View: [%s] (F5 toggle)  |  LMB: Attack/Mine  |  RMB: Place Block", g_is_first_person ? "First-Person" : "Third-Person");
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[F6] Exit Minecraft Mode  |  Keys [1-9] Switch Hotbar Slot");
            ImGui::End();
            ImGui::PopStyleColor();

            // 2. Render Full Authentic 16x16 Pixel Minecraft GUI
            RenderPixelMinecraftGUI(screen_w, screen_h);

            // 3. Render 3D Perspective Visuals
            if (g_is_first_person) {
                RenderFirstPersonViewmodel(screen_w, screen_h);
            } else {
                RenderThirdPersonSteve(screen_w, screen_h);
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
            ImGui::Begin("Minecraft Core Mod (mc-core)", nullptr, flags_win);
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[STANDBY] mc-core Sekiro Adapter Ready");
            ImGui::Separator();
            ImGui::Text("Target: Sekiro: Shadows Die Twice (DirectX 11)");
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), ">>> Press [F6] to ACTIVATE Minecraft Steve Mode <<<");
            ImGui::End();
            ImGui::PopStyleColor();
        }

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
                    Log("Frame %d: Rendered successfully on back buffer (%.0fx%.0f)", g_frame_log_count, screen_w, screen_h);
                }
            }
        }
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
        Log(" mc-core Sekiro Adapter v1.0.0 Loaded");
        Log("==================================================");
        CreateThread(nullptr, 0, LoaderThread, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        if (g_original_present) {
            MH_DisableHook(MH_ALL_HOOKS);
            MH_Uninitialize();
        }
        if (g_mod_initialized.load()) {
            SekiroMod_Shutdown();
        }
        if (g_imgui_initialized.load()) {
            if (g_srv_diamond_sword) g_srv_diamond_sword->Release();
            if (g_srv_diamond_pickaxe) g_srv_diamond_pickaxe->Release();
            if (g_srv_dirt) g_srv_dirt->Release();
            if (g_srv_stone) g_srv_stone->Release();
            if (g_srv_tnt) g_srv_tnt->Release();
            if (g_srv_heart) g_srv_heart->Release();
            if (g_srv_drumstick) g_srv_drumstick->Release();

            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
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
