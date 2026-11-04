#include "eldenring_locomotion.hpp"
#include "eldenring_ow_dash.hpp"

#include <algorithm>
#include <cstdarg>
#include <cmath>
#include <cstring>
#include <random>

namespace eldenring::movement {

void PlayerLocomotion::log(const char* fmt, ...) {
    if (!callbacks_.log) return;
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    callbacks_.log(std::string(buf));
}

PlayerLocomotion::PlayerLocomotion() {
    is_key_down_ = [](int vk) -> bool {
#ifdef _WIN32
        return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
        (void)vk;
        return false;
#endif
    };
}

void PlayerLocomotion::resetForTesting() {
    flying_.store(false, std::memory_order_relaxed);
    fly_have_written_ = false;
    std::memset(fly_own_, 0, sizeof(fly_own_));
    std::memset(fly_last_written_, 0, sizeof(fly_last_written_));
    fly_rebases_ = 0;
    flight_tap_.reset();
    last_flight_log_ms_ = 0;
    last_elytra_log_ms_ = 0;
    elytra_ = mc::ElytraFlightModel{};
    prev_elytra_jump_ = false;
    elytra_accum_ = 0.f;
    elytra_have_last_pos_ = false;
    elytra_have_pos_ = false;
    elytra_have_written_ = false;

    mc_jump_speed_.store(eldenring::jump::kMcJumpInitialVelocity, std::memory_order_relaxed);
    prev_jump_key_ = false;
    jump_start_ms_ = 0;
    jump_press_ms_ = 0;
    jump_consumed_ = false;
    jump_controller_.cancel();

    sprint_boost_.store(2.5f, std::memory_order_relaxed);
    fall_reset_.store(true, std::memory_order_relaxed);
    fall_hold_.store(1.0f, std::memory_order_relaxed);
    pre_fall_t_.store(0.f, std::memory_order_relaxed);
}

bool PlayerLocomotion::isKeyDown(int vk) const {
    if (is_key_down_) {
        return is_key_down_(vk);
    }
#ifdef _WIN32
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
#else
    (void)vk;
    return false;
#endif
}

bool PlayerLocomotion::ownPosition(float out[3]) const {
    if (flying_.load(std::memory_order_relaxed) && fly_have_written_) {
        std::memcpy(out, fly_own_, sizeof(fly_own_));
        return true;
    }
    if (elytra_.state().is_gliding && elytra_have_written_) {
        std::memcpy(out, elytra_pos_, sizeof(elytra_pos_));
        return true;
    }
    if (riptide_.active() && riptide_have_written_) {
        std::memcpy(out, riptide_pos_, sizeof(riptide_pos_));
        return true;
    }
    if (pearl_hold_) {
        std::memcpy(out, pearl_hold_target_, sizeof(pearl_hold_target_));
        return true;
    }
    return false;
}

uintptr_t PlayerLocomotion::fallModuleOf(uintptr_t chr) const {
    if (chr == 0 || !callbacks_.safe_read) return 0;
    uint64_t container = 0, fall = 0;
    if (!callbacks_.safe_read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !callbacks_.safe_read(static_cast<uintptr_t>(container) + 0x70, &fall, sizeof(fall))) {
        return 0;
    }
    return static_cast<uintptr_t>(fall);
}

uintptr_t PlayerLocomotion::actionFlagModuleOf(uintptr_t chr) const {
    if (chr == 0 || !callbacks_.safe_read) return 0;
    uint64_t container = 0, act = 0;
    if (!callbacks_.safe_read(chr + layout::kModuleContainerInChrIns, &container, sizeof(container)) || container == 0 ||
        !callbacks_.safe_read(static_cast<uintptr_t>(container) + 0x08, &act, sizeof(act))) {
        return 0;
    }
    return static_cast<uintptr_t>(act);
}

void PlayerLocomotion::unlockAirMovement(uintptr_t chr) const {
    if (const uintptr_t act = actionFlagModuleOf(chr)) {
        uint64_t flags = 0;
        if (callbacks_.safe_read && callbacks_.safe_read(act + 0x40, &flags, sizeof(flags)) &&
            eldenring::action::hasAirLocks(flags)) {
            const uint64_t cleared = eldenring::action::clearAirLocks(flags);
            if (callbacks_.safe_write) {
                callbacks_.safe_write(act + 0x40, &cleared, sizeof(cleared));
            }
        }
    }
}

float PlayerLocomotion::fallTimer(uintptr_t chr) const {
    float t = -1.f;
    if (const uintptr_t fall = fallModuleOf(chr)) {
        if (callbacks_.safe_read) {
            callbacks_.safe_read(fall + 0x18, &t, sizeof(t));
        }
    }
    return t;
}

void PlayerLocomotion::writeFallTimer(uintptr_t chr, float value) {
    if (const uintptr_t fall = fallModuleOf(chr)) {
        pre_fall_t_.store(fallTimer(chr), std::memory_order_relaxed);
        if (callbacks_.safe_write) {
            callbacks_.safe_write(fall + 0x18, &value, sizeof(value));
        }
    }
}

void PlayerLocomotion::resetFallTimer(uintptr_t chr) {
    if (!fall_reset_.load(std::memory_order_relaxed)) return;
    const float cur_t = fallTimer(chr);
    if (cur_t > fall_hold_.load(std::memory_order_relaxed) || eldenring::action::shouldResetFallTimer(cur_t, 0.2f)) {
        writeFallTimer(chr, 0.f);
    }
}

void PlayerLocomotion::landFallTimer(uintptr_t chr) {
    if (fall_reset_.load(std::memory_order_relaxed)) {
        writeFallTimer(chr, 0.f);
    }
    unlockAirMovement(chr);
}

void PlayerLocomotion::driveVelocity(uintptr_t module, bool force_zero, RuntimeContext& ctx) {
    float vx = 0.f, vz = 0.f;
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    if (!force_zero && fg) {
        const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
        const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
        const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                    isKeyDown('X');
        const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                              ctx.combat().shield.isBlocking();
        const bool sneaking = sneak_key_down;
        const bool sprinting = !sneaking && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
        ctx.mode().sneak_active.store(sneaking, std::memory_order_relaxed);

        const float target_speed = eldenring::shield::effectiveMovementSpeed(
            ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
            ctx.mode().sprint_speed.load(std::memory_order_relaxed),
            ctx.mode().sneak_speed.load(std::memory_order_relaxed),
            sneaking, sprinting, blocking);

        CameraPose cam{};
        if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
            float dir[2] = {0.f, 0.f};
            const bool moving = eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir);
            if (moving) {
                vx = dir[0] * target_speed;
                vz = dir[1] * target_speed;
                if (sneaking) {
                    float cur_feet[3] = {};
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsPosition, cur_feet, sizeof(cur_feet))) {
                        const float test_dx = vx * 0.05f;
                        const float test_dz = vz * 0.05f;
                        if (callbacks_.clamp_sneak) {
                            const auto test_res = callbacks_.clamp_sneak(cur_feet, test_dx, test_dz);
                            if (test_res.clamped_x) vx = 0.f;
                            if (test_res.clamped_z) vz = 0.f;
                        }
                    }
                }

                float target_yaw = std::atan2(dir[0], dir[1]);
                if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                    float fx = cam.forward[0], fz = cam.forward[2];
                    if (fx * fx + fz * fz > 1e-4f) target_yaw = std::atan2(fx, fz);
                }

                const bool instant = ctx.mode().instant_turn.load(std::memory_order_relaxed);
                float final_yaw = target_yaw;
                if (!instant) {
                    float curr_q[4] = {};
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsOrientation, curr_q, sizeof(curr_q))) {
                        const float curr_yaw = eldenring::motion::headingFromCharacterQuat(
                            eldenring::motion::OrientationQuat{curr_q[0], curr_q[1], curr_q[2], curr_q[3]});
                        final_yaw = eldenring::motion::stepYaw(curr_yaw, target_yaw, 0.0166667f, ctx.mode().turn_speed.load(), false);
                    }
                }
                const auto out_q = eldenring::motion::characterQuatFromHeading(final_yaw);
                const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
                if (callbacks_.safe_write) {
                    callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
                }
            } else if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                float fx = cam.forward[0], fz = cam.forward[2];
                if (fx * fx + fz * fz > 1e-4f) {
                    const float target_yaw = std::atan2(fx, fz);
                    float curr_q[4] = {};
                    bool should_update = true;
                    if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsOrientation, curr_q, sizeof(curr_q))) {
                        const float curr_yaw = eldenring::motion::headingFromCharacterQuat(
                            eldenring::motion::OrientationQuat{curr_q[0], curr_q[1], curr_q[2], curr_q[3]});
                        const float diff = std::abs(eldenring::motion::normalizeAngle(target_yaw - curr_yaw));
                        if (diff < 0.02f) should_update = false;
                    }
                    if (should_update && callbacks_.safe_write) {
                        const auto out_q = eldenring::motion::characterQuatFromHeading(target_yaw);
                        const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
                        callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
                    }
                }
            }
        }
    }
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + 0x120, &vx, sizeof(vx));
        callbacks_.safe_write(module + 0x120 + 8, &vz, sizeof(vz));
    }
}

