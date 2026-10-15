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

// ============================================================================
// 11. Minecraft Jump Physics Tests
// ============================================================================
#include "mc/jump.hpp"

TEST(McCoreJumpTest, PeakHeightFollowsVanillaSimulation) {
    EXPECT_NEAR(mc::mcJumpPeakHeight(), 1.25f, 0.06f);
    EXPECT_NEAR(mc::kMcJumpSpeed * mc::kMcJumpSpeed / (2.f * mc::kMcGravity), 1.10f, 0.01f);
}

TEST(McCoreJumpTest, JumpControllerSprintingAppliesMomentumBoost) {
    mc::MinecraftJumpController jump;
    EXPECT_FALSE(jump.isActive());

    // Normal jump with 5.0 m/s horizontal speed
    jump.start(10.0f, 5.0f, 0.0f, false);
    EXPECT_TRUE(jump.isActive());
    EXPECT_FLOAT_EQ(jump.vx(), 5.0f);
    EXPECT_FLOAT_EQ(jump.vy(), mc::kMcJumpInitialVelocity);

    // Sprint jump with 5.0 m/s horizontal speed: 1.15x boost
    jump.start(10.0f, 5.0f, 0.0f, true);
    EXPECT_FLOAT_EQ(jump.vx(), 5.0f * mc::kMcJumpSprintBoost);
}

TEST(McCoreJumpTest, JumpControllerAirSteeringAndLanding) {
    mc::MinecraftJumpController jump;
    jump.start(10.0f, 4.0f, 0.0f, false);

    // Update with input steering in opposite direction
    auto r1 = jump.update(0.05f, -1.0f, 0.0f, 4.0f, true);
    EXPECT_TRUE(r1.active);
    EXPECT_LT(r1.vx, 4.0f); // velocity turned towards desired direction
    EXPECT_GT(r1.y, 10.0f); // moving upwards initially

    // Land on ground
    jump.land(10.0f);
    EXPECT_FALSE(jump.isActive());
    EXPECT_FLOAT_EQ(jump.vy(), 0.0f);
}

TEST(McCoreJumpTest, CanTakeOffArbitratesGroundAndBlockSupport) {
    EXPECT_TRUE(mc::canTakeOff(true, false, false, false));
    EXPECT_TRUE(mc::canTakeOff(false, true, false, false));
    EXPECT_TRUE(mc::canTakeOff(false, false, true, false));
    EXPECT_FALSE(mc::canTakeOff(false, false, false, false));
    EXPECT_FALSE(mc::canTakeOff(true, true, true, true)); // dead entity cannot jump
}

// ============================================================================
// 12. Creative Flight Locomotion Tests
// ============================================================================
#include "mc/flight.hpp"

TEST(McCoreFlightTest, DoubleTapDetectorDetectsQuickSpacebar) {
    mc::DoubleTap tap;
    EXPECT_FALSE(tap.update(true, 1000));
    EXPECT_FALSE(tap.update(false, 1050));
    EXPECT_TRUE(tap.update(true, 1200)); // second press within 300ms
    EXPECT_FALSE(tap.update(false, 1250));
    EXPECT_FALSE(tap.update(true, 1300)); // third press begins new pair

    // Slow press exceeds 300ms
    tap.reset();
    EXPECT_FALSE(tap.update(true, 1000));
    EXPECT_FALSE(tap.update(false, 1100));
    EXPECT_FALSE(tap.update(true, 1301)); // 301ms later
}

TEST(McCoreFlightTest, HeadingFromCameraProjectsHorizontalPlane) {
    mc::FlightHeading h;
    const float up_look[3] = {0.f, 0.9f, 0.436f}, right[3] = {1.f, 0.f, 0.f};
    ASSERT_TRUE(mc::headingFromCamera(up_look, right, h));
    EXPECT_NEAR(h.fwd[0], 0.f, 1e-5f);
    EXPECT_NEAR(h.fwd[1], 1.f, 1e-5f);

    const float straight_up[3] = {0.f, 1.f, 0.f};
    EXPECT_FALSE(mc::headingFromCamera(straight_up, right, h));
}

