#include <gtest/gtest.h>

#include "eldenring_locomotion.hpp"
#include "eldenring_runtime_context.hpp"
#include <map>
#include <vector>

using namespace eldenring;
using namespace eldenring::movement;
using eldenring::live::CameraPose;
using eldenring::live::RayResult;

namespace {

// Simulated memory space for deterministic unit testing
class MockMemory {
public:
    void writeBytes(uintptr_t addr, const void* src, size_t size) {
        const uint8_t* p = static_cast<const uint8_t*>(src);
        for (size_t i = 0; i < size; ++i) {
            mem_[addr + i] = p[i];
        }
    }

    bool readBytes(uintptr_t addr, void* dst, size_t size) const {
        uint8_t* p = static_cast<uint8_t*>(dst);
        for (size_t i = 0; i < size; ++i) {
            auto it = mem_.find(addr + i);
            if (it == mem_.end()) return false;
            p[i] = it->second;
        }
        return true;
    }

    template <typename T>
    void write(uintptr_t addr, const T& val) {
        writeBytes(addr, &val, sizeof(T));
    }

    template <typename T>
    T read(uintptr_t addr) const {
        T val{};
        readBytes(addr, &val, sizeof(T));
        return val;
    }

private:
    std::map<uintptr_t, uint8_t> mem_;
};

} // namespace

TEST(EldenRingLocomotion, ActionLockClearingAndFallTimer) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kContainer = 0x2000;
    const uintptr_t kActModule = 0x3000;
    const uintptr_t kFallModule = 0x4000;

    // Setup module container pointers
    mem.write<uint64_t>(kChr + layout::kModuleContainerInChrIns, kContainer);
    mem.write<uint64_t>(kContainer + 0x08, kActModule);
    mem.write<uint64_t>(kContainer + 0x70, kFallModule);

    // Setup action lock flags: bits 19 & 20 set (air/WASD locks), plus other bits (e.g. bit 2 = 0x4)
    const uint64_t initial_flags = (1ULL << 19) | (1ULL << 20) | 0x4ULL;
    mem.write<uint64_t>(kActModule + 0x40, initial_flags);

    // Setup fall timer
    mem.write<float>(kFallModule + 0x18, 1.8f);

    auto& loco = PlayerLocomotion::Instance();
    loco.resetForTesting();

    LocomotionCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    loco.setCallbacks(cb);

    // 1. Verify module lookup
    EXPECT_EQ(loco.actionFlagModuleOf(kChr), kActModule);
    EXPECT_EQ(loco.fallModuleOf(kChr), kFallModule);

    // 2. Verify unlockAirMovement clears bits 19 & 20 while preserving bit 2
    loco.unlockAirMovement(kChr);
    const uint64_t cleared_flags = mem.read<uint64_t>(kActModule + 0x40);
    EXPECT_EQ(cleared_flags, 0x4ULL);
    EXPECT_FALSE(eldenring::action::hasAirLocks(cleared_flags));

    // 3. Verify fall timer reading and resetting
    EXPECT_FLOAT_EQ(loco.fallTimer(kChr), 1.8f);
    loco.setFallHold(1.0f);
    loco.resetFallTimer(kChr);
    EXPECT_FLOAT_EQ(mem.read<float>(kFallModule + 0x18), 0.0f);
    EXPECT_FLOAT_EQ(loco.preFallTime(), 1.8f);

    // 4. Verify landing resets timer and clears locks
    mem.write<uint64_t>(kActModule + 0x40, initial_flags);
    mem.write<float>(kFallModule + 0x18, 2.5f);
    loco.landFallTimer(kChr);
    EXPECT_FLOAT_EQ(mem.read<float>(kFallModule + 0x18), 0.0f);
    EXPECT_FALSE(eldenring::action::hasAirLocks(mem.read<uint64_t>(kActModule + 0x40)));
}

