#include <gtest/gtest.h>
#include "mc/consumables.hpp"
#include "mc/animator.hpp"

TEST(ConsumablesTest, CannotEatNonFoodItem) {
    mc::ConsumableSystem system;
    EXPECT_FALSE(system.startEating(mc::ItemId::DiamondSword));
    EXPECT_FALSE(system.isEating());
}

TEST(ConsumablesTest, GoldenAppleRequiresOnePointSixSeconds) {
    mc::ConsumableSystem system;
    EXPECT_TRUE(system.startEating(mc::ItemId::GoldenApple));
    EXPECT_TRUE(system.isEating());

    // Halfway through (0.8s)
    auto ev1 = system.update(0.8f);
    EXPECT_FALSE(ev1.completed);
    EXPECT_NEAR(system.getProgress(), 0.5f, 0.05f);

    // Cancel resets progress
    system.cancel();
    EXPECT_FALSE(system.isEating());
    EXPECT_FLOAT_EQ(system.getProgress(), 0.0f);

    // Eat again to completion
    system.startEating(mc::ItemId::GoldenApple);
    system.update(1.0f);
    auto ev2 = system.update(0.65f); // Total >= 1.6s
    EXPECT_TRUE(ev2.completed);
    EXPECT_TRUE(ev2.play_burp_sound);

    // Check granted effects
    EXPECT_GT(ev2.effects.size(), 0u);
    bool has_regen = false;
    bool has_absorption = false;
    for (const auto& eff : ev2.effects) {
        if (eff.type == mc::EffectType::Regeneration) has_regen = true;
        if (eff.type == mc::EffectType::Absorption) has_absorption = true;
    }
    EXPECT_TRUE(has_regen);
    EXPECT_TRUE(has_absorption);
}

TEST(ConsumablesTest, ChewingPulsesPeriodically) {
    mc::ConsumableSystem system;
    system.startEating(mc::ItemId::GoldenApple);

    int chew_ticks = 0;
    // Step in 0.05s increments over 1.6s
    for (int i = 0; i < 32; ++i) {
        auto ev = system.update(0.05f);
        if (ev.chew_sound) {
            chew_ticks++;
        }
    }
    // Should trigger chew sounds multiple times (every ~0.2s = 7-8 times)
    EXPECT_GE(chew_ticks, 5);
}

TEST(ConsumablesTest, AnimatorHandJitterWhileEating) {
    mc::SteveAnimator animator;
    mc::SteveAnimInput input_normal{};
    animator.update(0.05f, input_normal);
    const auto normal_arm = animator.getTransforms()[static_cast<size_t>(mc::StevePart::RightArm)];

    mc::SteveAnimInput input_eating{};
    input_eating.eating_progress = 0.5f;
    animator.update(0.05f, input_eating);
    const auto eating_arm = animator.getTransforms()[static_cast<size_t>(mc::StevePart::RightArm)];

    // Eating arm pitch must be significantly altered to bring food to mouth
    EXPECT_NE(normal_arm.rot.y, eating_arm.rot.y);
}
