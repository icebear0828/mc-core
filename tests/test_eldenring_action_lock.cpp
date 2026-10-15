#include <gtest/gtest.h>

#include "eldenring_action_lock.hpp"

using namespace eldenring::action;

namespace {

TEST(EldenRingActionLock, BitmaskConstantsMatchReverseEngineeringAddresses) {
    // 0x140406AED: bts rax, 0x13 -> Bit 19 = 0x80000
    EXPECT_EQ(kLockMoveInput, 0x80000ULL);
    EXPECT_EQ(kLockMoveInput, (1ULL << 19));

    // 0x140406ACF: shl rax, 0x14 -> Bit 20 = 0x100000
    EXPECT_EQ(kLockFallControl, 0x100000ULL);
    EXPECT_EQ(kLockFallControl, (1ULL << 20));

    EXPECT_EQ(kAirLockMask, (0x80000ULL | 0x100000ULL));
}

TEST(EldenRingActionLock, DetectsAndClearsAirLocksWithoutTouchingOtherLocks) {
    // A flag with DamageStagger (bit 2), ItemUse (bit 8), and MoveInput lock (bit 19)
    const uint64_t original = kLockDamageStagger | kLockItemUse | kLockMoveInput | kLockFallControl;
    EXPECT_TRUE(hasAirLocks(original));

    const uint64_t cleared = clearAirLocks(original);
    EXPECT_FALSE(hasAirLocks(cleared));

    // Must preserve DamageStagger and ItemUse
    EXPECT_TRUE(cleared & kLockDamageStagger);
    EXPECT_TRUE(cleared & kLockItemUse);
    EXPECT_FALSE(cleared & kLockMoveInput);
    EXPECT_FALSE(cleared & kLockFallControl);
}

TEST(EldenRingActionLock, CleanFlagsDoNotTriggerFalseAirLock) {
    const uint64_t clean = kLockDamageStagger | kLockLandingRecovery;
    EXPECT_FALSE(hasAirLocks(clean));
    EXPECT_EQ(clearAirLocks(clean), clean);
}

TEST(EldenRingActionLock, FallTimerThresholdChecks) {
    // Below minimal noise threshold (e.g. 0.05s) does not require reset
    EXPECT_FALSE(shouldResetFallTimer(0.02f, 0.1f));

    // Greater than threshold (e.g. 0.3s accumulation) triggers reset
    EXPECT_TRUE(shouldResetFallTimer(0.3f, 0.1f));

    // PhysicsModule+0x1B8 threshold is 1.0f (reverser constant at 0x1432A1938)
    EXPECT_FALSE(isFallMotionConfirmed(0.99f));
    EXPECT_TRUE(isFallMotionConfirmed(1.0f));
    EXPECT_TRUE(isFallMotionConfirmed(1.5f));
}

} // namespace