TEST(McCoreFlightTest, FlightStepMovesAtExactSpeedsWithoutDiagonalSpeedup) {
    float pos[3] = {0.f, 10.f, 0.f};
    mc::FlightHeading h;
    h.fwd[0] = 0.f; h.fwd[1] = 1.f;
    h.right[0] = 1.f; h.right[1] = 0.f;

    mc::FlightKeys k;
    k.forward = true;
    k.right = true; // diagonal

    bool moved = mc::flightStep(pos, k, h, 0.05f);
    EXPECT_TRUE(moved);

    // Displacement magnitude must equal speed * dt (10.9 * 0.05 = 0.545), NOT sqrt(2) * 0.545
    float disp = std::sqrt(pos[0] * pos[0] + pos[2] * pos[2]);
    EXPECT_NEAR(disp, mc::kFlightHorizontalSpeed * 0.05f, 1e-4f);

    // Vertical flight
    k.forward = false;
    k.right = false;
    k.up = true;
    mc::flightStep(pos, k, h, 0.05f);
    EXPECT_NEAR(pos[1], 10.0f + mc::kFlightVerticalSpeed * 0.05f, 1e-4f);
}

// ============================================================================
// 13. Melee Controller & Combat Feedback Tests
// ============================================================================
#include "mc/melee.hpp"

TEST(McCoreMeleeTest, MeleeControllerHotbarSelectionAndWrap) {
    mc::MeleeController m;
    EXPECT_EQ(m.selectedSlot(), 0);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondSword);

    m.scroll(1);
    EXPECT_EQ(m.selectedSlot(), 1);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondPickaxe);

    m.scroll(-2);
    EXPECT_EQ(m.selectedSlot(), 8); // wraps around

    m.select(4);
    EXPECT_EQ(m.selectedSlot(), 4);
    m.select(9); // out of range: ignored
    EXPECT_EQ(m.selectedSlot(), 4);
}

TEST(McCoreMeleeTest, AttackCooldownAndSwingProgress) {
    mc::MeleeController m;
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f);
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f);

    const float before = m.startSwing();
    EXPECT_FLOAT_EQ(before, 1.f);
    EXPECT_FLOAT_EQ(m.cooldown(), 0.f);

    m.tick(mc::Session::kSwingDurationSec * 0.5f);
    EXPECT_NEAR(m.swingProgress(), 0.5f, 1e-3f);

    m.tick(mc::Session::kSwingDurationSec);
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f); // finished swing

    m.tick(mc::Session::kAttackRechargeSec);
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f); // fully recharged
}

TEST(McCoreMeleeTest, JumpTrackerTracksCriticalHitState) {
    mc::JumpTracker tracker;
    EXPECT_FALSE(tracker.jumping());

    // Jump pressed on ground
    tracker.update(0.05f, true, false);
    EXPECT_TRUE(tracker.jumping());

    // Airborne
    tracker.update(0.05f, false, true);
    EXPECT_TRUE(tracker.jumping());

    // Landed
    tracker.update(0.05f, false, false);
    EXPECT_FALSE(tracker.jumping());
}

TEST(McCoreMeleeTest, HitFeedbackFadesOverDuration) {
    mc::HitFeedback fb;
    EXPECT_FLOAT_EQ(fb.hit(), 0.f);
    EXPECT_FALSE(fb.crit());

    fb.onHit(true);
    EXPECT_FLOAT_EQ(fb.hit(), 1.f);
    EXPECT_TRUE(fb.crit());

    fb.tick(mc::HitFeedback::kHitSec * 0.5f);
    EXPECT_NEAR(fb.hit(), 0.5f, 0.05f);
    EXPECT_TRUE(fb.crit());

    fb.tick(mc::HitFeedback::kHitSec);
    EXPECT_FLOAT_EQ(fb.hit(), 0.f);
    EXPECT_FALSE(fb.crit());
}

