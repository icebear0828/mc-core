#include <gtest/gtest.h>

#include "mc/combat.hpp"
#include "mc/damage_scaling.hpp"

#include <cmath>
#include <limits>

namespace {

mc::DamageScalingConfig defaults() {
    return mc::DamageScalingConfig{};
}

constexpr float kEps = 1e-5f;

} // namespace

// ---------------------------------------------------------------------------------------------
// DamageScalingConfig
// ---------------------------------------------------------------------------------------------

TEST(DamageScalingTest, DefaultsMatchTheSkyCraftDesign) {
    const mc::DamageScalingConfig cfg;
    EXPECT_FLOAT_EQ(cfg.mc_to_host_base, 5.0f);
    EXPECT_FLOAT_EQ(cfg.mc_to_host_per_level, 0.25f);
    EXPECT_FLOAT_EQ(cfg.host_to_mc_divisor, 5.0f);
}

TEST(DamageScalingTest, ValidationKeepsGoodValues) {
    mc::DamageScalingConfig cfg;
    cfg.mc_to_host_base = 4.0f;
    cfg.mc_to_host_per_level = 0.5f;
    cfg.host_to_mc_divisor = 2.0f;
    const mc::DamageScalingConfig v = cfg.validated();
    EXPECT_FLOAT_EQ(v.mc_to_host_base, 4.0f);
    EXPECT_FLOAT_EQ(v.mc_to_host_per_level, 0.5f);
    EXPECT_FLOAT_EQ(v.host_to_mc_divisor, 2.0f);
}

TEST(DamageScalingTest, ValidationClampsNegativeValuesToTheDefaults) {
    mc::DamageScalingConfig cfg;
    cfg.mc_to_host_base = -1.0f;
    cfg.mc_to_host_per_level = -0.5f;
    cfg.host_to_mc_divisor = -2.0f;
    const mc::DamageScalingConfig v = cfg.validated();
    EXPECT_FLOAT_EQ(v.mc_to_host_base, mc::DamageScalingConfig::kDefaultMcToHostBase);
    EXPECT_FLOAT_EQ(v.mc_to_host_per_level, mc::DamageScalingConfig::kDefaultMcToHostPerLevel);
    EXPECT_FLOAT_EQ(v.host_to_mc_divisor, mc::DamageScalingConfig::kDefaultHostToMcDivisor);
}

TEST(DamageScalingTest, ValidationKeepsTheDivisorAboveTheEpsilon) {
    mc::DamageScalingConfig cfg;
    cfg.host_to_mc_divisor = 0.0f;
    EXPECT_GE(cfg.validated().host_to_mc_divisor, mc::DamageScalingConfig::kMinHostToMcDivisor);
    cfg.host_to_mc_divisor = 1e-9f;
    EXPECT_GE(cfg.validated().host_to_mc_divisor, mc::DamageScalingConfig::kMinHostToMcDivisor);
    EXPECT_FLOAT_EQ(cfg.validated().host_to_mc_divisor, mc::DamageScalingConfig::kMinHostToMcDivisor);
}

TEST(DamageScalingTest, ValidationReplacesNonFiniteValues) {
    mc::DamageScalingConfig cfg;
    cfg.mc_to_host_base = std::numeric_limits<float>::quiet_NaN();
    cfg.mc_to_host_per_level = std::numeric_limits<float>::infinity();
    cfg.host_to_mc_divisor = -std::numeric_limits<float>::infinity();
    const mc::DamageScalingConfig v = cfg.validated();
    EXPECT_TRUE(std::isfinite(v.mc_to_host_base));
    EXPECT_TRUE(std::isfinite(v.mc_to_host_per_level));
    EXPECT_TRUE(std::isfinite(v.host_to_mc_divisor));
    EXPECT_GT(v.host_to_mc_divisor, 0.0f);
}

// ---------------------------------------------------------------------------------------------
// MC -> host
// ---------------------------------------------------------------------------------------------

TEST(DamageScalingTest, DiamondSwordCritAgainstALevelTenNpc) {
    // A full-charge diamond sword crit is 10.5 MC damage; DESIGN.md 13.1 quotes ~10 -> ~75.
    const float host = mc::scaleMcToHost(10.0f, 10, defaults());
    EXPECT_NEAR(host, 75.0f, 0.01f);

    const float crit = mc::scaleMcToHost(10.5f, 10, defaults());
    EXPECT_NEAR(crit, 78.75f, 0.01f);
    EXPECT_GE(crit, 70.0f);
    EXPECT_LE(crit, 85.0f);
}

