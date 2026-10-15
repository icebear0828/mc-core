#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gtest/gtest.h>

#include "eldenring_hooks.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_damage.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_buddy.hpp"
#include "eldenring_combat_service.hpp"
#include "eldenring_bullet.hpp"
#include "mc/contracts/combat_adapter.hpp"

using namespace eldenring;
using namespace eldenring::hooks;

namespace {

TEST(EldenRingHooksTest, OutcomeNameMapping) {
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Applied), "Applied");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Deferred), "Deferred");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::Expired), "Expired");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoTemplate), "NoTemplate");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoPlayer), "NoPlayer");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::VictimGone), "VictimGone");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::WrongModule), "WrongModule");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::VictimDead), "VictimDead");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NotHostile), "NotHostile");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::BadDamage), "BadDamage");
    EXPECT_STREQ(live::OutcomeName(live::DamageOutcome::NoLineOfSight), "NoLineOfSight");
}

TEST(EldenRingHooksTest, BlockSoundSelection) {
    // Stone maps to stone.step
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Stone), "block.stone.step");
    // Dirt/Gravel maps to gravel.step
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Dirt), "block.gravel.step");
    // Default grass/wood/tnt
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Wood), "block.grass.step");
    EXPECT_STREQ(blocks::blockSoundName(mc::BlockId::Tnt), "block.grass.step");
}

TEST(EldenRingHooksTest, KaidenWarriorSummonResolution) {
    EXPECT_EQ(buddy::kKaidenWarriorRequest, 231000);
    EXPECT_EQ(buddy::resolveSummonRequest("kaiden"), 231000);
    EXPECT_EQ(buddy::resolveSummonRequest("wolves"), 232000);
}

TEST(EldenRingHooksTest, BulletConstantsAndTemplateValidation) {
    auto& combat = combat::CombatService::Instance();
    // Initially bullet template is not loaded in unit test environment
    EXPECT_FALSE(combat.hasValidBulletTemplate());
    EXPECT_FLOAT_EQ(bullet::kCrossbowDamage, 9.0f);
}

} // namespace
