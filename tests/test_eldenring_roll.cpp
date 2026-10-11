#include <gtest/gtest.h>

#include "eldenring_roll.hpp"

using namespace eldenring::roll;

namespace {

TEST(EldenRingRoll, DefaultRollConstantsMatchDirectInputSpace) {
    EXPECT_EQ(kDefaultRollVk, 0x20); // VK_SPACE
    EXPECT_EQ(kDefaultRollDik, 0x39); // DIK_SPACE (DirectInput 0x39)
}

TEST(EldenRingRoll, ComputesMaskWhenRollDisabledInMcMode) {
    // Disabled in mc_mode: must produce DIK 0x39
    const auto res = computeRollMask(true, true, kDefaultRollVk);
    EXPECT_EQ(res.count, 1u);
    EXPECT_EQ(res.scancodes[0], 0x39);
}

TEST(EldenRingRoll, DoesNotMaskWhenDisabledIsFalseOrNotInMcMode) {
    const auto res_disabled = computeRollMask(false, true, kDefaultRollVk);
    EXPECT_EQ(res_disabled.count, 0u);

    const auto res_not_mc = computeRollMask(true, false, kDefaultRollVk);
    EXPECT_EQ(res_not_mc.count, 0u);
}

TEST(EldenRingRoll, MergeMaskedKeysAppendsWithoutDuplicates) {
    int dest[8] = {0x11, 0x1E}; // W, A
    size_t count = 2;

    const int to_add[3] = {0x1F, 0x39, 0x11}; // S, Space, W (duplicate)
    count = mergeMaskedKeys(dest, 8, count, to_add, 3);

    EXPECT_EQ(count, 4u);
    EXPECT_EQ(dest[0], 0x11);
    EXPECT_EQ(dest[1], 0x1E);
    EXPECT_EQ(dest[2], 0x1F);
    EXPECT_EQ(dest[3], 0x39);
}

TEST(EldenRingRoll, MergeMaskedKeysRespectsCapacity) {
    int dest[3] = {0x11, 0x1E};
    size_t count = 2;

    const int to_add[3] = {0x1F, 0x20, 0x39};
    count = mergeMaskedKeys(dest, 3, count, to_add, 3);

    EXPECT_EQ(count, 3u); // Cannot exceed max_dest = 3
    EXPECT_EQ(dest[2], 0x1F);
}

TEST(EldenRingRoll, PreventsRollWhenMcJumpIsZero) {
    // Simulates the bug scenario where mc_jump=0 left Space unmasked, causing rolls on Space press
    int hidden[8] = {};
    size_t n_hidden = 0;

    bool mc_jump = false;
    bool disable_roll = true;
    bool mc_mode = true;

    if (mc_jump && mc_mode) {
        hidden[n_hidden++] = kDefaultRollDik;
    }
    if (disable_roll && mc_mode) {
        const int roll_dik = kDefaultRollDik;
        n_hidden = mergeMaskedKeys(hidden, 8, n_hidden, &roll_dik, 1);
    }

    EXPECT_EQ(n_hidden, 1u);
    EXPECT_EQ(hidden[0], kDefaultRollDik); // Space is reliably masked!
}

} // namespace