TEST(DamageScalingTest, LevelAddsToTheMultiplier) {
    EXPECT_FLOAT_EQ(mc::mcToHostMultiplier(0, defaults()), 5.0f);
    EXPECT_FLOAT_EQ(mc::mcToHostMultiplier(1, defaults()), 5.25f);
    EXPECT_FLOAT_EQ(mc::mcToHostMultiplier(10, defaults()), 7.5f);
    EXPECT_FLOAT_EQ(mc::mcToHostMultiplier(40, defaults()), 15.0f);
}

TEST(DamageScalingTest, LevelZeroAndNegativeLevelsBehaveLikeLevelZero) {
    const float level_zero = mc::scaleMcToHost(4.0f, 0, defaults());
    EXPECT_FLOAT_EQ(level_zero, 20.0f);
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(4.0f, -3, defaults()), level_zero);
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(4.0f, -1000, defaults()), level_zero);
    EXPECT_FLOAT_EQ(mc::mcToHostMultiplier(-7, defaults()), 5.0f);
}

TEST(DamageScalingTest, CustomConfigChangesTheConversion) {
    mc::DamageScalingConfig cfg;
    cfg.mc_to_host_base = 1.0f;
    cfg.mc_to_host_per_level = 1.0f;
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(10.0f, 4, cfg), 50.0f);
}

// ---------------------------------------------------------------------------------------------
// host -> MC
// ---------------------------------------------------------------------------------------------

TEST(DamageScalingTest, TwentyHostDamageBecomesFourMcDamage) {
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(20.0f, defaults()), 4.0f);
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(15.0f, defaults()), 3.0f);
}

TEST(DamageScalingTest, CustomDivisorChangesTheConversion) {
    mc::DamageScalingConfig cfg;
    cfg.host_to_mc_divisor = 10.0f;
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(20.0f, cfg), 2.0f);
}

// ---------------------------------------------------------------------------------------------
// Clamping and finiteness
// ---------------------------------------------------------------------------------------------

