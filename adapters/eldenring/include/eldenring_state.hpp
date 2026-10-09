#pragma once

// Read-only game UI state: cursor/menu, loading, fade. Every constant states its evidence level (see
// docs/ELDENRING_REVERSE.md 1.12). A reader returns false (and leaves its output untouched) when the singleton
// is missing; callers treat "unknown" as "do not draw / do not act".

#include "eldenring_live.hpp"

#include <algorithm>

namespace eldenring::live {

namespace layout {
// CSMenuMan (singleton). +0x1A = "mouse cursor disabled": the constructor writes +0x1A=0. NOT a "menu open" flag:
// live capture (keyboard+mouse with a gamepad connected) read 1 in every menu and went to 0 only for ~0.4 s during a
// load. It probably follows the last-used input device, so it is exposed but never used to decide anything.
inline constexpr uintptr_t kMenuDisableMouseCursor = 0x1A;
// A (live, 0.1 s sampling): bitmasks of the focused menu layer. +0x1C: 0x10 main menu hub, 0x11 a page of it
// (equipment), 0x50 a deeper page, 0x04 world map; +0x1D: 0x01 system menu, 0x40 another page. All are 0 in the open
// world. "Any menu focused" = either one non-zero.
inline constexpr uintptr_t kMenuLayerMaskA = 0x1C;
inline constexpr uintptr_t kMenuLayerMaskB = 0x1D;
// B+: popup object pointer (non-null while a popup is open); the native HUD gate (0x7733F0) reads the same field.
inline constexpr uintptr_t kMenuPopup = 0x798;
// B: uint8_t[0x46] from +0x90; value 0 closed, 1 resident, 3 transitioning, 7 open and focused (inferred bit
// meanings). Slot meanings are from one sequence each and are NOT used here: only the raw bytes are exposed.
inline constexpr uintptr_t kMenuUiStates = 0x90;
inline constexpr uintptr_t kMenuUiStateCount = 0x46;

// CSNowLoadingHelper (singleton). A (live, 0.1 s sampling): the loading job pointer +0xE0 is a heap pointer only
// for about 1 s at the start of a map teleport; the whole black screen (about 3.9 s) is +0xED == 0 (it is 1 while
// playing). Use the latch for "the screen is a loading screen"; the job pointer is "a load was just kicked off".
inline constexpr uintptr_t kLoadingJob = 0xE0;
inline constexpr uintptr_t kLoadingLatch = 0xED;
inline constexpr uintptr_t kLoadCounter = 0x44; // A: +1 per load (3 -> 4 on a teleport)

// CSFade (singleton). [+0x10 + i*8] = FadePlate*, 9 plates; plate+0x10 is the current RGBA (source: fromsoftware-rs),
// so alpha is +0x1C. A (live): plate 2 is the full-screen black fade, it rises 0 -> 1 over about 0.5 s before a
// load screen and falls back after it, and it also does so for fades without any load (resting at a grace). Plates
// 7 and 8 did not change in any of them.
inline constexpr uintptr_t kFadePlates = 0x10;
inline constexpr uint32_t kFadePlateCount = 9;
inline constexpr uintptr_t kFadePlateAlpha = 0x1C;
inline constexpr uint32_t kFadeScreenPlate = 2;
} // namespace layout

// cursor_released: the engine has handed the mouse to a UI (title, system/equipment/map menu, popups); false while
// the mouse steers the 3D camera.
struct MenuState {
    bool cursor_released{false}; // see kMenuDisableMouseCursor: informational only
    bool popup_open{false};
    uint8_t layer_mask_a{0};
    uint8_t layer_mask_b{0};
    bool menu_focused{false}; // layer_mask_a | layer_mask_b != 0
};

inline bool readMenuState(const IMemoryReader& reader, uintptr_t menu_man, MenuState& out) {
    if (menu_man == 0) return false;
    uint8_t cursor_disabled = 0, mask_a = 0, mask_b = 0;
    uint64_t popup = 0;
    if (!reader.read(menu_man + layout::kMenuDisableMouseCursor, &cursor_disabled, 1)) return false;
    if (!reader.read(menu_man + layout::kMenuLayerMaskA, &mask_a, 1)) return false;
    if (!reader.read(menu_man + layout::kMenuLayerMaskB, &mask_b, 1)) return false;
    if (!reader.read(menu_man + layout::kMenuPopup, &popup, sizeof(popup))) return false;
    out.cursor_released = cursor_disabled == 0;
    out.popup_open = popup != 0;
    out.layer_mask_a = mask_a;
    out.layer_mask_b = mask_b;
    out.menu_focused = (mask_a | mask_b) != 0;
    return true;
}

// Raw ui_states bytes; interpretation is left to the caller until the slot map is confirmed.
inline bool readUiStates(const IMemoryReader& reader, uintptr_t menu_man, uint8_t out[layout::kMenuUiStateCount]) {
    if (menu_man == 0) return false;
    uint8_t tmp[layout::kMenuUiStateCount];
    if (!reader.read(menu_man + layout::kMenuUiStates, tmp, sizeof(tmp))) return false;
    std::copy(tmp, tmp + sizeof(tmp), out);
    return true;
}

struct LoadingState {
    bool job_active{false};   // +0xE0 != 0 (about the first second of a load)
    bool screen_loading{false}; // +0xED == 0 (the whole loading black screen)
    int32_t load_counter{0};
};

inline bool readLoadingState(const IMemoryReader& reader, uintptr_t helper, LoadingState& out) {
    if (helper == 0) return false;
    uint64_t job = 0;
    int32_t counter = 0;
    uint8_t latch = 1;
    if (!reader.read(helper + layout::kLoadingJob, &job, sizeof(job))) return false;
    if (!reader.read(helper + layout::kLoadingLatch, &latch, 1)) return false;
    if (!reader.read(helper + layout::kLoadCounter, &counter, sizeof(counter))) return false;
    out.job_active = job != 0;
    out.screen_loading = latch == 0;
    out.load_counter = counter;
    return true;
}

// Screen darkness 0..1 from the full-screen fade plate. NaN/out-of-range values are rejected (false). The
// CSFade+0x5C field reads 10.0 at rest, so it is a rate, not a multiplier: it is deliberately not applied.
inline bool readFadeAlpha(const IMemoryReader& reader, uintptr_t fade, float& out) {
    if (fade == 0) return false;
    uint64_t plate = 0;
    float alpha = 0.f;
    if (!reader.read(fade + layout::kFadePlates + layout::kFadeScreenPlate * 8, &plate, sizeof(plate)) || plate == 0) return false;
    if (!reader.read(static_cast<uintptr_t>(plate) + layout::kFadePlateAlpha, &alpha, sizeof(alpha))) return false;
    if (!(alpha >= 0.f && alpha <= 1.f)) return false; // also rejects NaN
    out = alpha;
    return true;
}

} // namespace eldenring::live