void PlayerLocomotion::stepGroundSprint(float dt, RuntimeContext& ctx) {
    const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                          ctx.combat().shield.isBlocking();
    if (!ctx.player().sprint_active.load(std::memory_order_relaxed) ||
        !ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        blocking) {
        return;
    }
    if (isFlying() || (callbacks_.is_player_airborne && callbacks_.is_player_airborne()) ||
        (callbacks_.is_player_dead && callbacks_.is_player_dead())) {
        return;
    }
    if ((callbacks_.is_game_in_foreground && !callbacks_.is_game_in_foreground()) ||
        (callbacks_.want_suppress && !callbacks_.want_suppress())) {
        return;
    }

    const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
    const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
    if (std::abs(fwd_key) < 0.1f && std::abs(right_key) < 0.1f) return;

    CameraPose cam{};
    if (!callbacks_.read_camera || !callbacks_.read_camera(cam)) return;

    float dir[2] = {0.f, 0.f};
    if (!eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir)) return;

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0) return;
    const uintptr_t module = callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0) return;

    float feet[3] = {};
    if (!callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, feet, sizeof(feet))) return;

    const float boost = sprint_boost_.load(std::memory_order_relaxed);
    const float clamped_dt = std::clamp(dt, 0.001f, 0.05f);
    const float dx = dir[0] * boost * clamped_dt;
    const float dz = dir[1] * boost * clamped_dt;

    const float target_x = feet[0] + dx;
    const float target_z = feet[2] + dz;

    if (callbacks_.check_sprint_collision && callbacks_.check_sprint_collision(target_x, feet[1], target_z)) {
        return;
    }

    feet[0] = target_x;
    feet[2] = target_z;
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, feet, sizeof(feet));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, feet, sizeof(feet));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));
    }
}

void PlayerLocomotion::stepJump(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    if (!ctx.mode().mc_jump.load(std::memory_order_relaxed) || !ctx.mode().mc_mode.load(std::memory_order_relaxed) ||
        ctx.ui().inv_open.load(std::memory_order_relaxed) || !fg) {
        prev_jump_key_ = false;
        jump_consumed_ = true; // a key held across a menu/focus change must be released before it jumps again
        jump_controller_.cancel();
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }

    // Creative flight owns the position; a space press must not also start a jump (the double-tap
    // that enables flight would otherwise write a jump arc on the same frame).
    if (isFlying()) {
        prev_jump_key_ = false;
        jump_consumed_ = true;
        jump_controller_.cancel();
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }

    // The elytra owns the position and velocity. If the jump controller from
    // the takeoff were still active it would write the same fields every frame
    // and fight the glide (observed as severe camera shake).
    if (elytra_.state().is_gliding) {
        prev_jump_key_ = false;
        jump_consumed_ = true;
        jump_controller_.cancel();
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    if (player == 0) {
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }
    const uintptr_t module = callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0) {
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }

    float feet[3] = {};
    if (!callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, feet, sizeof(feet))) {
        ctx.player().jump_active.store(false, std::memory_order_relaxed);
        return;
    }

    const bool down = isKeyDown(ctx.mode().mc_jump_vk.load(std::memory_order_relaxed));
    if (down && !prev_jump_key_) {
        jump_press_ms_ = now_ms;
        jump_consumed_ = false;
    }
    if (!down) jump_consumed_ = false;
    prev_jump_key_ = down;
    const bool held = down && jump_press_ms_ != 0 && now_ms >= jump_press_ms_ &&
                      (now_ms - jump_press_ms_) >= 25;
    const bool edge = held && !jump_consumed_;

    uint8_t ground = 0;
    if (callbacks_.safe_read) {
        callbacks_.safe_read(module + 0x92, &ground, 1);
    }
    const uint8_t zero = 0, one = 1;

    if (!jump_controller_.isActive()) {
        bool supported_by_block = false;
        if (callbacks_.is_supported_by_block) {
            supported_by_block = callbacks_.is_supported_by_block(feet);
        }
        if (!supported_by_block) {
            supported_by_block = ctx.mode().block_supported.load(std::memory_order_relaxed);
        }

        const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
        const bool can_jump = eldenring::jump::canTakeOff(
            ground == 1,
            supported_by_block,
            ctx.mode().on_ground.load(std::memory_order_relaxed),
            dead);

        if (edge && can_jump) {
            const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
            const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
            const bool sneak_key_down = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                                        isKeyDown('X');
            const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                                  ctx.combat().shield.isBlocking();
            const bool sneaking = sneak_key_down;
            const bool sprinting = !sneaking && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
            const float target_speed = eldenring::shield::effectiveMovementSpeed(
                ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
                ctx.mode().sprint_speed.load(std::memory_order_relaxed),
                ctx.mode().sneak_speed.load(std::memory_order_relaxed),
                sneaking, sprinting, blocking);

            float init_vx = 0.f, init_vz = 0.f;
            CameraPose cam{};
            if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
                float dir[2] = {0.f, 0.f};
                if (eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir)) {
                    init_vx = dir[0] * target_speed;
                    init_vz = dir[1] * target_speed;
                }
            }

            jump_controller_.start(feet[1], init_vx, init_vz, sprinting, mc_jump_speed_.load(std::memory_order_relaxed));
            jump_start_ms_ = now_ms;
            jump_consumed_ = true;
            ctx.player().jump_active.store(true, std::memory_order_relaxed);
            log("mcjump: start at y=%.3f, v=%.2f m/s, sprint=%d, h_speed=%.2f m/s",
                feet[1], jump_controller_.vy(), sprinting ? 1 : 0,
                std::sqrt(jump_controller_.vx() * jump_controller_.vx() + jump_controller_.vz() * jump_controller_.vz()));
        } else {
            return;
        }
    }

    unlockAirMovement(player);
    resetFallTimer(player);

    // The jump controller writes the vertical position every tick. Zero the game's vertical velocity and fall
    // speed while the arc is ours, otherwise the engine adds its own jump/gravity on top of the arc (the
    // "double jump / frame skip" seen when landing on a block).
    if (callbacks_.safe_write) {
        const float zero_vy = 0.f;
        callbacks_.safe_write(module + 0x120 + 4, &zero_vy, sizeof(zero_vy));
        callbacks_.safe_write(module + 0x1B8, &zero_vy, sizeof(zero_vy));
    }

    const float fwd_key = (isKeyDown('W') ? 1.f : 0.f) - (isKeyDown('S') ? 1.f : 0.f);
    const float right_key = (isKeyDown('D') ? 1.f : 0.f) - (isKeyDown('A') ? 1.f : 0.f);
    const bool steer_sneak = isKeyDown(ctx.mode().sneak_vk.load(std::memory_order_relaxed)) ||
                             isKeyDown('X');
    const bool blocking = ctx.mode().shield_blocking.load(std::memory_order_relaxed) ||
                          ctx.combat().shield.isBlocking();
    const bool sprinting = !steer_sneak && !blocking && ctx.player().sprint_active.load(std::memory_order_relaxed);
    const float steer_speed = eldenring::shield::effectiveMovementSpeed(
        ctx.mode().block_walk_speed.load(std::memory_order_relaxed),
        ctx.mode().sprint_speed.load(std::memory_order_relaxed),
        ctx.mode().sneak_speed.load(std::memory_order_relaxed),
        steer_sneak, sprinting, blocking);

    float dir[2] = {0.f, 0.f};
    bool has_steer = false;
    CameraPose cam{};
    if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
        has_steer = eldenring::motion::computeInputDirection(fwd_key, right_key, cam.forward, cam.right, dir);
    }

    const auto res = jump_controller_.update(dt, dir[0], dir[1], steer_speed, has_steer);
    float ny = res.y;
    float pos[3] = {feet[0], ny, feet[2]};
    bool landed = false;
    bool landed_on_block = false;

    if (callbacks_.resolve_block) {
        const blocks::Resolve r = callbacks_.resolve_block(pos);
        if (r.moved && r.feet[1] < ny - 1e-4f) {
            pos[1] = r.feet[1];
            ny = r.feet[1];
            jump_controller_.hitCeiling(r.feet[1]);
        } else if (res.vy <= 0.f && callbacks_.is_supported_by_block && callbacks_.is_supported_by_block(pos)) {
            landed = true;
            landed_on_block = true;
        } else if (res.vy <= 0.f && callbacks_.resolve_block_swept) {
            const float prev_pos[3] = {feet[0], jump_controller_.currentY() - res.vy * dt, feet[2]};
            const blocks::Resolve sw = callbacks_.resolve_block_swept(prev_pos, pos);
            if (sw.moved && sw.standing) {
                pos[1] = sw.feet[1];
                ny = sw.feet[1];
                landed = true;
                landed_on_block = true;
            }
        }
    }

    if (!landed && res.vy <= 0.f && callbacks_.cast_ray) {
        const float from[3] = {feet[0], ny + 0.5f, feet[2]};
        const float drop = std::max(2.0f, (jump_controller_.currentY() - jump_controller_.startY()) + 2.5f);
        const float disp[3] = {0.f, -drop, 0.f};
        const RayResult r = callbacks_.cast_ray(from, disp);
        if (r.ok && r.hit) {
            const float ground_y = from[1] - r.fraction * drop;
            if (ny <= ground_y + 0.02f) {
                pos[1] = ground_y;
                ny = ground_y;
                landed = true;
                landed_on_block = false;
            }
        } else if (ny < jump_controller_.startY() - 3.0f) {
            log("mcjump: no ground under the arc, native game takes over");
            jump_controller_.cancel();
            ctx.player().jump_active.store(false, std::memory_order_relaxed);
            if (callbacks_.safe_write) callbacks_.safe_write(module + 0x92, &zero, 1);
            return;
        }
    }

    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, pos, sizeof(pos));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, pos, sizeof(pos));
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));

        float vx = res.vx;
        float vz = res.vz;
        callbacks_.safe_write(module + 0x120, &vx, sizeof(vx));
        callbacks_.safe_write(module + 0x120 + 8, &vz, sizeof(vz));

        if (has_steer) {
            float target_yaw = std::atan2(dir[0], dir[1]);
            if (ctx.mode().modOwns(mc::ControlDomain::Camera)) {
                float fx = cam.forward[0], fz = cam.forward[2];
                if (fx * fx + fz * fz > 1e-4f) target_yaw = std::atan2(fx, fz);
            }
            const auto out_q = eldenring::motion::characterQuatFromHeading(target_yaw);
            const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
            callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
        }

        if (landed) {
            jump_controller_.land(ny);
            ctx.player().jump_active.store(false, std::memory_order_relaxed);
            const auto flags = eldenring::jump::computeLandingFlags(landed_on_block);
            const uint8_t ground_byte = flags.on_ground_flag ? 1 : 0;
            const uint8_t air_byte = flags.airborne_flag ? 1 : 0;
            const uint8_t supp_byte = flags.support_flag ? 1 : 0;
            callbacks_.safe_write(module + 0x92, &ground_byte, 1);
            callbacks_.safe_write(module + 0x1D1, &supp_byte, 1);
            callbacks_.safe_write(module + 0x1D0, &air_byte, 1);
            const float zero_f = 0.f;
            callbacks_.safe_write(module + 0x1B8, &zero_f, sizeof(zero_f));
            callbacks_.safe_write(module + 0x120 + 4, &zero_f, sizeof(zero_f));
            log("mcjump: landed %s, peak %.2f m, %.2f s in the air, fall timer was %.2f",
                landed_on_block ? "on block" : "on terrain", jump_controller_.peak(),
                static_cast<float>(now_ms - jump_start_ms_) / 1000.f, fallTimer(player));
            landFallTimer(player);
            unlockAirMovement(player);
        } else {
            callbacks_.safe_write(module + 0x92, &zero, 1);
            callbacks_.safe_write(module + 0x1D0, &one, 1);
        }
    }
}

