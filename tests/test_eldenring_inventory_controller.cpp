#include <gtest/gtest.h>

#include "eldenring_inventory_controller.hpp"
#include "eldenring_runtime_context.hpp"
#include "mc/inventory.hpp"

using namespace eldenring;
using namespace eldenring::ui;

class InventoryControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& ctx = RuntimeContext::Instance();
        ctx.resetForTesting();
        ctx.mode().mc_mode.store(true);
        ctx.mode().input_enabled.store(true);
        controller_.resetForTesting();
        inv_ = mc::Inventory{};

        InventoryCallbacks cb;
        cb.log = [](const char*) {};
        controller_.setCallbacks(cb);
    }

    InventoryController& controller_{InventoryController::Instance()};
    mc::Inventory inv_;
};

TEST_F(InventoryControllerTest, OpenAndCloseViaToggleKey) {
    auto& ctx = RuntimeContext::Instance();
    EXPECT_FALSE(controller_.isOpen(ctx));

    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.player_dead = false;
    input.screen_width = 1920.f;
    input.screen_height = 1080.f;

    // Press toggle key (edge)
    input.key_down = true;
    controller_.tickWithInput(input, inv_, ctx);

    EXPECT_TRUE(controller_.isOpen(ctx));
    EXPECT_FLOAT_EQ(ctx.ui().inv_mx.load(), 960.f);
    EXPECT_FLOAT_EQ(ctx.ui().inv_my.load(), 540.f);

    // Release key
    input.key_down = false;
    controller_.tickWithInput(input, inv_, ctx);
    EXPECT_TRUE(controller_.isOpen(ctx));

    // Press toggle key again (edge) -> should close
    input.key_down = true;
    controller_.tickWithInput(input, inv_, ctx);

    EXPECT_FALSE(controller_.isOpen(ctx));
}

TEST_F(InventoryControllerTest, CloseOnEscAndReturnCursor) {
    auto& ctx = RuntimeContext::Instance();

    // Open inventory first
    controller_.open(1920.f, 1080.f, ctx);
    EXPECT_TRUE(controller_.isOpen(ctx));

    // Put item on cursor
    inv_.setSlot(0, mc::ItemStack{mc::ItemId::BlockDirt, 32});
    inv_.click(0, mc::Button::Left, false); // cursor now holds dirt
    EXPECT_EQ(inv_.cursor().item, mc::ItemId::BlockDirt);
    EXPECT_EQ(inv_.cursor().count, 32u);

    // Press Esc
    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.esc_down = true;
    controller_.tickWithInput(input, inv_, ctx);

    // Inventory should be closed and cursor returned
    EXPECT_FALSE(controller_.isOpen(ctx));
    EXPECT_EQ(inv_.cursor().item, mc::ItemId::None);
    EXPECT_EQ(inv_.slot(0).item, mc::ItemId::BlockDirt);
    EXPECT_EQ(inv_.slot(0).count, 32u);
}

TEST_F(InventoryControllerTest, MouseMotionClampingAndSens) {
    auto& ctx = RuntimeContext::Instance();
    ctx.ui().inv_sens.store(2.0f);
    controller_.open(1920.f, 1080.f, ctx); // mx=960, my=540

    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.screen_width = 1920.f;
    input.screen_height = 1080.f;
    input.mouse_dx = 50;
    input.mouse_dy = -30;

    controller_.tickWithInput(input, inv_, ctx);

    // 960 + 50*2 = 1060; 540 + (-30)*2 = 480
    EXPECT_FLOAT_EQ(ctx.ui().inv_mx.load(), 1060.f);
    EXPECT_FLOAT_EQ(ctx.ui().inv_my.load(), 480.f);

    // Extreme delta to test clamping
    input.mouse_dx = 10000;
    input.mouse_dy = -10000;
    controller_.tickWithInput(input, inv_, ctx);

    EXPECT_FLOAT_EQ(ctx.ui().inv_mx.load(), 1919.f); // clamped to w - 1
    EXPECT_FLOAT_EQ(ctx.ui().inv_my.load(), 0.f);    // clamped to 0
}

TEST_F(InventoryControllerTest, OffhandSwapWithFKey) {
    auto& ctx = RuntimeContext::Instance();
    controller_.open(1920.f, 1080.f, ctx);

    inv_.setSlot(0, mc::ItemStack{mc::ItemId::DiamondSword, 1});
    inv_.setOffhand(mc::ItemStack{mc::ItemId::Shield, 1});

    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.f_down = true;

    controller_.tickWithInput(input, inv_, ctx);

    // Slot 0 and offhand should be swapped
    EXPECT_EQ(inv_.slot(0).item, mc::ItemId::Shield);
    EXPECT_EQ(inv_.offhand().item, mc::ItemId::DiamondSword);
}

