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
#include "mc/elytra_flight.hpp"
#include "mc/ender_pearl.hpp"
#include "mc/riptide.hpp"
#include "mc/thrown_trident.hpp"
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
    std::function<bool()> has_glider;
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

    // Ender pearl teleport: vanilla's hurtServer(5.0) and PLAYER_TELEPORT sound.
    std::function<void(float mc_damage)> apply_self_damage;
    std::function<void(const char* sound, float volume)> play_sound;
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
    // Minecraft 1.21.8 elytra: 20 Hz fixed step, continuous position writes.
    void stepElytra(float dt, uint64_t now_ms, RuntimeContext& ctx, bool ignore_focus = false);
    bool useElytraFirework(int ticks);
    bool isElytraGliding() const { return elytra_.state().is_gliding; }
    // Vanilla's wing pose blend f0: 1 at level or climbing, falling to 0 in a steep dive (wings fold back).
    float elytraBlend() const;
    // Minecraft 1.21.8 ender pearl: the throw request is queued on the input thread and launched on the
    // next camera step, where the owner's velocity is readable. The hit teleports the player to the
    // pearl's start-of-tick position and holds it for a few frames so the engine accepts the move.
    void requestEnderPearlThrow();
    void stepEnderPearl(float dt, uint64_t now_ms, RuntimeContext& ctx);
    bool isEnderPearlActive() const { return pearl_.active(); }
    bool isEnderPearlTeleportHolding() const { return pearl_hold_; }
    // The in-flight pearl's world position for the renderer; false when no pearl is active.
    bool pearlRenderPos(float out[3]) const;
    // Trident Riptide: 10 ticks of charge then a 20-tick spin dash along the look direction.
    bool startRiptide(int level);
    void stepRiptide(float dt, uint64_t now_ms, RuntimeContext& ctx);
    bool isRiptiding() const { return riptide_.active(); }
    // Plain trident throw (no riptide), used while gliding: an arrow-like projectile that leaves the glide alone.
    bool requestTridentThrow();
    void stepThrownTrident(float dt, uint64_t now_ms, RuntimeContext& ctx);
    bool tridentRenderState(float pos[3], float dir[3]) const;
    // OW rocket punch: forward displacement during state 3's thrust window.
    void stepOwRocketPunch(float dt, RuntimeContext& ctx);

    // Getters and Setters for configuration / state
    bool isFlying() const { return flying_.load(std::memory_order_relaxed); }
    void setFlying(bool f) { flying_.store(f, std::memory_order_relaxed); }
    bool isJumping() const { return jump_controller_.isActive(); }
    // True while one of our movement layers owns the player's position. The block step must not
    // clamp or re-drive it: a sneak/shield clamp on a gliding player snaps them back every frame.
    bool ownsPosition() const {
        return isFlying() || isJumping() || isElytraGliding() || isRiptiding() || isEnderPearlTeleportHolding();
    }
    // The position our own movement layer last wrote (flight, elytra, riptide or a pearl hold).
    // False when the native controller owns the position or our layer has not written yet.
    bool ownPosition(float out[3]) const;

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
    uint64_t last_elytra_log_ms_{0};
    bool ow_dash_active_{false};
    float ow_dash_start_[3]{};

    // Elytra state (y-up, blocks/tick).
    mc::ElytraFlightModel elytra_;
    bool prev_elytra_jump_{false};
    float elytra_accum_{0.f};
    bool elytra_have_last_pos_{false};
    float elytra_last_pos_[3]{};
    float elytra_last_vel_[3]{};
    bool elytra_have_pos_{false};
    float elytra_pos_[3]{};
    bool elytra_have_written_{false};
    float elytra_last_written_[3]{};

    // Ender pearl state (y-up, blocks/tick).
    mc::EnderPearl pearl_;
    std::atomic<bool> pearl_throw_requested_{false};
    float pearl_accum_{0.f};
    bool pearl_hold_{false};
    float pearl_hold_target_[3]{};
    int pearl_hold_frames_{0};
    // After the hold releases, watch for the engine pulling the player back and re-hold a couple of times.
    bool pearl_watch_{false};
    int pearl_watch_frames_{0};
    int pearl_retries_{0};
    bool pearl_have_player_pos_{false};
    float pearl_player_pos_[3]{};
    uint64_t last_pearl_log_ms_{0};

    // Trident Riptide state (y-up, blocks/tick).
    mc::RiptideModel riptide_;
    std::atomic<int> riptide_request_{0}; // level requested by the input thread, consumed on the camera step
    float riptide_accum_{0.f};
    bool riptide_have_pos_{false};
    float riptide_pos_[3]{};
    bool riptide_have_written_{false};
    float riptide_last_written_[3]{};
    bool riptide_was_gliding_{false}; // resume the elytra at the new position when the dash ends
    uint64_t last_riptide_log_ms_{0};

    // Thrown trident projectile (y-up, blocks/tick).
    mc::ThrownTrident thrown_trident_;
    std::atomic<bool> trident_throw_requested_{false};
    float trident_accum_{0.f};
    bool trident_have_player_pos_{false};
    float trident_player_pos_[3]{};
    uint64_t last_trident_log_ms_{0};

    void log(const char* fmt, ...);

    // Jump state
    std::atomic<float> mc_jump_speed_{eldenring::jump::kMcJumpInitialVelocity};
    bool prev_jump_key_{false};
    uint64_t jump_start_ms_{0};
    // A jump needs the key held for a couple of frames. A single-frame spurious edge (input state toggling
    // while the window gains focus, or touching a block) used to start a jump the player never asked for.
    uint64_t jump_press_ms_{0};
    bool jump_consumed_{false};
    eldenring::jump::MinecraftJumpController jump_controller_;

    // Sprint & fall timer state
    std::atomic<float> sprint_boost_{2.5f};
    std::atomic<bool> fall_reset_{true};
    std::atomic<float> fall_hold_{1.0f};
    std::atomic<float> pre_fall_t_{0.f};
};

} // namespace eldenring::movement