void PlayerLocomotion::stepFlight(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) &&
                         ctx.currentGameMode() == creative::GameMode::Creative &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed) &&
                         fg && !dead;
    if (!allowed) {
        flight_tap_.reset();
        fly_have_written_ = false;
        if (flying_.exchange(false)) {
            log("flight: stopped (creative mode, MC mode, focus, inventory or death changed)");
        }
        return;
    }

    if (flight_tap_.update(isKeyDown(VK_SPACE), now_ms)) {
        const bool on = !flying_.load(std::memory_order_relaxed);
        flying_.store(on, std::memory_order_relaxed);
        fly_have_written_ = false;
        if (on) {
            // The first press of the double-tap may have started a jump; drop it so it cannot
            // write a position on the frame flight starts.
            jump_controller_.cancel();
            prev_jump_key_ = false;
            ctx.player().jump_active.store(false, std::memory_order_relaxed);
        }
        log("flight: %s", on ? "started" : "stopped");
    }

    if (!flying_.load(std::memory_order_relaxed)) return;
    if (pearl_hold_) return; // the ender pearl teleport owns the position for these frames
    if (riptide_.active()) return; // the riptide dash owns the position for its 20 ticks

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    float read_pos[3] = {};
    if (module == 0 || !callbacks_.safe_read || !callbacks_.safe_read(module + layout::kPhysicsPosition, read_pos, sizeof(read_pos))) {
        fly_have_written_ = false;
        return;
    }

    if (!fly_have_written_) {
        std::memcpy(fly_own_, read_pos, sizeof(fly_own_));
    } else if (flight::positionOverwritten(fly_last_written_, read_pos, 3.f)) {
        const float before[3] = {fly_own_[0], fly_own_[1], fly_own_[2]};
        flight::followRebase(fly_own_, fly_last_written_, read_pos);
        if (++fly_rebases_ <= 20) {
            log("flight: coordinates re-based by the game: wrote=(%.2f %.2f %.2f) read=(%.2f %.2f %.2f) own (%.2f %.2f %.2f) -> (%.2f %.2f %.2f)",
                fly_last_written_[0], fly_last_written_[1], fly_last_written_[2],
                read_pos[0], read_pos[1], read_pos[2],
                before[0], before[1], before[2],
                fly_own_[0], fly_own_[1], fly_own_[2]);
        }
    }

    flight::Keys keys{};
    keys.forward = isKeyDown('W');
    keys.back = isKeyDown('S');
    keys.left = isKeyDown('A');
    keys.right = isKeyDown('D');
    keys.up = isKeyDown(VK_SPACE);
    keys.down = isKeyDown(VK_SHIFT);

    flight::Heading heading{};
    CameraPose cam{};
    const bool have_heading = callbacks_.read_camera && callbacks_.read_camera(cam) &&
                              flight::headingFromCamera(cam.forward, cam.right, heading);
    if (!have_heading) {
        keys.forward = keys.back = keys.left = keys.right = false;
    }

    flight::step(fly_own_, keys, heading, dt, fly_h_speed_.load(std::memory_order_relaxed), fly_v_speed_.load(std::memory_order_relaxed));

    if (have_heading) {
        const float fly_yaw = flight::bodyYaw(heading);
        fly_yaw_.store(fly_yaw, std::memory_order_relaxed);
        const auto out_q = eldenring::motion::characterQuatFromHeading(fly_yaw);
        const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
        if (callbacks_.safe_write) {
            callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
        }
    }

    writeFallTimer(player, 0.f);
    unlockAirMovement(player);

    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, fly_own_, sizeof(fly_own_));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, fly_own_, sizeof(fly_own_));

        uint8_t sync_before = 0;
        if (callbacks_.safe_read) callbacks_.safe_read(module + layout::kPhysicsProxySyncRequest, &sync_before, 1);
        if (fly_sync_.load(std::memory_order_relaxed)) {
            const uint8_t one = 1;
            callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, 1);
        }

        if (now_ms - last_flight_log_ms_ >= 1000) {
            last_flight_log_ms_ = now_ms;
            log("flight: pos=(%.2f %.2f %.2f) read_back=(%.2f %.2f %.2f) fall_t=%.2f sync91_before=%u keys[%d%d%d%d up=%d down=%d] dt=%.4f",
                fly_own_[0], fly_own_[1], fly_own_[2], read_pos[0], read_pos[1], read_pos[2],
                fallTimer(player), static_cast<unsigned>(sync_before),
                keys.forward ? 1 : 0, keys.back ? 1 : 0, keys.left ? 1 : 0, keys.right ? 1 : 0,
                keys.up ? 1 : 0, keys.down ? 1 : 0, dt);
        }
    }

    std::memcpy(fly_last_written_, fly_own_, sizeof(fly_last_written_));
    fly_have_written_ = true;
}

bool PlayerLocomotion::useElytraFirework(int ticks) {
    if (!elytra_.state().is_gliding) return false;
    if (!elytra_.useFireworkBoost(ticks)) return false;
    // Vanilla plays FIREWORK_ROCKET_LAUNCH on the rocket's first tick, whichever hand used it.
    if (callbacks_.play_sound) callbacks_.play_sound("entity.firework_rocket.launch", 1.0f);
    return true;
}

float PlayerLocomotion::elytraBlend() const {
    // Vanilla PlayerElytraAnimationState: f = 1 while not descending, else 1 - (-vy/|v|)^1.5.
    const mc::Vec3& v = elytra_.state().velocity;
    const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len <= 1e-6f || v.y >= 0.f) return 1.f;
    return 1.f - std::pow(-v.y / len, 1.5f);
}

