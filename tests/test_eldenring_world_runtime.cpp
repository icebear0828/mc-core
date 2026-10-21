#include <gtest/gtest.h>

#include "eldenring_world_runtime.hpp"
#include "eldenring_runtime_context.hpp"
#include <map>

using namespace eldenring;
using namespace eldenring::world;

namespace {

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

TEST(EldenRingWorldRuntime, ShadowGroundDetectsBlockTopAboveTerrain) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;

    float feet[3] = {0.5f, 10.0f, 0.5f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    // Terrain raycast finds ground down at y = 5.0f (fraction 0.5 of 10m drop from y=10.5)
    cb.cast_ray = [](const float[3], const float[3]) -> RayResult {
        RayResult r{};
        r.ok = true;
        r.hit = true;
        r.fraction = 0.55f;
        return r;
    };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().shadow_enabled.store(true);
    ctx.mode().steve_enabled.store(true);
    ctx.mode().mc_mode.store(true);

    // Place block at (0, 7, 0) -> top is y = 8.0f
    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        ctx.world().block_grid.place({0, 7, 0}, mc::BlockId::Stone);
    }

    runtime.stepShadowGround(ctx);

    EXPECT_TRUE(ctx.player().ground_valid.load());
    // Block top is at 8.0f, which is higher than terrain (~5.0f), so ground must be 8.0f!
    EXPECT_FLOAT_EQ(ctx.player().ground_y.load(), 8.0f);
}

TEST(EldenRingWorldRuntime, SweptAabbPlayerCollisionAndLedgeSafety) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;

    // Start player standing on top of block at (0, 0, 0) -> feet at y = 1.0f
    float feet[3] = {0.5f, 1.0f, 0.5f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();

    std::map<int, bool> keys_down;
    runtime.setInputProvider([&](int vk) { return keys_down[vk]; });

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().block_collision.store(true);

    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        ctx.world().block_grid.place({0, 0, 0}, mc::BlockId::Dirt);
    }

    // Step 1: establish initial position
    runtime.stepPlayerBlockCollision(ctx);
    EXPECT_TRUE(ctx.mode().block_supported.load());

    // Step 2: Sneak towards edge: move player feet towards z = 1.6f (completely over the edge!)
    keys_down[VK_SHIFT] = true;
    float stepped_feet[3] = {0.5f, 1.0f, 1.6f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, stepped_feet, sizeof(stepped_feet));

    runtime.stepPlayerBlockCollision(ctx);

    // Player position written to memory must be clamped at the block edge (z <= 1.30f)!
    float written_feet[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, written_feet, sizeof(written_feet));
    EXPECT_LE(written_feet[2], 1.30f);
    EXPECT_TRUE(ctx.mode().block_supported.load());
}

TEST(EldenRingWorldRuntime, TntExplosionIntentDrainAndDamageEvaluation) {
    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();

    std::map<uintptr_t, int> damages_applied;
    std::map<uintptr_t, std::array<float, 3>> enemy_positions;
    enemy_positions[0x2001] = {11.0f, 0.0f, 10.0f};
    enemy_positions[0x2002] = {30.0f, 0.0f, 30.0f};

    WorldRuntimeCallbacks cb{};
    cb.read_physics_pos = [&](uintptr_t chr, float out_f[3]) {
        auto it = enemy_positions.find(chr);
        if (it == enemy_positions.end()) return false;
        out_f[0] = it->second[0]; out_f[1] = it->second[1]; out_f[2] = it->second[2];
        return true;
    };
    cb.apply_damage_to_chr = [&](uintptr_t chr, int dmg) {
        damages_applied[chr] += dmg;
    };
    cb.play_sound = [](const char*, float) {};
    cb.spawn_fx = [](int, const float[3], int) {};
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();

    // Enqueue TNT detonation at (10, 0, 10)
    float tnt_pos[3] = {10.0f, 0.0f, 10.0f};
    runtime.enqueueTntIntent(tnt_pos, 8.0f, 1500.0f);
    EXPECT_EQ(runtime.pendingTntIntentCount(), 1u);

    // Create 2 enemies:
    // Enemy 1: close at (11, 0, 10), distance 1.0m -> should take large damage
    // Enemy 2: far at (30, 0, 30), distance 28m -> out of 8m radius, should take 0 damage
    std::vector<EnemyInfo> enemies;
    EnemyInfo e1{};
    e1.chr = 0x2001;
    e1.hp = 1000;
    e1.max_hp = 1000;
    enemies.push_back(e1);

    EnemyInfo e2{};
    e2.chr = 0x2002;
    e2.hp = 1000;
    e2.max_hp = 1000;
    enemies.push_back(e2);

    runtime.processTntExplosions(enemies, ctx);

    // Assert intent was drained
    EXPECT_EQ(runtime.pendingTntIntentCount(), 0u);

    // Assert damage was applied to e1 but not e2
    EXPECT_GT(damages_applied[0x2001], 0);
    EXPECT_EQ(damages_applied[0x2002], 0);
}

