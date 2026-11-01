#include <gtest/gtest.h>

#include "eldenring_hotkey_dispatcher.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_flight.hpp"
#include "eldenring_ow_dash.hpp"
#include "eldenring_roll.hpp"
#include <map>
#include <vector>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef VK_SPACE
#define VK_SPACE 0x20
#endif
#ifndef VK_F1
#define VK_F1 0x70
#endif
#ifndef VK_F2
#define VK_F2 0x71
#endif
#ifndef VK_F3
#define VK_F3 0x72
#endif
#ifndef VK_F4
#define VK_F4 0x73
#endif
#ifndef VK_F5
#define VK_F5 0x74
#endif
#ifndef VK_F6
#define VK_F6 0x75
#endif
#ifndef VK_F7
#define VK_F7 0x76
#endif
#ifndef VK_F8
#define VK_F8 0x77
#endif
#ifndef VK_F9
#define VK_F9 0x78
#endif
#ifndef VK_F10
#define VK_F10 0x79
#endif
#ifndef VK_F11
#define VK_F11 0x7A
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif
#ifndef VK_SHIFT
#define VK_SHIFT 0x10
#endif

using namespace eldenring;
using namespace eldenring::input;

namespace {

class HotkeyDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& ctx = RuntimeContext::Instance();
        ctx.resetForTesting();
        ctx.mode().mc_mode.store(true);
        ctx.mode().input_enabled.store(true);
        ctx.mode().click_attack.store(true);
        ctx.mode().damage_enabled.store(true);
        ctx.mode().bullet_fire_enabled.store(true);

        dispatcher_ = &HotkeyDispatcher::Instance();
        dispatcher_->resetKeyStates();

        cb_ = HotkeyCallbacks{};
        cb_.is_game_in_foreground = [this]() { return foreground_; };
        cb_.want_suppress = [this]() { return want_suppress_; };
        cb_.is_player_dead = [this]() { return player_dead_; };
        cb_.is_player_airborne = [this]() { return player_airborne_; };
        cb_.get_player_chr = []() { return 0x1000u; };
        cb_.read_physics_pos = [](uintptr_t, float out_pos[3]) {
            out_pos[0] = 10.f; out_pos[1] = 20.f; out_pos[2] = 30.f;
            return true;
        };
        cb_.play_sound = [this](const char* snd, float) { played_sounds_.push_back(snd); };
        cb_.log = [this](const std::string& msg) { logged_messages_.push_back(msg); };
        cb_.clip_duration = [](const char* name) -> float {
            const std::string n = name != nullptr ? name : "";
            if (n == "idle") return 2.0f;
            if (n == "inspect") return 4.1f;
            if (n == "inspect2") return 4.5f;
            if (n == "inspect3") return 4.867f;
            if (n == "deploy" || n == "deploy2" || n == "draw") return 1.0f;
            if (n == "fire") return 0.767f;
            if (n == "reload") return 2.433f;
            if (n.rfind("light_", 0) == 0 || n.rfind("heavy_", 0) == 0 ||
                n.rfind("backstab", 0) == 0) return 1.167f;
            return 0.f;
        };

        cb_.take_left_click = [this]() {
            bool r = left_click_edge_;
            left_click_edge_ = false;
            return r;
        };
        cb_.take_right_click = [this]() {
            bool r = right_click_edge_;
            right_click_edge_ = false;
            return r;
        };
        cb_.left_held = [this]() { return left_held_; };
        cb_.right_held = [this]() { return right_held_; };
        cb_.take_wheel_notches = [this]() {
            int n = wheel_notches_;
            wheel_notches_ = 0;
            return n;
        };
        cb_.set_suppress_mouse_buttons = [this](bool s) { suppressed_mouse_ = s; };
        cb_.set_suppress_mouse_wheel = [this](bool s) { suppressed_wheel_ = s; };
        cb_.set_suppress_mouse_motion = [this](bool s) { suppressed_motion_ = s; };
        cb_.set_suppress_keyboard = [this](bool s) { suppressed_keyboard_ = s; };
        cb_.set_masked_keys = [this](const int* keys, size_t n) { masked_keys_.assign(keys, keys + n); };
        cb_.take_keyboard_failures = [](long&) { return 0; };

        cb_.is_key_down = [this](int vk) {
            auto it = key_states_.find(vk);
            return (it != key_states_.end()) ? it->second : false;
        };