TEST(EldenRingLocomotion, FlightDoubleTapStateMachineAndRebase) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;
    const uintptr_t kContainer = 0x2000;
    const uintptr_t kFallModule = 0x4000;
    const uintptr_t kActModule = 0x3000;

    mem.write<uint64_t>(kChr + layout::kModuleContainerInChrIns, kContainer);
    mem.write<uint64_t>(kContainer + 0x08, kActModule);
    mem.write<uint64_t>(kContainer + 0x70, kFallModule);

    float initial_pos[3] = {10.f, 20.f, 30.f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, initial_pos, sizeof(initial_pos));

    auto& loco = PlayerLocomotion::Instance();
    loco.resetForTesting();

    std::map<int, bool> keys_down;
    loco.setInputProvider([&](int vk) { return keys_down[vk]; });

    LocomotionCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.want_suppress = []() { return true; };
    cb.is_player_dead = []() { return false; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    loco.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().game_mode.store(static_cast<uint8_t>(creative::GameMode::Creative));
    ctx.ui().inv_open.store(false);

    // Initial state: not flying
    EXPECT_FALSE(loco.isFlying());

    // First press of Spacebar at t = 1000ms
    keys_down[VK_SPACE] = true;
    loco.stepFlight(0.016f, 1000, ctx);
    EXPECT_FALSE(loco.isFlying());

    // Release Spacebar at t = 1050ms
    keys_down[VK_SPACE] = false;
    loco.stepFlight(0.016f, 1050, ctx);
    EXPECT_FALSE(loco.isFlying());

    // Second press of Spacebar at t = 1200ms -> triggers double-tap flight start!
    keys_down[VK_SPACE] = true;
    loco.stepFlight(0.016f, 1200, ctx);
    EXPECT_TRUE(loco.isFlying());

    // Release Spacebar, then fly forward with 'W' only
    keys_down[VK_SPACE] = false;
    float y_after_start = 0.f;
    mem.readBytes(kModule + layout::kPhysicsPosition + sizeof(float), &y_after_start, sizeof(float));
    keys_down['W'] = true;
    loco.stepFlight(0.1f, 1300, ctx);

    float new_pos[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, new_pos, sizeof(new_pos));
    EXPECT_GT(new_pos[2], initial_pos[2]); // Z advanced forward
    EXPECT_FLOAT_EQ(new_pos[1], y_after_start); // Y stayed steady in air while flying forward only

    // Opening inventory must terminate flight
    ctx.ui().inv_open.store(true);
    loco.stepFlight(0.016f, 1400, ctx);
    EXPECT_FALSE(loco.isFlying());
}

TEST(EldenRingLocomotion, JumpTakeoffTrajectoryAndCeilingResolution) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;
    const uintptr_t kContainer = 0x2000;
    const uintptr_t kFallModule = 0x4000;
    const uintptr_t kActModule = 0x3000;

    mem.write<uint64_t>(kChr + layout::kModuleContainerInChrIns, kContainer);
    mem.write<uint64_t>(kContainer + 0x08, kActModule);
    mem.write<uint64_t>(kContainer + 0x70, kFallModule);

    // Player initial position: feet at y = 10.0f
    float pos[3] = {0.f, 10.0f, 0.f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, pos, sizeof(pos));
    uint8_t ground_byte = 1;
    mem.writeBytes(kModule + 0x92, &ground_byte, 1);

    auto& loco = PlayerLocomotion::Instance();
    loco.resetForTesting();

    std::map<int, bool> keys_down;
    loco.setInputProvider([&](int vk) { return keys_down[vk]; });

    bool ceiling_hit = false;
    LocomotionCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.want_suppress = []() { return true; };
    cb.is_player_dead = []() { return false; };
    cb.is_player_airborne = []() { return false; };
    cb.is_supported_by_block = [](const float*) { return false; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    cb.resolve_block = [&](const float test_pos[3]) -> blocks::Resolve {
        blocks::Resolve res{};
        res.moved = false;
        res.feet[0] = test_pos[0];
        res.feet[1] = test_pos[1];
        res.feet[2] = test_pos[2];
        // Simulate a solid ceiling block at y = 10.8f
        if (test_pos[1] > 10.8f) {
            res.moved = true;
            res.feet[1] = 10.8f; // push feet down to ceiling bottom
            ceiling_hit = true;
        }
        return res;
    };
    cb.cast_ray = [](const float[3], const float[3]) -> RayResult {
        RayResult r{};
        r.ok = true;
        r.hit = true;
        r.fraction = 0.5f; // terrain at 10.0f
        return r;
    };
    loco.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().on_ground.store(true);

    // Initial state: controller inactive
    EXPECT_FALSE(loco.jumpController().isActive());

    // Press jump key
    keys_down[VK_SPACE] = true;
    loco.stepJump(0.016f, 1000, ctx);

    // Takeoff confirmed
    EXPECT_TRUE(loco.jumpController().isActive());
    EXPECT_GT(loco.jumpController().vy(), 0.f);

    // Step jump upwards towards ceiling
    for (int i = 0; i < 15 && !ceiling_hit; ++i) {
        loco.stepJump(0.016f, 1000 + (i + 1) * 16, ctx);
    }

    EXPECT_TRUE(ceiling_hit);
    // After hitting ceiling, vertical velocity must be <= 0
    EXPECT_LE(loco.jumpController().vy(), 0.0f);
}

