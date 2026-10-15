#include <gtest/gtest.h>

#include "eldenring_debug_probes.hpp"

using namespace eldenring::debug;

TEST(DebugProbesTest, ResetSlotProbeResetsAllState) {
    int cursor = 4;
    std::atomic<uint32_t> hide_slots{1u << 4};
    std::atomic<int> slot_probe{4};
    std::atomic<bool> slots_changed{false};

    ResetSlotProbe(cursor, hide_slots, slot_probe, slots_changed);

    EXPECT_EQ(cursor, -1);
    EXPECT_EQ(hide_slots.load(), 0xFFFFFFFFu);
    EXPECT_EQ(slot_probe.load(), -1);
    EXPECT_TRUE(slots_changed.load());

    // Second call is a no-op
    slots_changed.store(false);
    ResetSlotProbe(cursor, hide_slots, slot_probe, slots_changed);
    EXPECT_FALSE(slots_changed.load());
}

TEST(DebugProbesTest, ComputeProbeSlotMask) {
    EXPECT_EQ(computeProbeMask(-1), 0xFFFFFFFFu);
    EXPECT_EQ(computeProbeMask(0), 1u);
    EXPECT_EQ(computeProbeMask(3), 8u);
    EXPECT_EQ(computeProbeMask(31), (1u << 31));
}

TEST(DebugProbesTest, AdvanceSlotCursor) {
    // When starting at -1, advance finds the next valid slot
    int cursor = -1;
    cursor = advanceSlotCursor(cursor, 27, [](int slot) {
        return slot == 2 || slot == 5;
    });
    EXPECT_EQ(cursor, 2);

    cursor = advanceSlotCursor(cursor, 27, [](int slot) {
        return slot == 2 || slot == 5;
    });
    EXPECT_EQ(cursor, 5);

    // Past last valid slot -> wraps to -1
    cursor = advanceSlotCursor(cursor, 27, [](int slot) {
        return slot == 2 || slot == 5;
    });
    EXPECT_EQ(cursor, -1);
}