        cb_.step_mining = [this](float, bool) { return mining_consumed_; };
        cb_.try_ignite_tnt = [this]() {
            if (ignite_tnt_success_) { tnt_ignited_ = true; return true; }
            return false;
        };
        cb_.try_ignite_creeper = [this]() {
            if (ignite_creeper_success_) { creeper_ignited_ = true; return true; }
            return false;
        };
        cb_.try_place_block = [this]() {
            if (place_block_success_) { block_placed_ = true; return true; }
            return false;
        };
        cb_.try_use_spawn_egg = [this]() {
            if (use_spawn_egg_success_) { spawn_egg_used_ = true; return true; }
            return false;
        };
        cb_.click_attack = [this](float charged) { attack_charged_ = charged; attack_called_ = true; return attack_hit_; };
        cb_.enqueue_nearest_hostile = [this]() { hostile_enqueued_ = true; };
        cb_.queue_bullet_fire = [this](float dmg) { bullet_queued_ = true; bullet_damage_ = dmg; return true; };
        cb_.queue_cs_shot = [this](float damage, float spread, float range, uint32_t tracer, uint8_t slot) {
            cs_shot_queued_ = true;
            cs_shot_damage_ = damage;
            cs_shot_spread_ = spread;
            cs_shot_range_ = range;
            cs_shot_tracer_ = tracer;
            cs_shot_slot_ = slot;
            return true;
        };
        cb_.ow_strike = [this](int kind) {
            ow_strikes_.push_back(kind);
            return true;
        };
        cb_.queue_summon = [this](const char*) { summon_queued_ = true; return true; };
        cb_.cycle_hidden_slot = [this](int& c) { ++c; };
        cb_.reset_slot_probe = [this](int& c) { c = -1; };
        cb_.snapshot_native_parts = [this]() { snapshot_taken_ = true; };
        cb_.request_frame_trace = [this](int f) { trace_frames_ = f; };
        cb_.request_screenshot = [this](const char* p) { screenshot_path_ = p; };
        cb_.cycle_depth_candidate = [this]() { depth_cycled_ = true; };
        cb_.inventory_tick = [this](bool) { ++inventory_ticks_; };
        cb_.blocks_tick = [this]() { ++blocks_ticks_; };
        cb_.mc_flags_step = [this]() { ++flags_steps_; };
        cb_.check_summon_watch = [](uint64_t) {};

        dispatcher_->setCallbacks(cb_);
    }

    void TearDown() override {
        dispatcher_->setCallbacks(HotkeyCallbacks{});
    }

    void setKey(int vk, bool down) { key_states_[vk] = down; }

    HotkeyDispatcher* dispatcher_{nullptr};
    HotkeyCallbacks cb_;
    std::map<int, bool> key_states_;
    std::vector<std::string> logged_messages_;
    std::vector<std::string> played_sounds_;

    bool foreground_{true};
    bool want_suppress_{true};
    bool player_dead_{false};
    bool player_airborne_{false};
    bool left_click_edge_{false};
    bool right_click_edge_{false};
    bool left_held_{false};
    bool right_held_{false};
    int wheel_notches_{0};
    bool suppressed_mouse_{false};
    bool suppressed_wheel_{false};
    bool suppressed_motion_{false};
    bool suppressed_keyboard_{false};
    std::vector<int> masked_keys_;

    bool mining_consumed_{false};
    bool ignite_tnt_success_{false};
    bool tnt_ignited_{false};
    bool ignite_creeper_success_{false};
    bool creeper_ignited_{false};
    bool place_block_success_{false};
    bool block_placed_{false};
    bool use_spawn_egg_success_{false};
    bool spawn_egg_used_{false};
    bool attack_called_{false};
    bool attack_hit_{true};
    float attack_charged_{0.f};
    bool hostile_enqueued_{false};
    bool bullet_queued_{false};
    float bullet_damage_{0.f};
    bool cs_shot_queued_{false};
    float cs_shot_damage_{0.f};
    float cs_shot_spread_{0.f};
    float cs_shot_range_{0.f};
    uint32_t cs_shot_tracer_{0u};
    uint8_t cs_shot_slot_{0};
    std::vector<int> ow_strikes_;
    bool summon_queued_{false};
    bool snapshot_taken_{false};
    int trace_frames_{0};
    std::string screenshot_path_;
    bool depth_cycled_{false};
    int inventory_ticks_{0};
    int blocks_ticks_{0};
    int flags_steps_{0};
};

} // namespace