void PlayerLocomotion::stepElytra(float dt, uint64_t now_ms, RuntimeContext& ctx, bool ignore_focus) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool boost_before = ctx.mode().elytra_boosting.load(std::memory_order_relaxed);
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) && (ignore_focus || fg) && !dead &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed) && !isFlying();
    if (!allowed) {
        ctx.mode().elytra_boosting.store(false, std::memory_order_relaxed);
        if (elytra_.state().is_gliding) {
            elytra_.stopGliding();
            ctx.mode().elytra_gliding.store(false, std::memory_order_relaxed);
            log("elytra: stopped (mode/focus/inventory/death/flight)");
        }
        prev_elytra_jump_ = false;
        elytra_have_pos_ = false;
        elytra_have_written_ = false;
        return;
    }
    if (pearl_hold_) return; // the ender pearl teleport owns the position for these frames
    if (riptide_.active()) return; // the riptide dash owns the position for its 20 ticks

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0 || !callbacks_.safe_read) return;

    float pos[3] = {};
    if (!callbacks_.safe_read(module + layout::kPhysicsPosition, pos, sizeof(pos))) return;

    // Host velocity from the position delta: the vanilla glide starts with the
    // current deltaMovement. +0x80 is the previous frame's position. Clamp it:
    // the engine's own gravity pull between ticks is not player motion.
    float last_pos[3] = {};
    if (callbacks_.safe_read(module + layout::kPhysicsPosition + 0x10, last_pos, sizeof(last_pos)) && dt > 1e-4f) {
        const float vx = (pos[0] - last_pos[0]) / dt / 20.f;
        const float vy = (pos[1] - last_pos[1]) / dt / 20.f;
        const float vz = (pos[2] - last_pos[2]) / dt / 20.f;
        const float len = std::sqrt(vx * vx + vy * vy + vz * vz);
        const float dist = std::sqrt((pos[0] - last_pos[0]) * (pos[0] - last_pos[0]) +
                                     (pos[1] - last_pos[1]) * (pos[1] - last_pos[1]) +
                                     (pos[2] - last_pos[2]) * (pos[2] - last_pos[2]));
        if (dist < 5.f && len < 1.f) {
            elytra_last_vel_[0] = vx;
            elytra_last_vel_[1] = vy;
            elytra_last_vel_[2] = vz;
        } else {
            elytra_last_vel_[0] = elytra_last_vel_[1] = elytra_last_vel_[2] = 0.f;
        }
    }

    const bool on_ground = callbacks_.is_player_airborne ? !callbacks_.is_player_airborne() : true;
    const bool has_glider = callbacks_.has_glider ? callbacks_.has_glider() : false;
    const bool jump_down = isKeyDown(VK_SPACE);
    const bool jump_edge = jump_down && !prev_elytra_jump_;
    prev_elytra_jump_ = jump_down;

    if (!elytra_.state().is_gliding) {
        if (jump_edge && mc::ElytraFlightModel::canGlide(on_ground, false, false, has_glider, isFlying())) {
            const mc::Vec3 initial{elytra_last_vel_[0], elytra_last_vel_[1], elytra_last_vel_[2]};
            if (elytra_.startGliding(true, false, initial)) {
                elytra_accum_ = 0.f;
                elytra_have_pos_ = true;
                std::memcpy(elytra_pos_, pos, sizeof(elytra_pos_));
                elytra_have_written_ = true;
                std::memcpy(elytra_last_written_, pos, sizeof(elytra_last_written_));
                jump_controller_.cancel();
                prev_jump_key_ = false;
                ctx.player().jump_active.store(false, std::memory_order_relaxed);
                ctx.mode().elytra_gliding.store(true, std::memory_order_relaxed);
                log("elytra: started v=(%.2f %.2f %.2f) b/t", static_cast<double>(initial.x),
                    static_cast<double>(initial.y), static_cast<double>(initial.z));
            }
        }
        return;
    }

    if (on_ground || !has_glider) {
        elytra_.stopGliding();
        ctx.mode().elytra_gliding.store(false, std::memory_order_relaxed);
        ctx.mode().elytra_boosting.store(false, std::memory_order_relaxed);
        elytra_have_pos_ = false;
        elytra_have_written_ = false;
        log("elytra: stopped (%s)", on_ground ? "ground" : "no glider");
        return;
    }

    if (!elytra_have_pos_ || !elytra_have_written_) {
        std::memcpy(elytra_pos_, pos, sizeof(elytra_pos_));
        std::memcpy(elytra_last_written_, pos, sizeof(elytra_last_written_));
        elytra_have_pos_ = true;
        elytra_have_written_ = true;
    } else if (flight::positionOverwritten(elytra_last_written_, pos, 3.f)) {
        // The game re-bases Havok coordinates by whole 8 m steps. Follow the
        // re-base, but treat any residual jump as an engine teleport/load.
        const float before[3] = {elytra_pos_[0], elytra_pos_[1], elytra_pos_[2]};
        flight::followRebase(elytra_pos_, elytra_last_written_, pos);
        const float dx = elytra_pos_[0] - pos[0];
        const float dy = elytra_pos_[1] - pos[1];
        const float dz = elytra_pos_[2] - pos[2];
        const float residual = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (residual > 5.f) {
            elytra_.stopGliding();
            ctx.mode().elytra_gliding.store(false, std::memory_order_relaxed);
            ctx.mode().elytra_boosting.store(false, std::memory_order_relaxed);
            elytra_have_pos_ = false;
            elytra_have_written_ = false;
            log("elytra: stopped (engine teleported %.1f m)", static_cast<double>(residual));
            return;
        }
        log("elytra: rebase own=(%.2f %.2f %.2f) -> (%.2f %.2f %.2f) read=(%.2f %.2f %.2f)",
            static_cast<double>(before[0]), static_cast<double>(before[1]), static_cast<double>(before[2]),
            static_cast<double>(elytra_pos_[0]), static_cast<double>(elytra_pos_[1]),
            static_cast<double>(elytra_pos_[2]), static_cast<double>(pos[0]), static_cast<double>(pos[1]),
            static_cast<double>(pos[2]));
    }

    CameraPose cam{};
    const bool have_cam = callbacks_.read_camera && callbacks_.read_camera(cam);

    // 20 Hz velocity update (vanilla tick). The position is integrated every
    // frame below: holding a 20 Hz position produced a visible stair-step and
    // let the engine's per-frame gravity pull shake the camera.
    if (dt > 0.2f) dt = 0.2f;
    elytra_accum_ += dt;
    if (elytra_accum_ > 0.25f) elytra_accum_ = 0.05f;
    while (elytra_accum_ >= 0.05f) {
        elytra_accum_ -= 0.05f;
        if (!have_cam) break;
        const mc::Vec3 look{cam.forward[0], cam.forward[1], cam.forward[2]};
        elytra_.updateTick(look, false);
    }

    // The rocket's lerp runs at 20 Hz; the renderer reads this flag to stream the trail and
    // bumps the burst counter once when the last boost tick has been consumed (vanilla's POOF).
    const bool boosting = elytra_.state().firework_ticks_left > 0;
    ctx.mode().elytra_boosting.store(boosting, std::memory_order_relaxed);
    if (boost_before && !boosting) {
        ctx.mode().elytra_firework_burst_seq.fetch_add(1, std::memory_order_relaxed);
    }

    // Per-frame position integration using the current velocity.
    const mc::Vec3 v = elytra_.state().velocity;
    const float frac = std::clamp(dt / 0.05f, 0.f, 2.f);
    const float step[3] = {v.x * frac, v.y * frac, v.z * frac};
    float actual[3] = {elytra_pos_[0] + step[0], elytra_pos_[1] + step[1], elytra_pos_[2] + step[2]};
    bool hit_x = false;
    bool hit_z = false;
    if (callbacks_.cast_ray) {
        float fraction = 1.f;
        const float heights[3] = {0.4f, 1.0f, 1.6f};
        for (float h : heights) {
            const float from[3] = {elytra_pos_[0], elytra_pos_[1] + h, elytra_pos_[2]};
            const RayResult hit = callbacks_.cast_ray(from, step);
            if (hit.ok && hit.hit) fraction = std::min(fraction, std::max(0.f, hit.fraction - 0.05f));
        }
        if (fraction < 1.f) {
            actual[0] = elytra_pos_[0] + step[0] * fraction;
            actual[1] = elytra_pos_[1] + step[1] * fraction;
            actual[2] = elytra_pos_[2] + step[2] * fraction;
            hit_x = std::fabs(step[0] * fraction - step[0]) > 1e-4f;
            hit_z = std::fabs(step[2] * fraction - step[2]) > 1e-4f;
        }
    }
    if (hit_x || hit_z) {
        const mc::Vec3 after = mc::ElytraFlightModel::afterHorizontalCollision(v, hit_x, hit_z);
        const float damage = elytra_.collisionDamage(v, after);
        mc::ElytraFlightState state = elytra_.state();
        state.velocity = after;
        elytra_.setState(state);
        if (damage > 0.f) log("elytra: wall hit damage=%.2f", static_cast<double>(damage));
    }
    std::memcpy(elytra_pos_, actual, sizeof(elytra_pos_));

    // Write every frame, not only at 20 Hz: the engine pulls the player down
    // between ticks (observed +0x80 - +0x70 = 0.836 m per frame).
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, elytra_pos_, sizeof(elytra_pos_));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, elytra_pos_, sizeof(elytra_pos_));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, 1);
    }
    std::memcpy(elytra_last_written_, elytra_pos_, sizeof(elytra_last_written_));
    elytra_have_written_ = true;
    writeFallTimer(player, 0.f);

    // Face the glide direction, like creative flight.
    if (have_cam) {
        flight::Heading heading{};
        if (flight::headingFromCamera(cam.forward, cam.right, heading) && callbacks_.safe_write) {
            const float yaw = flight::bodyYaw(heading);
            const auto q = eldenring::motion::characterQuatFromHeading(yaw);
            const float q_raw[4] = {q.x, q.y, q.z, q.w};
            callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
        }
    }

    if (now_ms - last_elytra_log_ms_ >= 1000) {
        last_elytra_log_ms_ = now_ms;
        const auto& vel = elytra_.state().velocity;
        log("elytra: pos=(%.2f %.2f %.2f) v=(%.2f %.2f %.2f) b/t fall_t=%.2f",
            static_cast<double>(elytra_pos_[0]), static_cast<double>(elytra_pos_[1]),
            static_cast<double>(elytra_pos_[2]), static_cast<double>(vel.x), static_cast<double>(vel.y),
            static_cast<double>(vel.z), static_cast<double>(fallTimer(player)));
    }
}