TEST(DamageScalingTest, ZeroAndNegativeDamageProduceZero) {
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(0.0f, 10, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(-7.0f, 10, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(0.0f, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(-20.0f, defaults()), 0.0f);
}

TEST(DamageScalingTest, NonFiniteDamageProducesZero) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(nan, 5, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleMcToHost(inf, 5, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(nan, defaults()), 0.0f);
    EXPECT_FLOAT_EQ(mc::scaleHostToMc(inf, defaults()), 0.0f);
}

TEST(DamageScalingTest, ExtremeInputsStayFinite) {
    const float huge = mc::scaleMcToHost(std::numeric_limits<float>::max(), 1000000, defaults());
    EXPECT_TRUE(std::isfinite(huge));
    EXPECT_GT(huge, 0.0f);

    mc::DamageScalingConfig zero_divisor;
    zero_divisor.host_to_mc_divisor = 0.0f;
    EXPECT_TRUE(std::isfinite(mc::scaleHostToMc(20.0f, zero_divisor)));

    mc::DamageScalingConfig bad_divisor;
    bad_divisor.host_to_mc_divisor = -3.0f;
    EXPECT_TRUE(std::isfinite(mc::scaleHostToMc(20.0f, bad_divisor)));
}

// ---------------------------------------------------------------------------------------------
// HitIntent adapter
// ---------------------------------------------------------------------------------------------

TEST(DamageScalingTest, HitIntentScalesAbsoluteAndPercentTogether) {
    mc::HitIntent intent;
    intent.damage = 10.0f;
    intent.max_hp_percent = 0.05f;

    mc::applyScaledDamage(intent, 10, defaults());

    EXPECT_NEAR(intent.damage, 75.0f, 0.01f);
    EXPECT_NEAR(intent.max_hp_percent, 0.375f, kEps);
    // Both fields moved by the same multiplier, so they still describe one hit.
    const float before = 0.05f / 10.0f;
    const float after = intent.max_hp_percent / intent.damage;
    EXPECT_NEAR(after, before, 1e-7f);
}

TEST(DamageScalingTest, HitIntentUsesTheBaseMultiplierAtLevelZero) {
    mc::HitIntent intent;
    intent.damage = 7.0f;
    intent.max_hp_percent = 0.05f;

    mc::applyScaledDamage(intent, 0, defaults());

    EXPECT_FLOAT_EQ(intent.damage, 35.0f);
    EXPECT_NEAR(intent.max_hp_percent, 0.25f, kEps);
}

TEST(DamageScalingTest, HitIntentPercentStaysAFraction) {
    mc::HitIntent intent;
    intent.damage = 100.0f;
    intent.max_hp_percent = 0.5f;

    mc::applyScaledDamage(intent, 40, defaults()); // multiplier 15

    EXPECT_GE(intent.max_hp_percent, 0.0f);
    EXPECT_LE(intent.max_hp_percent, 1.0f);
    EXPECT_FLOAT_EQ(intent.max_hp_percent, 1.0f);
}

TEST(DamageScalingTest, HitIntentZeroDamageStaysZero) {
    mc::HitIntent intent;
    intent.damage = 0.0f;
    intent.max_hp_percent = 0.0f;

    mc::applyScaledDamage(intent, 10, defaults());

    EXPECT_FLOAT_EQ(intent.damage, 0.0f);
    EXPECT_FLOAT_EQ(intent.max_hp_percent, 0.0f);
}

TEST(DamageScalingTest, HitIntentNegativePercentClampsToZero) {
    mc::HitIntent intent;
    intent.damage = 10.0f;
    intent.max_hp_percent = -0.2f;

    mc::applyScaledDamage(intent, 10, defaults());

    EXPECT_FLOAT_EQ(intent.max_hp_percent, 0.0f);
    EXPECT_GT(intent.damage, 0.0f);
}

TEST(DamageScalingTest, HitIntentLeavesTheOtherFieldsAlone) {
    mc::HitIntent intent;
    intent.attacker_id = mc::EntityId{1};
    intent.victim_id = mc::EntityId{2};
    intent.damage = 10.0f;
    intent.max_hp_percent = 0.05f;
    intent.knockback_force = 800.0f;
    intent.is_critical = true;
    intent.is_sweeping = true;

    mc::applyScaledDamage(intent, 10, defaults());

    EXPECT_EQ(intent.attacker_id, mc::EntityId{1});
    EXPECT_EQ(intent.victim_id, mc::EntityId{2});
    EXPECT_FLOAT_EQ(intent.knockback_force, 800.0f);
    EXPECT_TRUE(intent.is_critical);
    EXPECT_TRUE(intent.is_sweeping);
}

// ---------------------------------------------------------------------------------------------
// HealthMirror
// ---------------------------------------------------------------------------------------------

TEST(HealthMirrorTest, StartsFull) {
    const mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);
    EXPECT_EQ(mirror.authority(), mc::HealthMirror::Authority::Minecraft);
    EXPECT_FLOAT_EQ(mirror.maxHp(), 20.0f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 20.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 1.0f);
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 1.0f);
    EXPECT_FALSE(mirror.isDead());
    EXPECT_FALSE(mirror.justDied());
}

TEST(HealthMirrorTest, MinecraftAuthorityDrivesTheHostFraction) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);

    mirror.setLocalHp(15.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.75f);
    EXPECT_TRUE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));

    mirror.markPushed();
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 0.75f);
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));
}

TEST(HealthMirrorTest, RemoteUpdateInsideEpsilonIsIgnored) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);
    mirror.markPushed(); // baseline is the full bar

    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(1.0f));
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(1.0f - 0.5f * mc::HealthMirror::kPushEpsilon));
    EXPECT_TRUE(mirror.shouldPushRemoteUpdate(1.0f - 2.0f * mc::HealthMirror::kPushEpsilon));
}

TEST(HealthMirrorTest, BigDropPushesAndTheRepeatIsSuppressed) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);

    mirror.setLocalHp(12.0f); // 0.6
    EXPECT_TRUE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));
    mirror.markPushed();
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));

    mirror.setLocalHp(11.9f); // 0.595: 0.005 drift, below the epsilon
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));

    mirror.setLocalHp(2.0f); // 0.1
    EXPECT_TRUE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));
}

TEST(HealthMirrorTest, LocalValuesClampToTheirRange) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);

    mirror.setLocalHp(999.0f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 20.0f);
    mirror.setLocalHp(-5.0f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 0.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.0f);

    mirror.observeRemoteHp(5.0f);
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 1.0f);
    mirror.observeRemoteHp(-1.0f);
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 0.0f);
}

