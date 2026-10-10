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

// The SpEffect the hard-landing function asks about, and the return address of that one call (0x14044E0DF + 5).
inline constexpr int kLandingSkipSpEffect = 0x8F;
inline constexpr uintptr_t kLandingSpEffectReturnRva = 0x44E0E4;

// Heights below this (metres) are ordinary walking and are not logged.
inline constexpr float kFallLogMinMetres = 1.f;
inline constexpr uint64_t kFallLogIntervalMs = 250;

// A null owner (no world yet) must never count as the player.
inline bool isPlayer(uintptr_t chr, uintptr_t player) { return chr != 0 && chr == player; }

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

inline std::string formatSpEffect(bool answer) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "creative: landing asked for SpEffect 0x8F, the game returned %d", answer ? 1 : 0);
    return buf;
}

} // namespace eldenring::creative