TEST_F(HotkeyDispatcherTest, NumberKeySelectsHotbarSlot) {
    auto& ctx = RuntimeContext::Instance();
    auto& melee = ctx.combat().melee;

    EXPECT_EQ(melee.selectedSlot(), 0);

    // Press '3'
    setKey('3', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(melee.selectedSlot(), 2);

    // Release '3' and press '9'
    setKey('3', false);
    setKey('9', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(melee.selectedSlot(), 8);
}

TEST_F(HotkeyDispatcherTest, FKeySwapsMainAndOffhand) {
    auto& ctx = RuntimeContext::Instance();
    auto& melee = ctx.combat().melee;

    const auto initial_offhand = melee.offhandItem();
    melee.inventory().setSlot(0, {mc::ItemId::DiamondSword, 1});
    melee.select(0);
    EXPECT_EQ(melee.heldItem(), mc::ItemId::DiamondSword);

    // Press 'F'
    setKey('F', true);
    dispatcher_->tick(0.016f);

    EXPECT_EQ(melee.heldItem(), initial_offhand);
    EXPECT_EQ(melee.offhandItem(), mc::ItemId::DiamondSword);

    // Release 'F' then press 'F' again
    setKey('F', false);
    dispatcher_->tick(0.016f);
    setKey('F', true);
    dispatcher_->tick(0.016f);

    EXPECT_EQ(melee.heldItem(), mc::ItemId::DiamondSword);
    EXPECT_EQ(melee.offhandItem(), initial_offhand);
}

TEST_F(HotkeyDispatcherTest, ModeTogglesF5AndF6) {
    auto& ctx = RuntimeContext::Instance();

    // Default is survival
    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Survival);

    // Press F5 -> Creative
    setKey(VK_F5, true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Creative);

    // Release and press F5 again -> Survival
    setKey(VK_F5, false);
    dispatcher_->tick(0.016f);
    setKey(VK_F5, true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.currentGameMode(), creative::GameMode::Survival);

    // Press F6 -> toggle MC mode when this build contains it.
    EXPECT_TRUE(ctx.mode().mc_mode.load());
    setKey(VK_F6, true);
    dispatcher_->tick(0.016f);
#if ENABLE_MC_MODE
    EXPECT_FALSE(ctx.mode().mc_mode.load());
#else
    // MC gameplay is compiled out: F6 has nothing to toggle.
    EXPECT_TRUE(ctx.mode().mc_mode.load());
#endif
}

TEST_F(HotkeyDispatcherTest, MiningConsumesLeftClickSoNoSwingAttack) {
    left_click_edge_ = true;
    left_held_ = true;
    mining_consumed_ = true; // Actively mining a placed block

    dispatcher_->tick(0.016f);

    // Because mining consumed the tick, click_attack must NOT be called
    EXPECT_FALSE(attack_called_);

    // When mining is NOT active, left click edge triggers attack
    left_click_edge_ = true;
    mining_consumed_ = false;

    dispatcher_->tick(0.016f);
    EXPECT_TRUE(attack_called_);
    EXPECT_GT(attack_charged_, 0.f);
}

TEST_F(HotkeyDispatcherTest, RightClickPriorityTntThenCreeperThenBlock) {
    // 1. TNT priority
    right_click_edge_ = true;
    ignite_tnt_success_ = true;
    place_block_success_ = true;

    dispatcher_->tick(0.016f);
    EXPECT_TRUE(tnt_ignited_);
    EXPECT_FALSE(block_placed_);

    // 2. Block placement when not TNT
    ignite_tnt_success_ = false;
    right_click_edge_ = true;
    place_block_success_ = true;

    dispatcher_->tick(0.016f);
    EXPECT_TRUE(block_placed_);
}

TEST_F(HotkeyDispatcherTest, MouseWheelScrollsHotbarSlots) {
    auto& ctx = RuntimeContext::Instance();
    auto& melee = ctx.combat().melee;

    melee.select(0);
    // Wheel away (+1 notch) should select previous slot (wrapping to 8)
    wheel_notches_ = 1;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(melee.selectedSlot(), 8);

    // Wheel toward user (-1 notch) should advance to next slot
    wheel_notches_ = -1;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(melee.selectedSlot(), 0);
}

TEST_F(HotkeyDispatcherTest, InputDisabledShortCircuitsClickAttackAndInteractions) {
    auto& ctx = RuntimeContext::Instance();
    // Simulate regression where SetupInput failed to enable input
    ctx.mode().input_enabled.store(false);

    left_click_edge_ = true;
    right_click_edge_ = true;
    place_block_success_ = true;

    dispatcher_->tick(0.016f);

    // Crucial regression assertion: when input_enabled is false, neither attack nor block placement can happen!
    EXPECT_FALSE(attack_called_);
    EXPECT_FALSE(block_placed_);

    // Restore input_enabled -> now it works
    ctx.mode().input_enabled.store(true);
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(attack_called_);
}

#if ENABLE_CS2_VIEWMODEL
TEST_F(HotkeyDispatcherTest, CsModeToggleAndKnifeInspectLifecycle) {
    auto& ctx = RuntimeContext::Instance();
    EXPECT_FALSE(ctx.mode().cs_mode.load());

    // Press F1 to toggle CS mode ON
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);

    EXPECT_TRUE(ctx.mode().cs_mode.load());
    EXPECT_TRUE(ctx.mode().first_person.load());
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 4); // Deploy triggered

    // Press F to trigger butterfly inspect
    setKey('F', true);
    dispatcher_->tick(0.016f);
    setKey('F', false);
    dispatcher_->tick(0.016f);

    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 1); // Inspect triggered
    EXPECT_TRUE(ctx.mode().cs_knife_inspect.load());
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "cs.knife.inspect") != played_sounds_.end());

    // Left click to trigger knife slash
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2); // Slash triggered
    EXPECT_FALSE(ctx.mode().cs_knife_inspect.load());
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "cs.knife.slash") != played_sounds_.end());

    // Right click to trigger backstab
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 3); // Backstab triggered
    EXPECT_FALSE(ctx.mode().cs_knife_inspect.load());
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "cs.knife.hit") != played_sounds_.end());

    // Advance time past the heavy-attack clip (1.167 s) -> knife returns to idle (0)
    dispatcher_->tick(1.2f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);
    EXPECT_FALSE(ctx.mode().cs_knife_inspect.load());
    EXPECT_FLOAT_EQ(ctx.mode().cs_knife_anim_time.load(), 0.f);

    // Press F1 again to toggle CS mode OFF
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().cs_mode.load());
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);
    EXPECT_FALSE(ctx.mode().cs_knife_inspect.load());

    // Toggle CS mode back ON via F1, then verify F6 toggles CS mode OFF
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(ctx.mode().cs_mode.load());

    setKey(VK_F6, true);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().cs_mode.load());