void PlayerLocomotion::requestEnderPearlThrow() {
    pearl_throw_requested_.store(true, std::memory_order_relaxed);
}

bool PlayerLocomotion::pearlRenderPos(float out[3]) const {
    if (!pearl_.active()) return false;
    out[0] = pearl_.state().position.x;
    out[1] = pearl_.state().position.y;
    out[2] = pearl_.state().position.z;
    return true;
}

void PlayerLocomotion::stepEnderPearl(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) && fg && !dead &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed);
    if (!allowed) {
        if (pearl_.active()) {
            pearl_.discard();
            log("pearl: discarded (mode/focus/inventory/death)");
        }
        if (pearl_hold_) {
            pearl_hold_ = false;
            log("pearl: teleport hold released (mode/focus/inventory/death)");
        }
        pearl_watch_ = false;
        pearl_retries_ = 0;
        pearl_throw_requested_.store(false, std::memory_order_relaxed);
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0 || !callbacks_.safe_read) return;
    float pos[3] = {};
    if (!callbacks_.safe_read(module + layout::kPhysicsPosition, pos, sizeof(pos))) return;

    // The engine re-bases Havok coordinates by whole 8 m steps. While the hold/watch writes a target, the read
    // position is that target in the engine's current frame: a grid difference is a re-base, so move the target
    // into the new frame (otherwise the engine re-bases back and forth and the player flips 40 m every frame).
    // Skip the first hold frame: the read is still the pre-teleport position, and the teleport distance itself
    // can look like a grid step (e.g. 24 / -8), which used to shift the target tens of metres.
    const bool first_hold_frame = pearl_hold_ && pearl_hold_frames_ == 0;
    if (pearl_hold_ || pearl_watch_) {
        if (!first_hold_frame) {
            float shift[3] = {0.f, 0.f, 0.f};
            bool rebase = false;
            for (int i = 0; i < 3; ++i) {
                const float d = pos[i] - pearl_hold_target_[i];
                if (std::fabs(d) < 4.f) continue;
                const float grid = std::round(d / 8.f) * 8.f;
                if (grid != 0.f && std::fabs(d - grid) < 1.5f) {
                    shift[i] = grid;
                    rebase = true;
                }
            }
            if (rebase) {
                pearl_hold_target_[0] += shift[0];
                pearl_hold_target_[1] += shift[1];
                pearl_hold_target_[2] += shift[2];
                log("pearl: hold target followed a re-base (%.1f %.1f %.1f)", static_cast<double>(shift[0]),
                    static_cast<double>(shift[1]), static_cast<double>(shift[2]));
            }
        }
        std::memcpy(pearl_player_pos_, pos, sizeof(pearl_player_pos_));
        pearl_have_player_pos_ = true;
    } else if (pearl_.active()) {
        // In flight: the player's own position is the reference. A grid jump is a re-base; anything else is a
        // player teleport/load, so the pearl's world reference is gone and it is dropped.
        if (pearl_have_player_pos_) {
            float shift[3] = {0.f, 0.f, 0.f};
            bool rebase = false;
            bool teleported = false;
            for (int i = 0; i < 3; ++i) {
                const float d = pos[i] - pearl_player_pos_[i];
                if (std::fabs(d) < 4.f) continue;
                const float grid = std::round(d / 8.f) * 8.f;
                if (grid != 0.f && std::fabs(d - grid) < 1.5f) {
                    shift[i] = grid;
                    rebase = true;
                } else {
                    teleported = true;
                }
            }
            if (teleported) {
                pearl_.discard();
                log("pearl: discarded (player teleported, not a grid re-base)");
            } else if (rebase) {
                mc::EnderPearlState st = pearl_.state();
                st.position.x += shift[0];
                st.position.y += shift[1];
                st.position.z += shift[2];
                st.prev_position.x += shift[0];
                st.prev_position.y += shift[1];
                st.prev_position.z += shift[2];
                pearl_.setState(st);
                log("pearl: followed a re-base (%.1f %.1f %.1f)", static_cast<double>(shift[0]),
                    static_cast<double>(shift[1]), static_cast<double>(shift[2]));
            }
        }
        std::memcpy(pearl_player_pos_, pos, sizeof(pearl_player_pos_));
        pearl_have_player_pos_ = true;
    } else {
        pearl_have_player_pos_ = false;
    }

    if (pearl_throw_requested_.exchange(false, std::memory_order_relaxed)) {
        CameraPose cam{};
        if (!callbacks_.read_camera || !callbacks_.read_camera(cam)) {
            log("pearl: throw ignored (no camera)");
        } else {
            // Vanilla inherits the owner's known movement: +0x80 is the previous frame's position.
            mc::Vec3 owner_v{};
            float last_pos[3] = {};
            if (callbacks_.safe_read(module + layout::kPhysicsPosition + 0x10, last_pos, sizeof(last_pos)) && dt > 1e-4f) {
                const float vx = (pos[0] - last_pos[0]) / dt / 20.f;
                const float vy = (pos[1] - last_pos[1]) / dt / 20.f;
                const float vz = (pos[2] - last_pos[2]) / dt / 20.f;
                const float dist = std::sqrt((pos[0] - last_pos[0]) * (pos[0] - last_pos[0]) +
                                             (pos[1] - last_pos[1]) * (pos[1] - last_pos[1]) +
                                             (pos[2] - last_pos[2]) * (pos[2] - last_pos[2]));
                const float len = std::sqrt(vx * vx + vy * vy + vz * vz);
                if (dist < 5.f && len < 1.f) owner_v = {vx, vy, vz};
            }
            const bool on_ground = callbacks_.is_player_airborne ? !callbacks_.is_player_airborne() : true;
            mc::EnderPearlLaunch launch;
            launch.origin = {pos[0], pos[1] + 1.52f, pos[2]}; // eye height 1.62 minus the vanilla 0.1 offset
            launch.look = {cam.forward[0], cam.forward[1], cam.forward[2]};
            launch.owner_velocity = owner_v;
            launch.owner_on_ground = on_ground;
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<float> unit(0.f, 1.f);
            float jitter[6];
            for (float& j : jitter) j = unit(rng);
            if (pearl_.launch(launch, jitter)) {
                pearl_accum_ = 0.f;
                // The vanilla pearl throw is random/bow (the arrow sound); use the ender eye launch whoosh instead.
                if (callbacks_.play_sound) callbacks_.play_sound("entity.ender_eye.launch", 1.0f);
                log("pearl: thrown from (%.2f %.2f %.2f) look=(%.2f %.2f %.2f) owner_v=(%.2f %.2f %.2f) ground=%d",
                    static_cast<double>(launch.origin.x), static_cast<double>(launch.origin.y),
                    static_cast<double>(launch.origin.z), static_cast<double>(launch.look.x),
                    static_cast<double>(launch.look.y), static_cast<double>(launch.look.z),
                    static_cast<double>(owner_v.x), static_cast<double>(owner_v.y), static_cast<double>(owner_v.z),
                    on_ground ? 1 : 0);
            }
        }
    }

    if (pearl_hold_) {
        // The engine pulls a one-shot write back on the next frame; hold the target like creative flight
        // and the elytra do, then release once it has taken.
        if (callbacks_.safe_write) {
            callbacks_.safe_write(module + layout::kPhysicsPosition, pearl_hold_target_, sizeof(pearl_hold_target_));
            callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, pearl_hold_target_, sizeof(pearl_hold_target_));
            const uint8_t one = 1;
            callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, 1);
        }
        writeFallTimer(player, 0.f);
        ++pearl_hold_frames_;
        float read_pos[3] = {};
        const bool have_read = callbacks_.safe_read(module + layout::kPhysicsPosition, read_pos, sizeof(read_pos));
        float dist = 1e9f;
        if (have_read) {
            const float dx = read_pos[0] - pearl_hold_target_[0];
            const float dy = read_pos[1] - pearl_hold_target_[1];
            const float dz = read_pos[2] - pearl_hold_target_[2];
            dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        }
        if (pearl_hold_frames_ >= 15 || (pearl_hold_frames_ >= 6 && dist < 0.5f)) {
            pearl_hold_ = false;
            pearl_watch_ = true;
            pearl_watch_frames_ = 0;
            log("pearl: teleport hold done after %d frames (read=(%.2f %.2f %.2f) target=(%.2f %.2f %.2f) dist=%.2f)",
                pearl_hold_frames_, static_cast<double>(read_pos[0]), static_cast<double>(read_pos[1]),
                static_cast<double>(read_pos[2]), static_cast<double>(pearl_hold_target_[0]),
                static_cast<double>(pearl_hold_target_[1]), static_cast<double>(pearl_hold_target_[2]),
                static_cast<double>(dist));
        }
        return;
    }

    // The engine occasionally resolves the capsule back after the hold; a couple of short re-holds make the
    // teleport stick without fighting normal movement (the watch only lasts ~200 ms).
    if (pearl_watch_) {
        float read_pos[3] = {};
        if (callbacks_.safe_read && callbacks_.safe_read(module + layout::kPhysicsPosition, read_pos, sizeof(read_pos))) {
            const float dx = read_pos[0] - pearl_hold_target_[0];
            const float dy = read_pos[1] - pearl_hold_target_[1];
            const float dz = read_pos[2] - pearl_hold_target_[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > 2.f && pearl_retries_ < 2) {
                ++pearl_retries_;
                pearl_hold_ = true;
                pearl_hold_frames_ = 0;
                log("pearl: teleport pulled back %.2f m, holding again (retry %d)", static_cast<double>(dist),
                    pearl_retries_);
                return;
            }
        }
        if (++pearl_watch_frames_ >= 12) pearl_watch_ = false;
    }

    if (!pearl_.active()) return;

    // 20 Hz velocity ticks; integrate every frame so the flight is smooth like the elytra.
    if (dt > 0.2f) dt = 0.2f;
    pearl_accum_ += dt;
    if (pearl_accum_ > 0.25f) pearl_accum_ = 0.05f;
    while (pearl_accum_ >= 0.05f) {
        pearl_accum_ -= 0.05f;
        pearl_.beginTick(false);
        if (!pearl_.active()) return;
    }

    const mc::Vec3 v = pearl_.moveVector();
    const float frac = std::clamp(dt / 0.05f, 0.f, 2.f);
    const mc::Vec3 cur = pearl_.state().position;
    const float step[3] = {v.x * frac, v.y * frac, v.z * frac};
    float now[3] = {cur.x, cur.y, cur.z};
    float hit_normal[3] = {0.f, 1.f, 0.f};
    bool hit = false;
    if (callbacks_.cast_ray) {
        // Sub-step the frame move so a fast pearl (or a long frame) cannot tunnel through a thin surface.
        const float len = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
        const int n = std::max(1, static_cast<int>(std::ceil(len / 0.5f)));
        for (int i = 0; i < n && !hit; ++i) {
            const float seg[3] = {step[0] / static_cast<float>(n), step[1] / static_cast<float>(n),
                                  step[2] / static_cast<float>(n)};
            const RayResult r = callbacks_.cast_ray(now, seg);
            if (r.ok && r.hit) {
                const float f = std::clamp(r.fraction, 0.f, 1.f);
                now[0] += seg[0] * f;
                now[1] += seg[1] * f;
                now[2] += seg[2] * f;
                hit = true;
                hit_normal[0] = r.normal[0];
                hit_normal[1] = r.normal[1];
                hit_normal[2] = r.normal[2];
            } else {
                now[0] += seg[0];
                now[1] += seg[1];
                now[2] += seg[2];
            }
        }
    } else {
        now[0] += step[0];
        now[1] += step[1];
        now[2] += step[2];
    }
    pearl_.commit({now[0], now[1], now[2]});

    if (hit) {
        // The vanilla oldPosition() lags up to a full 20 Hz tick (1.5 blocks at throw speed), which reads as
        // landing short. Use the hit point pushed out along the surface normal instead: a floor needs almost no
        // push (the feet sit on it), a wall needs the capsule radius so the engine does not resolve the teleport
        // back through the surface. No downward clamp: it used to lift wall hits onto a higher ledge.
        const float push = hit_normal[1] > 0.7f ? 0.05f : 0.45f;
        float target[3] = {now[0] + hit_normal[0] * push, now[1] + hit_normal[1] * push,
                           now[2] + hit_normal[2] * push};
        pearl_hold_target_[0] = target[0];
        pearl_hold_target_[1] = target[1];
        pearl_hold_target_[2] = target[2];
        pearl_hold_ = true;
        pearl_hold_frames_ = 0;
        pearl_watch_ = false;
        pearl_retries_ = 0;
        if (riptide_.active()) riptide_.stop(); // the teleport takes over the position
        if (elytra_.state().is_gliding) {
            elytra_.stopGliding();
            ctx.mode().elytra_gliding.store(false, std::memory_order_relaxed);
            elytra_have_pos_ = false;
            elytra_have_written_ = false;
        }
        if (isFlying()) {
            std::memcpy(fly_own_, pearl_hold_target_, sizeof(fly_own_));
            std::memcpy(fly_last_written_, pearl_hold_target_, sizeof(fly_last_written_));
            fly_have_written_ = true;
        }
        if (ctx.currentGameMode() == eldenring::creative::GameMode::Survival && callbacks_.apply_self_damage) {
            callbacks_.apply_self_damage(pearl_.config().damage);
        }
        if (callbacks_.play_sound) callbacks_.play_sound("entity.player.teleport", 1.0f);
        log("pearl: hit at (%.2f %.2f %.2f) normal (%.2f %.2f %.2f), teleport target (%.2f %.2f %.2f)",
            static_cast<double>(now[0]), static_cast<double>(now[1]), static_cast<double>(now[2]),
            static_cast<double>(hit_normal[0]), static_cast<double>(hit_normal[1]),
            static_cast<double>(hit_normal[2]), static_cast<double>(target[0]),
            static_cast<double>(target[1]), static_cast<double>(target[2]));
        pearl_.discard();
        return;
    }

    if (pearl_.state().position.y < -200.f) {
        pearl_.discard();
        log("pearl: discarded (below world)");
        return;
    }
    if (now_ms - last_pearl_log_ms_ >= 1000) {
        last_pearl_log_ms_ = now_ms;
        log("pearl: pos=(%.2f %.2f %.2f) v=(%.2f %.2f %.2f) b/t",
            static_cast<double>(pearl_.state().position.x), static_cast<double>(pearl_.state().position.y),
            static_cast<double>(pearl_.state().position.z), static_cast<double>(v.x), static_cast<double>(v.y),
            static_cast<double>(v.z));
    }
}