TEST(EldenRingWorldRuntime, EnemyBlockCollisionSweptBoxPush) {
    MockMemory mem;
    const uintptr_t kEnemyChr = 0x3001;
    const uintptr_t kEnemyModule = 0x6001;

    // Enemy feet placed intersecting block at (5, 0, 0)
    float e_feet[3] = {5.2f, 0.0f, 0.5f};
    mem.writeBytes(kEnemyModule + layout::kPhysicsPosition, e_feet, sizeof(e_feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();

    WorldRuntimeCallbacks cb{};
    cb.get_physics_module = [&](uintptr_t) { return kEnemyModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kEnemyModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.enumerate_enemies = [&](std::vector<EnemyInfo>& out) {
        EnemyInfo e{};
        e.chr = kEnemyChr;
        e.hp = 100;
        out.push_back(e);
        return true;
    };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().enemy_block_collision.store(true);

    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        ctx.world().block_grid.place({5, 0, 0}, mc::BlockId::Stone);
    }

    std::vector<EnemyInfo> enemies;
    EnemyInfo e{};
    e.chr = kEnemyChr;
    e.hp = 100;
    enemies.push_back(e);

    float player_feet[3] = {0.f, 0.f, 0.f};
    runtime.stepEnemyBlockCollision(player_feet, ctx);

    // Enemy should be pushed out of the block!
    float final_feet[3] = {};
    mem.readBytes(kEnemyModule + layout::kPhysicsPosition, final_feet, sizeof(final_feet));
    EXPECT_NE(final_feet[0], e_feet[0]);
}

TEST(EldenRingWorldRuntime, StepBlockCollisionDrivesVelocityAndUnlocksAir) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x2000;

    float feet[3] = {0.5f, 1.0f, 0.5f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();

    bool drive_called = false;
    bool reset_fall_called = false;
    bool unlock_air_called = false;

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.drive_velocity = [&](uintptr_t, bool, RuntimeContext&) { drive_called = true; };
    cb.reset_fall_timer = [&](uintptr_t) { reset_fall_called = true; };
    cb.unlock_air_movement = [&](uintptr_t) { unlock_air_called = true; };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().block_collision.store(true);
    ctx.mode().block_drive.store(true);

    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        ctx.world().block_grid.place({0, 0, 0}, mc::BlockId::Dirt);
    }

    runtime.stepPlayerBlockCollision(ctx);

    EXPECT_TRUE(ctx.mode().block_supported.load());
    EXPECT_TRUE(drive_called);
    EXPECT_TRUE(reset_fall_called);
    EXPECT_TRUE(unlock_air_called);
}

TEST(EldenRingWorldRuntime, StepBlockCollisionDirectDriveMovesPlayerOnBlock) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x2000;

    float feet[3] = {0.5f, 1.0f, 0.5f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();
    runtime.setDeltaTimeOverrideForTesting(0.016f);
    // Press 'W'
    runtime.setInputProvider([](int vk) { return vk == 'W'; });

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    cb.read_camera = [](CameraPose& cam) {
        cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
        cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
        return true;
    };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().block_collision.store(true);

    {
        std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
        for (int x = -1; x <= 1; ++x) {
            for (int z = -1; z <= 2; ++z) {
                ctx.world().block_grid.place({x, 0, z}, mc::BlockId::Stone);
            }
        }
    }

    // Step 1: Initialize
    runtime.stepPlayerBlockCollision(ctx);

    // Step 2: Step forward with W held
    runtime.stepPlayerBlockCollision(ctx);

    float written_feet[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, written_feet, sizeof(written_feet));
    // The player must have moved forward along Z (Z > 0.505f)!
    EXPECT_GT(written_feet[2], 0.505f);
}

TEST(EldenRingWorldRuntime, StepBlockCollisionClampsNativeTerrainDisplacementWhenShieldingOrSneaking) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x2000;

    // Start on native terrain (no blocks placed)
    float feet[3] = {0.0f, 0.0f, 0.0f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, feet, sizeof(feet));

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();
    runtime.setDeltaTimeOverrideForTesting(0.016f);
    runtime.setInputProvider([](int) { return false; });

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().block_collision.store(true);
    ctx.mode().shield_blocking.store(true); // Player is holding up shield!

    // Step 1: Establish baseline at (0, 0, 0)
    runtime.stepPlayerBlockCollision(ctx);

    // Step 2: Game engine attempts to displace player by 1.0m (unclamped normal speed)
    float new_feet[3] = {0.0f, 0.0f, 1.0f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, new_feet, sizeof(new_feet));

    runtime.stepPlayerBlockCollision(ctx);

    // Result written to memory must be clamped to sneak speed * dt (far less than 1.0m, e.g. < 0.20m)!
    float written_feet[3] = {};
    mem.readBytes(kModule + layout::kPhysicsPosition, written_feet, sizeof(written_feet));
    EXPECT_LT(written_feet[2], 0.20f);
    EXPECT_GT(written_feet[2], 0.001f);
}