#if ENABLE_MC_MODE
    EXPECT_TRUE(ctx.mode().mc_mode.load()); // combined build: F6 returns to MC
#else
    EXPECT_FALSE(ctx.mode().mc_mode.load()); // MC-less build: nothing to return to
#endif
}

TEST_F(HotkeyDispatcherTest, CsModeTurnsOffMcMode) {
    auto& ctx = RuntimeContext::Instance();
    ASSERT_TRUE(ctx.mode().mc_mode.load());

    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);

    EXPECT_TRUE(ctx.mode().cs_mode.load());
    EXPECT_FALSE(ctx.mode().mc_mode.load());
}

TEST_F(HotkeyDispatcherTest, F6LeavesCsModeAndRestoresMcWhenAvailable) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());
    ASSERT_FALSE(ctx.mode().mc_mode.load());

    // F6 leaves CS mode and returns to MC when this build contains MC.
    setKey(VK_F6, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F6, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().cs_mode.load());
#if ENABLE_MC_MODE
    EXPECT_TRUE(ctx.mode().mc_mode.load());
#else
    EXPECT_FALSE(ctx.mode().mc_mode.load());
#endif

    // A second F6 toggles MC off (a no-op in an MC-less build).
    setKey(VK_F6, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F6, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().mc_mode.load());
}

TEST_F(HotkeyDispatcherTest, McJumpMasksSpace) {
#ifdef _WIN32
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().mc_jump.store(true);
    dispatcher_->tick(0.016f);

    const int sc = static_cast<int>(MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC));
    EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), sc), masked_keys_.end());
#else
    GTEST_SKIP() << "scan codes are Windows-only";
#endif
}

TEST_F(HotkeyDispatcherTest, VanillaRollMasksShift) {
#ifdef _WIN32
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().disable_vanilla_roll.store(true);
    dispatcher_->tick(0.016f);

    EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), eldenring::roll::kDikLShift), masked_keys_.end());
    EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), eldenring::roll::kDikRShift), masked_keys_.end());
