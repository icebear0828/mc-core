#include <gtest/gtest.h>

#include "eldenring_creative.hpp"
#include "eldenring_sigscan.hpp"

#include <cmath>
#include <limits>

using namespace eldenring;
using namespace eldenring::creative;

TEST(EldenRingCreative, OwnerIsPlayerNeverMatchesNull) {
    EXPECT_TRUE(isPlayer(0x1000, 0x1000));
    EXPECT_FALSE(isPlayer(0x1000, 0x2000));
    EXPECT_FALSE(isPlayer(0, 0)); // no world yet: a null owner must not count as the player
    EXPECT_FALSE(isPlayer(0, 0x1000));
}

TEST(EldenRingCreative, FallHeightBelowOneMetreIsQuiet) {
    FallLogState st;
    EXPECT_FALSE(shouldLogFall(st, 0.f, 0x411324, 1000));
    EXPECT_FALSE(shouldLogFall(st, 0.99f, 0x411324, 2000));
    EXPECT_FALSE(shouldLogFall(st, -3.f, 0x411324, 3000));
}

TEST(EldenRingCreative, FallHeightIsRateLimitedPerCaller) {
    FallLogState st;
    EXPECT_TRUE(shouldLogFall(st, 5.f, 0x411324, 1000));
    EXPECT_FALSE(shouldLogFall(st, 6.f, 0x411324, 1100)); // same caller, 100 ms later
    EXPECT_TRUE(shouldLogFall(st, 6.f, 0x475E02, 1100));  // another caller is independent
    EXPECT_TRUE(shouldLogFall(st, 7.f, 0x411324, 1000 + kFallLogIntervalMs));
}

TEST(EldenRingCreative, TheForcedFallValueAndNonFiniteValuesAreLogged) {
    FallLogState st;
    EXPECT_TRUE(shouldLogFall(st, 10000.f, 0x411324, 1000)); // what the game returns when FallModule+0x1D is set
    EXPECT_TRUE(shouldLogFall(st, std::numeric_limits<float>::quiet_NaN(), 0x475E02, 1000));
    EXPECT_TRUE(shouldLogFall(st, std::numeric_limits<float>::infinity(), 0x49D4F7, 1000));
}

TEST(EldenRingCreative, LinesCarryTheCallerRvaAndPlayerFlag) {
    const std::string kill = formatKill(0x42BC1E, true);
    EXPECT_NE(kill.find("kill wrapper"), std::string::npos);
    EXPECT_NE(kill.find("0x42BC1E"), std::string::npos);
    EXPECT_NE(kill.find("player=1"), std::string::npos);
    const std::string fall = formatFall(0x411324, 12.5f);
    EXPECT_NE(fall.find("fall height"), std::string::npos);
    EXPECT_NE(fall.find("0x411324"), std::string::npos);
    EXPECT_NE(fall.find("12.50"), std::string::npos);
}

TEST(EldenRingCreative, ProbeSignaturesParseToTheDocumentedLengths) {
    using live::Signature;
    EXPECT_EQ(Signature::parse(live::sigs::kKillWrapper)->size(), 55u);
    EXPECT_EQ(Signature::parse(live::sigs::kFallHeight)->size(), 38u);
    EXPECT_EQ(Signature::parse(live::sigs::kChrEventDispatch)->size(), 45u);
    EXPECT_EQ(Signature::parse(live::sigs::kFallTimeExceeded)->size(), 28u);
}

TEST(EldenRingCreative, FallProtectionNeverSkipsTheKillOfAPlayerWhoAlreadyHasNoHitPoints) {
    // 2026-10-09 log: a 25.7 m fall zeroed the hit points through the damage path, then both kill wrapper calls had their KillChr skipped
    // by fall protection, so the player sat at hp=0/576 without ever dying and could not respawn.
    EXPECT_TRUE(shouldSkipPlayerKill(true, true, true, 316));
    EXPECT_FALSE(shouldSkipPlayerKill(true, true, true, 0));
    EXPECT_FALSE(shouldSkipPlayerKill(true, true, true, -5));
    EXPECT_FALSE(shouldSkipPlayerKill(false, true, true, 316)); // fall_protect=0
    EXPECT_FALSE(shouldSkipPlayerKill(true, false, true, 316)); // MC mode off
    EXPECT_FALSE(shouldSkipPlayerKill(true, true, false, 316)); // no blocks and no recent jump of ours
}

TEST(EldenRingCreative, FallHeightIsZeroedOnlyForThePlayerInMcModeWhenEnabled) {
    EXPECT_TRUE(shouldZeroFall(true, true, true));
    EXPECT_FALSE(shouldZeroFall(false, true, true)); // creative_nofall off (the default)
    EXPECT_FALSE(shouldZeroFall(true, false, true)); // MC mode off: vanilla falling
    EXPECT_FALSE(shouldZeroFall(true, true, false)); // enemies and NPCs still take fall damage
}

TEST(EldenRingCreative, ZeroedFallLineSaysWhatWasReplaced) {
    const std::string line = formatFallZeroed(0x411324, 25.72f);
    EXPECT_NE(line.find("zeroed"), std::string::npos);
    EXPECT_NE(line.find("25.72"), std::string::npos);
    EXPECT_NE(line.find("0x411324"), std::string::npos);
}