bool PlayerLocomotion::startRiptide(int level) {
    if (level <= 0) return false;
    riptide_request_.store(level, std::memory_order_relaxed);
    return true;
}

void PlayerLocomotion::stepRiptide(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) && fg && !dead &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed);
    if (!allowed) {
        if (riptide_.active()) {
            riptide_.stop();
            ctx.mode().riptide_active.store(false, std::memory_order_relaxed);
            log("riptide: stopped (mode/focus/inventory/death)");
        }
        riptide_request_.store(0, std::memory_order_relaxed);
        riptide_have_pos_ = false;
        riptide_have_written_ = false;
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0 || !callbacks_.safe_read) return;
    float pos[3] = {};
    if (!callbacks_.safe_read(module + layout::kPhysicsPosition, pos, sizeof(pos))) return;

    // A release request from the input thread: start on the camera thread, where the view is readable.
    const int request = riptide_request_.exchange(0, std::memory_order_relaxed);
    if (request > 0 && !riptide_.active() && !pearl_hold_) {
        CameraPose cam{};
        if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
            const bool on_ground = callbacks_.is_player_airborne ? !callbacks_.is_player_airborne() : true;
            if (riptide_.start(request, {cam.forward[0], cam.forward[1], cam.forward[2]}, on_ground, {})) {
                std::memcpy(riptide_pos_, pos, sizeof(riptide_pos_));
                std::memcpy(riptide_last_written_, pos, sizeof(riptide_last_written_));
                riptide_have_pos_ = true;
                riptide_have_written_ = true;
                riptide_accum_ = 0.f;
                ctx.mode().riptide_active.store(true, std::memory_order_relaxed);
                // The dash owns the position for its 20 ticks; the elytra is paused and resumes at the new
                // position when the dash ends (using the trident while gliding).
                riptide_was_gliding_ = elytra_.state().is_gliding;
                if (isFlying()) setFlying(false);
                if (callbacks_.play_sound) {
                    const char* sound = request >= 3 ? "item.trident.riptide_3"
                                        : request == 2 ? "item.trident.riptide_2"
                                                       : "item.trident.riptide_1";
                    callbacks_.play_sound(sound, 1.0f);
                }
                const auto& v = riptide_.state().velocity;
                log("riptide: started level=%d v=(%.2f %.2f %.2f) b/t ground=%d", request,
                    static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z), on_ground ? 1 : 0);
            }
        }
    }

    if (!riptide_.active()) return;

    // The game re-bases its coordinates by whole 8 m steps: follow the grid like the elytra does.
    if (!riptide_have_pos_ || !riptide_have_written_) {
        std::memcpy(riptide_pos_, pos, sizeof(riptide_pos_));
        std::memcpy(riptide_last_written_, pos, sizeof(riptide_last_written_));
        riptide_have_pos_ = true;
        riptide_have_written_ = true;
    } else if (flight::positionOverwritten(riptide_last_written_, pos, 3.f)) {
        flight::followRebase(riptide_pos_, riptide_last_written_, pos);
        const float dx = riptide_pos_[0] - pos[0];
        const float dy = riptide_pos_[1] - pos[1];
        const float dz = riptide_pos_[2] - pos[2];
        if (std::sqrt(dx * dx + dy * dy + dz * dz) > 5.f) {
            riptide_.stop();
            ctx.mode().riptide_active.store(false, std::memory_order_relaxed);
            log("riptide: stopped (engine teleported)");
            riptide_have_pos_ = false;
            riptide_have_written_ = false;
            return;
        }
    }

    // Per-frame integration with sub-stepped collision (a 3 b/t dash must not tunnel).
    const mc::Vec3 v = riptide_.state().velocity;
    const float frac = std::clamp(dt / 0.05f, 0.f, 2.f);
    const float step[3] = {v.x * frac, v.y * frac, v.z * frac};
    float now[3] = {riptide_pos_[0], riptide_pos_[1], riptide_pos_[2]};
    bool hit = false;
    if (callbacks_.cast_ray) {
        const float len = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
        const int n = std::max(1, static_cast<int>(std::ceil(len / 0.5f)));
        for (int i = 0; i < n && !hit; ++i) {
            const float seg[3] = {step[0] / static_cast<float>(n), step[1] / static_cast<float>(n),
                                  step[2] / static_cast<float>(n)};
            const RayResult r = callbacks_.cast_ray(now, seg);
            if (r.ok && r.hit) {
                const float f = std::clamp(r.fraction, 0.f, 1.f);
                now[0] += seg[0] * f;
                now[1] += seg[1] * f;
                now[2] += seg[2] * f;
                hit = true;
            } else {
                now[0] += seg[0];
                now[1] += seg[1];
                now[2] += seg[2];
            }
        }
    } else {
        now[0] += step[0];
        now[1] += step[1];
        now[2] += step[2];
    }
    std::memcpy(riptide_pos_, now, sizeof(riptide_pos_));
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, riptide_pos_, sizeof(riptide_pos_));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, riptide_pos_, sizeof(riptide_pos_));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, 1);
    }
    writeFallTimer(player, 0.f);
    std::memcpy(riptide_last_written_, riptide_pos_, sizeof(riptide_last_written_));

    // Vanilla travel order: move first, then apply gravity and drag for the next tick.
    riptide_accum_ += dt;
    if (riptide_accum_ > 0.25f) riptide_accum_ = 0.05f;
    while (riptide_accum_ >= 0.05f) {
        riptide_accum_ -= 0.05f;
        riptide_.endTick();
    }
    if (hit) {
        riptide_.stop();
        log("riptide: stopped (wall)");
    }
    if (!riptide_.active()) {
        ctx.mode().riptide_active.store(false, std::memory_order_relaxed);
        if (riptide_was_gliding_ && elytra_.state().is_gliding) {
            std::memcpy(elytra_pos_, riptide_pos_, sizeof(elytra_pos_));
            std::memcpy(elytra_last_written_, riptide_pos_, sizeof(elytra_last_written_));
            elytra_have_pos_ = true;
            elytra_have_written_ = true;
            log("riptide: resumed the glide at the new position");
        }
        riptide_was_gliding_ = false;
        log("riptide: ended");
        riptide_have_pos_ = false;
        riptide_have_written_ = false;
        return;
    }
    if (now_ms - last_riptide_log_ms_ >= 1000) {
        last_riptide_log_ms_ = now_ms;
        log("riptide: pos=(%.2f %.2f %.2f) v=(%.2f %.2f %.2f) b/t ticks=%d",
            static_cast<double>(riptide_pos_[0]), static_cast<double>(riptide_pos_[1]),
            static_cast<double>(riptide_pos_[2]), static_cast<double>(v.x), static_cast<double>(v.y),
            static_cast<double>(v.z), riptide_.ticksLeft());
    }
}