#else
    GTEST_SKIP() << "scan codes are Windows-only";
#endif
}

TEST_F(HotkeyDispatcherTest, CreativeFlightMasksItsMovementKeys) {
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().flying.store(true);
    dispatcher_->tick(0.016f);

    for (const int key : eldenring::flight::kHiddenKeys) {
        EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), key), masked_keys_.end());
    }
}

TEST_F(HotkeyDispatcherTest, InventorySuppressesMotionAndKeyboard) {
    auto& ctx = RuntimeContext::Instance();

    ctx.ui().inv_open.store(true);
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(suppressed_motion_);
    EXPECT_TRUE(suppressed_keyboard_);

    ctx.ui().inv_open.store(false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(suppressed_motion_);
    EXPECT_FALSE(suppressed_keyboard_);
}

TEST_F(HotkeyDispatcherTest, CsModeMasksItsVanillaKeys) {
#ifdef _WIN32
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());

    auto masked = [this](int vk) {
        const int sc = static_cast<int>(MapVirtualKeyW(static_cast<UINT>(vk), MAPVK_VK_TO_VSC));
        return std::find(masked_keys_.begin(), masked_keys_.end(), sc) != masked_keys_.end();
    };
    EXPECT_TRUE(masked('R')); // reload must not also use a quick item
    EXPECT_TRUE(masked('F')); // inspect must not also trigger the vanilla action
    for (int d = '1'; d <= '4'; ++d) EXPECT_TRUE(masked(d)); // weapon slots must not select vanilla quick slots
    EXPECT_TRUE(suppressed_wheel_);

    // Leaving CS mode releases both.
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_FALSE(ctx.mode().cs_mode.load());
    EXPECT_FALSE(masked('R'));
    EXPECT_FALSE(suppressed_wheel_);
#else
    GTEST_SKIP() << "scan codes are Windows-only";
#endif
}

TEST_F(HotkeyDispatcherTest, CsModeDoesNotScrollTheMcHotbar) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());

    const int before = ctx.combat().melee.selectedSlot();
    wheel_notches_ = 1;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.combat().melee.selectedSlot(), before);
}

TEST_F(HotkeyDispatcherTest, CsModeDigitKeysSelectWeaponSlotsAndRReloads) {
    auto& ctx = RuntimeContext::Instance();

    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());
    EXPECT_EQ(ctx.mode().cs_weapon_slot.load(), 0);

    // 1 = AK-47 primary, deploy animation.
    setKey('1', true);
    dispatcher_->tick(0.016f);
    setKey('1', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_weapon_slot.load(), 1);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 4);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 30u);
    EXPECT_EQ(ctx.mode().cs_reserve.load(), 90u);
    EXPECT_EQ(ctx.mode().cs_mag_capacity.load(), 30u);

    // Automatic fire consumes one round.
    left_held_ = true;
    dispatcher_->tick(0.016f);
    left_held_ = false;
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 29u);

    // R = reload while a gun is active.
    setKey('R', true);
    dispatcher_->tick(0.016f);
    setKey('R', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 5);

    // Reload completes after 2.5 s and moves one round from reserve to magazine.
    dispatcher_->tick(2.6f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 30u);
    EXPECT_EQ(ctx.mode().cs_reserve.load(), 89u);

    // 3 = butterfly knife.
    setKey('3', true);
    dispatcher_->tick(0.016f);
    setKey('3', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_weapon_slot.load(), 0);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 0u);
}

TEST_F(HotkeyDispatcherTest, CsReloadAudioFollowsTheAnimationTimeline) {
    auto& ctx = RuntimeContext::Instance();

    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    setKey('1', true);
    dispatcher_->tick(0.016f);
    setKey('1', false);
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 1);

    // Let the deploy take finish, then start from a clean sound log.
    dispatcher_->tick(1.1f);
    ASSERT_EQ(ctx.mode().cs_knife_state.load(), 0);
    played_sounds_.clear();

    setKey('R', true);
    dispatcher_->tick(0.016f);
    setKey('R', false);
    ASSERT_EQ(ctx.mode().cs_knife_state.load(), 5);
    // The old code fired one reload sound at t=0; the timeline has none there.
    EXPECT_EQ(std::count(played_sounds_.begin(), played_sounds_.end(), "cs.ak47.magout"), 0);

    auto tick_until = [&](float t) {
        while (ctx.mode().cs_knife_anim_time.load() < t) dispatcher_->tick(0.05f);
    };
    auto count = [&](const char* name) {
        return std::count(played_sounds_.begin(), played_sounds_.end(), name);
    };

    tick_until(0.40f); // frame 12: magazine still seated
    EXPECT_EQ(count("cs.ak47.magout"), 0);
    tick_until(0.45f); // frame 13
    EXPECT_EQ(count("cs.ak47.magout"), 1);
    tick_until(0.80f); // frame 24
    EXPECT_EQ(count("cs.ak47.magin"), 1);
    tick_until(1.60f); // frame 48
    EXPECT_EQ(count("cs.ak47.bolt"), 1);
}

