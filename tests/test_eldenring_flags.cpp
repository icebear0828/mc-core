#include <gtest/gtest.h>

#include "eldenring_flags.hpp"

using eldenring::flags::ByteFlagHold;

TEST(EldenRingFlags, ForcesTheBitsOnAndOnlyWritesWhenTheByteChanges) {
    ByteFlagHold h;
    auto w = h.step(0x10, true, 0xC0);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0xD0);                      // the other bits are kept
    EXPECT_FALSE(h.step(0xD0, true, 0xC0));   // already set: no write
    w = h.step(0x10, true, 0xC0);             // the game cleared them again (it rewrites the byte): put them back
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0xD0);
}

TEST(EldenRingFlags, ReleasingRestoresOnlyTheBitsWeAdded) {
    ByteFlagHold h;
    // 0x80 was already set by the game, 0x40 was not: releasing must leave 0x80 alone and clear 0x40
    ASSERT_EQ(*h.step(0x80, true, 0xC0), 0xC0);
    const auto w = h.step(0xC0, false, 0xC0);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0x80);
    EXPECT_FALSE(h.step(0x80, false, 0xC0)); // released: nothing more to do
    EXPECT_FALSE(h.active());
}

TEST(EldenRingFlags, NeverTouchesAByteItDidNotForce) {
    ByteFlagHold h;
    EXPECT_FALSE(h.step(0x37, false, 0xC0));
    EXPECT_FALSE(h.active());
}

TEST(EldenRingFlags, KeepsTheBitsTheGameChangedWhileHeld) {
    ByteFlagHold h;
    ASSERT_EQ(*h.step(0x01, true, 0x40), 0x41);
    // the game sets another bit meanwhile; releasing clears our bit and keeps the game's change
    const auto w = h.step(0x43, false, 0x40);
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0x03);
}

TEST(EldenRingFlags, ToggleByteForcesAValueAndRestoresTheOriginal) {
    eldenring::flags::ByteValueHold v;
    EXPECT_FALSE(v.step(0, false, 0));        // never forced: untouched
    auto w = v.step(1, true, 0);              // the HUD setting is 1 (shown): force 0
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0);
    EXPECT_FALSE(v.step(0, true, 0));         // already forced
    w = v.step(1, true, 0);                   // the game rewrote the setting
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 0);
    w = v.step(0, false, 0);                  // released: back to what it was before the first force
    ASSERT_TRUE(w.has_value());
    EXPECT_EQ(*w, 1);
    EXPECT_FALSE(v.step(1, false, 0));
}

TEST(EldenRingFlags, PolicyHoldsTheFlagsOnlyInMcModeAndTheDeathProofOnlyInCreative) {
    using eldenring::flags::policyFor;
    auto p = policyFor(true, false, true); // survival
    EXPECT_TRUE(p.no_stamina);
    EXPECT_TRUE(p.hide_hud);
    EXPECT_FALSE(p.cannot_die);              // survival must still be able to die: the HP-delta wrapper uses the same can't-die check as the falls
    p = policyFor(true, true, true);         // creative
    EXPECT_TRUE(p.no_stamina);
    EXPECT_TRUE(p.hide_hud);
    EXPECT_TRUE(p.cannot_die);
    p = policyFor(false, true, true);        // MC mode off: nothing is held
    EXPECT_FALSE(p.no_stamina);
    EXPECT_FALSE(p.hide_hud);
    EXPECT_FALSE(p.cannot_die);
    p = policyFor(true, true, false);        // switched off by mc_er_noflags.txt
    EXPECT_FALSE(p.no_stamina);
    EXPECT_FALSE(p.hide_hud);
    EXPECT_FALSE(p.cannot_die);
}
