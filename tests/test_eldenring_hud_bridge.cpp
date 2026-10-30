#include <gtest/gtest.h>

#include "eldenring_hud_bridge.hpp"
#include "eldenring_model.hpp"
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

TEST(HudBridgeTest, PopulateSteveStateCarriesTheHurtSideForTheThirdPersonTilt) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.combat().feedback.onHurt(-1.0f);

    erov::SteveState steve{};
    PopulateSteveStateFromContext(steve, ctx, false /* dead */, false /* sleeping */);

    EXPECT_FLOAT_EQ(steve.hurt, 1.0f);
    EXPECT_FLOAT_EQ(steve.hurt_side, -1.0f);
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

TEST(HudBridgeTest, NativeModelAndShadowDispFlagsHidingAndRestoringWithMockModel) {
    std::unordered_map<uintptr_t, debug::HiddenWord> hidden_flags;
    std::mutex native_mutex;

    // Build mock memory structures:
    // player_chr + 0x648 -> asm_model
    // asm_model + 0x00 -> vtable (matching measured vtable 0x2B35980)
    // asm_model + 0x18 -> root_disp_ptr (CSModelDispEntity of character model base)
    // asm_model + 0x188 -> shadow_disp_ptr (CSModelDispEntity of character shadow/outline)
    // asm_model + 0x28 + slot*8 -> part
    // part + 0x18 -> disp_entity
    // disp_entity + 0x20 -> disp_flags1 (0x000100A1)
    // disp_entity + 0x24 -> disp_flags2 (0x00000001)
    // disp_entity + 0x220 -> shadow_flags (0x00000011)

    struct MockDispEntity {
        uint8_t pad_00[0x20];
        uint32_t disp_flags1; // +0x20
        uint32_t disp_flags2; // +0x24
        uint8_t pad_28[0x1F8]; // 0x28..0x220
        uint32_t shadow_flags; // +0x220
    };

    MockDispEntity disp_entity{};
    disp_entity.disp_flags1 = 0x000100A1; // Visible bit 0 and shadow bits
    disp_entity.disp_flags2 = 0x00000001; // Shadow bit in word 2
    disp_entity.shadow_flags = 0x00000011; // Bits 0 (mesh visible) and 4 (shadow visible)

    MockDispEntity root_disp{};
    root_disp.disp_flags1 = 0x000100A1;
    root_disp.disp_flags2 = 0x00000001;
    root_disp.shadow_flags = 0x00000011;

    MockDispEntity shadow_disp{};
    shadow_disp.disp_flags1 = 0x000100A1;
    shadow_disp.disp_flags2 = 0x00000001;
    shadow_disp.shadow_flags = 0x00000011;

    struct MockPart {
        uint8_t pad_00[0x18];
        uint64_t disp_ptr; // +0x18
    } part{};
    part.disp_ptr = reinterpret_cast<uintptr_t>(&disp_entity);

    struct MockAsmModel {
        uint64_t vtable; // +0x00
        uint8_t pad_08[0x10]; // 0x08..0x18
        uint64_t root_disp_ptr; // +0x18
        uint32_t asm_flags; // +0x20 (bit 2 = shadow cast)
        uint8_t pad_24[0x04]; // 0x24..0x28
        uint64_t parts[27]; // +0x28
        uint8_t pad_100[0x88]; // 0x100..0x188
        uint64_t shadow_disp_ptr; // +0x188
        uint8_t pad_190[0x160]; // 0x190..0x2F0
        uint8_t cast_shadow; // +0x2F0
    } asm_model{};
    constexpr uintptr_t kMockImageBase = 0x140000000;
    constexpr uintptr_t kMockDispVtable = kMockImageBase + 0x1000;
    constexpr uintptr_t kMockCol = kMockImageBase + 0x2000;
    constexpr uintptr_t kMockTdRva = 0x3000;
    asm_model.vtable = kMockImageBase + live::layout::kAsmModelVtableRva;
    asm_model.root_disp_ptr = reinterpret_cast<uintptr_t>(&root_disp);
    asm_model.shadow_disp_ptr = reinterpret_cast<uintptr_t>(&shadow_disp);
    asm_model.parts[0] = reinterpret_cast<uintptr_t>(&part);
    asm_model.asm_flags = 0x00000007; // bits 0, 1, 2 set
    asm_model.cast_shadow = 1;

    // Mock x64 RTTI for CSModelDispEntity: vtable[-1] -> COL, COL+0x0C ->
    // TypeDescriptor RVA, TypeDescriptor+0x10 -> mangled class name.
    std::vector<uint8_t> mock_image(0x4000, 0);
    const uint64_t col_ptr = kMockCol;
    std::memcpy(mock_image.data() + 0x1000 - 8, &col_ptr, sizeof(col_ptr));
    const uint32_t col_sig = 1;
    std::memcpy(mock_image.data() + 0x2000, &col_sig, sizeof(col_sig));
    std::memcpy(mock_image.data() + 0x2000 + 0x0C, &kMockTdRva, sizeof(kMockTdRva));
    const char disp_class[] = ".?AVCSModelDispEntity@CS@@";
    std::memcpy(mock_image.data() + kMockTdRva + 0x10, disp_class, sizeof(disp_class));
    *reinterpret_cast<uint64_t*>(disp_entity.pad_00) = kMockDispVtable;
    *reinterpret_cast<uint64_t*>(root_disp.pad_00) = kMockDispVtable;
    *reinterpret_cast<uint64_t*>(shadow_disp.pad_00) = kMockDispVtable;

    struct MockPlayer {
        uint8_t pad_00[0x648];
        uint64_t asm_model_ptr; // +0x648
    } player{};
    player.asm_model_ptr = reinterpret_cast<uintptr_t>(&asm_model);

    class MockMemReader : public live::IMemoryReader {
    public:
        MockMemReader(const std::vector<uint8_t>& image, uintptr_t base) : image_(image), base_(base) {}
        bool read(uintptr_t address, void* dst, size_t size) const override {
            if (address >= base_ && address + size <= base_ + image_.size()) {
                std::memcpy(dst, image_.data() + (address - base_), size);
                return true;
            }
            std::memcpy(dst, reinterpret_cast<const void*>(address), size);
            return true;
        }
    private:
        const std::vector<uint8_t>& image_;
        uintptr_t base_;
    } mock_reader(mock_image, kMockImageBase);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.ui().hide_slots.store(0xFFFFFFFFu); // All slots
    ctx.ui().hide_mask1.store(0x100A1);     // Model + shadow mask 1
    ctx.ui().hide_mask2.store(0x1);         // Shadow mask 2

    const uintptr_t player_addr = reinterpret_cast<uintptr_t>(&player);

    const auto collected = live::collectAsmModelDispFlagAddresses(mock_reader, kMockImageBase, player_addr);
    ASSERT_EQ(collected.size(), 3u);

    // 1. Hide native model and shadow
    UpdateNativeModel(player_addr, true /* hide */, mock_reader, kMockImageBase, ctx, hidden_flags, native_mutex);

    // Assert deep semantic verification: part disp, root model disp, and shadow disp MUST be masked!
    EXPECT_EQ(disp_entity.disp_flags1 & 0x100A1, 0u);
    EXPECT_EQ(disp_entity.disp_flags2 & 0x1, 0u);
    EXPECT_EQ(root_disp.disp_flags1 & 0x100A1, 0u);
    EXPECT_EQ(root_disp.disp_flags2 & 0x1, 0u);
    EXPECT_EQ(shadow_disp.disp_flags1 & 0x100A1, 0u);
    EXPECT_EQ(shadow_disp.disp_flags2 & 0x1, 0u);

    // Deep semantic assertion for shadow fields:
    // +0x220 shadow flags must have bit 0 (mesh) and bit 4 (shadow) cleared!
    EXPECT_EQ(disp_entity.shadow_flags & 0x11, 0u);
    EXPECT_EQ(root_disp.shadow_flags & 0x11, 0u);
    EXPECT_EQ(shadow_disp.shadow_flags & 0x11, 0u);

    // asm_model flags +0x20 must have bit 2 (shadow) cleared!
    EXPECT_EQ(asm_model.asm_flags & 0x4, 0u);
    // asm_model +0x2F0 cast_shadow byte must be 0!
    EXPECT_EQ(asm_model.cast_shadow, 0u);

    // 2. Restore native model and shadow
    RestoreNativeModel(hidden_flags, native_mutex);
    EXPECT_TRUE(hidden_flags.empty());
    EXPECT_EQ(disp_entity.disp_flags1, 0x000100A1);
    EXPECT_EQ(disp_entity.disp_flags2, 0x00000001);
    EXPECT_EQ(root_disp.disp_flags1, 0x000100A1);
    EXPECT_EQ(root_disp.disp_flags2, 0x00000001);
    EXPECT_EQ(shadow_disp.disp_flags1, 0x000100A1);
    EXPECT_EQ(shadow_disp.disp_flags2, 0x00000001);

    EXPECT_EQ(disp_entity.shadow_flags, 0x00000011);
    EXPECT_EQ(root_disp.shadow_flags, 0x00000011);
    EXPECT_EQ(shadow_disp.shadow_flags, 0x00000011);
    EXPECT_EQ(asm_model.asm_flags, 0x00000007);
    EXPECT_EQ(asm_model.cast_shadow, 1u);
}