TEST_F(HotkeyDispatcherTest, CsDigit4SelectsTheM4A1S) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());

    setKey('4', true);
    dispatcher_->tick(0.016f);
    setKey('4', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_weapon_slot.load(), 3);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 20u);
    EXPECT_EQ(ctx.mode().cs_reserve.load(), 80u);
}
TEST_F(HotkeyDispatcherTest, CsGunFireQueuesHitscanShotWithWeaponBallistics) {
    auto& ctx = RuntimeContext::Instance();

    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());

    // 1 = AK-47.
    setKey('1', true);
    dispatcher_->tick(0.016f);
    setKey('1', false);
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 1);

    cs_shot_queued_ = false;
    left_held_ = true;
    dispatcher_->tick(0.016f);
    left_held_ = false;

    EXPECT_TRUE(cs_shot_queued_);
    EXPECT_FLOAT_EQ(cs_shot_damage_, 36.f);
    EXPECT_FLOAT_EQ(cs_shot_spread_, 0.f); // first shot after a pause is accurate
    EXPECT_FLOAT_EQ(cs_shot_range_, 300.f);
    EXPECT_EQ(cs_shot_tracer_, 0u); // tracers are disabled by design
    EXPECT_EQ(cs_shot_slot_, 1);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 29u);

    // The 600 RPM cooldown blocks a second round on the next tick.
    cs_shot_queued_ = false;
    left_held_ = true;
    dispatcher_->tick(0.016f);
    left_held_ = false;
    EXPECT_FALSE(cs_shot_queued_);
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 29u);

    // After 0.1 s the next round fires.
    cs_shot_queued_ = false;
    left_held_ = true;
    dispatcher_->tick(0.11f);
    left_held_ = false;
    EXPECT_TRUE(cs_shot_queued_);
    EXPECT_FLOAT_EQ(cs_shot_spread_, 0.45f); // sustained fire uses the weapon spread
    EXPECT_EQ(ctx.mode().cs_ammo.load(), 28u);
}

TEST_F(HotkeyDispatcherTest, CsGunClickDoesNotSwingMelee) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    setKey('1', true);
    dispatcher_->tick(0.016f);
    setKey('1', false);
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 1);

    attack_called_ = false;
    cs_shot_queued_ = false;
    left_click_edge_ = true;
    left_held_ = true;
    dispatcher_->tick(0.016f);
    left_click_edge_ = false;
    left_held_ = false;

    EXPECT_TRUE(cs_shot_queued_);
    EXPECT_FALSE(attack_called_); // a gun shot must not also swing the MC melee
}

TEST_F(HotkeyDispatcherTest, CsKnifeDoesNotQueueHitscanShots) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 0);

    cs_shot_queued_ = false;
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(cs_shot_queued_);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2);
    EXPECT_TRUE(attack_called_); // the knife still uses the MC melee path
    EXPECT_TRUE(ctx.mode().cs_action_hit.load());
}

TEST_F(HotkeyDispatcherTest, CsKnifeMissSelectsTheMissTake) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 0);

    attack_hit_ = false;
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2);
    EXPECT_FALSE(ctx.mode().cs_action_hit.load());

    // The next swing connects: the hit take comes back.
    attack_hit_ = true;
    dispatcher_->tick(1.2f); // finish the 1.167 s light clip
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2);
    EXPECT_TRUE(ctx.mode().cs_action_hit.load());
}