TEST(EldenRingCreative, GameModeDefaultsToSurvivalAndToggles) {
    EXPECT_EQ(GameMode{}, GameMode::Survival);
    EXPECT_EQ(toggled(GameMode::Survival), GameMode::Creative);
    EXPECT_EQ(toggled(GameMode::Creative), GameMode::Survival);
    EXPECT_STREQ(modeName(GameMode::Creative), "Creative Mode");
    EXPECT_STREQ(modeName(GameMode::Survival), "Survival Mode");
}

TEST(EldenRingCreative, ModePolicyMatchesTheAgreedDifferences) {
    // Creative: item palette, no fall damage, nothing is used up, no health/hunger bars.
    EXPECT_TRUE(showsPalette(GameMode::Creative));
    EXPECT_TRUE(zeroesFall(GameMode::Creative));
    EXPECT_FALSE(consumesItems(GameMode::Creative));
    EXPECT_FALSE(showsVitals(GameMode::Creative));
    // Survival is the vanilla-like mode.
    EXPECT_FALSE(showsPalette(GameMode::Survival));
    EXPECT_FALSE(zeroesFall(GameMode::Survival));
    EXPECT_TRUE(consumesItems(GameMode::Survival));
    EXPECT_TRUE(showsVitals(GameMode::Survival));
}

TEST(EldenRingCreative, CreativeModeBlocksTheWholeKillWrapperWhileTheHitPointsLast) {
    // 2026-10-10 log: after 14.6 s in the air the animation event handler (0x140428DE0, event 12) called the kill wrapper with hp > 0. Skipping
    // only KillChr left the rest of the death processing running (death effects without dying), so creative mode skips the whole wrapper.
    EXPECT_TRUE(shouldBlockPlayerKill(GameMode::Creative, true, 316));
    EXPECT_FALSE(shouldBlockPlayerKill(GameMode::Creative, true, 0));  // the hit points are gone: a real death must go through
    EXPECT_FALSE(shouldBlockPlayerKill(GameMode::Creative, false, 316)); // MC mode off: vanilla
    EXPECT_FALSE(shouldBlockPlayerKill(GameMode::Survival, true, 316)); // survival keeps the old behaviour
}

TEST(EldenRingCreative, OnlyTheDeathRelatedEventTypesOfTheDispatcherAreLogged) {
    // 0x140428DE0 switches on the event type: 12 and 47 reach the kill wrappers, 46 sets a state bit on the character, 48 is the clean-up
    // after a death, 126 (0x7E) tears the character down.
    for (uint32_t t : {12u, 46u, 47u, 48u, 126u}) EXPECT_TRUE(isDeathEventType(t)) << t;
    for (uint32_t t : {0u, 1u, 11u, 13u, 45u, 49u, 125u}) EXPECT_FALSE(isDeathEventType(t)) << t;
}

TEST(EldenRingCreative, EventLineHasTypeConditionFlagBytesAndCaller) {
    const unsigned char raw[16] = {0x0C, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xAA, 0xBB, 0xCC};
    const std::string line = formatChrEvent(12, 0x01F4, true, raw, 0x428EF0);
    EXPECT_NE(line.find("event 12"), std::string::npos);
    EXPECT_NE(line.find("needs SpEffect 500"), std::string::npos);
    EXPECT_NE(line.find("flag=1"), std::string::npos);
    EXPECT_NE(line.find("0C 00 00 00 01 02"), std::string::npos);
    EXPECT_NE(line.find("AA BB CC"), std::string::npos);
    EXPECT_NE(line.find("0x428EF0"), std::string::npos);
}

TEST(EldenRingCreative, StackLineListsReturnAddressesAsRvas) {
    const uintptr_t base = 0x7FF600000000ull;
    const uintptr_t frames[3] = {base + 0x425BEF, base + 0x3F8543, 0x1234}; // the last one is outside the image
    const std::string line = formatStack(frames, 3, base, 0x4000000);
    EXPECT_NE(line.find("0x425BEF"), std::string::npos);
    EXPECT_NE(line.find("0x3F8543"), std::string::npos);
    EXPECT_NE(line.find("?"), std::string::npos); // addresses outside the image are not RVAs
}

TEST(EldenRingCreative, CreativeModeNeverLetsTheGameDecideThePlayerFellTooLong) {
    // 2026-10-10 log: fall timer 12.02 s -> die event 12 (the timer is FallModule+0x18; 0x14044E3A0 compares it with a threshold and has one
    // caller, inside the fall damage evaluator). It is not the height: the player was 3.7 m up.
    EXPECT_TRUE(shouldDenyLongFall(GameMode::Creative, true, true, false));
    EXPECT_FALSE(shouldDenyLongFall(GameMode::Creative, true, false, false)); // enemies keep their rules
    EXPECT_FALSE(shouldDenyLongFall(GameMode::Creative, false, true, false)); // MC mode off
}

TEST(EldenRingCreative, SurvivalOnlyDeniesItWhileOurOwnMovementHoldsThePlayerUp) {
    // 2026-10-10 log, survival: standing on placed blocks (not Havok geometry) the game believes the player is falling; at 12.03 s the die event
    // came and the old KillChr-only skip left a fake death. Standing on blocks (or within 3 s of our movement layer) the answer is no too.
    EXPECT_TRUE(shouldDenyLongFall(GameMode::Survival, true, true, true));
    EXPECT_FALSE(shouldDenyLongFall(GameMode::Survival, true, true, false)); // a real long fall in survival is left to the game
    EXPECT_FALSE(shouldDenyLongFall(GameMode::Survival, true, false, true)); // not the player
    EXPECT_FALSE(shouldDenyLongFall(GameMode::Survival, false, true, true)); // MC mode off
}
