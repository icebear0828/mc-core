#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "eldenring_debug_probes.hpp"
#include "eldenring_overlay.hpp"
#include "eldenring_runtime_context.hpp"

namespace eldenring::ui {

// Pure mapping helpers used across HUD and unit tests
void PopulateHudStateFromContext(erov::HudState& out, const RuntimeContext& ctx);
void PopulateSteveStateFromContext(erov::SteveState& steve, const RuntimeContext& ctx, bool dead, bool sleeping);

#if defined(WIN32) && !defined(MC_CORE_TESTS)
// Native player model visibility control
void RestoreNativeModel(std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags, std::mutex& native_mutex);
void UpdateNativeModel(uintptr_t player, bool hide, const live::IMemoryReader& reader, uintptr_t base,
                       RuntimeContext& ctx, std::unordered_map<uintptr_t, debug::HiddenWord>& hidden_flags,
                       std::mutex& native_mutex);

// Frame HUD provider callback invoked by Present hook
bool HudProvider(erov::HudState& out, erov::SteveState& steve);

// Overlay setup and config initialization
void SetupOverlay(const std::string& game_dir, RuntimeContext& ctx);
#endif

} // namespace eldenring::ui