TEST(HealthMirrorTest, NonFiniteInputsAreIgnored) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Host, 100.0f);
    mirror.observeRemoteHp(0.5f);
    mirror.observeRemoteHp(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 0.5f);
    mirror.setLocalHp(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(mirror.localHp(), 0.0f);
}

TEST(HealthMirrorTest, MaxHpRescalesTheLocalFraction) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);
    mirror.setLocalHp(10.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.5f);

    mirror.setMaxHp(40.0f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 10.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.25f);

    mirror.setMaxHp(0.0f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 0.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.0f);
}

TEST(HealthMirrorTest, HostAuthorityMirrorsTheRemoteFraction) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Host, 200.0f);

    mirror.observeRemoteHp(0.5f);
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 0.5f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.5f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 100.0f);
    EXPECT_FALSE(mirror.isDead());

    // The host owns the value, so there is nothing to push back.
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(0.1f));
}

TEST(HealthMirrorTest, RemoteDeathFiresExactlyOnce) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Host, 100.0f);
    mirror.observeRemoteHp(0.5f);
    EXPECT_FALSE(mirror.isDead());
    EXPECT_FALSE(mirror.consumeDeath());

    mirror.observeRemoteHp(0.0f);
    EXPECT_TRUE(mirror.isDead());
    EXPECT_TRUE(mirror.justDied());
    EXPECT_TRUE(mirror.consumeDeath());
    EXPECT_FALSE(mirror.justDied());
    EXPECT_FALSE(mirror.consumeDeath());

    mirror.observeRemoteHp(0.0f); // still dead, no second event
    EXPECT_TRUE(mirror.isDead());
    EXPECT_FALSE(mirror.consumeDeath());
}

TEST(HealthMirrorTest, LocalDeathUnderMinecraftAuthorityFiresOnce) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);

    mirror.setLocalHp(0.0f);
    EXPECT_TRUE(mirror.isDead());
    EXPECT_TRUE(mirror.consumeDeath());
    mirror.setLocalHp(0.0f);
    EXPECT_FALSE(mirror.consumeDeath());

    // Under Minecraft authority a host-side death report is informational only.
    mirror.revive();
    mirror.setLocalHp(20.0f);
    mirror.observeRemoteHp(0.0f);
    EXPECT_FALSE(mirror.isDead());
    EXPECT_FALSE(mirror.consumeDeath());
}

TEST(HealthMirrorTest, ReviveAboveTheThresholdClearsTheLatch) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Host, 100.0f);
    mirror.observeRemoteHp(0.0f);
    ASSERT_TRUE(mirror.isDead());
    ASSERT_TRUE(mirror.consumeDeath());

    mirror.observeRemoteHp(0.5f);
    EXPECT_FALSE(mirror.isDead());
    EXPECT_FALSE(mirror.justDied());
    EXPECT_FALSE(mirror.consumeDeath());
}

TEST(HealthMirrorTest, ExplicitReviveResetsTheLatchAndForcesAPush) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);
    mirror.setLocalHp(0.0f);
    ASSERT_TRUE(mirror.isDead());
    ASSERT_TRUE(mirror.consumeDeath());

    mirror.revive();
    EXPECT_FALSE(mirror.isDead());
    EXPECT_FALSE(mirror.justDied());
    EXPECT_TRUE(mirror.shouldPushRemoteUpdate(mirror.localFraction()));

    mirror.setLocalHp(20.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 1.0f);
    EXPECT_FALSE(mirror.isDead());
}

TEST(HealthMirrorTest, SwitchingAuthorityReseedsTheMirror) {
    mc::HealthMirror mirror(mc::HealthMirror::Authority::Minecraft, 20.0f);
    mirror.setLocalHp(5.0f); // 0.25
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.25f);

    mirror.setAuthority(mc::HealthMirror::Authority::Host);
    EXPECT_FLOAT_EQ(mirror.remoteFraction(), 0.25f);
    EXPECT_FALSE(mirror.shouldPushRemoteUpdate(0.9f));

    mirror.observeRemoteHp(0.75f);
    EXPECT_FLOAT_EQ(mirror.localHp(), 15.0f);
    EXPECT_FLOAT_EQ(mirror.localFraction(), 0.75f);
}
