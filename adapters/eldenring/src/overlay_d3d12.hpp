#pragma once

// D3D12 overlay for Elden Ring (milestone 1): draws a Dear ImGui HUD into the swap chain's back buffer just before
// the game presents. Hooks (vtable function addresses taken from a throw-away device, MinHook):
//   ID3D12CommandQueue::ExecuteCommandLists - learns the DIRECT queue the game renders with (verified: a single one),
//   IDXGISwapChain::Present                 - records and submits our command list on that queue,
//   IDXGISwapChain::ResizeBuffers           - drops our resources so they are rebuilt after the resize.

#include "mc/rig.hpp"

namespace erov {

struct HudState {
    bool show{false};     // false: draw nothing this frame (title, menus, loading, fades...)
    bool mc_mode{false};  // F6 toggle: show the Minecraft HUD
    int hp{0};
    int max_hp{0};
    int selected_slot{0}; // 0..8
    float hit{0.f};       // crosshair hit marker 0..1 (fades by itself)
    bool hit_crit{false}; // that marker is for a critical hit
    float kill{0.f};      // kill marker 0..1
    uint16_t hotbar[9]{}; // mc::ItemId of each hotbar slot
    uint8_t hotbar_count[9]{}; // stack sizes (shown when above 1)
    float absorption_mc{0.f}; // golden hearts, in Minecraft hit points (2 per heart)
    float eating{0.f};    // 0..1 progress of the meal being eaten (0 = not eating)
    float totem{0.f};     // 0..1 totem-of-undying pop animation (1 = just popped)
    int slot_probe{-1};   // >= 0: the F9 part-slot probe is on, this is the only hidden part slot
};

// The 3D figure for this frame: camera, standing point (feet, game metres) and heading (rotation about +Y).
struct SteveState {
    bool draw{false};
    mc::rig::Camera cam{};
    float fov_y{0.8378f};
    float feet[3]{};
    float yaw{0.f};
    float swing{0.f}; // arm swing progress 0..1 (0 = idle)
    bool dead{false}; // the player's HP is 0: the figure falls over (Minecraft death flip)
    float hurt{0.f};  // 1 just hurt .. 0: the figure flashes red
    bool cam_valid{false}; // cam/fov_y are a real camera this frame (also set while the figure itself is not drawn)
};

// Tuning read from mc_er_steve.txt (key=value lines).
struct SteveConfig {
    bool occlusion{true};
    int debug{0};
    float depthview_gain{0.f}; // > 0: draw the scene depth as a grey overlay with this gain (calibration) // 1: colour the figure by how the scene depth compares with it (calibration)
    float depth_const{0.0501f};
    float rel_bias{0.08f};
    float abs_bias{0.05f};
    float scene_height{0.f}; // 0: use the back buffer height for the projection aspect
};
void SetSteveConfig(const SteveConfig& cfg);
// The Steve skin (RGBA8, 64x64) the loader decoded; call before the overlay is created. Without it the figure is flat grey-brown.
void SetSteveSkin(const uint8_t* rgba, unsigned width, unsigned height);
// The HUD atlas (RGBA8, 256x256, the locally extracted mc_hud_atlas.png); call before the overlay is created. Without it the HUD
// is drawn with plain rectangles.
void SetHudAtlas(const uint8_t* rgba, unsigned width, unsigned height);
// Writes `frames` lines of per-frame figure and camera positions to the log (diagnosing jitter).
void RequestFrameTrace(int frames);

// Combat particles (Minecraft's critical hit stars, damage hearts, sweep arc) at a world position (game metres); `count` is the
// number of hearts for Damage. Thread safe; the overlay projects and draws them.
enum class FxKind { Crit, Damage, Sweep };
void SpawnFx(FxKind kind, const float world_pos[3], int count);
// Debug: bind the next captured 1920x1080 R32G8X24 depth resource (the game creates several).
void CycleDepthCandidate();

// Called on the Present thread once per frame; fills `out`. Return false to skip the overlay entirely.
using HudProvider = bool (*)(HudState& out, SteveState& steve);
using LogFn = void (*)(const char* fmt, ...);

// Creates the hooks. Returns false (and logs why) when anything fails; nothing is left half-installed.
// MinHook must already be initialised by the caller.
bool Install(HudProvider provider, LogFn log);

} // namespace erov
