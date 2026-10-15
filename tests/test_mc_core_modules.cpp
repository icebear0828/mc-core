#include <gtest/gtest.h>

#include "mc/heart_anim.hpp"
#include "mc/hunger.hpp"
#include "mc/mining.hpp"
#include "mc/shield.hpp"
#include "mc/xp.hpp"

// ============================================================================
// 1. HungerModel Tests
// ============================================================================
TEST(McCoreHungerTest, InitialStateIsFullFoodAndSaturation) {
    mc::HungerModel hunger;
    EXPECT_EQ(hunger.food(), 20);
    EXPECT_FLOAT_EQ(hunger.saturation(), 5.0f);
    EXPECT_FALSE(hunger.shaking());
    EXPECT_FALSE(hunger.canEat(mc::ItemId::Bread));
    EXPECT_TRUE(hunger.canEat(mc::ItemId::GoldenApple)); // always edible
}

TEST(McCoreHungerTest, ExhaustionDrainsSaturationThenFood) {
    mc::HungerModel hunger;
    // 5.0 saturation -> takes 5 * 4.0 = 20.0 exhaustion to drain saturation to 0
    hunger.addExhaustion(20.0f);
    EXPECT_EQ(hunger.food(), 20);
    EXPECT_FLOAT_EQ(hunger.saturation(), 0.0f);
    EXPECT_TRUE(hunger.shaking());

    // Next 4.0 exhaustion drains food by 1
    hunger.addExhaustion(4.0f);
    EXPECT_EQ(hunger.food(), 19);
    EXPECT_TRUE(hunger.canEat(mc::ItemId::Bread));
}

TEST(McCoreHungerTest, EatingFoodRestoresNutritionAndSaturationCappedByFood) {
    mc::HungerModel hunger;
    hunger.addExhaustion(40.0f); // drains saturation (5.0) and food by 5 -> food 15, sat 0
    EXPECT_EQ(hunger.food(), 15);

    hunger.eat(mc::ItemId::Bread); // +5 food, +6.0 sat (capped at food 20)
    EXPECT_EQ(hunger.food(), 20);
    EXPECT_FLOAT_EQ(hunger.saturation(), 6.0f);
}

TEST(McCoreHungerTest, NaturalRegenerationTicksHealHurtPlayer) {
    mc::HungerModel hunger;
    // Food full (20) and saturation > 0: heals every 10 ticks (0.5s)
    float healed = 0.0f;
    for (int i = 0; i < 10; ++i) {
        healed += hunger.tick(0.05f, true /* hurt */);
    }
    EXPECT_GT(healed, 0.5f);
    EXPECT_LT(hunger.saturation(), 5.0f); // exhaustion applied
}

// ============================================================================
// 2. XpModel Tests
// ============================================================================
TEST(McCoreXpTest, PointsForNextLevelFollowsVanillaFormula) {
    EXPECT_EQ(mc::XpModel::pointsForNextLevel(0), 7);
    EXPECT_EQ(mc::XpModel::pointsForNextLevel(14), 7 + 14 * 2); // 35
    EXPECT_EQ(mc::XpModel::pointsForNextLevel(15), 37);
    EXPECT_EQ(mc::XpModel::pointsForNextLevel(29), 37 + (29 - 15) * 5); // 107
    EXPECT_EQ(mc::XpModel::pointsForNextLevel(30), 112);
}

TEST(McCoreXpTest, AddingPointsLevelsUpAndMaintainsProgress) {
    mc::XpModel xp;
    EXPECT_EQ(xp.level(), 0);
    EXPECT_FLOAT_EQ(xp.progress(), 0.0f);

    int levels = xp.add(7); // exactly level 0 -> 1
    EXPECT_EQ(levels, 1);
    EXPECT_EQ(xp.level(), 1);
    EXPECT_FLOAT_EQ(xp.progress(), 0.0f);

    xp.add(9); // level 1 needs 9 points -> level 2
    EXPECT_EQ(xp.level(), 2);

    xp.add(5); // level 2 needs 11 points -> 5 / 11 progress
    EXPECT_NEAR(xp.progress(), 5.0f / 11.0f, 1e-4f);
}

TEST(McCoreXpTest, XpForKillScaling) {
    EXPECT_EQ(mc::xpForKill(100), 5);
    EXPECT_EQ(mc::xpForKill(3500), 20);
    EXPECT_EQ(mc::xpForKill(12000), 50);
}

