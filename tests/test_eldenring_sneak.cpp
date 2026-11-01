#include <gtest/gtest.h>

#include "eldenring_runtime_context.hpp"
#include "eldenring_sneak.hpp"
#include "eldenring_world_runtime.hpp"

using namespace eldenring;
using namespace eldenring::world;

namespace {

constexpr int kVkShift = 0x10; // VK_SHIFT

void resetSneakRuntime(VoxelWorldRuntime& runtime, RuntimeContext& ctx) {
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().first_person.store(true);
    ctx.mode().eye_height.store(sneak::kStandingEyeHeight);

    runtime.resetForTesting();
}

WorldRuntimeCallbacks foregroundCallbacks() {
    WorldRuntimeCallbacks cb{};
    cb.is_game_in_foreground = []() { return true; };
    return cb;
}

} // namespace

// ---- Pure interpolation -----------------------------------------------------------------------------------------

TEST(EldenRingSneak, EyeHeightTargetsAreTheMinecraftValues) {
    EXPECT_FLOAT_EQ(sneak::kStandingEyeHeight, 1.65f);
    EXPECT_FLOAT_EQ(sneak::kSneakingEyeHeight, 1.27f);
}

TEST(EldenRingSneak, EyeHeightConvergesToTheSneakingTarget) {
    float eye = sneak::kStandingEyeHeight;
    for (int i = 0; i < 240; ++i) eye = sneak::stepSneakEyeHeight(eye, true, true, 1.f / 60.f);
    EXPECT_NEAR(eye, sneak::kSneakingEyeHeight, 1e-4f);
}

TEST(EldenRingSneak, EyeHeightConvergesBackToTheStandingTarget) {
    float eye = sneak::kSneakingEyeHeight;
    for (int i = 0; i < 240; ++i) eye = sneak::stepSneakEyeHeight(eye, false, true, 1.f / 60.f);
    EXPECT_NEAR(eye, sneak::kStandingEyeHeight, 1e-4f);
}

TEST(EldenRingSneak, EyeHeightIsANoOpWhenNotFirstPerson) {
    const float eye = 1.42f;
    EXPECT_FLOAT_EQ(sneak::stepSneakEyeHeight(eye, true, false, 0.05f), eye);
    EXPECT_FLOAT_EQ(sneak::stepSneakEyeHeight(eye, false, false, 0.05f), eye);
}

TEST(EldenRingSneak, EyeHeightMovesTowardsTheTargetWithoutOvershooting) {
    const float next = sneak::stepSneakEyeHeight(sneak::kStandingEyeHeight, true, true, 0.05f);
    EXPECT_LT(next, sneak::kStandingEyeHeight);
    EXPECT_GT(next, sneak::kSneakingEyeHeight);
}

TEST(EldenRingSneak, EyeHeightIsFrameRateIndependent) {
    const auto simulate = [](float dt, int steps) {
        float eye = sneak::kStandingEyeHeight;
        for (int i = 0; i < steps; ++i) eye = sneak::stepSneakEyeHeight(eye, true, true, dt);
        return eye;
    };
    // The same wall-clock time (0.1 s) at 30 Hz and 120 Hz must land in the same place.
    const float at30 = simulate(1.f / 30.f, 3);
    const float at120 = simulate(1.f / 120.f, 12);
    EXPECT_NEAR(at30, at120, 1e-4f);
}

// ---- Runtime step (general per-frame path) ----------------------------------------------------------------------

TEST(EldenRingSneak, RuntimeStepDescendsEyeHeightWithoutAnyBlocks) {
    auto& runtime = VoxelWorldRuntime::Instance();
    auto& ctx = RuntimeContext::Instance();
    resetSneakRuntime(runtime, ctx);

    runtime.setInputProvider([](int vk) { return vk == kVkShift; });
    runtime.setCallbacks(foregroundCallbacks());

    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);

    EXPECT_NEAR(ctx.mode().eye_height.load(), sneak::kSneakingEyeHeight, 1e-4f);
    EXPECT_TRUE(ctx.mode().sneak_active.load());
    EXPECT_EQ(ctx.world().block_grid.count(), 0u);
}

TEST(EldenRingSneak, RuntimeStepRisesBackWhenSneakIsReleased) {
    auto& runtime = VoxelWorldRuntime::Instance();
    auto& ctx = RuntimeContext::Instance();
    resetSneakRuntime(runtime, ctx);
    ctx.mode().eye_height.store(sneak::kSneakingEyeHeight);

    bool shift = true;
    runtime.setInputProvider([&shift](int vk) { return vk == kVkShift && shift; });
    runtime.setCallbacks(foregroundCallbacks());

    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);
    ASSERT_NEAR(ctx.mode().eye_height.load(), sneak::kSneakingEyeHeight, 1e-4f);

    shift = false;
    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);

    EXPECT_NEAR(ctx.mode().eye_height.load(), sneak::kStandingEyeHeight, 1e-4f);
    EXPECT_FALSE(ctx.mode().sneak_active.load());
}

TEST(EldenRingSneak, RuntimeStepIsANoOpWhenNotFirstPerson) {
    auto& runtime = VoxelWorldRuntime::Instance();
    auto& ctx = RuntimeContext::Instance();
    resetSneakRuntime(runtime, ctx);
    ctx.mode().first_person.store(false);

    runtime.setInputProvider([](int vk) { return vk == kVkShift; });
    runtime.setCallbacks(foregroundCallbacks());

    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);

    EXPECT_FLOAT_EQ(ctx.mode().eye_height.load(), sneak::kStandingEyeHeight);
    EXPECT_TRUE(ctx.mode().sneak_active.load()); // the sneak signal itself still updates
}

TEST(EldenRingSneak, RuntimeStepRunsWithoutPlayerOrBlockCallbacks) {
    auto& runtime = VoxelWorldRuntime::Instance();
    auto& ctx = RuntimeContext::Instance();
    resetSneakRuntime(runtime, ctx);

    runtime.setInputProvider([](int vk) { return vk == kVkShift; });
    // Deliberately no player/physics callbacks: stepPlayerBlockCollision would early-return here.

    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);

    EXPECT_NEAR(ctx.mode().eye_height.load(), sneak::kSneakingEyeHeight, 1e-4f);
}

TEST(EldenRingSneak, RuntimeStepAcceptsTheNativeCrouchKey) {
    auto& runtime = VoxelWorldRuntime::Instance();
    auto& ctx = RuntimeContext::Instance();
    resetSneakRuntime(runtime, ctx);

    runtime.setInputProvider([](int vk) { return vk == 'X'; });
    runtime.setCallbacks(foregroundCallbacks());

    for (int i = 0; i < 240; ++i) runtime.stepSneakEyeHeight(1.f / 60.f, ctx);

    EXPECT_NEAR(ctx.mode().eye_height.load(), sneak::kSneakingEyeHeight, 1e-4f);
}
