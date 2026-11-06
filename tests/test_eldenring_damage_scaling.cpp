#include <gtest/gtest.h>

#include "eldenring_damage_scaling.hpp"
#include "eldenring_melee.hpp"

#include <cmath>
#include <limits>

using namespace eldenring::live;

namespace {

mc::HitIntent makeIntent(float damage, float percent = 0.02f) {
    mc::HitIntent intent;
    intent.damage = damage;
    intent.max_hp_percent = percent;
    return intent;
}

} // namespace

// The new rule must be a drop-in replacement for the old hardcoded erDamage() when the config
// keeps ER's historical values.
TEST(ErDamageScalingTest, DefaultConfigMatchesLegacyErDamage) {
    const ErDamageConfig cfg = defaultErDamageConfig();
    for (const int max_hp : {100, 1000, 10000}) {
        for (const float damage : {1.0f, 7.0f, 10.0f, 13.5f}) {
            const mc::HitIntent intent = makeIntent(damage);
            EXPECT_EQ(erDamageScaled(intent, max_hp, 0, cfg), erDamage(intent, max_hp, 20.0f, 0.02f))
                << "damage=" << damage << " max_hp=" << max_hp;
        }
    }
}

TEST(ErDamageScalingTest, BaseScalesTheFlatTermOnly) {
    ErDamageConfig cfg = defaultErDamageConfig();
    cfg.scaling.mc_to_host_base = 40.0f;
    const mc::HitIntent intent = makeIntent(7.0f);
    // Flat 7*40 = 280, percent 1000*0.02*1 = 20.
    EXPECT_EQ(erDamageScaled(intent, 1000, 0, cfg), 300);
}

TEST(ErDamageScalingTest, PerLevelAddsToTheMultiplier) {
    ErDamageConfig cfg = defaultErDamageConfig();
    cfg.scaling.mc_to_host_base = 5.0f;
    cfg.scaling.mc_to_host_per_level = 0.25f;
    cfg.hp_percent = 0.0f;
    const mc::HitIntent intent = makeIntent(10.0f);
    // (5 + 0.25*10) * 10 = 75; level 0 gives 50.
    EXPECT_EQ(erDamageScaled(intent, 1000, 10, cfg), 75);
    EXPECT_EQ(erDamageScaled(intent, 1000, 0, cfg), 50);
}

TEST(ErDamageScalingTest, DisabledReproducesLegacyWhateverTheBase) {
    ErDamageConfig cfg = defaultErDamageConfig();
    cfg.enabled = false;
    cfg.scaling.mc_to_host_base = 99.0f;
    const mc::HitIntent intent = makeIntent(7.0f);
    EXPECT_EQ(erDamageScaled(intent, 1000, 0, cfg), erDamage(intent, 1000, 20.0f, 0.02f));
}

TEST(ErDamageScalingTest, ZeroNegativeAndNonFiniteDamageDealNothing) {
    const ErDamageConfig cfg = defaultErDamageConfig();
    EXPECT_EQ(erDamageScaled(makeIntent(0.0f), 1000, 0, cfg), 0);
    EXPECT_EQ(erDamageScaled(makeIntent(-5.0f), 1000, 0, cfg), 0);
    EXPECT_EQ(erDamageScaled(makeIntent(std::numeric_limits<float>::quiet_NaN()), 1000, 0, cfg), 0);
}

TEST(ErDamageScalingTest, UnknownMaxHpStillDealsTheFlatTerm) {
    const ErDamageConfig cfg = defaultErDamageConfig();
    EXPECT_EQ(erDamageScaled(makeIntent(7.0f), 0, 0, cfg), 140);
    EXPECT_EQ(erDamageScaled(makeIntent(7.0f), -1, 0, cfg), 140);
}

TEST(ErDamageScalingTest, SweepIntentScalesLikeAnyOtherHit) {
    const ErDamageConfig cfg = defaultErDamageConfig();
    mc::HitIntent sweep = makeIntent(2.8f); // 40% of a 7-damage sword
    sweep.is_sweeping = true;
    EXPECT_EQ(erDamageScaled(sweep, 1000, 0, cfg), erDamage(sweep, 1000, 20.0f, 0.02f));
    EXPECT_GT(erDamageScaled(sweep, 1000, 0, cfg), 0);
}

TEST(ErHealthMirrorTest, HeartHalvesFollowTheHostFraction) {
    EXPECT_EQ(heartHalvesFromVitals(0, 1000), 0);
    EXPECT_EQ(heartHalvesFromVitals(1, 1000), 1);
    EXPECT_EQ(heartHalvesFromVitals(500, 1000), 10);
    EXPECT_EQ(heartHalvesFromVitals(1000, 1000), 20);
    EXPECT_EQ(heartHalvesFromVitals(2000, 1000), 20); // over-full clamps
    EXPECT_EQ(heartHalvesFromVitals(-5, 1000), 0);
    EXPECT_EQ(heartHalvesFromVitals(10, 0), 0); // no max: no hearts
}

TEST(ErHealthMirrorTest, FullHealthIsTenHearts) {
    EXPECT_EQ(heartHalvesFromVitals(1350, 1350), 20);
    EXPECT_EQ(heartHalvesFromVitals(1351, 1350), 20);
}