TEST(EldenRingWorldRuntime, DetectsFloatingOriginRebaseAndShiftsWorld) {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;

    auto& runtime = VoxelWorldRuntime::Instance();
    runtime.resetForTesting();
    runtime.setDeltaTimeOverrideForTesting(0.016f);
    runtime.setInputProvider([](int) { return false; });

    WorldRuntimeCallbacks cb{};
    cb.get_player_chr = [&]() { return kChr; };
    cb.get_physics_module = [&](uintptr_t) { return kModule; };
    cb.read_physics_pos = [&](uintptr_t, float out_f[3]) {
        return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
    };
    cb.safe_read = [&](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
    cb.safe_write = [&](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
    cb.is_game_in_foreground = []() { return true; };

    float rebase_broadcast[3] = {0.0f, 0.0f, 0.0f};
    int rebase_calls = 0;
    cb.on_origin_rebase = [&](float rx, float ry, float rz) {
        rebase_broadcast[0] = rx;
        rebase_broadcast[1] = ry;
        rebase_broadcast[2] = rz;
        ++rebase_calls;
    };
    runtime.setCallbacks(cb);

    auto& ctx = RuntimeContext::Instance();
    ctx.resetForTesting();
    ctx.mode().mc_mode.store(true);
    ctx.mode().block_collision.store(true);

    // Place a block at (-7, 1, -21)
    ctx.world().block_grid.place({-7, 1, -21}, mc::BlockId::Dirt);
    EXPECT_EQ(ctx.world().block_grid.get({-7, 1, -21}), mc::BlockId::Dirt);

    // Frame 1: Player is at (-7.02, 1.17, -32.15)
    float p1[3] = {-7.02f, 1.17f, -32.15f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, p1, sizeof(p1));
    runtime.stepPlayerBlockCollision(ctx);
    EXPECT_EQ(rebase_calls, 0);

    // Frame 2: Engine rebases coordinates by +32.0m on Z!
    // Player is now read at (-7.02, 1.11, -0.15) (rebase of +32.0m on Z)
    float p2[3] = {-7.02f, 1.11f, -0.15f};
    mem.writeBytes(kModule + layout::kPhysicsPosition, p2, sizeof(p2));
    runtime.stepPlayerBlockCollision(ctx);

    // Rebase callback must have fired with (0, 0, +32)
    EXPECT_EQ(rebase_calls, 1);
    EXPECT_FLOAT_EQ(rebase_broadcast[0], 0.0f);
    EXPECT_FLOAT_EQ(rebase_broadcast[1], 0.0f);
    EXPECT_FLOAT_EQ(rebase_broadcast[2], 32.0f);

    // BlockGrid must have been shifted by (0, 0, +32)
    // Old block at (-7, 1, -21) is gone, new block is at (-7, 1, 11)
    EXPECT_EQ(ctx.world().block_grid.get({-7, 1, -21}), mc::BlockId::Air);
    EXPECT_EQ(ctx.world().block_grid.get({-7, 1, 11}), mc::BlockId::Dirt);

    // Relative distance on Z from player to block:
    // Before: (-21.0f) - (-32.15f) = +11.15f
    // After:  (11.0f)  - (-0.15f)  = +11.15f
    // Relative distance is 100% PRESERVED!
}

// ---- Block movement path isolation (mc_er_blockmove.txt) ------------------------------------------------
// The player on a placed block can be moved by three independent mechanisms: a direct position drive, a
// velocity write (+0x120) and fake-grounded flags (+0x92/+0x1D0/+0x1D1). These tests pin each one behind its
// own switch so the in-game probe can compare them without guessing which path did the work.

namespace {

struct BlockMoveHarness {
    MockMemory mem;
    const uintptr_t kChr = 0x1000;
    const uintptr_t kModule = 0x5000;
    std::map<int, bool> keys;
    std::vector<std::string> logs;
    std::function<void(uintptr_t, bool, RuntimeContext&)> drive;
    bool drive_called = false;

    BlockMoveHarness() {
        auto& runtime = VoxelWorldRuntime::Instance();
        runtime.resetForTesting();
        runtime.setDeltaTimeOverrideForTesting(0.05f);
        runtime.setInputProvider([this](int vk) { return keys[vk]; });

        WorldRuntimeCallbacks cb{};
        cb.get_player_chr = [this]() { return kChr; };
        cb.get_physics_module = [this](uintptr_t) { return kModule; };
        cb.read_physics_pos = [this](uintptr_t, float out_f[3]) {
            return mem.readBytes(kModule + layout::kPhysicsPosition, out_f, sizeof(float) * 3);
        };
        cb.safe_read = [this](uintptr_t a, void* d, size_t s) { return mem.readBytes(a, d, s); };
        cb.safe_write = [this](uintptr_t a, const void* s, size_t sz) { mem.writeBytes(a, s, sz); return true; };
        cb.is_game_in_foreground = []() { return true; };
        cb.read_camera = [](CameraPose& cam) {
            cam.forward[0] = 0.f; cam.forward[1] = 0.f; cam.forward[2] = 1.f;
            cam.right[0] = 1.f; cam.right[1] = 0.f; cam.right[2] = 0.f;
            return true;
        };
        cb.drive_velocity = [this](uintptr_t module, bool force_zero, RuntimeContext& c) {
            drive_called = true;
            if (drive) drive(module, force_zero, c);
        };
        cb.log = [this](const std::string& s) { logs.push_back(s); };
        runtime.setCallbacks(cb);

        auto& ctx = RuntimeContext::Instance();
        ctx.resetForTesting();
        ctx.mode().mc_mode.store(true);
        ctx.mode().block_collision.store(true);
        ctx.mode().block_anchor.store(false);
        ctx.mode().block_walk_speed.store(4.317f);

        {
            std::lock_guard<std::mutex> g(ctx.world().blocks_mutex);
            ctx.world().block_grid.place({0, 0, 0}, mc::BlockId::Stone);
        }
        setFeet(0.5f, 1.0f, 0.5f);
        keys['W'] = true;
    }

    void step() { VoxelWorldRuntime::Instance().stepPlayerBlockCollision(RuntimeContext::Instance()); }
    void setFeet(float x, float y, float z) {
        const float f[3] = {x, y, z};
        mem.writeBytes(kModule + layout::kPhysicsPosition, f, sizeof(f));
    }
    std::array<float, 3> feet() const {
        float f[3]{};
        mem.readBytes(kModule + layout::kPhysicsPosition, f, sizeof(f));
        return {f[0], f[1], f[2]};
    }
};

} // namespace

TEST(EldenRingWorldRuntime, BlockPositionDriveDefaultsOn) {
    BlockMoveHarness h;
    EXPECT_TRUE(RuntimeContext::Instance().mode().block_position_drive.load());
    EXPECT_TRUE(RuntimeContext::Instance().mode().block_drive.load());
    EXPECT_TRUE(RuntimeContext::Instance().mode().block_ground.load());
}

TEST(EldenRingWorldRuntime, BlockPositionDriveMovesThePlayerWhenEnabled) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(true);
    ctx.mode().block_drive.store(false);
    ctx.mode().block_ground.store(false);

    h.step(); // one tick: the direct drive runs on the first tick too (prev_feet is only used for the jump guard)

    const auto f = h.feet();
    EXPECT_NEAR(f[0], 0.5f, 1e-4f);
    EXPECT_NEAR(f[1], 1.0f, 1e-4f);
    EXPECT_NEAR(f[2], 0.5f + 4.317f * 0.05f, 0.02f);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + layout::kPhysicsProxySyncRequest), 1);
}