bool PlayerLocomotion::requestTridentThrow() {
    trident_throw_requested_.store(true, std::memory_order_relaxed);
    return true;
}

bool PlayerLocomotion::tridentRenderState(float pos[3], float dir[3]) const {
    if (!thrown_trident_.active()) return false;
    const auto& s = thrown_trident_.state();
    pos[0] = s.position.x;
    pos[1] = s.position.y;
    pos[2] = s.position.z;
    const float len = s.velocity.length();
    if (len > 1e-5f) {
        dir[0] = s.velocity.x / len;
        dir[1] = s.velocity.y / len;
        dir[2] = s.velocity.z / len;
    } else {
        dir[0] = 0.f;
        dir[1] = 0.f;
        dir[2] = 1.f;
    }
    return true;
}

void PlayerLocomotion::stepThrownTrident(float dt, uint64_t now_ms, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool allowed = ctx.mode().mc_mode.load(std::memory_order_relaxed) && fg && !dead &&
                         !ctx.ui().inv_open.load(std::memory_order_relaxed);
    if (!allowed) {
        if (thrown_trident_.active()) {
            thrown_trident_.discard();
            log("trident: discarded (mode/focus/inventory/death)");
        }
        trident_throw_requested_.store(false, std::memory_order_relaxed);
        trident_have_player_pos_ = false;
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0 || !callbacks_.safe_read) return;
    float pos[3] = {};
    if (!callbacks_.safe_read(module + layout::kPhysicsPosition, pos, sizeof(pos))) return;

    if (trident_throw_requested_.exchange(false, std::memory_order_relaxed)) {
        CameraPose cam{};
        if (callbacks_.read_camera && callbacks_.read_camera(cam)) {
            mc::Vec3 owner{};
            float last_pos[3] = {};
            if (callbacks_.safe_read(module + layout::kPhysicsPosition + 0x10, last_pos, sizeof(last_pos)) && dt > 1e-4f) {
                const float vx = (pos[0] - last_pos[0]) / dt / 20.f;
                const float vy = (pos[1] - last_pos[1]) / dt / 20.f;
                const float vz = (pos[2] - last_pos[2]) / dt / 20.f;
                const float dist = std::sqrt((pos[0] - last_pos[0]) * (pos[0] - last_pos[0]) +
                                             (pos[1] - last_pos[1]) * (pos[1] - last_pos[1]) +
                                             (pos[2] - last_pos[2]) * (pos[2] - last_pos[2]));
                const float len = std::sqrt(vx * vx + vy * vy + vz * vz);
                if (dist < 5.f && len < 1.f) owner = {vx, vy, vz};
            }
            const bool on_ground = callbacks_.is_player_airborne ? !callbacks_.is_player_airborne() : true;
            static thread_local std::mt19937 rng{std::random_device{}()};
            std::uniform_real_distribution<float> unit(0.f, 1.f);
            float jitter[6];
            for (float& j : jitter) j = unit(rng);
            if (thrown_trident_.launch({pos[0], pos[1] + 1.52f, pos[2]},
                                       {cam.forward[0], cam.forward[1], cam.forward[2]}, owner, on_ground, jitter)) {
                trident_accum_ = 0.f;
                trident_have_player_pos_ = true;
                std::memcpy(trident_player_pos_, pos, sizeof(trident_player_pos_));
                if (callbacks_.play_sound) callbacks_.play_sound("item.trident.throw", 1.0f);
                const auto& v = thrown_trident_.state().velocity;
                log("trident: thrown from (%.2f %.2f %.2f) v=(%.2f %.2f %.2f) b/t", static_cast<double>(pos[0]),
                    static_cast<double>(pos[1] + 1.52f), static_cast<double>(pos[2]), static_cast<double>(v.x),
                    static_cast<double>(v.y), static_cast<double>(v.z));
            }
        }
    }

    if (!thrown_trident_.active()) {
        trident_have_player_pos_ = false;
        return;
    }

    // Follow the engine's 8 m floating-origin re-bases; a non-grid jump is a player teleport, so drop the trident.
    if (trident_have_player_pos_) {
        float shift[3] = {0.f, 0.f, 0.f};
        bool rebase = false;
        bool teleported = false;
        for (int i = 0; i < 3; ++i) {
            const float d = pos[i] - trident_player_pos_[i];
            if (std::fabs(d) < 4.f) continue;
            const float grid = std::round(d / 8.f) * 8.f;
            if (grid != 0.f && std::fabs(d - grid) < 1.5f) {
                shift[i] = grid;
                rebase = true;
            } else {
                teleported = true;
            }
        }
        if (teleported) {
            thrown_trident_.discard();
            log("trident: discarded (player teleported)");
            trident_have_player_pos_ = false;
            return;
        }
        if (rebase) {
            mc::ThrownTridentState st = thrown_trident_.state();
            st.position.x += shift[0];
            st.position.y += shift[1];
            st.position.z += shift[2];
            thrown_trident_.setState(st);
            log("trident: followed a re-base (%.1f %.1f %.1f)", static_cast<double>(shift[0]),
                static_cast<double>(shift[1]), static_cast<double>(shift[2]));
        }
    }
    std::memcpy(trident_player_pos_, pos, sizeof(trident_player_pos_));
    trident_have_player_pos_ = true;

    // 20 Hz velocity ticks; integrate every frame with sub-stepped collision.
    if (dt > 0.2f) dt = 0.2f;
    trident_accum_ += dt;
    if (trident_accum_ > 0.25f) trident_accum_ = 0.05f;
    while (trident_accum_ >= 0.05f) {
        trident_accum_ -= 0.05f;
        thrown_trident_.beginTick();
        if (!thrown_trident_.active()) return;
    }

    const mc::Vec3 v = thrown_trident_.moveVector();
    const float frac = std::clamp(dt / 0.05f, 0.f, 2.f);
    const mc::Vec3 cur = thrown_trident_.state().position;
    const float step[3] = {v.x * frac, v.y * frac, v.z * frac};
    float now[3] = {cur.x, cur.y, cur.z};
    bool hit = false;
    if (callbacks_.cast_ray) {
        const float len = std::sqrt(step[0] * step[0] + step[1] * step[1] + step[2] * step[2]);
        const int n = std::max(1, static_cast<int>(std::ceil(len / 0.5f)));
        for (int i = 0; i < n && !hit; ++i) {
            const float seg[3] = {step[0] / static_cast<float>(n), step[1] / static_cast<float>(n),
                                  step[2] / static_cast<float>(n)};
            const RayResult r = callbacks_.cast_ray(now, seg);
            if (r.ok && r.hit) {
                const float f = std::clamp(r.fraction, 0.f, 1.f);
                now[0] += seg[0] * f;
                now[1] += seg[1] * f;
                now[2] += seg[2] * f;
                hit = true;
            } else {
                now[0] += seg[0];
                now[1] += seg[1];
                now[2] += seg[2];
            }
        }
    } else {
        now[0] += step[0];
        now[1] += step[1];
        now[2] += step[2];
    }
    thrown_trident_.commit({now[0], now[1], now[2]});

    if (hit) {
        if (callbacks_.play_sound) callbacks_.play_sound("item.trident.hit", 1.0f);
        log("trident: hit at (%.2f %.2f %.2f)", static_cast<double>(now[0]), static_cast<double>(now[1]),
            static_cast<double>(now[2]));
        thrown_trident_.discard();
        return;
    }
    if (thrown_trident_.state().position.y < -200.f) {
        thrown_trident_.discard();
        log("trident: discarded (below world)");
        return;
    }
    if (now_ms - last_trident_log_ms_ >= 1000) {
        last_trident_log_ms_ = now_ms;
        log("trident: pos=(%.2f %.2f %.2f) v=(%.2f %.2f %.2f) b/t", static_cast<double>(now[0]),
            static_cast<double>(now[1]), static_cast<double>(now[2]), static_cast<double>(v.x),
            static_cast<double>(v.y), static_cast<double>(v.z));
    }
}