TEST_F(HotkeyDispatcherTest, CsKnifeActionsUseAuthoredTakesAndClipDurations) {
    auto& ctx = RuntimeContext::Instance();

    // Enter CS mode with the knife active (slot 0).
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().cs_mode.load());
    ASSERT_EQ(ctx.mode().cs_weapon_slot.load(), 0);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 4); // Deploy
    EXPECT_EQ(ctx.mode().cs_action_variant.load(), 0);

    // The deploy clip is 1.0 s; it must not be cut at the old 0.8 s constant.
    dispatcher_->tick(1.05f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    // LMB alternates the two authored light-attack takes.
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2);
    EXPECT_EQ(ctx.mode().cs_action_variant.load(), 1); // light_hit2
    dispatcher_->tick(1.1f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 2); // 1.167 s clip not finished yet
    dispatcher_->tick(0.1f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_action_variant.load(), 0); // light_hit
    dispatcher_->tick(1.2f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    // RMB is the heavy attack, not the backstab clip.
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 3);
    dispatcher_->tick(1.1f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 3);
    dispatcher_->tick(0.1f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    // Duration must come from the loaded clip, not a C++ constant: make
    // heavy_hit 0.25 s and the state ends after 0.3 s.
    cb_.clip_duration = [](const char* name) -> float {
        return (name != nullptr && std::string(name) == "heavy_hit") ? 0.25f : 0.f;
    };
    dispatcher_->setCallbacks(cb_);
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 3);
    dispatcher_->tick(0.3f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    // Inspect cycles lookat01 -> lookat02 -> lookat03.
    const uint8_t start = ctx.mode().cs_action_variant.load();
    setKey('F', true);
    dispatcher_->tick(0.016f);
    setKey('F', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 1);
    EXPECT_EQ(ctx.mode().cs_action_variant.load(), static_cast<uint8_t>((start + 1u) % 3u));
    dispatcher_->tick(4.0f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 1); // 4.1/4.5/4.867 s take
}

#else
TEST_F(HotkeyDispatcherTest, PureMcBuildDisablesCsHotkeysAndInteractions) {
    auto& ctx = RuntimeContext::Instance();
    EXPECT_FALSE(ctx.mode().cs_mode.load());

    // Press F1 -> In pure MC build, F1 MUST NOT enable CS mode
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().cs_mode.load());
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);

    // Press 'F' -> In pure MC build, it must not trigger knife inspect sound
    setKey('F', true);
    dispatcher_->tick(0.016f);
    setKey('F', false);
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "cs.knife.inspect") == played_sounds_.end());

    // Press digit 1 -> Must select MC melee weapon slot, NOT trigger CS knife deploy
    setKey('1', true);
    dispatcher_->tick(0.016f);
    setKey('1', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().cs_knife_state.load(), 0);
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "cs.knife.deploy") == played_sounds_.end());
}
#endif

#if ENABLE_OW_VIEWMODEL
TEST_F(HotkeyDispatcherTest, OwModeShiftF7TogglePerspectiveAndIsolation) {
    auto& ctx = RuntimeContext::Instance();
    EXPECT_FALSE(ctx.mode().ow_mode.load());
    EXPECT_TRUE(ctx.mode().mc_mode.load());

    // 1. Shift + F7: Activate OW Doomfist mode
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);

    EXPECT_TRUE(ctx.mode().ow_mode.load());
    EXPECT_FALSE(ctx.mode().mc_mode.load());
    EXPECT_FALSE(ctx.mode().cs_mode.load());
    EXPECT_TRUE(ctx.mode().first_person.load());

    // 2. F7 alone in OW mode: Toggle 1P / 3P perspective
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().first_person.load()); // switched to 3P

    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(ctx.mode().first_person.load()); // switched back to 1P

    // 3. F6: Leaves OW mode cleanly without MC mode active
    setKey(VK_F6, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F6, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().ow_mode.load());
    EXPECT_FALSE(ctx.mode().mc_mode.load());

    // 4. Reactivate OW mode, then press F1 (CS mode toggle): OW mode must turn off
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    EXPECT_TRUE(ctx.mode().ow_mode.load());

#if ENABLE_CS2_VIEWMODEL
    setKey(VK_F1, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F1, false);
    dispatcher_->tick(0.016f);
    EXPECT_FALSE(ctx.mode().ow_mode.load());
    EXPECT_TRUE(ctx.mode().cs_mode.load());
#endif
}

