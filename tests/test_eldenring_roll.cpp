#include <gtest/gtest.h>

#include "eldenring_roll.hpp"

using namespace eldenring::roll;

namespace {

TEST(EldenRingRoll, DefaultRollConstantsMatchDirectInputShift) {
    EXPECT_EQ(kDefaultRollVk, kVkShift);
    EXPECT_EQ(kDikLShift, 0x2A); // DIK_LSHIFT
    EXPECT_EQ(kDikRShift, 0x36); // DIK_RSHIFT
    EXPECT_EQ(kDikSpace, 0x39);  // DIK_SPACE
    EXPECT_EQ(kDikF, 0x21);      // DIK_F (Dual Wield)
    EXPECT_EQ(kDikE, 0x12);      // DIK_E (Inventory)
}

TEST(EldenRingRoll, ComputesMaskWhenShiftRollDisabledInMcMode) {
    // When roll is bound to Shift, both LShift and RShift must be masked
    const auto res = computeRollMask(true, true, kVkShift);
    EXPECT_EQ(res.count, 2u);
    EXPECT_EQ(res.scancodes[0], kDikLShift);
    EXPECT_EQ(res.scancodes[1], kDikRShift);
}

TEST(EldenRingRoll, PreservesEssentialKeysWhenRollIsShift) {
    int hidden[16] = {};
    size_t n_hidden = 0;

    const auto res = computeRollMask(true, true, kVkShift);
    n_hidden = mergeMaskedKeys(hidden, 16, n_hidden, res.scancodes, res.count);

    // Essential keys must NEVER be masked by roll suppression:
    EXPECT_TRUE(isEssentialKeyPreserved(hidden, n_hidden, kDikSpace)); // Space (Jump) must NOT be masked!
    EXPECT_TRUE(isEssentialKeyPreserved(hidden, n_hidden, kDikF));     // F (Dual Wield) must NOT be masked!
    EXPECT_TRUE(isEssentialKeyPreserved(hidden, n_hidden, kDikE));     // E (Inventory) must NOT be masked!

    // Shift MUST be masked:
    EXPECT_FALSE(isEssentialKeyPreserved(hidden, n_hidden, kDikLShift));
    EXPECT_FALSE(isEssentialKeyPreserved(hidden, n_hidden, kDikRShift));
}

TEST(EldenRingRoll, DoesNotMaskWhenDisabledIsFalseOrNotInMcMode) {
    const auto res_disabled = computeRollMask(false, true, kVkShift);
    EXPECT_EQ(res_disabled.count, 0u);

    const auto res_not_mc = computeRollMask(true, false, kVkShift);
    EXPECT_EQ(res_not_mc.count, 0u);
}

TEST(EldenRingRoll, MergeMaskedKeysAppendsWithoutDuplicates) {
    int dest[8] = {0x11, 0x1E}; // W, A
    size_t count = 2;

    const int to_add[3] = {0x1F, 0x2A, 0x11}; // S, LShift, W (duplicate)
    count = mergeMaskedKeys(dest, 8, count, to_add, 3);

    EXPECT_EQ(count, 4u);
    EXPECT_EQ(dest[0], 0x11);
    EXPECT_EQ(dest[1], 0x1E);
    EXPECT_EQ(dest[2], 0x1F);
    EXPECT_EQ(dest[3], 0x2A);
}

TEST(EldenRingRoll, MergeMaskedKeysRespectsCapacity) {
    int dest[3] = {0x11, 0x1E};
    size_t count = 2;

    const int to_add[3] = {0x1F, 0x20, 0x2A};
    count = mergeMaskedKeys(dest, 3, count, to_add, 3);

    EXPECT_EQ(count, 3u); // Cannot exceed max_dest = 3
    EXPECT_EQ(dest[2], 0x1F);
}

TEST(EldenRingRoll, PreventsShiftRollWhilePreservingSpaceJump) {
    int hidden[8] = {};
    size_t n_hidden = 0;

    bool disable_roll = true;
    bool mc_mode = true;
    int roll_vk = kVkShift;

    if (disable_roll && mc_mode) {
        const auto res = computeRollMask(true, true, roll_vk);
        n_hidden = mergeMaskedKeys(hidden, 8, n_hidden, res.scancodes, res.count);
    }

    // Space is completely free for Jump:
    EXPECT_TRUE(isEssentialKeyPreserved(hidden, n_hidden, kDikSpace));
    // Shift is masked for roll:
    EXPECT_FALSE(isEssentialKeyPreserved(hidden, n_hidden, kDikLShift));
}

} // namespace