// ============================================================================
// 14. Survival Defense & Totem Mechanics Tests
// ============================================================================
#include "mc/survival.hpp"

TEST(McCoreSurvivalTest, TotemOnlyActsOnLethalHitLeavesOneHp) {
    auto a = mc::totemClamp(50, 100, true);
    EXPECT_FALSE(a.popped);
    EXPECT_EQ(a.damage, 50);

    auto b = mc::totemClamp(100, 100, true); // exactly lethal
    EXPECT_TRUE(b.popped);
    EXPECT_EQ(b.damage, 99); // leaves 1 HP

    auto c = mc::totemClamp(300, 100, true); // overkill
    EXPECT_TRUE(c.popped);
    EXPECT_EQ(c.damage, 99);

    auto d = mc::totemClamp(300, 100, false); // no totem
    EXPECT_FALSE(d.popped);
    EXPECT_EQ(d.damage, 300);

    auto e = mc::totemClamp(5, 1, true); // on 1 HP already
    EXPECT_TRUE(e.popped);
    EXPECT_EQ(e.damage, 0);

    EXPECT_FALSE(mc::totemClamp(300, 0, true).popped); // already dead
}

TEST(McCoreSurvivalTest, ArmorDamageReductionCapsAtEightyPercent) {
    auto r1 = mc::computeArmorDamageReduction(100, 100, 20, 0.50f);
    EXPECT_EQ(r1.final_damage, 50);

    // Over-capped ratio (e.g. 0.95f) must cap at 80% reduction (final damage >= 20)
    auto r2 = mc::computeArmorDamageReduction(100, 100, 20, 0.95f);
    EXPECT_EQ(r2.final_damage, 20);

    // No armor
    auto r3 = mc::computeArmorDamageReduction(100, 100, 0, 0.0f);
    EXPECT_EQ(r3.final_damage, 100);
}

TEST(McCoreSurvivalTest, SurvivalStateAbsorptionAndRegeneration) {
    mc::SurvivalState s;
    s.apply({mc::ActiveEffect{mc::EffectType::Absorption, 1, 120.f}}); // 4 HP = 200 at max 1000
    EXPECT_EQ(s.absorb(150, 1000), 0);
    EXPECT_NEAR(s.absorptionMc(), 1.f, 0.01f); // 50 native HP left = 1 MC HP
    EXPECT_EQ(s.absorb(100, 1000), 50);
    EXPECT_FLOAT_EQ(s.absorptionMc(), 0.f);
}

// ============================================================================
// 15. Combat Particles & Camera Hurt Tilt Tests
// ============================================================================
#include "mc/particles.hpp"

TEST(McCoreParticlesTest, ParticleSystemSpawnsCritParticlesWithGravity) {
    mc::ParticleSystem sys;
    mc::Rng rng(42);
    const float at[3] = {0.f, 1.f, 0.f};

    sys.spawnCrit(at, rng);
    EXPECT_EQ(sys.alive().size(), static_cast<size_t>(mc::ParticleSystem::kCritCount));

    const float initial_vy = sys.alive()[0].vel[1];
    sys.update(0.1f);
    EXPECT_LT(sys.alive()[0].vel[1], initial_vy); // gravity pulls downward
}

TEST(McCoreParticlesTest, ParticleSystemSweepAndFadeLifecycle) {
    mc::ParticleSystem sys;
    const float at[3] = {0.f, 1.f, 0.f};

    sys.spawnSweep(at);
    ASSERT_EQ(sys.alive().size(), 1u);
    EXPECT_EQ(sys.alive()[0].kind, mc::ParticleKind::Sweep);
    EXPECT_EQ(sys.alive()[0].frame, 0);

    sys.update(0.2f); // half of 0.4s life
    EXPECT_EQ(sys.alive()[0].frame, 4);
    EXPECT_NEAR(mc::ParticleSystem::fade(sys.alive()[0]), 0.5f, 0.1f);

    sys.update(0.25f); // exceeded lifetime
    EXPECT_EQ(sys.alive().size(), 0u);
}