void PlayerLocomotion::stepOwRocketPunch(float dt, RuntimeContext& ctx) {
    const bool fg = callbacks_.is_game_in_foreground ? callbacks_.is_game_in_foreground() : true;
    const bool dead = callbacks_.is_player_dead ? callbacks_.is_player_dead() : false;
    const bool state3 = ctx.mode().ow_mode.load(std::memory_order_relaxed) &&
                        ctx.mode().ow_state.load(std::memory_order_relaxed) == 3 &&
                        !ctx.ui().inv_open.load(std::memory_order_relaxed) && fg && !dead;
    if (!state3) {
        if (ow_dash_active_) {
            ow_dash_active_ = false;
            log("OW-DASH: ended by state change");
        }
        return;
    }

    eldenring::ow::RocketPunchDashConfig cfg;
    cfg.distance_m = ctx.mode().ow_dash_distance.load(std::memory_order_relaxed);
    const float anim_time = ctx.mode().ow_anim_time.load(std::memory_order_relaxed);

    CameraPose cam{};
    float dx = 0.f, dz = 0.f;
    if (!callbacks_.read_camera || !callbacks_.read_camera(cam) ||
        !eldenring::ow::rocketPunchDirection(cam.forward, dx, dz)) {
        return;
    }

    const uintptr_t player = callbacks_.get_player_chr ? callbacks_.get_player_chr() : 0;
    const uintptr_t module = player != 0 && callbacks_.get_physics_module ? callbacks_.get_physics_module(player) : 0;
    if (module == 0 || !callbacks_.safe_read) return;

    float feet[3] = {};
    if (!callbacks_.safe_read(module + layout::kPhysicsPosition, feet, sizeof(feet))) return;

    if (anim_time >= cfg.end_time) {
        if (ow_dash_active_) {
            const float ax = feet[0] - ow_dash_start_[0];
            const float az = feet[2] - ow_dash_start_[2];
            log("OW-DASH: done configured=%.2f actual=%.2f", static_cast<double>(cfg.distance_m),
                static_cast<double>(std::sqrt(ax * ax + az * az)));
            ow_dash_active_ = false;
        }
        return;
    }

    const float speed = eldenring::ow::rocketPunchSpeed(anim_time, cfg);
    if (speed <= 0.f) return;

    if (!ow_dash_active_) {
        ow_dash_active_ = true;
        ow_dash_start_[0] = feet[0];
        ow_dash_start_[1] = feet[1];
        ow_dash_start_[2] = feet[2];
        log("OW-DASH: start configured=%.2f window=%.2f..%.2f", static_cast<double>(cfg.distance_m),
            static_cast<double>(cfg.start_time), static_cast<double>(cfg.end_time));
    }

    const float clamped_dt = std::clamp(dt, 0.001f, 0.05f);
    float step = speed * clamped_dt;
    if (step <= 0.f) return;

    // Native collision: cast at waist and chest height and keep the shortest
    // allowed step. The 0.4 m margin is the character capsule radius.
    if (callbacks_.cast_ray) {
        const float heights[2] = {0.4f, 1.0f};
        for (const float h : heights) {
            const float from[3] = {feet[0], feet[1] + h, feet[2]};
            const float disp[3] = {dx * step, 0.f, dz * step};
            const RayResult hit = callbacks_.cast_ray(from, disp);
            if (hit.ok && hit.hit) {
                step = std::min(step, std::max(0.f, hit.fraction * step - 0.4f));
            }
        }
    }
    if (step <= 1e-4f) return;

    feet[0] += dx * step;
    feet[2] += dz * step;
    if (callbacks_.safe_write) {
        callbacks_.safe_write(module + layout::kPhysicsPosition, feet, sizeof(feet));
        callbacks_.safe_write(module + layout::kPhysicsPosition + 0x10, feet, sizeof(feet));
        const uint8_t one = 1;
        callbacks_.safe_write(module + layout::kPhysicsProxySyncRequest, &one, sizeof(one));

        // Face the punch while dashing.
        const auto out_q = eldenring::motion::characterQuatFromHeading(std::atan2(dx, dz));
        const float q_raw[4] = {out_q.x, out_q.y, out_q.z, out_q.w};
        callbacks_.safe_write(module + layout::kPhysicsOrientation, q_raw, sizeof(q_raw));
    }
}
} // namespace eldenring::movement