TEST(EldenRingWorldRuntime, BlockPositionDriveCanBeDisabled) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(false);
    ctx.mode().block_drive.store(false);
    ctx.mode().block_ground.store(false);

    h.step();
    h.step();

    const auto f = h.feet();
    EXPECT_NEAR(f[0], 0.5f, 1e-4f);
    EXPECT_NEAR(f[1], 1.0f, 1e-4f);
    EXPECT_NEAR(f[2], 0.5f, 1e-4f);
    EXPECT_TRUE(ctx.mode().block_supported.load());
}

TEST(EldenRingWorldRuntime, VelocityDriveWritesHorizontalVelocityWithoutMovingPosition) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(false);
    ctx.mode().block_drive.store(true);
    ctx.mode().block_ground.store(true);
    h.drive = [&h](uintptr_t module, bool, RuntimeContext&) {
        const float v[3] = {0.f, 0.f, 4.317f};
        h.mem.writeBytes(module + 0x120, v, sizeof(v));
    };

    h.step();
    h.step();

    EXPECT_TRUE(h.drive_called);
    float v[3]{};
    h.mem.readBytes(h.kModule + 0x120, v, sizeof(v));
    EXPECT_NEAR(v[2], 4.317f, 1e-4f);
    const auto f = h.feet();
    EXPECT_NEAR(f[2], 0.5f, 1e-4f);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x92), 1);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x1D1), 1);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x1D0), 1);
}