// ============================================================================
// 3. HeartAnimator Tests
// ============================================================================
TEST(McCoreHeartAnimTest, LowHealthTriggersHeartShaking) {
    mc::HeartAnimator anim;
    anim.update(0.1f, 4 /* <= 4 half hearts */, 0, false);
    bool any_shaking = false;
    for (int h = 0; h < mc::HeartAnimator::kHearts; ++h) {
        if (anim.shake(h) != 0) any_shaking = true;
    }
    EXPECT_TRUE(any_shaking);

    // Full health -> shake stops
    anim.update(0.1f, 20, 0, false);
    for (int h = 0; h < mc::HeartAnimator::kHearts; ++h) {
        EXPECT_EQ(anim.shake(h), 0);
    }
}

TEST(McCoreHeartAnimTest, TakingDamageTriggersPeriodicWhiteBlink) {
    mc::HeartAnimator anim;
    anim.update(0.05f, 20, 0, false);
    EXPECT_FALSE(anim.blink());

    // Take damage: blink window opens
    anim.update(0.05f, 14, 0, false);
    EXPECT_EQ(anim.displayHealth(), 20); // flashes old health

    // Over next 20 ticks (1 second), blink should flip periodically
    int blink_on_ticks = 0;
    int flips = 0;
    bool prev = anim.blink();
    for (int i = 0; i < 20; ++i) {
        anim.update(0.05f, 14, 0, false);
        if (anim.blink()) ++blink_on_ticks;
        if (anim.blink() != prev) {
            ++flips;
            prev = anim.blink();
        }
    }
    EXPECT_GT(blink_on_ticks, 6);
    EXPECT_GT(flips, 4);
}

// ============================================================================
// 4. MiningController Tests
// ============================================================================
TEST(McCoreMiningTest, ComputeBreakTimeAndCanHarvestRules) {
    // Stone
    EXPECT_FLOAT_EQ(mc::MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false), 0.3f);
    EXPECT_FLOAT_EQ(mc::MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondSword, false), 7.5f);
    EXPECT_TRUE(mc::MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false));
    EXPECT_FALSE(mc::MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondSword, false));

    // Creative mode: instant break, no drops
    EXPECT_FLOAT_EQ(mc::MiningController::computeBreakTime(mc::BlockId::Stone, mc::ItemId::DiamondSword, true), 0.0f);
    EXPECT_FALSE(mc::MiningController::canHarvest(mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, true));
}