TEST(McCoreParticlesTest, HurtTiltFormulaPeaksAtFourteenDegrees) {
    // 14 degrees in radians = ~0.2443 rad
    constexpr float kFourteenDegRad = 14.f * 0.01745329252f;

    // t = 1.0 (just hurt): sin(pi) = 0
    EXPECT_NEAR(mc::hurtTiltRadians(1.0f, 1.0f), 0.f, 1e-4f);

    // t around 0.84: t^4 ~ 0.5 -> sin(pi/2) ~ 1.0 -> peaks at 14 deg
    const float peak_t = std::pow(0.5f, 0.25f);
    EXPECT_NEAR(mc::hurtTiltRadians(peak_t, 1.0f), kFourteenDegRad, 1e-3f);
    EXPECT_NEAR(mc::hurtTiltRadians(peak_t, -1.0f), -kFourteenDegRad, 1e-3f); // opposite roll
}

// ============================================================================
// 16. Mob Lifecycle & Corpse Animation Tests
// ============================================================================
#include "mc/mobs.hpp"

TEST(McCoreMobsTest, MobLifecycleTracksHurtAndDeathState) {
    mc::MobLifecycleTracker mob;
    EXPECT_FALSE(mob.isHurt());
    EXPECT_FALSE(mob.isDying());

    // Initial observation on spawn: 500 HP
    mob.updateHp(500, 500, 0.05f);
    EXPECT_FALSE(mob.isHurt());
    EXPECT_FALSE(mob.isDying());

    // Mob takes damage: 500 -> 400
    mob.updateHp(400, 500, 0.05f);
    EXPECT_TRUE(mob.isHurt());
    EXPECT_FALSE(mob.isDying());

    // Mob dies: 400 -> 0
    mob.updateHp(0, 500, 0.05f);
    EXPECT_TRUE(mob.isDying());
    EXPECT_TRUE(mob.isHurt()); // dying mob stays tinted red

    // After death duration expires (> 1.0s / 20 ticks)
    for (int i = 0; i < 25; ++i) {
        mob.tick(0.05f);
    }
    EXPECT_TRUE(mob.isCorpseExpired());
}

TEST(McCoreMobsTest, DeathFlipFractionInterpolatesSmoothly) {
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(-0.1f), 0.f);
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(0.0f), 0.f);
    EXPECT_GT(mc::deathFlipFraction(0.25f), 0.f);
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(0.5f), 1.0f); // fully tipped over
    EXPECT_FLOAT_EQ(mc::deathFlipFraction(1.0f), 1.0f);
}

// ============================================================================
// 17. Voxel Blocks & Mesh Generation Tests
// ============================================================================
#include "mc/blocks.hpp"
#include "mc/blockmesh.hpp"

TEST(McCoreBlockMeshTest, SingleBlockHasSixFacesTwentyFourVertices) {
    mc::BlockGrid grid;
    grid.place({0, 0, 0}, mc::BlockId::Dirt);

    auto mesh = mc::buildBlockMesh(grid);
    // 6 faces * 4 vertices = 24 vertices
    EXPECT_EQ(mesh.vertices.size(), 24u);
    // 6 faces * 6 indices (2 triangles) = 36 indices
    EXPECT_EQ(mesh.indices.size(), 36u);
}

TEST(McCoreBlockMeshTest, AdjacentBlocksCullSharedInternalFace) {
    mc::BlockGrid grid;
    grid.place({0, 0, 0}, mc::BlockId::Dirt);
    grid.place({1, 0, 0}, mc::BlockId::Dirt); // shares x=1 / x=0 face

    auto mesh = mc::buildBlockMesh(grid);
    // 2 blocks with 1 shared face culled = (6 + 6 - 2) = 10 faces = 40 vertices, 60 indices
    EXPECT_EQ(mesh.vertices.size(), 40u);
    EXPECT_EQ(mesh.indices.size(), 60u);
}

