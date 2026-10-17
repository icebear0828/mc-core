#include <gtest/gtest.h>
#include "eldenring_game_hooks.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_combat_service.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace eldenring::hooks;
using eldenring::RuntimeContext;

class GameHooksTest : public ::testing::Test {
protected:
    void SetUp() override {
        RuntimeContext::Instance().resetForTesting();
        GameHooks::Instance().resetForTesting();
    }

    void TearDown() override {
        GameHooks::Instance().resetForTesting();
        RuntimeContext::Instance().resetForTesting();
    }
};

TEST_F(GameHooksTest, ClampDetourInvokesDrainOnlyOnGameThread) {
    auto& hooks = GameHooks::Instance();
    bool clamp_tick_called = false;
    uintptr_t tick_module = 0;

    GameHooksCallbacks cb;
    cb.get_game_tid = []() -> uint32_t {
#ifdef _WIN32
        return GetCurrentThreadId();
#else
        return 12345;
#endif
    };
    cb.on_clamp_tick = [&](uintptr_t mod) {
        clamp_tick_called = true;
        tick_module = mod;
    };
    hooks.setCallbacks(cb);

    static int orig_clamp_called = 0;
    orig_clamp_called = 0;
    hooks.setClampOrigForTesting([](void* mod, int32_t) -> void* {
        orig_clamp_called++;
        return mod;
    });

    int dummy_module = 42;
    void* res = GameHooks::InvokeClampDetour(&dummy_module, 100);
    EXPECT_EQ(res, &dummy_module);
    EXPECT_EQ(orig_clamp_called, 1);
    EXPECT_TRUE(clamp_tick_called);
    EXPECT_EQ(tick_module, reinterpret_cast<uintptr_t>(&dummy_module));

    // Now test with non-matching thread ID
    clamp_tick_called = false;
    cb.get_game_tid = []() -> uint32_t { return 999999; }; // Different from caller
    hooks.setCallbacks(cb);

    GameHooks::InvokeClampDetour(&dummy_module, 100);
    EXPECT_FALSE(clamp_tick_called);
}

TEST_F(GameHooksTest, CameraStepDetourRunsPreAndPostSteps) {
    auto& hooks = GameHooks::Instance();
    bool pre_step_called = false;
    float captured_dt = 0.f;
    bool post_step_called = false;
    uintptr_t captured_cam_addr = 0;

    GameHooksCallbacks cb;
    cb.on_camera_step_pre = [&](float dt) {
        pre_step_called = true;
        captured_dt = dt;
    };
    cb.check_chr_cam = [&](uint64_t, uintptr_t& out_cam_addr, float out_before[3]) -> bool {
        out_cam_addr = 0x12345678;
        out_before[0] = 1.0f;
        out_before[1] = 2.0f;
        out_before[2] = 3.0f;
        return true;
    };
    cb.on_camera_step_post = [&](bool have_before, uintptr_t cam_addr, const float before[3]) {
        if (have_before && before[0] == 1.0f) {
            post_step_called = true;
            captured_cam_addr = cam_addr;
        }
    };
    hooks.setCallbacks(cb);

    static int orig_camstep_called = 0;
    orig_camstep_called = 0;
    hooks.setCameraStepOrigForTesting([](uint64_t self, float, uint64_t, uint64_t) -> uint64_t {
        orig_camstep_called++;
        return self + 1;
    });

    uint64_t res = GameHooks::InvokeCameraStepDetour(1000, 0.016f, 2000, 1);
    EXPECT_EQ(res, 1001ULL);
    EXPECT_EQ(orig_camstep_called, 1);
    EXPECT_TRUE(pre_step_called);
    EXPECT_FLOAT_EQ(captured_dt, 0.016f);
    EXPECT_TRUE(post_step_called);
    EXPECT_EQ(captured_cam_addr, 0x12345678ULL);
}

TEST_F(GameHooksTest, ProcessDamageDetourTotemAndHitLogging) {
    auto& hooks = GameHooks::Instance();
    bool hit_logged = false;
    bool totem_healed = false;
    uint32_t target_heal = 0;

    GameHooksCallbacks cb;
    cb.player_hit_survival = [](void*, void*, uint8_t*, uint8_t, uint32_t& heal_to) -> bool {
        heal_to = 500;
        return true; // Totem popped!
    };
    cb.log_player_hit = [&](void*, void*, uint8_t*, uint8_t) {
        hit_logged = true;
    };
    cb.on_totem_popped_heal = [&](uint32_t heal_to) {
        totem_healed = true;
        target_heal = heal_to;
    };
    hooks.setCallbacks(cb);

    hooks.setProcessDamageOrigForTesting([](void*, void*, uint8_t*, uint32_t, uint8_t) -> uint64_t {
        return 123;
    });

    uint8_t ctx[16] = {};
    uint64_t r = GameHooks::InvokeProcessDamageDetour(nullptr, nullptr, ctx, 0, 0);
    EXPECT_EQ(r, 123ULL);
    EXPECT_TRUE(totem_healed);
    EXPECT_EQ(target_heal, 500u);
}