TEST(EldenRingLocomotion, SneakEdgeProtectionAndGroundSprint) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;

    float feet[3] = {0.5f, 10.0f, 0.5f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& loco = PlayerLocomotion::Instance();
    loco.resetForTesting();

    std::map<int, bool> keys_down;
    loco.setInputProvider([&](int vk) { return keys_down[vk]; });

    bool sneak_clamped = false;
    LocomotionCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.want_suppress = []() { return true; };
    cb.is_player_dead = []() { return false; };
    cb.is_player_airborne = []() { return false; };
    cb.clamp_sneak = [&](const float*, float, float) -> blocks::SneakMoveResult {
        blocks::SneakMoveResult res{};
        res.clamped_x = true;
        res.clamped_z = true;
        sneak_clamped = true;
        return res;
    };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    cb.check_sprint_collision = [](float, float, float) { return false; };
    loco.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();

    // 1. Sneak edge clamp verification
    keys_down['W'] = true;
    keys_down[VK_SHIFT] = true; // sneak key
    loco.driveVelocity(kModule, false, ctx);

    EXPECT_TRUE(sneak_clamped);
    float vx = mem.read<float>(kModule + 0x120);
    float vz = mem.read<float>(kModule + 0x128);
    EXPECT_FLOAT_EQ(vx, 0.0f);
    EXPECT_FLOAT_EQ(vz, 0.0f);

    // 2. Sprint boost inhibited while shield is blocking
    ctx.mode().mc_mode.store(true);
    ctx.player().sprint_active.store(true);
    ctx.combat().shield.update(1.0f, true, true); // blocking active!

    float pos_before[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, pos_before, sizeof(pos_before));
    loco.stepGroundSprint(0.016f, ctx);
    float pos_after[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, pos_after, sizeof(pos_after));
    EXPECT_FLOAT_EQ(pos_before[2], pos_after[2]); // no movement boost applied
}

TEST(EldenRingLocomotion, ShieldBlockingSlowdownUsingModeFlag) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x2000;

    auto& loco = PlayerLocomotion::Instance();
    loco.resetForTesting();

    std::map<int, bool> keys_down;
    loco.setInputProvider([&](int vk) { return keys_down[vk]; });

    LocomotionCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.want_suppress = []() { return true; };
    cb.is_player_dead = []() { return false; };
    cb.is_player_airborne = []() { return false; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    cb.check_sprint_collision = [](float, float, float) { return false; };
    loco.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);

    // Only set mode().shield_blocking, combat().shield remains inactive
    ctx.mode().shield_blocking.store(true);
    keys_down['W'] = true;

    loco.driveVelocity(kModule, false, ctx);

    const float vz = mem.read<float>(kModule + 0x128);
    const float expected_speed = ctx.mode().block_walk_speed.load() * eldenring::shield::kShieldBlockSpeedMultiplier;
    EXPECT_NEAR(vz, expected_speed, 1e-3f);

    // Also verify ground sprint is inhibited by mode().shield_blocking
    ctx.player().sprint_active.store(true);
    float pos_before[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, pos_before, sizeof(pos_before));
    loco.stepGroundSprint(0.016f, ctx);
    float pos_after[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, pos_after, sizeof(pos_after));
    EXPECT_FLOAT_EQ(pos_before[2], pos_after[2]);
}
