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

TEST(EldenRingCreative, SpEffectIsLoggedOnlyForTheLandingCallOfThePlayer) {
    EXPECT_TRUE(shouldLogSpEffect(0x8F, kLandingSpEffectReturnRva, true));
    EXPECT_FALSE(shouldLogSpEffect(0x8F, kLandingSpEffectReturnRva, false));
    EXPECT_FALSE(shouldLogSpEffect(0x8E, kLandingSpEffectReturnRva, true));
    EXPECT_FALSE(shouldLogSpEffect(0x8F, kLandingSpEffectReturnRva + 5, true)); // some other caller asking for 0x8F
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
    EXPECT_NE(formatLanding(true).find("hard landing"), std::string::npos);
    EXPECT_NE(formatSpEffect(true).find("returned 1"), std::string::npos);
    EXPECT_NE(formatSpEffect(false).find("returned 0"), std::string::npos);
}

TEST(EldenRingCreative, ProbeSignaturesParseToTheDocumentedLengths) {
    using live::Signature;
    EXPECT_EQ(Signature::parse(live::sigs::kKillWrapper)->size(), 55u);
    EXPECT_EQ(Signature::parse(live::sigs::kHardLanding)->size(), 39u);
    EXPECT_EQ(Signature::parse(live::sigs::kFallHeight)->size(), 38u);
    EXPECT_EQ(Signature::parse(live::sigs::kHasSpEffect)->size(), 70u);
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