TEST(EldenRingWorldRuntime, GroundFlagsAreOptional) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(false);
    ctx.mode().block_drive.store(false);
    ctx.mode().block_ground.store(false);

    h.step();
    h.step();

    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x92), 0);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x1D1), 0);
    EXPECT_EQ(h.mem.read<uint8_t>(h.kModule + 0x1D0), 0);
}

TEST(EldenRingWorldRuntime, BlockMoveProbeLogsWithoutChangingMovement) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(false);
    ctx.mode().block_drive.store(false);
    ctx.mode().block_ground.store(false);
    ctx.mode().block_move_probe.store(true);

    h.step();
    h.step();

    ASSERT_FALSE(h.logs.empty());
    EXPECT_NE(h.logs.back().find("BLOCKMOVE:"), std::string::npos);
    const auto f = h.feet();
    EXPECT_NEAR(f[2], 0.5f, 1e-4f);
}

TEST(EldenRingWorldRuntime, BlockStepDoesNotResolveWhileOurJumpOwnsTheArc) {
    BlockMoveHarness h;
    auto& ctx = RuntimeContext::Instance();
    ctx.mode().block_position_drive.store(true); // even with the position drive on, the jump is the authority
    ctx.player().jump_active.store(true);
    h.setFeet(0.5f, 0.95f, 0.5f); // sunk 5 cm into the block top (top at y = 1.0)

    h.step();

    const auto f = h.feet();
    EXPECT_NEAR(f[0], 0.5f, 1e-4f);
    EXPECT_NEAR(f[1], 0.95f, 1e-4f); // no lift onto the block while the jump is active
    EXPECT_NEAR(f[2], 0.5f, 1e-4f);
}
