#include <gtest/gtest.h>

#include "eldenring_hudtex.hpp"
#include "eldenring_melee.hpp"

#include "mc/contracts/combat_adapter.hpp"

using namespace eldenring::live;

namespace {

class NullCombat : public mc::ICombatAdapter {
public:
    bool processHit(const mc::HitIntent&) override { return true; }
    float getMaxHealth(mc::EntityId) override { return 100.f; }
    void triggerStaggerOrRagdoll(mc::EntityId, const mc::Vec3&, float) override {}
};

} // namespace

TEST(EldenRingMelee, HotbarStartsOnSwordAndWrapsOnScroll) {
    MeleeController m;
    EXPECT_EQ(m.selectedSlot(), 0);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondSword);
    m.scroll(1);
    EXPECT_EQ(m.selectedSlot(), 1);
    EXPECT_EQ(m.heldItem(), mc::ItemId::DiamondPickaxe);
    m.scroll(-2);
    EXPECT_EQ(m.selectedSlot(), 8); // wraps
    m.select(4);
    EXPECT_EQ(m.selectedSlot(), 4);
    m.select(9); // out of range: ignored
    m.select(-1);
    EXPECT_EQ(m.selectedSlot(), 4);
}

TEST(EldenRingMelee, SwingStartsAnimationAndResetsCooldown) {
    MeleeController m;
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f);
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f);
    const float before = m.startSwing();
    EXPECT_FLOAT_EQ(before, 1.f);
    EXPECT_FLOAT_EQ(m.cooldown(), 0.f);
    m.tick(mc::Session::kSwingDurationSec * 0.5f);
    EXPECT_NEAR(m.swingProgress(), 0.5f, 1e-3f);
    m.tick(mc::Session::kSwingDurationSec); // past the end
    EXPECT_FLOAT_EQ(m.swingProgress(), 0.f);
}

TEST(EldenRingMelee, CooldownRechargesToOne) {
    MeleeController m;
    m.startSwing();
    m.tick(mc::Session::kAttackRechargeSec * 0.5f);
    EXPECT_NEAR(m.cooldown(), 0.5f, 1e-3f);
    m.tick(10.f);
    EXPECT_FLOAT_EQ(m.cooldown(), 1.f);
}

TEST(EldenRingMelee, DamageDependsOnHeldItemAndCooldown) {
    NullCombat adapter;
    mc::CombatEngine engine(adapter);
    MeleeController m;
    const mc::Vec3 dir{1.f, 0.f, 0.f};

    mc::HitIntent sword = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    m.select(1);
    mc::HitIntent pick = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    m.select(2); // dirt block: bare hand
    mc::HitIntent hand = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, true, {}, dir);
    EXPECT_GT(sword.damage, pick.damage);
    EXPECT_GT(pick.damage, hand.damage);

    m.select(0);
    mc::HitIntent weak = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 0.f, false, true, {}, dir);
    EXPECT_LT(weak.damage, sword.damage * 0.3f); // an uncharged swing is a poor hit
}

TEST(EldenRingMelee, FallingFullChargeIsCritical) {
    NullCombat adapter;
    mc::CombatEngine engine(adapter);
    MeleeController m;
    const mc::HitIntent crit = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, true, false, {}, {1.f, 0.f, 0.f});
    EXPECT_TRUE(crit.is_critical);
    const mc::HitIntent normal = m.makeIntent(engine, mc::EntityId::LocalPlayer, mc::EntityId::None, 1.f, false, false, {}, {1.f, 0.f, 0.f});
    EXPECT_FALSE(normal.is_critical);
    EXPECT_GT(crit.damage, normal.damage);
}

TEST(EldenRingMelee, ErDamageScalesWithEnemyMaxHp) {
    mc::HitIntent sword;
    sword.damage = 7.f;
    sword.max_hp_percent = 0.05f;
    EXPECT_EQ(erDamage(sword, 1000), 50);   // a full sword hit = 5% of max HP
    EXPECT_EQ(erDamage(sword, 20000), 1000);

    mc::HitIntent half = sword;
    half.damage = 3.5f; // half charge
    EXPECT_EQ(erDamage(half, 1000), 25);

    mc::HitIntent tiny = sword;
    tiny.damage = 0.1f;
    EXPECT_EQ(erDamage(tiny, 100), 1); // never below 1 for a landed hit
    EXPECT_EQ(erDamage(sword, 0), 0);  // unknown max HP: nothing to scale
}

TEST(EldenRingJump, PressStartsAJumpThatEndsOnLanding) {
    JumpTracker j;
    EXPECT_FALSE(j.jumping());
    j.update(0.016f, /*jump_pressed=*/true, /*airborne=*/false); // pressed on the ground: the wind-up counts
    EXPECT_TRUE(j.jumping());
    j.update(0.5f, false, false); // still winding up
    EXPECT_TRUE(j.jumping());
    j.update(0.05f, false, true); // left the ground
    EXPECT_TRUE(j.jumping());
    j.update(0.1f, false, false); // landed
    EXPECT_FALSE(j.jumping());
}

