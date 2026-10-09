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
// Debug: bind the next captured 1920x1080 R32G8X24 depth resource (the game creates several).
void CycleDepthCandidate();

// Called on the Present thread once per frame; fills `out`. Return false to skip the overlay entirely.
using HudProvider = bool (*)(HudState& out, SteveState& steve);
using LogFn = void (*)(const char* fmt, ...);

// Creates the hooks. Returns false (and logs why) when anything fails; nothing is left half-installed.
// MinHook must already be initialised by the caller.
bool Install(HudProvider provider, LogFn log);

} // namespace erov
