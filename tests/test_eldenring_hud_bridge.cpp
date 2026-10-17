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

    ctx.combat().feedback.onHurt(1.0f);

    erov::SteveState steve{};
    PopulateSteveStateFromContext(steve, ctx, false /* dead */, false /* sleeping */);

    EXPECT_TRUE(steve.sprinting);
    EXPECT_TRUE(steve.first_person);
    EXPECT_FALSE(steve.dead);
    EXPECT_FALSE(steve.sleeping);
    EXPECT_GT(steve.hurt, 0.0f);
}

TEST(HudBridgeTest, PopulateSteveStateSynchronizesBowState) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().steve_enabled.store(true);
    ctx.mode().bow_power.store(0.75f);
    ctx.mode().bow_ticks.store(15.0f);
    ctx.mode().bow_offhand.store(true);
    ctx.combat().shield.update(0.f, false, false);

    erov::SteveState steve{};
    PopulateSteveStateFromContext(steve, ctx, false /* dead */, false /* sleeping */);

    EXPECT_FLOAT_EQ(steve.bow_charge, 0.75f);
    EXPECT_FLOAT_EQ(steve.bow_ticks, 15.0f);
    EXPECT_TRUE(steve.bow_offhand);

    // If dead, bow charge and ticks must zero out
    erov::SteveState dead_steve{};
    PopulateSteveStateFromContext(dead_steve, ctx, true /* dead */, false /* sleeping */);
    EXPECT_FLOAT_EQ(dead_steve.bow_charge, 0.0f);
    EXPECT_FLOAT_EQ(dead_steve.bow_ticks, 0.0f);
}

TEST(HudBridgeTest, PopulateHudStateHotbarAndInventory) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);

    // Setup inventory with items
    ctx.combat().melee.inventory().setSlot(0, {mc::ItemId::DiamondSword, 1});
    ctx.combat().melee.inventory().setSlot(1, {mc::ItemId::Bow, 1});
    ctx.combat().melee.inventory().setSlot(2, {mc::ItemId::Arrow, 64});
    ctx.combat().melee.inventory().setOffhand({mc::ItemId::Shield, 1});
    ctx.combat().melee.inventory().setArmor(0, {mc::ItemId::DiamondHelmet, 1}); // 3 armor pts

    erov::HudState hud{};
    PopulateHudStateFromContext(hud, ctx);

    EXPECT_EQ(hud.hotbar[0], static_cast<uint16_t>(mc::ItemId::DiamondSword));
    EXPECT_EQ(hud.hotbar_count[0], 1);
    EXPECT_EQ(hud.hotbar[1], static_cast<uint16_t>(mc::ItemId::Bow));
    EXPECT_EQ(hud.hotbar_count[1], 1);
    EXPECT_EQ(hud.offhand_item, static_cast<uint16_t>(mc::ItemId::Shield));
    EXPECT_EQ(hud.offhand_count, 1);
    EXPECT_EQ(hud.food, 20); // Default full hunger
    EXPECT_GT(hud.armor_points, 0);
}

TEST(HudBridgeTest, NativeModelSlotHidingAndRestoration) {
    std::unordered_map<uintptr_t, debug::HiddenWord> hidden_flags;
    std::mutex native_mutex;

    // Simulate memory with a display flag word that has drawn bit set
    std::vector<uint8_t> memory_pool(0x1000, 0);
    const uintptr_t flag_addr = reinterpret_cast<uintptr_t>(memory_pool.data()) + 0x200;
    *reinterpret_cast<uint32_t*>(flag_addr) = 0x000100A1; // Drawn bit set

    // Mock reader
    class MockMemReader : public live::IMemoryReader {
    public:
        bool read(uintptr_t address, void* dst, size_t size) const override {
            std::memcpy(dst, reinterpret_cast<const void*>(address), size);
            return true;
        }
    } mock_reader;

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.ui().hide_slots.store(0xFFFFFFFFu); // All slots
    ctx.ui().hide_mask1.store(1u);          // Mask bit 0

    // Hide native model
    UpdateNativeModel(flag_addr, true /* hide */, mock_reader, 0x140000000, ctx, hidden_flags, native_mutex);
    
    // Now restore
    RestoreNativeModel(hidden_flags, native_mutex);
    EXPECT_TRUE(hidden_flags.empty());
}