TEST_F(HotkeyDispatcherTest, OwModePlaysAuthenticSoundEvents) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().ow_mode.load());

    played_sounds_.clear();
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 2);
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "ow.cannon.fire") != played_sounds_.end());

    played_sounds_.clear();
    right_held_ = true; // RMB is hold-to-charge; the punch fires on release
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 3);
    EXPECT_TRUE(played_sounds_.empty()) << "the windup must not play the release sound";

    // Holding charges up to the authored release frame and no further.
    for (int i = 0; i < 200; ++i) dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 3);
    EXPECT_NEAR(ctx.mode().ow_anim_time.load(), eldenring::ow::kRocketPunchReleaseTime, 1e-3f);
    EXPECT_TRUE(played_sounds_.empty()) << "the punch fires when the button comes up";

    // Releasing plays the strike and the authentic release sound.
    right_held_ = false;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 3);
    EXPECT_TRUE(std::find(played_sounds_.begin(), played_sounds_.end(), "ow.punch.release") != played_sounds_.end());

    for (int i = 0; i < 100; ++i) dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 0);
}

TEST_F(HotkeyDispatcherTest, OwModeRocketPunchFiresOnRelease) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().ow_mode.load());

    right_held_ = true;
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ctx.mode().ow_state.load(), 3);

    dispatcher_->tick(0.5f); // charge for half a second
    ASSERT_LT(ctx.mode().ow_anim_time.load(), eldenring::ow::kRocketPunchReleaseTime);

    right_held_ = false; // release: jump to the release frame and fire
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 3);
    EXPECT_NEAR(ctx.mode().ow_anim_time.load(), eldenring::ow::kRocketPunchReleaseTime, 1e-3f);
}

TEST_F(HotkeyDispatcherTest, OwModePlaysUppercutAndSlamOnEAndQ) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().ow_mode.load());

    setKey('E', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 4);
    setKey('E', false);
    dispatcher_->tick(0.016f);

    setKey('Q', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 5);
    setKey('Q', false);
    dispatcher_->tick(0.016f);
}

TEST_F(HotkeyDispatcherTest, OwAttacksRequestDamageForEveryAbility) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().ow_mode.load());
    ow_strikes_.clear();

    // Hand cannon: left click.
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    ASSERT_EQ(ow_strikes_.size(), 1u);
    EXPECT_EQ(ow_strikes_[0], 0);

    // Rocket punch: hold RMB, release.
    right_held_ = true;
    right_click_edge_ = true;
    dispatcher_->tick(0.016f);
    right_held_ = false;
    dispatcher_->tick(0.016f);
    ASSERT_GE(ow_strikes_.size(), 2u);
    EXPECT_EQ(ow_strikes_.back(), 1);

    // Rising uppercut.
    setKey('E', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ow_strikes_.back(), 2);
    setKey('E', false);
    dispatcher_->tick(0.016f);

    // Seismic slam.
    setKey('Q', true);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ow_strikes_.back(), 3);
    setKey('Q', false);
    dispatcher_->tick(0.016f);
}

TEST_F(HotkeyDispatcherTest, OwModeDoesNotLeakMcGameplay) {
    auto& ctx = RuntimeContext::Instance();
    setKey(VK_SHIFT, true);
    setKey(VK_F7, true);
    dispatcher_->tick(0.016f);
    setKey(VK_F7, false);
    setKey(VK_SHIFT, false);
    dispatcher_->tick(0.016f);
    ASSERT_TRUE(ctx.mode().ow_mode.load());

    // The OW cannon owns the left click; the generic MC melee swing must not fire.
    attack_called_ = false;
    left_click_edge_ = true;
    dispatcher_->tick(0.016f);
    EXPECT_EQ(ctx.mode().ow_state.load(), 2);
    EXPECT_FALSE(attack_called_) << "OW left click must not also start the MC melee swing";

#ifdef _WIN32
    // Both physical Shift keys are hidden from DirectInput.
    EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), eldenring::roll::kDikLShift), masked_keys_.end());
    EXPECT_NE(std::find(masked_keys_.begin(), masked_keys_.end(), eldenring::roll::kDikRShift), masked_keys_.end());
#endif

    // F must not swap the MC main/offhand while OW owns the view.
    auto& inv = ctx.combat().melee.inventory();
    inv.setSlot(0, {mc::ItemId::DiamondSword, 1});
    inv.setOffhand({mc::ItemId::Shield, 1});
    setKey('F', true);
    dispatcher_->tick(0.016f);
    setKey('F', false);
    dispatcher_->tick(0.016f);
    EXPECT_EQ(inv.slot(0).item, mc::ItemId::DiamondSword);
    EXPECT_EQ(inv.offhand().item, mc::ItemId::Shield);
}
#endif

