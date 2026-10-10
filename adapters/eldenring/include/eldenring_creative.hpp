#pragma once

// Creative-mode probes (feat/creative). Everything here is log-only: no hook installed from this file changes the game's
// behaviour. The probes answer one question before we skip anything: which path kills the player after a fall?
//
// Evidence (docs/ELDENRING_REVERSE.md 31, bytes and callers checked against eldenring.exe 2.7.1.0):
//  - 0x1403EDA70 kill wrapper: tests the dead bit, calls KillChr (0x1403FCD90), then the death clean-up. Four callers:
//    0x1403E93DE (script), 0x1403F8543 (fatal damage), 0x140428EEB (unload), 0x14042BC1E (map kill box).
//  - 0x14044E090 hard landing (rcx = FallModule*): asks 0x1404FA370(spEffectContainer, 0x8F) at 0x14044E0DF; when it answers
//    true the function jumps to its exit 0x14044E206, otherwise it writes [[chr+0x190]+8]+0x34 = 6 and sets the shake flag.
//  - 0x14044E240 fall height (rcx = FallModule*, returns metres in xmm0): FallModule+0x1D != 0 returns the constant 10000.0
//    (0x142A1D924); otherwise takeoff Y - current Y. Five callers; 0x14041131F multiplies it by 100 (centimetres, the fall
//    damage input), 0x1405A7A7F compares it with 60.0 m.

#include "eldenring_live.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

namespace eldenring::creative {

// ---- creative / survival (F5, inside MC mode) -----------------------------------------------------------------------------
// Survival is the default and behaves like vanilla; Creative gets the item palette, no fall damage, items that are not used up and no
// health/hunger/experience bars. One place decides every difference so the HUD, the inventory and the hooks cannot disagree.
enum class GameMode : uint8_t { Survival, Creative };

inline GameMode toggled(GameMode m) { return m == GameMode::Survival ? GameMode::Creative : GameMode::Survival; }
inline const char* modeName(GameMode m) { return m == GameMode::Creative ? "Creative Mode" : "Survival Mode"; }
inline bool showsPalette(GameMode m) { return m == GameMode::Creative; }
inline bool zeroesFall(GameMode m) { return m == GameMode::Creative; }
inline bool consumesItems(GameMode m) { return m == GameMode::Survival; }
inline bool showsVitals(GameMode m) { return m == GameMode::Survival; }

// The SpEffect the hard-landing function asks about, and the return address of that one call (0x14044E0DF + 5).
inline constexpr int kLandingSkipSpEffect = 0x8F;
inline constexpr uintptr_t kLandingSpEffectReturnRva = 0x44E0E4;

// Heights below this (metres) are ordinary walking and are not logged.
inline constexpr float kFallLogMinMetres = 1.f;
inline constexpr uint64_t kFallLogIntervalMs = 250;

// A null owner (no world yet) must never count as the player.
inline bool isPlayer(uintptr_t chr, uintptr_t player) { return chr != 0 && chr == player; }

// Fall protection (blocks or our own jump make the game measure a fall against the real ground) skips the game's KillChr for the
// player. It must not do so once the hit points are already gone: the damage path killed the player, KillChr is what starts the death,
// and skipping it leaves a player at 0 hp that never dies and never respawns.
inline bool shouldSkipPlayerKill(bool fall_protect, bool mc_mode, bool blocks_or_recent_jump, int hp) {
    return fall_protect && mc_mode && blocks_or_recent_jump && hp > 0;
}

// Creative mode, no fall damage: the game's fall-height function (0x14044E240) answers 0 for the player. Callers turn the height into
// the fall damage (0x14041131F multiplies it by 100) and compare it with thresholds, so 0 means no damage and no lethal fall.
inline bool shouldZeroFall(bool creative_nofall, bool mc_mode, bool owner_is_player) { return creative_nofall && mc_mode && owner_is_player; }

// Creative mode cannot be killed while it has hit points: the kill wrapper (0x1403EDA70) is skipped for the player as a whole, whoever calls
// it (the animation event "die" after a long time in the air, the map kill box, scripts). Once the hit points are 0 the death goes through.
inline bool shouldBlockPlayerKill(GameMode mode, bool mc_mode, int hp) { return mode == GameMode::Creative && mc_mode && hp > 0; }

// "The fall lasted too long" (FallModule+0x18 above a threshold) is decided by 0x14044E3A0 and starts the fall death. Creative mode answers no.
inline bool shouldDenyLongFall(GameMode mode, bool mc_mode, bool owner_is_player) { return mode == GameMode::Creative && mc_mode && owner_is_player; }

inline bool shouldLogSpEffect(int sp_effect, uintptr_t caller_rva, bool owner_is_player) {
    return owner_is_player && sp_effect == kLandingSkipSpEffect && caller_rva == kLandingSpEffectReturnRva;
}

// Per-caller rate limit for the fall height probe: it runs every frame from several callers.
struct FallLogState {
    static constexpr int kSlots = 8;
    uintptr_t caller[kSlots]{};
    uint64_t last_ms[kSlots]{};
    int used{0};
};

inline bool shouldLogFall(FallLogState& st, float metres, uintptr_t caller_rva, uint64_t now_ms) {
    const bool interesting = !std::isfinite(metres) || metres >= kFallLogMinMetres;
    if (!interesting) return false;
    for (int i = 0; i < st.used; ++i) {
        if (st.caller[i] != caller_rva) continue;
        if (now_ms - st.last_ms[i] < kFallLogIntervalMs) return false;
        st.last_ms[i] = now_ms;
        return true;
    }
    if (st.used < FallLogState::kSlots) {
        st.caller[st.used] = caller_rva;
        st.last_ms[st.used] = now_ms;
        ++st.used;
    }
    return true;
}

inline std::string formatKill(uintptr_t caller_rva, bool player) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "creative: kill wrapper called (caller RVA 0x%llX) player=%d", static_cast<unsigned long long>(caller_rva),
                  player ? 1 : 0);
    return buf;
}

