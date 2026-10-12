#pragma once

#include <cstdint>
#include <cmath>

namespace eldenring::action {

// Action lock bits in CSChrActionFlagModule + 0x40 (uint64_t action_lock_flags).
// Verified in eldenring.exe (v2.7.1.0):
//   0x140406AED: bts rax, 0x13 -> Bit 19 = 0x80000 (Movement Input Lock / WASD translation bypass)
//   0x140406ACF: shl rax, 0x14 -> Bit 20 = 0x100000 (Air / Fall Control Lock)
//   Bit 17 = 0x20000 (Jump pre-takeoff lock)
//   Bit 15 = 0x8000 (Landing recovery impact lock)
//   Bit 8  = 0x100 (Item use / Consumable sip lock)
//   Bit 2  = 0x04 (Damage stagger / break poise lock)
enum ActionLockBit : uint64_t {
    kLockDamageStagger    = (1ULL << 2),   // 0x04
    kLockItemUse          = (1ULL << 8),   // 0x100
    kLockLandingRecovery  = (1ULL << 15),  // 0x8000
    kLockJumpPre          = (1ULL << 17),  // 0x20000
    kLockMoveInput        = (1ULL << 19),  // 0x80000 (0x140406AED: bts rax, 0x13)
    kLockFallControl      = (1ULL << 20),  // 0x100000 (0x140406ACF: shl rax, 0x14)
};

// Mask of locks that prevent movement/WASD driving due to air-time timeout.
inline constexpr uint64_t kAirLockMask = kLockMoveInput | kLockFallControl;

// Checks whether air-induced movement locks are currently active.
inline bool hasAirLocks(uint64_t flags) {
    return (flags & kAirLockMask) != 0;
}

// Clears air-induced movement locks while strictly preserving all other combat/animation locks.
inline uint64_t clearAirLocks(uint64_t flags) {
    return flags & ~kAirLockMask;
}

// Evaluates whether fall timer at FallModule+0x18 should be reset.
// Prevents resetting noise (< 0.1s) while cutting off accumulation before reaching 3.0s lock or 12.0s instakill.
inline bool shouldResetFallTimer(float current_timer, float min_threshold = 0.1f) {
    return current_timer > min_threshold;
}

// Checks if PhysicsModule+0x1B8 is in falling motion confirmed zone (threshold 1.0f at 0x1432A1938).
inline bool isFallMotionConfirmed(float phys_1b8) {
    return phys_1b8 >= 1.0f;
}

} // namespace eldenring::action