TEST(McCoreMiningTest, ProgressiveCrackStagesAndBreakEvent) {
    mc::MiningController controller;
    const mc::GridPos cell{10, 5, 20};

    // Update with left click down on Stone using Diamond Pickaxe (0.3s break time)
    controller.update(0.15f, true, true, cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_TRUE(controller.state().is_mining);
    EXPECT_EQ(controller.state().stage, 5); // 0.15 / 0.3 = 0.5 progress -> stage 5
    EXPECT_FALSE(controller.state().broken);

    // Finish mining
    controller.update(0.16f, true, true, cell, mc::BlockId::Stone, mc::ItemId::DiamondPickaxe, false);
    EXPECT_TRUE(controller.state().broken);
    EXPECT_FALSE(controller.state().is_mining);
}

// ============================================================================
// 5. ShieldController & Blocking Logic Tests
// ============================================================================
TEST(McCoreShieldTest, FrontDotCalculationIdentifiesAttackDirection) {
    const float player_pos[3] = {0.0f, 0.0f, 0.0f};
    const float player_yaw = 0.0f; // facing +Z

    const float attacker_ahead[3] = {0.0f, 0.0f, 10.0f};
    EXPECT_NEAR(mc::frontDot(player_pos, player_yaw, attacker_ahead), 1.0f, 1e-4f);

    const float attacker_behind[3] = {0.0f, 0.0f, -10.0f};
    EXPECT_NEAR(mc::frontDot(player_pos, player_yaw, attacker_behind), -1.0f, 1e-4f);

    EXPECT_TRUE(mc::isAttackBlocked(player_pos, player_yaw, attacker_ahead));
    EXPECT_FALSE(mc::isAttackBlocked(player_pos, player_yaw, attacker_behind));
}

TEST(McCoreShieldTest, ShieldControllerRequiresHoldingForRaiseDelay) {
    mc::ShieldController shield;
    EXPECT_FALSE(shield.isBlocking());
    EXPECT_FLOAT_EQ(shield.raiseProgress(), 0.0f);

    // Hold for 0.1s (less than 0.25s)
    shield.update(0.1f, true, true);
    EXPECT_FALSE(shield.isBlocking());
    EXPECT_NEAR(shield.raiseProgress(), 0.4f, 1e-4f);

    // Complete 0.25s raise
    shield.update(0.16f, true, true);
    EXPECT_TRUE(shield.isBlocking());
    EXPECT_FLOAT_EQ(shield.raiseProgress(), 1.0f);

    // Releasing cancels block
    shield.update(0.01f, false, true);
    EXPECT_FALSE(shield.isBlocking());
}

// ============================================================================
// 6. Targeting & Spatial Query Tests
// ============================================================================
#include "mc/targeting.hpp"

TEST(McCoreTargetingTest, PickTargetPicksEntityUnderCrosshairWithinReach) {
    const float cam[3] = {0.0f, 1.7f, 0.0f};
    const float dir[3] = {0.0f, 0.0f, 1.0f}; // looking +Z

    std::vector<mc::TargetableEntity> entities;
    mc::TargetableEntity e1;
    e1.id = 1001;
    e1.rel_pos = {0.0f, 0.0f, 2.0f}; // 2m ahead, torso at y=1.0
    e1.is_hostile = true;
    entities.push_back(e1);

    int idx = mc::pickTarget(cam, dir, entities);
    EXPECT_EQ(idx, 0);

    // Entity farther than 4.5m reach is skipped
    entities[0].rel_pos = {0.0f, 0.0f, 5.0f};
    EXPECT_EQ(mc::pickTarget(cam, dir, entities), -1);

    // Dead entity is skipped
    entities[0].rel_pos = {0.0f, 0.0f, 2.0f};
    entities[0].is_alive = false;
    EXPECT_EQ(mc::pickTarget(cam, dir, entities), -1);
}

TEST(McCoreTargetingTest, PickTargetPrioritizesHostileOverPassive) {
    const float cam[3] = {0.0f, 1.7f, 0.0f};
    const float dir[3] = {0.0f, 0.0f, 1.0f};

    std::vector<mc::TargetableEntity> entities;
    mc::TargetableEntity sheep;
    sheep.id = 1;
    sheep.rel_pos = {0.0f, 0.0f, 2.0f};
    sheep.is_hostile = false; // passive
    entities.push_back(sheep);

    mc::TargetableEntity zombie;
    zombie.id = 2;
    zombie.rel_pos = {0.0f, 0.0f, 3.0f};
    zombie.is_hostile = true;
    entities.push_back(zombie);

    // Should prioritize the hostile zombie even though sheep is closer
    int idx = mc::pickTarget(cam, dir, entities);
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(entities[idx].id, 2);
}

TEST(McCoreTargetingTest, FindNearestHostileReturnsClosestWithinRadius) {
    std::vector<mc::TargetableEntity> entities;
    mc::TargetableEntity far_mob;
    far_mob.id = 10;
    far_mob.rel_pos = {10.0f, 0.0f, 10.0f}; // ~14.1m
    far_mob.is_hostile = true;
    entities.push_back(far_mob);

    mc::TargetableEntity near_mob;
    near_mob.id = 20;
    near_mob.rel_pos = {1.0f, 0.0f, 2.0f}; // ~2.2m
    near_mob.is_hostile = true;
    entities.push_back(near_mob);

    mc::TargetableEntity passive_close;
    passive_close.id = 30;
    passive_close.rel_pos = {0.5f, 0.0f, 0.5f};
    passive_close.is_hostile = false;
    entities.push_back(passive_close);

    int idx = mc::findNearestHostile(entities, 25.0f);
    EXPECT_EQ(idx, 1);
    EXPECT_EQ(entities[idx].id, 20);

    // If max radius is 2m, neither hostile is found
    EXPECT_EQ(mc::findNearestHostile(entities, 2.0f), -1);
}

TEST(McCoreTargetingTest, ScanThrottlePreventsRedundantScansWhileStationary) {
    mc::ScanThrottle throttle;
    const mc::Vec3 pos1{10.0f, 2.0f, 20.0f};

    // First scan at t=1000ms: no enemies found
    throttle.record(1000, pos1, true);

    // 50ms later at same position: should throttle!
    EXPECT_TRUE(throttle.shouldThrottle(1050, pos1, 200, 0.5f));

    // 250ms later (exceeds 200ms window): should NOT throttle!
    EXPECT_FALSE(throttle.shouldThrottle(1250, pos1, 200, 0.5f));

    // Moved > 0.5m even within 50ms: should NOT throttle!
    const mc::Vec3 moved_pos{10.8f, 2.0f, 20.0f};
    EXPECT_FALSE(throttle.shouldThrottle(1050, moved_pos, 200, 0.5f));

    // If last scan found enemies (empty == false), should NEVER throttle
    throttle.record(2000, pos1, false);
    EXPECT_FALSE(throttle.shouldThrottle(2050, pos1, 200, 0.5f));
}

// ============================================================================
// 7. SprintController Tests
// ============================================================================
#include "mc/sprint.hpp"

TEST(McCoreSprintTest, CtrlWithForwardActivatesSprintWhenHungerAllows) {
    mc::SprintController sprint;
    EXPECT_FALSE(sprint.isSprinting());
    EXPECT_FLOAT_EQ(sprint.fovMultiplier(), 1.0f);
    EXPECT_FLOAT_EQ(sprint.boostSpeed(), 0.0f);

    // Ctrl + W with full food (20)
    sprint.update(0.05f, true /* w */, false /* s */, true /* ctrl */, 20);
    EXPECT_TRUE(sprint.isSprinting());
    EXPECT_GT(sprint.fovMultiplier(), 1.0f);
    EXPECT_FLOAT_EQ(sprint.boostSpeed(), 2.5f);

    // Hunger <= 6 cancels sprint
    sprint.update(0.05f, true /* w */, false /* s */, true /* ctrl */, 6);
    EXPECT_FALSE(sprint.isSprinting());
    EXPECT_FLOAT_EQ(sprint.boostSpeed(), 0.0f);

    // Moving backward (S) cancels sprint even with Ctrl + W and full food
    sprint.update(0.05f, true /* w */, true /* s */, true /* ctrl */, 20);
    EXPECT_FALSE(sprint.isSprinting());
}

TEST(McCoreSprintTest, FovSmoothlyTransitionsToSprintMultiplier) {
    mc::SprintController sprint;
    for (int i = 0; i < 20; ++i) {
        sprint.update(0.05f, true, false, true, 20);
    }
    EXPECT_NEAR(sprint.fovMultiplier(), mc::SprintController::kFovSprintMultiplier, 0.01f);
}

// ============================================================================
// 8. BowController Tests
// ============================================================================
#include "mc/bow.hpp"

TEST(McCoreBowTest, PowerCalculationAndDamageScaling) {
    // 0 ticks -> 0 power
    EXPECT_FLOAT_EQ(mc::powerForTicks(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(mc::damageForPower(0.0f), 0.0f);

    // 20 ticks (1.0s) -> full power 1.0, damage 6 + 3 = 9.0
    EXPECT_FLOAT_EQ(mc::powerForTicks(20.0f), 1.0f);
    EXPECT_FLOAT_EQ(mc::damageForPower(1.0f), 9.0f);

    // Overdrawn ticks capped at 1.0
    EXPECT_FLOAT_EQ(mc::powerForTicks(40.0f), 1.0f);
}

TEST(McCoreBowTest, ControllerTracksDrawReleaseAndCancel) {
    mc::BowController bow;
    EXPECT_FALSE(bow.drawing());

    // Press right click with usable bow
    auto u1 = bow.update(0.05f, true, true);
    EXPECT_TRUE(u1.started);
    EXPECT_TRUE(bow.drawing());

    // Hold for 1 second (20 ticks)
    for (int i = 0; i < 20; ++i) {
        bow.update(0.05f, true, true);
    }
    EXPECT_NEAR(bow.power(), 1.0f, 0.01f);

    // Release right click fires arrow
    auto u2 = bow.update(0.05f, false, true);
    EXPECT_TRUE(u2.released);
    EXPECT_NEAR(u2.power, 1.0f, 0.01f);
    EXPECT_FALSE(bow.drawing());

    // Very short draw (< 3 ticks / power < 0.1) cancels
    bow.update(0.05f, true, true);
    auto u3 = bow.update(0.05f, false, true); // only 1 tick
    EXPECT_TRUE(u3.cancelled);
    EXPECT_FALSE(u3.released);
}

TEST(McCoreBowTest, UsabilityRulesPrioritizeMainHandActions) {
    // Bow in main hand is usable
    EXPECT_TRUE(mc::bowHandUsable(mc::ItemId::Bow, 1, mc::ItemId::None, 0));

    // Shield in main hand claims right click, blocks offhand bow
    EXPECT_FALSE(mc::bowHandUsable(mc::ItemId::Shield, 1, mc::ItemId::Bow, 1));
    EXPECT_FALSE(mc::bowInOffhand(mc::ItemId::Shield, 1, mc::ItemId::Bow, 1));

    // Sword in main hand (no right click action) allows offhand bow
    EXPECT_TRUE(mc::bowHandUsable(mc::ItemId::DiamondSword, 1, mc::ItemId::Bow, 1));
    EXPECT_TRUE(mc::bowInOffhand(mc::ItemId::DiamondSword, 1, mc::ItemId::Bow, 1));
}

// ============================================================================
// 9. CreeperManager Tests
// ============================================================================
#include "mc/creeper.hpp"

TEST(McCoreCreeperTest, ProximityStartsFuseAndHissThenExplodes) {
    mc::CreeperManager creeper(1.0f); // 1.0s fuse for test
    mc::CreeperTarget summon;
    summon.id = 100;
    summon.feet[0] = 0.0f; summon.feet[1] = 0.0f; summon.feet[2] = 0.0f;
    summon.is_creeper = true;
    summon.hp = 100;

    mc::CreeperEnemyTarget enemy;
    enemy.chr = 200;
    enemy.feet[0] = 2.0f; enemy.feet[1] = 0.0f; enemy.feet[2] = 0.0f; // 2m distance (<= 3.5m)
    enemy.hostile = true;
    enemy.hp = 100;

    // First tick: detects proximity, triggers Hiss event
    auto ev1 = creeper.update(0.05f, {summon}, {enemy});
    ASSERT_EQ(ev1.size(), 1u);
    EXPECT_EQ(ev1[0].kind, mc::CreeperEventKind::Hiss);
    EXPECT_TRUE(creeper.isPrimed(100));

    // Update for 0.95s (fuse reaches 0)
    std::vector<mc::CreeperEvent> ev_final;
    for (int i = 0; i < 20; ++i) {
        auto ev = creeper.update(0.05f, {summon}, {enemy});
        if (!ev.empty()) ev_final = ev;
    }

    ASSERT_EQ(ev_final.size(), 1u);
    EXPECT_EQ(ev_final[0].kind, mc::CreeperEventKind::Explosion);
    EXPECT_TRUE(ev_final[0].kill_creeper);
}

TEST(McCoreCreeperTest, ManualIgnitionAndDefuseRules) {
    mc::CreeperManager creeper(1.5f);
    mc::CreeperTarget summon;
    summon.id = 100;
    summon.is_creeper = true;

    // Manual ignition with Flint and Steel primes creeper even without enemies
    EXPECT_TRUE(creeper.ignite(100));
    EXPECT_TRUE(creeper.isPrimed(100));

    // Dying cancels fuse
    summon.hp = 0;
    creeper.update(0.05f, {summon}, {});
    EXPECT_FALSE(creeper.isPrimed(100));
}

// ============================================================================
// 10. TNT Rules & Damage Tests
// ============================================================================
#include "mc/tnt.hpp"

TEST(McCoreTntTest, RightClickOnlyIgnitesWithFlintAndSteel) {
    EXPECT_TRUE(mc::shouldIgniteTntOnRightClick(mc::ItemId::FlintAndSteel, mc::BlockId::Tnt));
    EXPECT_FALSE(mc::shouldIgniteTntOnRightClick(mc::ItemId::BlockDirt, mc::BlockId::Tnt));
    EXPECT_FALSE(mc::shouldIgniteTntOnRightClick(mc::ItemId::DiamondSword, mc::BlockId::Tnt));
    EXPECT_FALSE(mc::shouldIgniteTntOnLeftClick(mc::ItemId::FlintAndSteel, mc::BlockId::Tnt));
}

TEST(McCoreTntTest, ExplosionDamageFollowsLinearFalloff) {
    const float center[3] = {0.0f, 0.0f, 0.0f};
    const float at_center[3] = {0.0f, 0.0f, 0.0f};
    const float half_dist[3] = {4.0f, 0.0f, 0.0f}; // 4m on 8m radius
    const float outside[3] = {10.0f, 0.0f, 0.0f};

    EXPECT_EQ(mc::computeTntExplosionDamage(center, at_center, 8.0f, 1000.0f), 1000);
    EXPECT_NEAR(mc::computeTntExplosionDamage(center, half_dist, 8.0f, 1000.0f), 500, 5);
    EXPECT_EQ(mc::computeTntExplosionDamage(center, outside, 8.0f, 1000.0f), 0);
}