TEST_F(InventoryControllerTest, InputDetoursInterceptWhenOpen) {
    auto& ctx = RuntimeContext::Instance();
    InventoryController::setOriginalHooks([]() -> uint64_t { return 42; },
                                          [](void*) -> uint8_t { return 99; });

    // When closed:
    ctx.ui().inv_open.store(false);
    EXPECT_EQ(InventoryController::IsInputBlockedDetour(), 42u);
    EXPECT_EQ(InventoryController::MenuFreezeDetour(nullptr), 99);
    EXPECT_EQ(ctx.ui().isblocked_forced.load(), 0u);
    EXPECT_EQ(ctx.ui().menufreeze_forced.load(), 0u);

    // When open:
    ctx.ui().inv_open.store(true);
    EXPECT_EQ(InventoryController::IsInputBlockedDetour(), 1u);
    EXPECT_EQ(ctx.ui().isblocked_forced.load(), 1u);

    EXPECT_EQ(InventoryController::MenuFreezeDetour(nullptr), 1);
    EXPECT_EQ(ctx.ui().menufreeze_forced.load(), 1u);
}

TEST_F(InventoryControllerTest, SlotClickDispatching) {
    auto& ctx = RuntimeContext::Instance();
    controller_.open(1920.f, 1080.f, ctx);

    // Setup item in inventory slot 9 (first backpack slot)
    inv_.setSlot(9, mc::ItemStack{mc::ItemId::GoldenApple, 10});

    // Determine layout slot position
    const mc::InventoryLayout lay(1920.f, 1080.f, false);
    const mc::HudRect rect = lay.invSlot(9);
    const float slot_center_x = rect.x + rect.w * 0.5f;
    const float slot_center_y = rect.y + rect.h * 0.5f;

    // Position mouse cursor over slot 9
    ctx.ui().inv_mx.store(slot_center_x);
    ctx.ui().inv_my.store(slot_center_y);

    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.screen_width = 1920.f;
    input.screen_height = 1080.f;
    input.left_click = true; // Left click picks up full stack

    controller_.tickWithInput(input, inv_, ctx);

    // Cursor should now hold the Golden Apples and slot 9 should be empty
    EXPECT_EQ(inv_.cursor().item, mc::ItemId::GoldenApple);
    EXPECT_EQ(inv_.cursor().count, 10u);
    EXPECT_EQ(inv_.slot(9).item, mc::ItemId::None);

    // Click outside should drop the items
    ctx.ui().inv_mx.store(10.f);
    ctx.ui().inv_my.store(10.f);
    input.left_click = true;
    controller_.tickWithInput(input, inv_, ctx);

    EXPECT_EQ(inv_.cursor().item, mc::ItemId::None);
}

TEST_F(InventoryControllerTest, HotbarDigitSwap) {
    auto& ctx = RuntimeContext::Instance();
    controller_.open(1920.f, 1080.f, ctx);

    // Slot 0 (hotbar 0) has DiamondSword
    // Slot 12 (backpack) has BlockDirt
    inv_.setSlot(0, mc::ItemStack{mc::ItemId::DiamondSword, 1});
    inv_.setSlot(12, mc::ItemStack{mc::ItemId::BlockDirt, 64});

    const mc::InventoryLayout lay(1920.f, 1080.f, false);
    const mc::HudRect rect = lay.invSlot(12);
    ctx.ui().inv_mx.store(rect.x + rect.w * 0.5f);
    ctx.ui().inv_my.store(rect.y + rect.h * 0.5f);

    InventoryInputState input{};
    input.fg = true;
    input.want_suppress = true;
    input.screen_width = 1920.f;
    input.screen_height = 1080.f;
    // Press '1' key (digit_down[0]) while hovering over slot 12
    input.digit_down[0] = true;

    controller_.tickWithInput(input, inv_, ctx);

    // Hotbar slot 0 and slot 12 should be swapped!
    EXPECT_EQ(inv_.slot(0).item, mc::ItemId::BlockDirt);
    EXPECT_EQ(inv_.slot(0).count, 64u);
    EXPECT_EQ(inv_.slot(12).item, mc::ItemId::DiamondSword);
    EXPECT_EQ(inv_.slot(12).count, 1u);
}

TEST_F(InventoryControllerTest, IsToggleKeyMatchesBothEAndIAndCustomConfiguredKey) {
    // Standard Minecraft 'E' key (0x45 / 69) should match even if configured is 73 ('I')
    EXPECT_TRUE(InventoryController::isToggleKey('E', 73));
    EXPECT_TRUE(InventoryController::isToggleKey('e', 73));
    EXPECT_TRUE(InventoryController::isToggleKey(0x45, 73));

    // 'I' key (0x49 / 73) should match even if configured is 0x45 ('E')
    EXPECT_TRUE(InventoryController::isToggleKey('I', 0x45));
    EXPECT_TRUE(InventoryController::isToggleKey('i', 0x45));
    EXPECT_TRUE(InventoryController::isToggleKey(73, 0x45));

    // Custom configured key should match
    EXPECT_TRUE(InventoryController::isToggleKey('B', 'B'));
    EXPECT_TRUE(InventoryController::isToggleKey('X', 'X'));

    // Non-toggle keys should return false
    EXPECT_FALSE(InventoryController::isToggleKey(0x1B, 73)); // Esc
    EXPECT_FALSE(InventoryController::isToggleKey(0x20, 73)); // Space
    EXPECT_FALSE(InventoryController::isToggleKey('W', 73));
    EXPECT_FALSE(InventoryController::isToggleKey(0, 73));
}