TEST(EldenRingJump, WindUpThatNeverLeavesTheGroundTimesOut) {
    JumpTracker j;
    j.update(0.016f, true, false);
    j.update(JumpTracker::kWindUpTimeoutSec + 0.1f, false, false);
    EXPECT_FALSE(j.jumping());
}

TEST(EldenRingJump, AirborneWithoutPressIsAJump) {
    JumpTracker j; // stepping off a ledge
    j.update(0.016f, false, true);
    EXPECT_TRUE(j.jumping());
    j.update(0.3f, false, true);
    EXPECT_TRUE(j.jumping());
    j.update(0.016f, false, false);
    EXPECT_FALSE(j.jumping());
}

TEST(EldenRingJump, PressDuringFlightDoesNotRestartTheTimeout) {
    JumpTracker j;
    j.update(0.016f, true, false);
    j.update(0.1f, false, true);
    j.update(0.5f, true, true); // pressed again in the air
    EXPECT_TRUE(j.jumping());
    j.update(0.016f, false, false);
    EXPECT_FALSE(j.jumping()); // a landing ends it regardless
}

TEST(EldenRingJump, CanBeReArmedAfterLanding) {
    JumpTracker j;
    j.update(0.016f, true, false);
    j.update(0.8f, false, true);
    j.update(0.1f, false, false);
    EXPECT_FALSE(j.jumping());
    j.update(0.016f, true, false);
    EXPECT_TRUE(j.jumping());
}

TEST(EldenRingFeedback, HitMarkerFadesAndKeepsItsKindWhileVisible) {
    HitFeedback f;
    EXPECT_FLOAT_EQ(f.hit(), 0.f);
    f.onHit(false);
    EXPECT_FLOAT_EQ(f.hit(), 1.f);
    EXPECT_FALSE(f.crit());
    f.tick(HitFeedback::kHitSec * 0.5f);
    EXPECT_NEAR(f.hit(), 0.5f, 1e-3f);
    f.tick(HitFeedback::kHitSec);
    EXPECT_FLOAT_EQ(f.hit(), 0.f);
}

TEST(EldenRingFeedback, CritMarkerLatchesUntilItFades) {
    HitFeedback f;
    f.onHit(true);
    EXPECT_TRUE(f.crit());
    f.tick(HitFeedback::kHitSec * 0.5f);
    f.onHit(false); // a plain hit right after: refreshes, the crit look is replaced by the newest hit
    EXPECT_FLOAT_EQ(f.hit(), 1.f);
    EXPECT_FALSE(f.crit());
    f.onHit(true);
    f.tick(10.f);
    EXPECT_FALSE(f.crit()); // faded: no stale flag
}

TEST(EldenRingFeedback, KillMarkerIsSeparateAndLonger) {
    HitFeedback f;
    f.onHit(false);
    f.onKill();
    EXPECT_FLOAT_EQ(f.kill(), 1.f);
    f.tick(HitFeedback::kHitSec + 0.01f);
    EXPECT_FLOAT_EQ(f.hit(), 0.f);
    EXPECT_GT(f.kill(), 0.4f); // still showing after the hit marker is gone
    f.tick(HitFeedback::kKillSec);
    EXPECT_FLOAT_EQ(f.kill(), 0.f);
}

TEST(EldenRingHudTex, EveryDefaultHotbarItemHasAnIconInTheOrderOfTheAtlas) {
    MeleeController m;
    for (int slot = 0; slot < MeleeController::kSlots; ++slot) {
        const mc::hud::HudUV* uv = eldenring::render::uvForItem(m.itemAt(slot));
        ASSERT_NE(uv, nullptr) << "slot " << slot;
        EXPECT_FLOAT_EQ(uv->u0, mc::hud::kUV_ITEMS[slot].u0) << "slot " << slot;
        EXPECT_FLOAT_EQ(uv->v0, mc::hud::kUV_ITEMS[slot].v0) << "slot " << slot;
    }
    EXPECT_EQ(eldenring::render::uvForItem(mc::ItemId::Arrow), nullptr);
    EXPECT_EQ(eldenring::render::uvForItem(mc::ItemId::None), nullptr);
}

TEST(EldenRingHudTex, UpscaleNearestReplicatesEveryPixel) {
    const uint8_t src[2 * 2 * 4] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    const auto out = eldenring::render::upscaleNearest(src, 2, 2, 3);
    ASSERT_EQ(out.size(), 6u * 6u * 4u);
    auto px = [&](unsigned x, unsigned y, unsigned c) { return out[(static_cast<size_t>(y) * 6 + x) * 4 + c]; };
    EXPECT_EQ(px(0, 0, 0), 1);
    EXPECT_EQ(px(2, 2, 3), 4);   // still inside the first source pixel
    EXPECT_EQ(px(3, 0, 0), 5);   // the second source pixel starts at x = 3
    EXPECT_EQ(px(0, 3, 0), 9);   // the third at y = 3
    EXPECT_EQ(px(5, 5, 2), 15);
    EXPECT_TRUE(eldenring::render::upscaleNearest(nullptr, 2, 2, 3).empty());
    EXPECT_TRUE(eldenring::render::upscaleNearest(src, 2, 2, 0).empty());
}
