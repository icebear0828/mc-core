#include <gtest/gtest.h>

#include <filesystem>

#include "eldenring_overlay_frame_state.hpp"

using namespace erov::detail;

TEST(OverlayState, HookStateMachineStartsIdle) {
    HookStateMachine sm;
    EXPECT_EQ(sm.phase, HookPhase::Idle);
    EXPECT_EQ(sm.init_attempts, 0);
    EXPECT_TRUE(sm.can_init());
    EXPECT_FALSE(sm.failed());
}

TEST(OverlayState, QueueLearningIsIdempotent) {
    HookStateMachine sm;
    EXPECT_TRUE(sm.on_queue_learned());
    EXPECT_EQ(sm.phase, HookPhase::QueueKnown);
    EXPECT_FALSE(sm.on_queue_learned());
    EXPECT_EQ(sm.phase, HookPhase::QueueKnown);
}

TEST(OverlayState, InitFailureGivesUpAfterTwentyAttempts) {
    HookStateMachine sm;
    for (int i = 0; i < 19; ++i) sm.on_init_failed();
    EXPECT_EQ(sm.init_attempts, 19);
    EXPECT_FALSE(sm.failed());
    EXPECT_TRUE(sm.can_init());

    sm.on_init_failed();
    EXPECT_EQ(sm.init_attempts, 20);
    EXPECT_TRUE(sm.failed());
    EXPECT_EQ(sm.phase, HookPhase::Failed);
    EXPECT_FALSE(sm.can_init());
}

TEST(OverlayState, ResizeReturnsToIdleAndRebuilds) {
    HookStateMachine sm;
    sm.on_init_ok();
    EXPECT_EQ(sm.phase, HookPhase::Ready);

    sm.on_resize();
    EXPECT_EQ(sm.phase, HookPhase::Resizing);

    sm.on_frame_rendered();
    EXPECT_EQ(sm.phase, HookPhase::Ready);
}

TEST(OverlayState, FailedStaysFailed) {
    HookStateMachine sm;
    for (int i = 0; i < 20; ++i) sm.on_init_failed();
    ASSERT_TRUE(sm.failed());

    sm.on_init_ok();
    sm.on_resize();
    sm.on_queue_learned();
    sm.on_frame_rendered();
    EXPECT_TRUE(sm.failed());
    EXPECT_EQ(sm.phase, HookPhase::Failed);
}

TEST(OverlayState, ExceptionDisablesImmediately) {
    HookStateMachine sm;
    sm.on_init_ok();
    ASSERT_EQ(sm.phase, HookPhase::Ready);

    sm.on_exception();
    EXPECT_TRUE(sm.failed());
    EXPECT_EQ(sm.phase, HookPhase::Failed);
}

TEST(OverlayState, BarrierPlanWithoutScreenshot) {
    const FrameBarrierPlan plan = plan_frame_barriers(false);
    EXPECT_TRUE(plan.transition_to_rt);
    EXPECT_FALSE(plan.copy_screenshot);
    EXPECT_TRUE(plan.transition_to_present);
}

TEST(OverlayState, BarrierPlanWithScreenshot) {
    const FrameBarrierPlan plan = plan_frame_barriers(true);
    EXPECT_TRUE(plan.transition_to_rt);
    EXPECT_TRUE(plan.copy_screenshot);
    EXPECT_TRUE(plan.transition_to_present);
}

TEST(OverlayState, SrvHandleUsesIncrementTimesSlot) {
    // The overlay heap is 64 descriptors; slot 44 is the CS2 decal, slot 3 the HUD atlas.
    EXPECT_EQ(srv_handle(0x1000, 32, 44), 0x1000u + 44u * 32u);
    EXPECT_EQ(srv_handle(0, 32, 3), 96u);
    EXPECT_EQ(srv_handle(0x100, 1, 7), 0x107u);
}

TEST(OverlayState, ScreenshotPathStaysRelativeNextToExe) {
    namespace fs = std::filesystem;

    const std::string joined = resolve_screenshot_path("shot.png", "C:/Game");
    EXPECT_EQ(fs::path(joined).filename().string(), "shot.png");
    EXPECT_EQ(fs::path(joined).parent_path(), fs::path("C:/Game"));

    // An absolute path is left alone.
    const fs::path absolute = fs::absolute("abs.png");
    EXPECT_EQ(resolve_screenshot_path(absolute.string(), "C:/Game"), absolute.string());

    // Empty inputs are returned unchanged.
    EXPECT_EQ(resolve_screenshot_path("", "C:/Game"), "");
    EXPECT_EQ(resolve_screenshot_path("shot.png", ""), "shot.png");
}
