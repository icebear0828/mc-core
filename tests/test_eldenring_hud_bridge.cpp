#include <gtest/gtest.h>

#include "eldenring_hud_bridge.hpp"
#include "eldenring_runtime_context.hpp"

using namespace eldenring;
using namespace eldenring::ui;

TEST(HudBridgeTest, PopulateHudStateFromContext) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.ui().inv_open.store(true);
    ctx.ui().inv_mx.store(640.f);
    ctx.ui().inv_my.store(360.f);
    ctx.player().survival.apply({mc::ActiveEffect{mc::EffectType::Absorption, 2, 120.f}});

    erov::HudState hud{};
    PopulateHudStateFromContext(hud, ctx);

    EXPECT_TRUE(hud.mc_mode);
    EXPECT_TRUE(hud.inv_open);
    EXPECT_FLOAT_EQ(hud.mouse_x, 640.f);
    EXPECT_FLOAT_EQ(hud.mouse_y, 360.f);
    EXPECT_FLOAT_EQ(hud.absorption_mc, 8.0f);
}

TEST(HudBridgeTest, PopulateSteveStateFromContext) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().steve_enabled.store(true);
    ctx.mode().first_person.store(true);
    ctx.player().sprint_active.store(true);

    erov::SteveState steve{};
    PopulateSteveStateFromContext(steve, ctx, false /* dead */, false /* sleeping */);

    EXPECT_TRUE(steve.sprinting);
    EXPECT_TRUE(steve.first_person);
    EXPECT_FALSE(steve.dead);
    EXPECT_FALSE(steve.sleeping);
}