TEST(McCoreBlockMeshTest, MiningOverlayCrackStageMapsValidUV) {
    EXPECT_NE(mc::crackUVForStage(0), nullptr);
    EXPECT_NE(mc::crackUVForStage(9), nullptr);
    EXPECT_EQ(mc::crackUVForStage(-1), nullptr);
    EXPECT_EQ(mc::crackUVForStage(10), nullptr);
}

// ============================================================================
// 18. Motion Orientation & Camera Projection Tests
// ============================================================================
#include "mc/orientation.hpp"

TEST(McCoreOrientationTest, YawQuatRoundtripPreservesAngles) {
    for (float angle_deg : {0.f, 45.f, 90.f, 135.f, 180.f, -90.f, -135.f}) {
        const float rad = angle_deg * 0.01745329252f;
        auto q = mc::OrientationQuat::fromYaw(rad);
        EXPECT_NEAR(std::abs(mc::normalizeAngle(q.toYaw() - rad)), 0.f, 1e-4f);
    }
}

TEST(McCoreOrientationTest, ComputeInputDirectionNormalizesDiagonal) {
    const float cam_fwd[3] = {0.f, 0.f, 1.f};
    const float cam_right[3] = {1.f, 0.f, 0.f};
    float dir[2]{0.f, 0.f};

    // Forward + Right (W + D diagonal)
    bool has_dir = mc::computeInputDirection(1.f, 1.f, cam_fwd, cam_right, dir);
    EXPECT_TRUE(has_dir);

    // Direction magnitude must be exactly 1.0 (no diagonal speed increase)
    const float len = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1]);
    EXPECT_NEAR(len, 1.0f, 1e-5f);
    EXPECT_NEAR(dir[0], 0.70710678f, 1e-4f);
    EXPECT_NEAR(dir[1], 0.70710678f, 1e-4f);
}

TEST(McCoreOrientationTest, StepYawRespectsTurnSpeedLimit) {
    const float current = 0.f;
    const float target = 1.0f; // ~57 degrees
    const float speed = 2.0f;  // rad/s

    // At dt = 0.1s, max step = 0.2 rad -> reaches 0.2
    float stepped = mc::stepYaw(current, target, 0.1f, speed, false);
    EXPECT_NEAR(stepped, 0.2f, 1e-5f);

    // Instant turn bypasses speed
    float instant = mc::stepYaw(current, target, 0.1f, speed, true);
    EXPECT_NEAR(instant, target, 1e-5f);
}

// ============================================================================
// 19. Blob Shadow Generation Tests
// ============================================================================
#include "mc/shadow.hpp"

TEST(McCoreShadowTest, ShadowAtFadesLinearlyWithHeight) {
    // Standing on ground (feet_y = 10, ground_y = 10)
    auto s0 = mc::shadowAt(0.f, 10.f, 0.f, 10.f);
    EXPECT_TRUE(s0.visible);
    EXPECT_FLOAT_EQ(s0.strength, mc::kMaxStrength);
    EXPECT_FLOAT_EQ(s0.radius, mc::kBaseRadius);
    EXPECT_FLOAT_EQ(s0.y, 10.f + mc::kLift);

    // 8m above ground (halfway to kFadeHeight = 16m)
    auto s1 = mc::shadowAt(0.f, 18.f, 0.f, 10.f);
    EXPECT_TRUE(s1.visible);
    EXPECT_NEAR(s1.strength, mc::kMaxStrength * 0.5f, 0.01f);

    // 20m above ground (exceeds kFadeHeight = 16m)
    auto s2 = mc::shadowAt(0.f, 30.f, 0.f, 10.f);
    EXPECT_FALSE(s2.visible);
}

TEST(McCoreShadowTest, BuildShadowDiscProducesValidTriangleFan) {
    auto disc = mc::buildShadowDisc(16);
    // Center vertex (1) + 16 rim vertices = 17 vertices
    EXPECT_EQ(disc.vertices.size(), 17u);
    // 16 triangles * 3 = 48 indices
    EXPECT_EQ(disc.indices.size(), 48u);
}




