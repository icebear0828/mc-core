#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "eldenring_action_lock.hpp"
#include "eldenring_blocks.hpp"
#include "eldenring_camera.hpp"
#include "eldenring_flight.hpp"
#include "eldenring_jump.hpp"
#include "eldenring_los.hpp"
#include "eldenring_orientation.hpp"
#include "eldenring_runtime_context.hpp"
#include "eldenring_shield.hpp"
#include "eldenring_world.hpp"

namespace eldenring::movement {

namespace layout = ::eldenring::live::layout;

using eldenring::live::CameraPose;
using eldenring::live::RayResult;

struct LocomotionCallbacks {
    // Memory and pointer accessors
    std::function<uintptr_t()> get_player_chr;
    std::function<uintptr_t(uintptr_t chr)> get_physics_module;
    std::function<bool(uintptr_t addr, void* dst, size_t size)> safe_read;
    std::function<bool(uintptr_t addr, const void* src, size_t size)> safe_write;

    // Environment & game state
    std::function<bool()> is_game_in_foreground;
    std::function<bool()> want_suppress;
    std::function<bool()> is_player_dead;
    std::function<bool()> is_player_airborne;
    std::function<bool(CameraPose& out_cam)> read_camera;
    std::function<RayResult(const float from[3], const float disp[3])> cast_ray;

    // Voxel world interaction
    std::function<bool(const float feet[3])> is_supported_by_block;
    std::function<blocks::Resolve(const float pos[3])> resolve_block;
    std::function<blocks::Resolve(const float prev[3], const float curr[3])> resolve_block_swept;
    std::function<blocks::SneakMoveResult(const float feet[3], float dx, float dz)> clamp_sneak;
    std::function<bool(float target_x, float feet_y, float target_z)> check_sprint_collision;

    // Diagnostics / logging
    std::function<void(const std::string&)> log;
};

class PlayerLocomotion {
public:
    static PlayerLocomotion& Instance() {
        static PlayerLocomotion s_instance;
        return s_instance;
    }

    void setCallbacks(LocomotionCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }

    void setInputProvider(std::function<bool(int vk)> is_key_down) {
        is_key_down_ = std::move(is_key_down);
    }

    // Module resolution & fall timer utilities
    uintptr_t fallModuleOf(uintptr_t chr) const;
    uintptr_t actionFlagModuleOf(uintptr_t chr) const;
    void unlockAirMovement(uintptr_t chr) const;
    float fallTimer(uintptr_t chr) const;
    void writeFallTimer(uintptr_t chr, float value);
    void resetFallTimer(uintptr_t chr);
    void landFallTimer(uintptr_t chr);

    // Kinematic updates
    void driveVelocity(uintptr_t module, bool force_zero, RuntimeContext& ctx);
    void stepGroundSprint(float dt, RuntimeContext& ctx);
    void stepJump(float dt, uint64_t now_ms, RuntimeContext& ctx);
    void stepFlight(float dt, uint64_t now_ms, RuntimeContext& ctx);

    // Getters and Setters for configuration / state
    bool isFlying() const { return flying_.load(std::memory_order_relaxed); }
    void setFlying(bool f) { flying_.store(f, std::memory_order_relaxed); }

    bool isMcJumpEnabled() const { return mc_jump_.load(std::memory_order_relaxed); }
    void setMcJumpEnabled(bool v) { mc_jump_.store(v, std::memory_order_relaxed); }

    int mcJumpVk() const { return mc_jump_vk_.load(std::memory_order_relaxed); }
    void setMcJumpVk(int vk) { mc_jump_vk_.store(vk, std::memory_order_relaxed); }

    float mcJumpSpeed() const { return mc_jump_speed_.load(std::memory_order_relaxed); }
    void setMcJumpSpeed(float s) { mc_jump_speed_.store(s, std::memory_order_relaxed); }

    float sprintBoost() const { return sprint_boost_.load(std::memory_order_relaxed); }
    void setSprintBoost(float b) { sprint_boost_.store(b, std::memory_order_relaxed); }

    bool fallReset() const { return fall_reset_.load(std::memory_order_relaxed); }
    void setFallReset(bool r) { fall_reset_.store(r, std::memory_order_relaxed); }

    float fallHold() const { return fall_hold_.load(std::memory_order_relaxed); }
    void setFallHold(float h) { fall_hold_.store(h, std::memory_order_relaxed); }

    float preFallTime() const { return pre_fall_t_.load(std::memory_order_relaxed); }

    float flyHorizontalSpeed() const { return fly_h_speed_.load(std::memory_order_relaxed); }
    void setFlyHorizontalSpeed(float s) { fly_h_speed_.store(s, std::memory_order_relaxed); }

    float flyVerticalSpeed() const { return fly_v_speed_.load(std::memory_order_relaxed); }
    void setFlyVerticalSpeed(float s) { fly_v_speed_.store(s, std::memory_order_relaxed); }

    float flyYaw() const { return fly_yaw_.load(std::memory_order_relaxed); }
    void setFlyYaw(float y) { fly_yaw_.store(y, std::memory_order_relaxed); }

    bool flySync() const { return fly_sync_.load(std::memory_order_relaxed); }
    void setFlySync(bool s) { fly_sync_.store(s, std::memory_order_relaxed); }

    bool isKeyDown(int vk) const;

    const eldenring::jump::MinecraftJumpController& jumpController() const { return jump_controller_; }
    eldenring::jump::MinecraftJumpController& jumpController() { return jump_controller_; }

    void resetForTesting();

private:
    PlayerLocomotion();

    LocomotionCallbacks callbacks_;
    std::function<bool(int vk)> is_key_down_;

    // Flight state
    std::atomic<bool> flying_{false};
    std::atomic<float> fly_h_speed_{flight::kHorizontalSpeed};
    std::atomic<float> fly_v_speed_{flight::kVerticalSpeed};
    std::atomic<float> fly_yaw_{0.f};
    std::atomic<bool> fly_sync_{true};
    bool fly_have_written_{false};
    float fly_own_[3]{0.f, 0.f, 0.f};
    float fly_last_written_[3]{0.f, 0.f, 0.f};
    unsigned fly_rebases_{0};
    flight::DoubleTap flight_tap_;
    uint64_t last_flight_log_ms_{0};

    void log(const char* fmt, ...);

    // Jump state
    std::atomic<bool> mc_jump_{true};
    std::atomic<int> mc_jump_vk_{0x20}; // VK_SPACE
    std::atomic<float> mc_jump_speed_{eldenring::jump::kMcJumpInitialVelocity};
    bool prev_jump_key_{false};
    uint64_t jump_start_ms_{0};
    eldenring::jump::MinecraftJumpController jump_controller_;

    // Sprint & fall timer state
    std::atomic<float> sprint_boost_{2.5f};
    std::atomic<bool> fall_reset_{true};
    std::atomic<float> fall_hold_{1.0f};
    std::atomic<float> pre_fall_t_{0.f};
};

} // namespace eldenring::movement