TEST_F(GameHooksTest, BreakTestDetourSoftensReactionInMcMode) {
    auto& hooks = GameHooks::Instance();
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().mc_mode.store(true);
    ctx.mode().knockdown_fix.store(true);

    auto& svc = eldenring::combat::CombatService::Instance();
    svc.resetForTesting();

    const uintptr_t kPlayerChr = 0x1000;
    uintptr_t dmg_module_mem[32] = {};
    dmg_module_mem[1] = kPlayerChr; // offset 8 is victim chr pointer

    uint8_t hit_buffer[64] = {};
    dmg_module_mem[eldenring::live::layout::kDamageHitBuffer / sizeof(uintptr_t)] = reinterpret_cast<uintptr_t>(hit_buffer);

    eldenring::combat::CombatCallbacks combat_cb{};
    combat_cb.get_player_chr = [&]() { return kPlayerChr; };
    combat_cb.safe_read = [&](uintptr_t a, void* d, size_t s) -> bool {
        if (a >= reinterpret_cast<uintptr_t>(dmg_module_mem) && a + s <= reinterpret_cast<uintptr_t>(dmg_module_mem) + sizeof(dmg_module_mem)) {
            std::memcpy(d, reinterpret_cast<const void*>(a), s);
            return true;
        }
        if (a >= reinterpret_cast<uintptr_t>(hit_buffer) && a + s <= reinterpret_cast<uintptr_t>(hit_buffer) + sizeof(hit_buffer)) {
            std::memcpy(d, reinterpret_cast<const void*>(a), s);
            return true;
        }
        return false;
    };
    combat_cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) -> bool {
        if (a >= reinterpret_cast<uintptr_t>(hit_buffer) && a + sz <= reinterpret_cast<uintptr_t>(hit_buffer) + sizeof(hit_buffer)) {
            std::memcpy(reinterpret_cast<void*>(a), s, sz);
            return true;
        }
        return false;
    };
    svc.setCallbacks(combat_cb);

    static bool orig_break_called = false;
    orig_break_called = false;
    hooks.setBreakTestOrigForTesting([](void*, uint8_t*) -> bool {
        orig_break_called = true;
        return true; // Engine says knockdown!
    });

    uint8_t hit_ctx[0x300] = {};
    bool broke = GameHooks::InvokeBreakTestDetour(dmg_module_mem, hit_ctx);
    EXPECT_TRUE(orig_break_called);
    EXPECT_FALSE(broke); // In MC mode, heavy reaction is suppressed
}

TEST_F(GameHooksTest, KillChrDetourSkipsWhenProtected) {
    auto& hooks = GameHooks::Instance();
    bool skip_kill = true;
    GameHooksCallbacks cb;
    cb.should_skip_kill_chr = [&](void*) -> bool {
        return skip_kill;
    };
    hooks.setCallbacks(cb);

    static int orig_kill_called = 0;
    orig_kill_called = 0;
    hooks.setKillChrOrigForTesting([](void*) {
        orig_kill_called++;
    });

    int dummy_chr = 1;
    // When skip is true, orig must not be called
    GameHooks::InvokeKillChrDetour(&dummy_chr);
    EXPECT_EQ(orig_kill_called, 0);

    // When skip is false, orig must be called
    skip_kill = false;
    GameHooks::InvokeKillChrDetour(&dummy_chr);
    EXPECT_EQ(orig_kill_called, 1);
}

TEST_F(GameHooksTest, SpawnBulletDetourCapturesRequestAndLogs) {
    auto& hooks = GameHooks::Instance();
    const void* captured_req = nullptr;
    GameHooksCallbacks cb;
    cb.on_spawn_bullet = [&](const void* req) {
        captured_req = req;
    };
    hooks.setCallbacks(cb);

    static uint32_t dummy_handle = 777;
    hooks.setSpawnBulletOrigForTesting([](void*, uint32_t* out, void*, void*) -> uint32_t* {
        return out;
    });

    int dummy_request = 999;
    uint32_t* res = GameHooks::InvokeSpawnBulletDetour(nullptr, &dummy_handle, &dummy_request, nullptr);
    EXPECT_EQ(res, &dummy_handle);
    EXPECT_EQ(captured_req, &dummy_request);
}
