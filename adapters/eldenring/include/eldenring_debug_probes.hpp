#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "eldenring_live.hpp"
#include "eldenring_runtime_context.hpp"

namespace eldenring::debug {

struct HiddenWord {
    uint32_t original;
    uint32_t mask;
};

inline uint32_t computeProbeMask(int slot) {
    if (slot < 0) return 0xFFFFFFFFu;
    return 1u << static_cast<unsigned>(slot);
}

template <typename SlotPredicate>
inline int advanceSlotCursor(int current_cursor, int max_slots, SlotPredicate&& has_slot) {
    int next = current_cursor;
    for (int tries = 0; tries <= max_slots; ++tries) {
        next = next + 1;
        if (next >= max_slots) {
            return -1;
        }
        if (has_slot(next)) {
            return next;
        }
    }
    return -1;
}

inline void ResetSlotProbe(int& cursor,
                           std::atomic<uint32_t>& hide_slots,
                           std::atomic<int>& slot_probe,
                           std::atomic<bool>& slots_changed,
                           std::function<void(const char*)> log = {}) {
    if (cursor < 0 && slot_probe.load() < 0 && hide_slots.load() == 0xFFFFFFFFu) return;
    cursor = -1;
    hide_slots.store(0xFFFFFFFFu);
    slot_probe.store(-1);
    slots_changed.store(true);
    if (log) log("slots: probe reset, hiding ALL part slots");
}

#if defined(WIN32)
// Hardware write breakpoint watch for camera position (opt-in mc_er_camwatch.txt)
struct CamWatchHit {
    uint64_t rip{0};
    uint64_t stack[6]{};
};

LONG CALLBACK CamWatchVeh(PEXCEPTION_POINTERS info);
void ArmThreadWatch(DWORD tid, uintptr_t addr);
DWORD WINAPI CamWatchThread(LPVOID);

// Spirit-ash buddy thread (mc_er_buddylog.txt)
DWORD WINAPI BuddyThread(LPVOID);

// Diagnostic loggers
void LogGroundBytesOnChange(const live::IMemoryReader& reader, uintptr_t base, uintptr_t world);
void LogPhysicsVectors(const live::IMemoryReader& reader, uintptr_t base, uintptr_t world);
void LogPlayerPoseForBuddy(const live::IMemoryReader& reader, uintptr_t base, uintptr_t player);
void SnapshotNativeParts(const live::IMemoryReader& reader, uintptr_t base, uintptr_t world,
                         const RuntimeContext& ctx, const std::unordered_map<uintptr_t, HiddenWord>& hidden_flags,
                         std::mutex& native_mutex);
void CycleHiddenSlot(int& cursor, uintptr_t player, const live::IMemoryReader& reader, uintptr_t base,
                     std::atomic<uint32_t>& hide_slots, std::atomic<int>& slot_probe, std::atomic<bool>& slots_changed);
void LogHitGeometry(const char* tag, void* attacker, uintptr_t player, const uint8_t* ctx,
                    const live::IMemoryReader& reader, uintptr_t base);
void LogPlayerHit(void* module, void* attacker, const uint8_t* ctx, uint8_t blocked_flag,
                  const live::IMemoryReader& reader, uintptr_t base, uintptr_t world);
void LogChrScan(uintptr_t chr, size_t index, const live::IMemoryReader& reader, uintptr_t base, uintptr_t player);
#endif

} // namespace eldenring::debug