TEST(HudBridgeTest, PopulateSteveStateSynchronizesShieldBlocking) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().steve_enabled.store(true);
    ctx.player().sprint_active.store(true);

    // Case 1: Offhand shield blocking via mode().shield_blocking
    ctx.mode().shield_blocking.store(true);
    ctx.mode().shield_offhand.store(true);

    erov::SteveState steve{};
    PopulateSteveStateFromContext(steve, ctx, false /* dead */, false /* sleeping */);

    EXPECT_TRUE(steve.blocking);
    EXPECT_TRUE(steve.block_offhand);
    EXPECT_FALSE(steve.sprinting);

    // Case 2: Main hand shield blocking
    ctx.mode().shield_offhand.store(false);
    erov::SteveState steve_main{};
    PopulateSteveStateFromContext(steve_main, ctx, false /* dead */, false /* sleeping */);

    EXPECT_TRUE(steve_main.blocking);
    EXPECT_FALSE(steve_main.block_offhand);

    // Case 3: Dead player clears blocking
    erov::SteveState steve_dead{};
    PopulateSteveStateFromContext(steve_dead, ctx, true /* dead */, false /* sleeping */);
    EXPECT_FALSE(steve_dead.blocking);
}

TEST(HudBridgeTest, PopulateHudStateHungerShakingOnlyAtCriticalFood) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);

    // Initial state: food 20, sat 5.0 -> no shaking
    erov::HudState hud{};
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 20);
    EXPECT_FALSE(hud.food_shaking);

    // Deplete saturation (5.0 * 4 = 20 exhaustion): food remains 20, saturation 0
    ctx.player().hunger.addExhaustion(20.0f);
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 20);
    // MUST NOT shake at full food even if saturation is 0!
    EXPECT_FALSE(hud.food_shaking);

    // Deplete food down to 10 points (5 drumsticks)
    ctx.player().hunger.addExhaustion(40.0f); // 10 food drained
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 10);
    EXPECT_FALSE(hud.food_shaking);

    // Deplete food down to 2 points (1 drumstick)
    ctx.player().hunger.addExhaustion(32.0f); // 8 food drained -> food 2
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 2);
    EXPECT_FALSE(hud.food_shaking);

    // Deplete food down to 1 point (half a drumstick / 半格以下)
    ctx.player().hunger.addExhaustion(4.0f); // 1 food drained -> food 1
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 1);
    EXPECT_TRUE(hud.food_shaking);

    // Deplete food down to 0 points (starving)
    ctx.player().hunger.addExhaustion(4.0f); // 1 food drained -> food 0
    PopulateHudStateFromContext(hud, ctx);
    EXPECT_EQ(hud.food, 0);
    EXPECT_TRUE(hud.food_shaking);
}

#if ENABLE_CS2_VIEWMODEL
TEST(HudBridgeTest, PopulateHudStateSynchronizesCsMode) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().cs_mode.store(true);

    erov::HudState hud{};
    PopulateHudStateFromContext(hud, ctx);

    EXPECT_TRUE(hud.cs_mode);
}
#else
TEST(HudBridgeTest, PopulateHudStatePureMcDisablesCsMode) {
    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().cs_mode.store(true);

    erov::HudState hud{};
    PopulateHudStateFromContext(hud, ctx);

    EXPECT_FALSE(hud.cs_mode);
}
#endif