inline std::string formatLanding(bool player) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "creative: hard landing handler entered player=%d", player ? 1 : 0);
    return buf;
}

inline std::string formatFall(uintptr_t caller_rva, float metres) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "creative: fall height %.2f m (caller RVA 0x%llX)", static_cast<double>(metres),
                  static_cast<unsigned long long>(caller_rva));
    return buf;
}

inline std::string formatFallZeroed(uintptr_t caller_rva, float original_metres) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "creative: fall height %.2f m zeroed for the player (caller RVA 0x%llX)", static_cast<double>(original_metres),
                  static_cast<unsigned long long>(caller_rva));
    return buf;
}

// 0x140428DE0 dispatches character events by type ([[event+8]] = type, word [+0xE] = the SpEffect the event requires, byte [event+0x18] = flag).
// Types that end in a death or a tear-down of the character; everything else (attacks, sounds...) is far too frequent to log.
inline bool isDeathEventType(uint32_t type) { return type == 12 || type == 46 || type == 47 || type == 48 || type == 126; }

inline std::string formatChrEvent(uint32_t type, unsigned required_sp_effect, bool flag, const unsigned char raw[16], uintptr_t caller_rva) {
    char buf[320];
    int n = std::snprintf(buf, sizeof(buf), "creative: chr event %u (needs SpEffect %u) flag=%d caller RVA 0x%llX raw:", type, required_sp_effect, flag ? 1 : 0,
                          static_cast<unsigned long long>(caller_rva));
    for (int i = 0; i < 16 && n > 0 && static_cast<size_t>(n) + 4 < sizeof(buf); ++i) n += std::snprintf(buf + n, sizeof(buf) - static_cast<size_t>(n), " %02X", raw[i]);
    return buf;
}

// A call stack as RVAs of the game image ("?" for an address outside it): who raised an event.
inline std::string formatStack(const uintptr_t* frames, size_t n, uintptr_t image_base, uintptr_t image_size) {
    std::string out = "creative:   stack:";
    for (size_t i = 0; i < n; ++i) {
        char b[32];
        if (frames[i] >= image_base && frames[i] - image_base < image_size) {
            std::snprintf(b, sizeof(b), " 0x%llX", static_cast<unsigned long long>(frames[i] - image_base));
        } else {
            std::snprintf(b, sizeof(b), " ?");
        }
        out += b;
    }
    return out;
}

inline std::string formatSpEffect(bool answer) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "creative: landing asked for SpEffect 0x8F, the game returned %d", answer ? 1 : 0);
    return buf;
}

} // namespace eldenring::creative
