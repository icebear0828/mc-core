#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "eldenring_blocks.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_creeper.hpp"
#include "eldenring_los.hpp"
#include "eldenring_orientation.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_shadow.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_tnt.hpp"
#include "eldenring_world.hpp"

namespace eldenring::world {

namespace layout = ::eldenring::live::layout;
using eldenring::live::CameraPose;
using eldenring::live::RayResult;
using eldenring::live::RayCastFn;
using eldenring::live::EnemyInfo;

struct WorldRuntimeCallbacks {
    // Memory and pointer accessors
    std::function<uintptr_t()> get_player_chr;
    std::function<uintptr_t(uintptr_t chr)> get_physics_module;
    std::function<bool(uintptr_t chr, float feet[3])> read_physics_pos;
    std::function<bool(uintptr_t addr, void* dst, size_t size)> safe_read;
    std::function<bool(uintptr_t addr, const void* src, size_t size)> safe_write;

    // Environment & game state
    std::function<bool()> is_game_in_foreground;
    std::function<bool(CameraPose& out_cam)> read_camera;
    std::function<RayResult(const float from[3], const float disp[3])> cast_ray;
    std::function<bool(std::vector<EnemyInfo>& out_enemies)> enumerate_enemies;

    // Combat & damage integration
    std::function<void(uintptr_t chr, int damage)> apply_damage_to_chr;
    std::function<void(const char* sound, float vol)> play_sound;
    std::function<void(int fx_kind, const float pos[3], int count)> spawn_fx;

    // Player locomotion integration on supported blocks
    std::function<void(uintptr_t module, bool force_zero, RuntimeContext& ctx)> drive_velocity;
    std::function<void(uintptr_t chr)> reset_fall_timer;
    std::function<void(uintptr_t chr)> unlock_air_movement;

    // Diagnostics / logging
    std::function<void(const std::string&)> log;
};

class VoxelWorldRuntime {
public:
    static VoxelWorldRuntime& Instance() {
        static VoxelWorldRuntime s_instance;
        return s_instance;
    }

    void setCallbacks(WorldRuntimeCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }

    void setInputProvider(std::function<bool(int vk)> is_key_down) {
        is_key_down_ = std::move(is_key_down);
    }

    bool isKeyDown(int vk) const;

    // Runtime steps
    void stepShadowGround(RuntimeContext& ctx);
    void stepPlayerBlockCollision(RuntimeContext& ctx);
    void stepEnemyBlockCollision(const float player_feet[3], RuntimeContext& ctx);
    void processTntExplosions(const std::vector<EnemyInfo>& enemies, RuntimeContext& ctx);
    void processCreeperExplosions(const std::vector<EnemyInfo>& enemies, const float player_feet[3], RuntimeContext& ctx);
    void triggerTntExplosion(const float center[3], RuntimeContext& ctx);

    // TNT Intent queue management
    void enqueueTntIntent(const float center[3], float radius, float max_damage);
    size_t pendingTntIntentCount() const;

    void resetForTesting();
    void setDeltaTimeOverrideForTesting(float dt) { delta_time_override_ = dt; }

private:
    VoxelWorldRuntime();

    void log(const char* fmt, ...);

    WorldRuntimeCallbacks callbacks_;
    std::function<bool(int vk)> is_key_down_;

    // Player collision state
    bool have_prev_player_{false};
    float prev_player_feet_[3]{0.f, 0.f, 0.f};
    unsigned player_pushes_{0};
    bool anchored_{false};
    float anchor_[2]{0.f, 0.f};
#ifdef _WIN32
    LARGE_INTEGER last_block_qpc_{};
#endif
    float delta_time_override_{0.f};

    // Enemy collision tracking
    unsigned enemy_pushes_{0};
    std::unordered_map<uintptr_t, std::array<float, 3>> prev_enemy_feet_;

    // TNT Intent Queue
    mutable std::mutex tnt_intent_mutex_;
    eldenring::tnt::ErExplosionIntentQueue tnt_damage_intents_;
};

} // namespace eldenring::world
