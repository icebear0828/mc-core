#pragma once

// D3D12 overlay for Elden Ring (milestone 1): draws a Dear ImGui HUD into the swap chain's back buffer just before
// the game presents. Hooks (vtable function addresses taken from a throw-away device, MinHook):
//   ID3D12CommandQueue::ExecuteCommandLists - learns the DIRECT queue the game renders with (verified: a single one),
//   IDXGISwapChain::Present                 - records and submits our command list on that queue,
//   IDXGISwapChain::ResizeBuffers           - drops our resources so they are rebuilt after the resize.

namespace erov {

struct HudState {
    bool show{false};     // false: draw nothing this frame (title, menus, loading, fades...)
    bool mc_mode{false};  // F6 toggle: show the Minecraft HUD
    int hp{0};
    int max_hp{0};
    int selected_slot{0}; // 0..8
};

// Called on the Present thread once per frame; fills `out`. Return false to skip the overlay entirely.
using HudProvider = bool (*)(HudState& out);
using LogFn = void (*)(const char* fmt, ...);

// Creates the hooks. Returns false (and logs why) when anything fails; nothing is left half-installed.
// MinHook must already be initialised by the caller.
bool Install(HudProvider provider, LogFn log);

} // namespace erov
